/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t: not in stdlib.h off MSVC */
#include <stdlib.h>   /* getenv, exit: the spin verdict below */
#include <string.h>   /* strcmp: RECOMP_ICALL_FATAL */

/* One diagnostic report, one piece.
 *
 * Each report below is a dozen fprintf calls and the CRT takes its lock per
 * call, so with more than one guest thread running the lines interleave in
 * the middle of a report. Dino Crisis 3 runs six workers once it is past its
 * menus, and its log came out like this:
 *
 *   callers: 0x004DB424 <- 0x002705B4 <- 0x002705B4[ICALL] unresolved jump
 *   target 0x41B0B870 -- 1 time(s) (total calls: 6930990)
 *
 * -- two threads' reports spliced together, with a caller list that stops
 * mid-sentence and an address that belongs to neither line as read. These
 * diagnostics exist to be read during exactly the multi-threaded bring-up
 * that breaks them.
 *
 * The CRT exposes the lock these calls were already taking one at a time, so
 * hold it across the whole report instead. */
#if defined(_MSC_VER)
#  define RECOMP_DIAG_LOCK()   _lock_file(stderr)
#  define RECOMP_DIAG_UNLOCK() _unlock_file(stderr)
#elif defined(__unix__) || defined(__APPLE__)
#  define RECOMP_DIAG_LOCK()   flockfile(stderr)
#  define RECOMP_DIAG_UNLOCK() funlockfile(stderr)
#else
#  define RECOMP_DIAG_LOCK()   ((void)0)
#  define RECOMP_DIAG_UNLOCK() ((void)0)
#endif

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

/* The guest registers (g_eax, g_esp, g_icall_saved_esp, ...) are fields of
 * one thread-local struct, declared once in recomp_cpu.h, which the lifter
 * copies into gen/ beside the code it describes. Take them from there; never
 * redeclare one here. They used to be redeclared by hand, and a declaration
 * without the thread-local storage class linked and silently read other
 * storage, so every read got 0 -- which is why the log below once reported
 * no call site: not because the guest esp was stale, but because this file
 * was not reading the guest esp at all. The names are macros now, so a
 * redeclaration no longer compiles. */
#include "recomp/gen/recomp_cpu.h"

/* g_eax: recomp_cpu.h. */
/* The guest stack pointer. At the moment an indirect call is refused, the
 * caller has already pushed its guest return address, so the top of the
 * guest stack is the call site -- the one thing the old log did not say. */
/* g_esp: recomp_cpu.h. */
extern ptrdiff_t g_xbox_mem_offset;
/* The lifted code sections, so a value on the guest stack can be told
 * apart from data when naming the callers of a refused call. */
extern uint32_t g_xbox_code_lo;
extern uint32_t g_xbox_code_hi;
/* The esp the dispatch macro captured. Not g_esp, which is stale by the time
 * a refused call is reported. Zero when the title's generated header predates
 * this, and the log then says it has no callers rather than inventing them. */
/* g_icall_saved_esp: recomp_cpu.h. */
/* Which dispatch form was refused: 0 unknown, 1 call, 2 jump. Unknown
 * means this title was lifted before the macros published it. */
/* g_icall_dispatch_form: recomp_cpu.h. */

/* ── Sid Meier's Pirates!: skip the intro movies ───────────────
 *
 * RECOMP_PIRATES_SKIP_INTRO=1 (the default during bring-up) goes from boot
 * straight to the title screen; =0 plays the 2K and Firaxis logos and the
 * Pirates! intro as the disc does. Asked for by the project so a test run is
 * not a minute of movies before anything can be pressed -- START does not
 * skip them.
 *
 * How: sub_000DB7A0 is the game's movie player (cdecl: the movie's name at
 * [esp+4] and a volume at [esp+8], a mode in eax). It plays a whole movie and
 * returns, and it already has an exit for "not played" -- `xor al, al; ret`
 * at 0x000DB88A, taken when its "movies off" byte 0x004D67C7 is set. For the
 * three intro movies this wrapper takes that same exit before the player is
 * entered: no file is opened and the caller carries on as after a movie it
 * did not get to play. The three intro ones are queued by sub_00126050 at
 * boot (2KG_NTSC.bik, Firaxis_Logo.bik) and by the title screen's attract
 * loop sub_001267C0 (those two and Pirates!.bik).
 *
 * Every other movie is skipped the same way (the project asked for anything
 * movie-like that slows testing down to go): the land battle intro, victory
 * and defeat, the hint movies and the bonus content. None is needed for the
 * game to advance -- the player returns "not played" for them on a console
 * with its movies-off byte set too. RECOMP_PIRATES_SKIP_INTRO=intro keeps
 * the old behaviour, skipping only the three intro movies.
 *
 * Not the game's own option byte at 0x004F92E5: it looks like one (it is
 * tested before the logos) but it also sends the game from boot into the
 * Xbox Live menu instead of the title screen -- a "started from a Live
 * invitation" path, not a skip.
 *
 * Delete this when it is no longer wanted: at 0 the wrapper only passes
 * through. */
int xbox_EnvSwitch(const char *name, int default_on);
/* pirates.ini (src/settings_title.h): the file's line, or the environment
 * variable named, which wins. xboxrecomp/src/config/recomp_config.h. */
const char *recomp_config_lookup(const char *env_name, const char *key);
int recomp_config_bool(const char *env_name, const char *key, int fallback);
int recomp_config_int(const char *env_name, const char *key, int fallback);
extern void sub_000DB7A0_gen(void);
#if !defined(_MSC_VER)
#  include <strings.h>
#  define _stricmp strcasecmp
#endif

static int pirates_is_intro_movie(const char *name)
{
    static const char *const intro[] = {
        "2KG_NTSC.bik", "Firaxis_Logo.bik", "Pirates!.bik"
    };
    size_t i;
    for (i = 0; i < sizeof intro / sizeof intro[0]; i++)
        if (_stricmp(name, intro[i]) == 0)
            return 1;
    return 0;
}

void sub_000DB7A0(void)
{
    static int skip = -1;
    uint32_t name_va = *(volatile uint32_t *)((uintptr_t)(g_esp + 4u) + g_xbox_mem_offset);
    const char *name = name_va
        ? (const char *)((uintptr_t)name_va + g_xbox_mem_offset) : "";

    if (skip < 0) {
        const char *v = recomp_config_lookup("RECOMP_PIRATES_SKIP_INTRO", "skip_intro");
        /* 2 = every movie (the default), 1 via "intro" = the three only. */
        if (v && _stricmp(v, "intro") == 0)
            skip = 1;
        else
            skip = recomp_config_bool("RECOMP_PIRATES_SKIP_INTRO", "skip_intro", 1) ? 2 : 0;
        fprintf(stderr, "[PIRATES] movies: %s (RECOMP_PIRATES_SKIP_INTRO)\n",
                skip == 2 ? "all skipped" : skip == 1 ? "intro skipped" : "all played");
        fflush(stderr);
    }
    if (skip == 2 || (skip == 1 && pirates_is_intro_movie(name))) {
        fprintf(stderr, "[PIRATES] skipped movie %s\n", name);
        fflush(stderr);
        g_eax &= ~0xFFu;   /* xor al, al: "not played" */
        g_esp += 4;        /* ret: pop the return address the caller pushed */
        return;
    }
    sub_000DB7A0_gen();
}

/* ── Sid Meier's Pirates!: the debug MINIGAMES menu ────────────
 *
 * RECOMP_PIRATES_MINIGAMES=1 adds "MINIGAMES (*DEBUG!*)", the developers'
 * own main-menu entry, between BONUS CONTENT and VS MODE. It starts the
 * sword duel, land battle, dancing and the rest straight from the main menu,
 * which is how they are tested without a career that happens to reach them.
 * Off by default: a player never sees it.
 *
 * How: sub_00126BC0 is the main menu. It draws that entry, and counts it in
 * the cursor's wrap-around, only while the byte 0x004D69C0 is set; the same
 * function toggles that byte on a debug input (event 0x57) that no Xbox pad
 * produces. sub_0012A1D0, which acts on the choice, reads the same byte, so
 * setting it before the menu runs is all it takes. */
extern void sub_00126BC0_gen(void);
extern void xbox_D3D8SetWideFrames(int wide);
/* The front end in widescreen: the game draws its 4:3 menu into the 16:9
 * buffer with more background either side, so stretched to 16:9 it looks
 * wide. While the main menu runs the frames are called 4:3 (the toolkit
 * shows the buffer's centre between bars); sub_001A958A calls them 16:9
 * again once it has not run for 300 ms of game time. */
static volatile int g_pirates_menu_seen, g_pirates_menu_narrow;

void sub_00126BC0(void)
{
    g_pirates_menu_seen = 1;
    if (!g_pirates_menu_narrow) {
        g_pirates_menu_narrow = 1;
        xbox_D3D8SetWideFrames(0);
    }
    static int on = -1;
    if (on < 0) {
        on = recomp_config_bool("RECOMP_PIRATES_MINIGAMES", "minigames", 0);
        if (on) {
            *(volatile uint8_t *)((uintptr_t)0x004D69C0u + g_xbox_mem_offset) = 1;
            fprintf(stderr, "[PIRATES] debug MINIGAMES menu on (RECOMP_PIRATES_MINIGAMES)\n");
            fflush(stderr);
        }
    }
    sub_00126BC0_gen();
}

/* ── Sid Meier's Pirates!: a treasure map on demand (debug) ────
 *
 * RECOMP_PIRATES_TREASURE_MAP=n (treasure_map = n) buys up to n pieces of a
 * buried-treasure map from the mysterious traveller, free, when a town menu
 * opens and no hunt is in progress, so a treasure hunt can be tested without
 * waiting for the traveller (a random town event) or for a beaten pirate to
 * drop a map. 2 makes a whole map on the default difficulty. Off (0) by
 * default; a player never needs it.
 *
 * How: sub_00021D70 is the town menu; the town is the 32-byte entry
 * 0x004A1968 + 32 * MEM32(0x00424828) (+0 x, +4 y). The traveller's "yes"
 * (0x0001923C, in the town events of sub_00016C70) takes the gold and calls
 * sub_000AA3C0(0, x, y, 1, 0, 0, 0): hunt slot 0, the town it was bought in,
 * one purchase. That picks the nearest top-ten pirate's treasure (0x0049C4BC),
 * starts the hunt in slot 0 (0x0049C3A8, the treasure's x, nonzero while one
 * runs; 0x0049C4B8 holds the missing pieces in bits 8-11) and shows the map.
 * The same call is made here, before the town menu opens, once per piece. */
extern void sub_00021D70_gen(void);
extern void sub_000AA3C0(void);

#define PIRATES_TOWN_INDEX 0x00424828u
#define PIRATES_TOWNS      0x004A1968u
#define PIRATES_HUNT0      0x0049C3A8u

void sub_00021D70(void)
{
    static int pieces = -1;
    if (pieces < 0) {
        pieces = recomp_config_int("RECOMP_PIRATES_TREASURE_MAP", "treasure_map", 0);
        if (pieces > 4) pieces = 4;
        if (pieces > 0) {
            fprintf(stderr, "[PIRATES] treasure map: %d piece(s) on a town visit with no hunt (RECOMP_PIRATES_TREASURE_MAP)\n", pieces);
            fflush(stderr);
        }
    }
    if (pieces > 0 && *(volatile uint32_t *)((uintptr_t)PIRATES_HUNT0 + g_xbox_mem_offset) == 0) {
        uint32_t town = *(volatile uint32_t *)((uintptr_t)PIRATES_TOWN_INDEX + g_xbox_mem_offset);
        uint32_t e = PIRATES_TOWNS + town * 32u;
        uint32_t x = *(volatile uint32_t *)((uintptr_t)e + g_xbox_mem_offset);
        uint32_t y = *(volatile uint32_t *)((uintptr_t)(e + 4u) + g_xbox_mem_offset);
        uint32_t args[7] = { 0, x, y, 1, 0, 0, 0 };
        uint32_t sp = g_esp, ebp = g_ebp, seh = g_seh_ebp;
        int i, n;
        for (n = 0; n < pieces; n++) {   /* one purchase each: the map shows each time */
            for (i = 6; i >= 0; i--) {
                g_esp -= 4;
                *(volatile uint32_t *)((uintptr_t)g_esp + g_xbox_mem_offset) = args[i];
            }
            g_esp -= 4;   /* the return address; sub_000AA3C0's ret pops it */
            *(volatile uint32_t *)((uintptr_t)g_esp + g_xbox_mem_offset) = 0x00021D70u;
            sub_000AA3C0();
            g_esp = sp; g_ebp = ebp; g_seh_ebp = seh;
            fprintf(stderr, "[PIRATES] treasure map: town %u (%d,%d) -> pirate %d, hunt %08X state %08X\n",
                    town, (int)x, (int)y,
                    (int)*(volatile uint32_t *)((uintptr_t)0x0049C4BCu + g_xbox_mem_offset),
                    *(volatile uint32_t *)((uintptr_t)PIRATES_HUNT0 + g_xbox_mem_offset),
                    *(volatile uint32_t *)((uintptr_t)0x0049C4B8u + g_xbox_mem_offset));
            fflush(stderr);
            if (!(*(volatile uint32_t *)((uintptr_t)0x0049C4B8u + g_xbox_mem_offset) & 0xF00u))
                break;   /* the map is whole */
        }
    }
    sub_00021D70_gen();
}

/* ── Sid Meier's Pirates!: a test probe for scripted play (debug) ──
 *
 * RECOMP_PIRATES_TEST_PROBE=<folder> (test_probe = <folder>) lets a test
 * script see where the ships are and move the player's ship, so it can sail
 * a career by itself (tools that steer from these files; the left stick turns
 * the ship, about 150 degrees a second). Off (empty) by default; a player
 * never needs it. Written from the game's clock read below, every 150-500 ms:
 *
 *   pos.txt     tick, player x, y (0x004564BC/C0, map units * 4,000,000,
 *               y grows northward), map cell x, y (0x004C70EC/E8)
 *   ships.txt   one line per object at sea: index, x, y, kind; the array
 *               starts with the player at 0x004564BC, stride 0x45C
 *   battle.txt  tick, ours: x, y, heading, crew (0x004D8F10..+0x1C),
 *               enemy: x, y, heading, crew (0x004DBE08..+0x1C)
 *   teleport.txt  written by the script: "x y" in map units moves the
 *               player's ship there once, then the file is deleted. */
static void pirates_probe_write(const char *dir, const char *name, const char *text)
{
    char fn[512]; FILE *fp;
    snprintf(fn, sizeof fn, "%s/%s", dir, name);
    if ((fp = fopen(fn, "w")) != NULL) { fputs(text, fp); fclose(fp); }
}

static int32_t pirates_rd32(uint32_t va)
{
    return *(volatile int32_t *)((uintptr_t)va + g_xbox_mem_offset);
}

static void pirates_test_probe(uint32_t now)
{
    static int on = -1;
    static char dir[400];
    static uint32_t last_pos, last_ships, last_battle;
    char buf[8192], fn[512];
    FILE *fp;
    int tx, ty, i, n;

    if (on < 0) {
        const char *v = recomp_config_lookup("RECOMP_PIRATES_TEST_PROBE", "test_probe");
        on = v && *v && strcmp(v, "0") != 0;
        if (on) {
            snprintf(dir, sizeof dir, "%s", v);
            fprintf(stderr, "[PIRATES] test probe writing to %s (RECOMP_PIRATES_TEST_PROBE)\n", dir);
            fflush(stderr);
        }
    }
    if (!on)
        return;
    if (now - last_pos > 200u) {
        last_pos = now;
        snprintf(fn, sizeof fn, "%s/teleport.txt", dir);
        if ((fp = fopen(fn, "r")) != NULL) {
            if (fscanf(fp, "%d %d", &tx, &ty) == 2) {
                *(volatile int32_t *)((uintptr_t)0x004564BCu + g_xbox_mem_offset) = tx * 4000000;
                *(volatile int32_t *)((uintptr_t)0x004564C0u + g_xbox_mem_offset) = ty * 4000000;
                fprintf(stderr, "[PIRATES] test probe: ship moved to %d,%d\n", tx, ty);
            }
            fclose(fp);
            remove(fn);
        }
        snprintf(buf, sizeof buf, "%u %d %d %d %d\n", now,
                 pirates_rd32(0x004564BCu), pirates_rd32(0x004564C0u),
                 pirates_rd32(0x004C70ECu), pirates_rd32(0x004C70E8u));
        pirates_probe_write(dir, "pos.txt", buf);
    }
    if (now - last_battle > 150u) {
        last_battle = now;
        snprintf(buf, sizeof buf, "%u %d %d %d %d %d %d %d %d\n", now,
                 pirates_rd32(0x004D8F10u), pirates_rd32(0x004D8F14u),
                 pirates_rd32(0x004D8F1Cu), pirates_rd32(0x004D8F2Cu),
                 pirates_rd32(0x004DBE08u), pirates_rd32(0x004DBE0Cu),
                 pirates_rd32(0x004DBE14u), pirates_rd32(0x004DBE24u));
        pirates_probe_write(dir, "battle.txt", buf);
    }
    if (now - last_ships > 500u) {
        last_ships = now;
        for (i = 1, n = 0; i < 128 && n < (int)sizeof buf - 64; i++) {
            uint32_t a = 0x004564BCu + (uint32_t)i * 0x45Cu;
            int32_t x = pirates_rd32(a), y = pirates_rd32(a + 4u);
            if (x || y)
                n += snprintf(buf + n, sizeof buf - n, "%d %d %d %u\n", i, x, y,
                              (uint32_t)pirates_rd32(a - 0xCu));
        }
        buf[n] = 0;
        pirates_probe_write(dir, "ships.txt", buf);
    }
}

/* ── Sid Meier's Pirates!: keep the disc-to-cache copy running ─
 *
 * On first use the game copies its .FPK archives from the disc into the
 * utility drive's cache (Z:, the save folder's Cache), one file at a time
 * through TRANSFER.TMP, on a job thread (sub_00154390). That thread copies
 * a chunk only while a "go" event is set: the object at 0x004D67B0, its
 * handle at +0x38 (wait at 0x00154F62). The game sets it for the length of
 * a loading screen (sub_001264A0) and of each movie it plays (the logos,
 * the attract movie, the opening story in sub_00090CC0), and resets it
 * afterwards, so the copy never competes with gameplay for the drive.
 *
 * With the movies skipped (RECOMP_PIRATES_SKIP_INTRO) each of those
 * windows is a few microseconds, so the copier stopped part-way through a
 * file. Loading a save then needed that file: sub_0008F7D0 runs with the
 * event reset (just after the opening story's Set/Reset in sub_00090CC0)
 * and waited for a copy that was paused, and the game froze on its loading
 * screen. Three of three loads on a fresh save folder froze, each at a
 * different file; with a full cache, none did.
 *
 * So the reset is not passed on for that one event: the copy runs in the
 * background until it is done (a minute or two, once per save folder).
 * Every other ResetEvent goes through unchanged. A PC drive does not need
 * the pause the console's DVD did. RECOMP_PIRATES_CACHE_PAUSE=1 passes the
 * resets on as the disc does. */
extern void sub_001A9220_gen(void);

void sub_001A9220(void)   /* XAPI ResetEvent(HANDLE), stdcall */
{
    static int pause = -1;
    uint32_t h = *(volatile uint32_t *)((uintptr_t)(g_esp + 4u) + g_xbox_mem_offset);
    uint32_t obj = *(volatile uint32_t *)((uintptr_t)0x004D67B0u + g_xbox_mem_offset);

    if (pause < 0)
        pause = recomp_config_bool("RECOMP_PIRATES_CACHE_PAUSE", "cache_pause", 0);
    if (!pause && obj && h &&
        h == *(volatile uint32_t *)((uintptr_t)(obj + 0x38u) + g_xbox_mem_offset)) {
        g_eax = 1;         /* TRUE */
        g_esp += 8;        /* ret 4: the return address and the handle */
        return;
    }
    sub_001A9220_gen();
}

/* The frame limiter at sea and in battles, and frame interpolation.
 *
 * Those scenes end each frame by sleeping, two milliseconds at a time, until
 * the tick count (GetTickCount, this function) is 31 ms past the frame's
 * start (sea: 0x00091DC2-0x00091DF5 in sub_00091170 and a second copy at
 * 0x000A4AE9; ship battles: 0x000C3B30, which waits 100 ms instead while
 * bit 0x20 of 0x0049C298 is set, left alone here; sword duels: 0x00036C41,
the same wait with the same slow flag; and a 30 ms copy at 0x0004DF70,
found with RECOMP_PIRATES_TICK_SITES=1); on the console the Swap then
 * waits for the next vblank, so they run at 30. Their logic steps once a
 * frame: without the limiter the ship sails several times too fast.
 *
 * So the frame rate cannot be raised there; what can be raised is what is
 * shown. With frame interpolation on (frame_interp = 2 or more in
 * pirates.ini, RECOMP_FRAME_INTERP), the limiter's wait is moved into the
 * flip gate: the comparison after the tick read is told the 31 ms have passed, and
 * the gate holds those scenes to one Swap every two vblanks, the console's
 * 30, lending the wait to the interpolation, which shows the frames between
 * (toolkit patch 0015). The menus and the towns, which never reach the
 * limiter, go back to one Swap a vblank 200 ms after it was last reached.
 * The player's frame cap outranks the title's interval in the gate, so with
 * frame_cap other than adaptive or 30 the game keeps its own wait (normal
 * speed, no frames in between there; patch 0016). Without frame
 * interpolation nothing changes. */
extern void sub_001A958A_gen(void);
extern int recomp_config_int(const char *env_name, const char *key, int fallback);
extern int recomp_settings_default_frame_interp(void);
extern int hle_frame_interp(void);   /* toolkit: frames shown per frame drawn, now */
extern void xbox_Nv2aFlipGateSetTitleInterval(uint32_t interval);
extern int xbox_Nv2aFlipGateVblanksPerSwap(void);
#define PIRATES_LIMITER_SEA  0x00091DE1u     /* after the limiters' tick reads: */
#define PIRATES_LIMITER_SEA2 0x000A4AEEu     /* the sea (two loops), */
#define PIRATES_LIMITER_BTL  0x000C3B4Eu     /* ship battles */
#define PIRATES_LIMITER_DUEL 0x00036C60u     /* sword duels (120 ms when slowed) */
#define PIRATES_LIMITER_30MS 0x0004DF8Fu     /* a 30 ms copy (sub_0004DE..) */
#define PIRATES_SLOW_FLAGS   0x0049C298u     /* bit 0x20: battles wait 100 ms */
#define PIRATES_FRAME_START  0x005061F8u     /* the limiter's time base */
void sub_001A958A(void)
{
    static int env_set, at30;
    int smooth;
    static uint32_t last_hit;
    uint32_t ra = *(volatile uint32_t *)((uintptr_t)g_esp + g_xbox_mem_offset);
    uint32_t now;

    sub_001A958A_gen();
    pirates_test_probe(g_eax);
    {   /* the main menu's 4:3 frames end when it stops running */
        static uint32_t menu_at;
        if (g_pirates_menu_seen) {
            g_pirates_menu_seen = 0;
            menu_at = g_eax;
        } else if (g_pirates_menu_narrow && g_eax - menu_at > 300u) {
            g_pirates_menu_narrow = 0;
            xbox_D3D8SetWideFrames(1);
        }
    }
    {   /* RECOMP_PIRATES_TICK_SITES=1: name each new caller once (which
         * limiter a scene uses) */
        static int sites = -1;
        static uint32_t seen[256];
        static unsigned nseen;
        if (sites < 0)
            sites = getenv("RECOMP_PIRATES_TICK_SITES") != NULL;
        if (sites) {
            unsigned i;
            for (i = 0; i < nseen && seen[i] != ra; i++)
                ;
            if (i == nseen && nseen < 256u) {
                seen[nseen++] = ra;
                fprintf(stderr, "[PIRATES] tick read from 0x%08X\n", ra);
            }
        }
    }
    if (!env_set) {
        /* The gate honours the title's interval only when told to. Told
         * always: the interval stays 1 unless interpolation is on and a 30
         * fps scene asks below, and interpolation can be turned on in the
         * settings menu while the game runs. */
        env_set = 1;
/* And a player's cap is a ceiling, not an override: at frame_cap = 60
         * a 30 fps scene still presents at 30, and interpolation fills it to
         * 60. Overruled, the sea presented every vblank, half of them
         * repeats, and interpolation had nothing to fill. */
#ifdef _WIN32
        _putenv("RECOMP_TITLE_INTERVAL=1");
        _putenv("RECOMP_TITLE_INTERVAL_CEILING=1");
#else
        setenv("RECOMP_TITLE_INTERVAL", "1", 1);
        setenv("RECOMP_TITLE_INTERVAL_CEILING", "1", 1);
#endif
    }
    /* What is in effect now (frame_interp, or the settings menu since). */
    smooth = hle_frame_interp() >= 2;
    if (!smooth) {
        if (at30) {                       /* turned off in a 30 fps scene */
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
    now = g_eax;
    if (ra == PIRATES_LIMITER_SEA || ra == PIRATES_LIMITER_SEA2 || ra == PIRATES_LIMITER_30MS ||
        ((ra == PIRATES_LIMITER_BTL || ra == PIRATES_LIMITER_DUEL) &&
         !(*(volatile uint8_t *)((uintptr_t)PIRATES_SLOW_FLAGS + g_xbox_mem_offset) & 0x20))) {
        last_hit = now;
        if (!at30) {
            at30 = 1;
            xbox_Nv2aFlipGateSetTitleInterval(2);  /* D3DPRESENT_INTERVAL_TWO */
        }
        /* Only while the gate really holds each Swap to two vblanks or more:
         * a frame cap the player set (frame_cap = 60, say) or the F10 toggle
         * decides instead of the title's interval, and then this wait is all
         * that keeps these scenes at their speed -- skipping it with the gate
         * at 60 ran the game twice too fast. */
        if (xbox_Nv2aFlipGateVblanksPerSwap() >= 2)
            g_eax = *(volatile uint32_t *)((uintptr_t)PIRATES_FRAME_START + g_xbox_mem_offset)
                    + 0x40000000u;               /* "31 ms have passed" */
    } else if (at30 && now - last_hit > 200u) {
        at30 = 0;
        xbox_Nv2aFlipGateSetTitleInterval(1);
    }
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    /*
     * Rate-limited per target, reporting at 1, 10, 100 ...
     *
     * An unresolved target inside a loop is the normal case, not the rare
     * one: Mortal Kombat: Deadly Alliance produced 46843 of these for a
     * single address in one 30-second run, seventeen lines each. That buries
     * every other diagnostic in the log and says nothing the first report did
     * not. The progression is the useful part -- one line says a target was
     * never identified, a run of them says the title is stuck on it.
     */
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }

    RECOMP_DIAG_LOCK();
    fprintf(stderr, "[ICALL] unresolved %starget 0x%08X -- %llu time(s) "
                    "(total calls: %llu)\n",
            g_icall_dispatch_form == 1 ? "call " :
            g_icall_dispatch_form == 2 ? "jump " : "",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    if (g_icall_dispatch_form == 2 && hits[i] == 1)
        fprintf(stderr, "  a jump, not a call: if this address is inside a "
                        "function rather than at its start, the guest is doing "
                        "its own control flow (a coroutine, a longjmp, or a "
                        "switch arm) and seeding it as a function will not "
                        "help\n");

    /*
     * Who called it. The lifted caller pushed its return address on the guest
     * stack, so code-range values above esp name the chain -- the same scan
     * the memory watchpoints use. It is a heuristic: stale return addresses
     * from earlier frames show up too, and the first entry is the reliable
     * one. It is still the difference between an address with no context and
     * a function to open, which is what an unresolved target needs.
     */
    {
        const uint8_t *stack = (const uint8_t *)g_xbox_mem_offset
                             + g_icall_saved_esp;
        int shown = 0;
        fprintf(stderr, "  callers:");
        for (i = 0; g_xbox_mem_offset && g_icall_saved_esp
                    && i < 160 && shown < 6; i++) {
            uint32_t v;
            memcpy(&v, stack + (size_t)i * 4, sizeof v);
            if (v >= g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "%s 0x%08X", shown ? " <-" : "", v);
                shown++;
            }
        }
        if (!shown)
            fprintf(stderr, " (none on the stack)");
        fprintf(stderr, "\n");
    }

    /* The recent-target ring buffer, once per address. What ran just before
     * an unresolved call is context for the first report and noise after. */
    if (hits[i] == 1) {
        fprintf(stderr, "  Recent ICALL targets:\n");
        for (i = 0; i < 16; i++) {
            int idx = (g_icall_trace_idx - 16 + i) & 15;
            if (g_icall_trace[idx])
                fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
        }
    }
    RECOMP_DIAG_UNLOCK();
    fflush(stderr);
}
/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va, uint32_t saved_esp)
{
    /* A power of ten, because the rate limiter above only reaches this
     * function body at 1, 10, 100 ... and the verdict has to land on one of
     * those. A hundred thousand skips of one target is unambiguous: no title
     * makes progress through that. */
    enum { SLOTS = 16, SPIN_VERDICT = 100000 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    static int said_it;   /* the verdict below is said once, not once per target */
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    {
        /* The call site, read off the guest stack. Without it the log
         * says a wild pointer was skipped but not by whom, and the
         * caller is the only thing that leads anywhere.
         *
         * It comes from the esp the dispatch macro captured, not from
         * g_esp. The lifted call site pushes its return address onto the
         * *local* esp and only syncs g_esp at certain points, so g_esp is
         * stale here -- it read 0 exactly when a null target most needed
         * explaining, which sent three rounds of Jet Set Radio Future
         * chasing inferences instead of a call site. saved_esp is the value
         * before that push, so the return address is the dword below it.
         *
         * Only for the call form. A tail jump pushes no return address, so
         * that dword is whatever the frame happened to leave there: Tony
         * Hawk's Pro Skater 2X reported "from 0x00000008" and then "from
         * 0x00000004" for the same million skipped calls, and both are
         * plainly not code. Printing a number that cannot be an address as
         * though it were the caller is worse than printing nothing, because
         * it is the one field a reader goes to next.
         *
         * So: name the form, and where the top of the stack cannot answer,
         * scan for code-range values the way the unresolved-target logger
         * above does. Stale return addresses from earlier frames come back
         * too and the first is the reliable one, but a list of real code
         * addresses is a place to start and 0x00000008 is not. */
        uint32_t caller = 0;
        if (g_icall_dispatch_form != 2 && saved_esp >= 4 && g_xbox_mem_offset)
            caller = *(const uint32_t *)((const uint8_t *)g_xbox_mem_offset
                                         + (saved_esp - 4));
        if (caller < g_xbox_code_lo || caller >= g_xbox_code_hi)
            caller = 0;
        RECOMP_DIAG_LOCK();
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped "
                        "%llu time(s) via a %s",
                va, (unsigned long long)hits[i],
                g_icall_dispatch_form == 2 ? "jump" : "call");
        if (caller)
            fprintf(stderr, " from 0x%08X", caller);
        fprintf(stderr, " (null or wild function pointer, at call #%llu)\n",
                (unsigned long long)g_icall_count);

        /* The stack scan, once per address: the same heuristic the
         * unresolved-target logger uses, and the only thing that names a
         * caller when the return address is not where it should be. */
        if (hits[i] == 1) {
            const uint8_t *stack = (const uint8_t *)g_xbox_mem_offset
                                 + saved_esp;
            int shown = 0, k;
            fprintf(stderr, "  callers:");
            for (k = 0; g_xbox_mem_offset && saved_esp
                        && k < 160 && shown < 6; k++) {
                uint32_t v;
                memcpy(&v, stack + (size_t)k * 4, sizeof v);
                if (v >= g_xbox_code_lo && v < g_xbox_code_hi) {
                    fprintf(stderr, "%s 0x%08X", shown ? " <-" : "", v);
                    shown++;
                }
            }
            if (!shown)
                fprintf(stderr, " (none on the stack)");
            fprintf(stderr, "\n");
        }

        /* RECOMP_ICALL_FATAL=1: fault here instead of skipping, so the crash
         * handler prints the guest call stack that reached this call.
         *
         * The caller printed above is read from the top of the guest stack,
         * which is right only when the lifted call site pushed a return
         * address there and nothing has since moved esp. When it reads 0 --
         * exactly the case where a null target most needs explaining -- the
         * line says a wild pointer was skipped and nothing about by whom. A
         * deliberate fault costs the run and buys the backtrace. */
        {
            static int fatal = -1;
            if (fatal < 0) {
                const char *v = getenv("RECOMP_ICALL_FATAL");
                fatal = v && *v && strcmp(v, "0") != 0;
            }
            if (fatal) {
                fprintf(stderr, "[ICALL] RECOMP_ICALL_FATAL: faulting here for "
                                "a backtrace\n");
                fflush(stderr);
                *(volatile int *)0 = 1;
            }
        }
    }

    /* Past a certain count this stops being a warning and becomes a verdict.
     *
     * A skipped call sets eax to 0 and returns. Zero is a fine answer for a
     * null function pointer in most code -- it reads as NULL, false, or
     * nothing -- but it is also S_OK, so in COM-shaped code a loop that
     * repeats while the result is non-negative can never leave. Panzer
     * Dragoon Orta hung exactly there, three billion skips in forty seconds,
     * with no frame ever presented.
     *
     * The value is deliberately left alone: there is no return value that is
     * right for both conventions, and guessing failure would break the
     * callers for which zero is correct. What can be fixed is the silence.
     * Say plainly that the title is hung, once, and say what to do about it.
     */
    if (hits[i] == SPIN_VERDICT && !said_it) {
        said_it = 1;
        fprintf(stderr,
            "[ICALL] ^^ this is a hang, not slow progress. One target has been\n"
            "        skipped %d times. The skip returns eax = 0, which is also\n"
            "        S_OK, so a loop testing for a non-negative result will\n"
            "        never exit. Try `py -3 -m tools.seed_from_log <log>` to\n"
            "        recover the target as a function, and read\n"
            "        docs/technical/memory-watchpoints.md for who wrote the\n"
            "        pointer. Set RECOMP_ICALL_SPIN_FATAL=1 to stop here\n"
            "        instead of spinning.\n", SPIN_VERDICT);
        fflush(stderr);
        if (getenv("RECOMP_ICALL_SPIN_FATAL")) {
            fprintf(stderr, "[ICALL] RECOMP_ICALL_SPIN_FATAL is set; exiting.\n");
            fflush(stderr);
            exit(3);
        }
    }
    RECOMP_DIAG_UNLOCK();
    fflush(stderr);
}

