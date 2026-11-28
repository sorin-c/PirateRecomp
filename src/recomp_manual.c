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
#include <math.h>     /* fabs: the sea map's part of a pixel */
#include <string.h>   /* strcmp: RECOMP_ICALL_FATAL */
#include <time.h>     /* timespec_get: the battle camera log */

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
int recomp_config_set(const char *key, const char *value);
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

/* ── Sid Meier's Pirates!: skip the legal screen ───────────────
 *
 * RECOMP_PIRATES_SKIP_LEGAL=1 (skip_legal, the default) leaves out the text
 * screen shown once at boot, after the logo movies: the Firaxis, Microsoft,
 * NDL, Bink and Miles copyright lines and the ESRB notice, with the Bink and
 * Miles logos. =0 shows it as the disc does.
 *
 * How: the boot state sub_001264A0 builds the screen (sub_000C0E40, its
 * constructor, thiscall-like with the object in edi and its time on screen
 * as a float at [esp+4]: 5.0, ret 4) and starts its thread, whose loop
 * (vtable slot 1, sub_000C0F80) draws it until the time shown reaches that
 * float or the object is told to stop. No button ends it. Meanwhile the boot
 * thread lets the .FPK cache copy run ([0x004D67B0]+0x38), loads the main
 * menu (sub_001003E0), then waits for the screen's thread to end and goes on
 * to the title screen (sub_001265B0). This wrapper hands the constructor 0.0
 * for that time: the screen's thread ends on its first pass without drawing,
 * and the loading, the cache copy and the wait run as they did, so the title
 * screen comes up as soon as the main menu is loaded. sub_001264A0 is its
 * only caller.
 *
 * Delete this when it is no longer wanted: at 0 it only passes through. */
extern void sub_000C0E40_gen(void);

void sub_000C0E40(void)
{
    static int skip = -1;

    if (skip < 0) {
        skip = recomp_config_bool("RECOMP_PIRATES_SKIP_LEGAL", "skip_legal", 1) ? 1 : 0;
        fprintf(stderr, "[PIRATES] legal screen: %s (RECOMP_PIRATES_SKIP_LEGAL)\n",
                skip ? "skipped" : "shown");
        fflush(stderr);
    }
    if (skip) {
        /* [esp] is the return address, [esp+4] the float seconds. */
        volatile float *secs = (volatile float *)((uintptr_t)(g_esp + 4u) + g_xbox_mem_offset);
        *secs = 0.0f;
    }
    sub_000C0E40_gen();
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
/* The front end in widescreen: the game draws its 4:3 screens (legal text,
 * title, main menu, the new-career pages) into the 16:9 buffer with more
 * background either side, so stretched to 16:9 they look wide. From start-up
 * and whenever the main menu runs, frames are called 4:3 (the toolkit shows
 * the buffer's centre between bars); the game world calls them 16:9 again --
 * the sea, battle and duel frame waits (sub_001A958A) and the town menu
 * (sub_00021D70). */
static volatile int g_pirates_front = -1;   /* -1 not said yet */

/* The HUD in widescreen: the game draws it as quads in front of the camera
 * (FVF 0x142) at its 4:3 places, so in 16:9 the crew count, the food and
 * the compass sat inside the middle 4:3. Moved sideways towards the
 * picture's edges (xbox_D3D8SetEdgeTest): pieces drawn from the HUD bar
 * (128x32) or the icon and font sheets (256x256) whose middle is in the top
 * band or the bottom-right corner, by which third of the screen they are in.
 * Each group ends as far from its side edge as it is from the top (the
 * compass: from the bottom), on screen, so its corner looks square: in the
 * 720-wide buffer that the 16:9 picture stretches by 1.185, the sword ring's
 * 41-row top gap is a 35-pixel side gap, the food's the same, the compass's
 * 35-row bottom gap a 30-pixel one. Measured at sea, widescreen: the ring
 * edges of the 4:3 layout at buffer x 87 (sword), 676 (food), 678 (compass).
 * The date banner (bottom middle), the town labels (their own 128x64 boxes)
 * and the world stay where they are.
 *
 * Only while a game-world scene runs: the pause menu (status, ship, log,
 * the world map's "Select City") and the town menus draw their text and
 * button prompts from the same font and icon sheets, laid out for 16:9 by
 * the game itself, and the test moved them too -- the map's B BACK prompt
 * slid off its box past the right edge, FLEET STORES and the scroll's R
 * button out of place (xemu, widescreen, draws them where they were). Those
 * screens never run inside the world's frames (sub_001A958A below), so the
 * test answers 0 outside them (pirates_hud_scene). */
#define PIRATES_WORLD_RECENT_MS 150u
static volatile uint32_t g_pirates_world_ms;   /* host ms of the last world frame wait */
static volatile int      g_pirates_world_seen;

/* The toolkit's monotonic clock (QueryPerformanceCounter on Windows): scene
 * timing must not jump when the wall clock is set back or forward. */
extern int64_t host_time_ns(void);

static uint32_t pirates_host_ms(void)
{
    return (uint32_t)(host_time_ns() / 1000000);
}

/* At sea the answer is exact, frame by frame: the sea's frame functions read
 * the tick first thing (0x9190F, 0xA43FA: time since the last frame) and
 * last (their frame waits, 0x91DE1, 0xA4AEE), and draw the HUD in between;
 * the pause menu's frames run outside them. A timer alone let the first
 * menu frames (BACK, the scroll's R) and the first sea frames after it (the
 * HUD) jump. Battles and duels, which have no such start read, keep the
 * timer. */
static volatile int g_pirates_in_sea_frame;
static volatile int g_pirates_battle_seen;
static volatile uint32_t g_pirates_battle_ms;

static volatile uint32_t g_pirates_ship_battle_ms;  /* a ship battle's frame wait (0xC3B4E) */
static volatile int      g_pirates_ship_battle_seen;

static int pirates_hud_scene(void)
{
    if (g_pirates_in_sea_frame)
        return 1;
    return g_pirates_battle_seen &&
           pirates_host_ms() - g_pirates_battle_ms <= PIRATES_WORLD_RECENT_MS;
}

/* A ship battle's HUD, not a duel's or a land battle's. */
static int pirates_hud_battle(void)
{
    return !g_pirates_in_sea_frame && g_pirates_ship_battle_seen &&
           pirates_host_ms() - g_pirates_ship_battle_ms <= PIRATES_WORLD_RECENT_MS;
}

typedef int (*pirates_edge_test_t)(uint32_t handle, unsigned w, unsigned h, unsigned fmt,
                                   const void *vertices, unsigned stride, unsigned count,
                                   const float *world, const float *view, const float *proj);
extern void xbox_D3D8SetEdgeTest(pirates_edge_test_t test);

static void pirates_mat_mul(float *o, const float *a, const float *b)
{
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            float v = 0.0f;
            for (k = 0; k < 4; k++)
                v += a[r * 4 + k] * b[k * 4 + c];
            o[r * 4 + c] = v;
        }
}

#define PIRATES_HUD_DX_LEFT    (-53)    /* sword ring 87 -> 35 (now 47 -> 35: 40 + 13) */
#define PIRATES_HUD_DX_RIGHT   (57)     /* food ring 676 -> 720 - 35 */
#define PIRATES_HUD_DX_COMPASS (64)     /* compass ring 678 -> 720 - 30 */

/* A ship battle's HUD (pirates_hud_battle), in widescreen: drawn from the
 * HUD sheet (256x128), the bars' fills (untextured), the font (256x256) and
 * the flags (cloth meshes, FVF 0x152, 128x128, 60 vertices), as quads in
 * front of the camera at their 4:3 places, the 720-wide buffer stretched
 * 1.185 to 16:9. Moved sideways only, each group as far from its side as
 * from the top (or bottom) on screen -- a gap of g rows is g / 1.185
 * columns: the two bars with their flags, dots, ammo and crew counts (top
 * gap 40 rows: 34 columns; their outer edges at x 89 and 631), the ammo
 * ring (bottom gap 40: 34; at x 83), and the compass, bottom middle on the
 * console, into the bottom-right corner (bottom gap 33: 28; its right edge
 * at 393). A second enemy's bar (bottom right, x 431..631) sits just left
 * of the compass, 10 columns clear. Text boxes and prompts in the middle
 * rows stay where they are. */
#define PIRATES_BTL_DX_TOP_LEFT     (-55)   /* 89 -> 34 */
#define PIRATES_BTL_DX_TOP_RIGHT    (55)    /* 631 -> 686 */
#define PIRATES_BTL_DX_RING         (-49)   /* 83 -> 34 */
#define PIRATES_BTL_DX_COMPASS      (299)   /* 327..393 -> 626..692 */
#define PIRATES_BTL_DX_BOTTOM_RIGHT (-15)   /* 631 -> 616, 10 left of the compass */

static int pirates_hud_battle_dx(unsigned handle, unsigned w, unsigned h, unsigned fmt,
                                 unsigned count, float cx, float cy, float lo_x, float hi_x)
{
    int sheet = handle == 0x142u && ((w == 256u && h == 128u && fmt == 0x06u) ||
                                     (w == 0u && h == 0u) ||
                                     (w == 256u && h == 256u && fmt == 0x0Eu));
    int flag = handle == 0x152u && w == 128u && h == 128u && fmt == 0x0Cu && count == 60u;

    if (!sheet && !flag)
        return 0;
    if (cy < 140.0f)                                     /* the two bars' groups */
        return cx < 360.0f ? PIRATES_BTL_DX_TOP_LEFT : PIRATES_BTL_DX_TOP_RIGHT;
    if (flag || cy < 330.0f)
        return 0;                                        /* the middle: dialogs */
    if (cx < 240.0f)
        return PIRATES_BTL_DX_RING;
    if (lo_x > 320.0f && hi_x < 400.0f)
        return PIRATES_BTL_DX_COMPASS;
    if (cx > 420.0f)
        return PIRATES_BTL_DX_BOTTOM_RIGHT;
    return 0;
}

/* RECOMP_PIRATES_CAM_LOG=<file> (test): in a ship battle, the camera -- the
 * view matrix of the first HUD draw of each battle frame -- with the host
 * time, a line a frame: "us m00 .. m33 | ..." (btl_cam_extra has the rest).
 * Needs widescreen (the HUD's edge test). Set by btl_begin_frame. */
static volatile int g_cam_new_frame;
static void btl_cam_extra(FILE *f);
static void pirates_cam_log(const float *view)
{
    static int on = -1;
    static FILE *f;
    if (on < 0) {
        const char *v = getenv("RECOMP_PIRATES_CAM_LOG");
        on = v && *v && (f = fopen(v, "w")) != NULL;
        if (f) setvbuf(f, NULL, _IOFBF, 1 << 20);
    }
    if (on && view) {
        struct timespec ts;
        int i;
        timespec_get(&ts, TIME_UTC);
        fprintf(f, "%lld", (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
        for (i = 0; i < 16; i++)
            fprintf(f, " %.4f", view[i]);
        /* then the battle camera's own centre and height (sub_000C4220),
         * whether this frame ticked, and its length */
        btl_cam_extra(f);
        fflush(f);   /* the test stops the game without closing it */
    }
}

/* hud_layout (RECOMP_PIRATES_HUD_LAYOUT, F2 Display "HUD layout", live):
 * "edges", the default, moves the HUD out to the picture's edges as above;
 * "centered" leaves every piece where the game draws it, in the middle 4:3,
 * as the console shows it in widescreen. Read from the settings at most
 * four times a second (the test runs for every HUD draw), so a change in
 * the F2 menu shows within a quarter of a second. Called only from
 * pirates_hud_edge, which the D3D8 draw path runs on its one thread (it
 * works on the unlocked g_device_state there too), so the cache needs no
 * lock. */
static int pirates_hud_centered(void)
{
    static int centered = -1;
    static uint32_t read_ms;
    uint32_t now = pirates_host_ms();

    if (centered < 0 || now - read_ms >= 250u) {
        const char *v = recomp_config_lookup("RECOMP_PIRATES_HUD_LAYOUT", "hud_layout");
        int c = v && (strcmp(v, "centered") == 0 || strcmp(v, "centred") == 0 ||
                      strcmp(v, "center") == 0 || strcmp(v, "4:3") == 0);
        read_ms = now;
        if (c != centered) {
            if (centered >= 0 || c)
                fprintf(stderr, "[PIRATES] HUD layout: %s\n",
                        c ? "centered 4:3, as the console" : "anchored to the edges");
            centered = c;
        }
    }
    return centered;
}

static int pirates_hud_edge(uint32_t handle, unsigned w, unsigned h, unsigned fmt,
                            const void *vertices, unsigned stride, unsigned count,
                            const float *world, const float *view, const float *proj)
{
    int battle;
    float wv[16], m[16], lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f, cx, cy;
    unsigned i;

    battle = pirates_hud_battle();
    if (battle && g_cam_new_frame) {
        g_cam_new_frame = 0;
        pirates_cam_log(view);
    }
    if (pirates_hud_centered())
        return 0;                            /* hud_layout = centered: as drawn */
    if (!battle && (handle != 0x142u || !(fmt == 0x0Fu && w == 128u && h == 32u) &&
                    !(fmt == 0x0Eu && w == 256u && h == 256u)))
        return 0;
    if (battle && handle != 0x142u && handle != 0x152u)
        return 0;
    if (!pirates_hud_scene())
        return 0;                            /* a menu, not the world */
    pirates_mat_mul(wv, world, view);
    pirates_mat_mul(m, wv, proj);
    for (i = 0; i < count; i++) {
        const float *v = (const float *)((const char *)vertices + (size_t)i * stride);
        float x = v[0] * m[0] + v[1] * m[4] + v[2] * m[8]  + m[12];
        float y = v[0] * m[1] + v[1] * m[5] + v[2] * m[9]  + m[13];
        float ww = v[0] * m[3] + v[1] * m[7] + v[2] * m[11] + m[15];
        if (!(ww > 1e-6f))
            return 0;
        x = (x / ww * 0.5f + 0.5f) * 720.0f;
        y = (0.5f - y / ww * 0.5f) * 480.0f;
        if (x < lo_x) lo_x = x;
        if (x > hi_x) hi_x = x;
        if (y < lo_y) lo_y = y;
        if (y > hi_y) hi_y = y;
    }
    cx = (lo_x + hi_x) * 0.5f;
    cy = (lo_y + hi_y) * 0.5f;
    if (battle && handle == 0x152u) {
        /* A flag: the HUD's are just in front of the camera (w about 720),
         * the ships' far out (5000 and more). */
        const float *v = (const float *)vertices;
        if (!(v[0] * m[3] + v[1] * m[7] + v[2] * m[11] + m[15] < 1500.0f))
            return 0;
    }
    if (battle)
        return pirates_hud_battle_dx(handle, w, h, fmt, count, cx, cy, lo_x, hi_x);
    /* The sea's HUD has fixed places; the towns' names (the font, as the
     * HUD's numbers are) move with the world, and the test moved those too
     * whenever one passed the top band or the compass's corner: the name
     * slid a box-width right of its own box (which, a 128x64 texture, never
     * moves). So a piece moves only where the HUD's own are: in the top
     * band the rings (y 32..82, 43..71), the bars and the counts (44..67,
     * 45..67) -- at most four digits, 16 vertices, where a town's name is 5
     * letters or more; the compass, single quads centred on its middle
     * (599, 411 in the buffer: its four pieces, 566..632 x 378..444,
     * 572..626 x 400..420, 576..623 x 387..434, 589..611 x 366..456). A
     * town's name is drawn in one call, 4 vertices a letter, so never as a
     * single quad, and would also have to sit exactly on that middle. */
    if (lo_y >= 30.0f && lo_y <= 47.0f && hi_y >= 65.0f && hi_y <= 84.0f && count <= 16u &&
        hi_x - lo_x <= 130.0f)               /* top band: crew, food */
        return cx < 240.0f ? PIRATES_HUD_DX_LEFT : cx > 480.0f ? PIRATES_HUD_DX_RIGHT : 0;
    if (count == 4u && cx > 596.0f && cx < 603.0f && cy > 407.0f && cy < 414.0f &&
        lo_x >= 560.0f && hi_x <= 640.0f && lo_y >= 360.0f && hi_y <= 462.0f)
        return PIRATES_HUD_DX_COMPASS;       /* bottom right: the compass */
    return 0;
}

/* LB and RB at sea. The game's default bindings give the bumpers to the
 * triggers (RECOMP_TITLE_BUMPERS_AS_TRIGGERS, patch 0040), which the pause
 * menu's scroll and the other screens use; but sailing reads Black (the map
 * overlay) and White (the sails), and has no use for the triggers. So while
 * the sea's frame wait runs (sub_001A958A, the last 150 ms) LB and RB are
 * Black and White again, as on a pad with no triggers to spare (toolkit
 * patch 0041); LT and RT, and the W and Q keys, work everywhere. */
typedef int (*pirates_bumper_test_t)(void);
extern void recomp_bindings_set_bumper_test(pirates_bumper_test_t test);
static volatile uint32_t g_pirates_sea_ms;
static volatile int      g_pirates_sea_seen;

static int pirates_bumpers_black_white(void)
{
    int on = g_pirates_sea_seen && pirates_host_ms() - g_pirates_sea_ms <= PIRATES_WORLD_RECENT_MS;
    static int said = -1;
    if (on != said) {
        said = on;
        fprintf(stderr, "[PIRATES] LB/RB now %s\n", on ? "Black/White (sailing)" : "LT/RT");
    }
    return on;
}

static void pirates_front_end(int on)
{
    if (g_pirates_front != on) {
        g_pirates_front = on;
        xbox_D3D8SetEdgeTest(pirates_hud_edge);
        recomp_bindings_set_bumper_test(pirates_bumpers_black_white);
        xbox_D3D8SetWideFrames(on ? 0 : 1);
    }
}

void sub_00126BC0(void)
{
    pirates_front_end(1);
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

/* ── Sid Meier's Pirates!: EXIT on the main menu ───────────────
 *
 * The console's main menu has no way out (the dashboard button does that),
 * so on a PC the only way to quit was the window's close button. An EXIT row
 * is added under OPTIONS, drawn by the game's own menu code in the same font,
 * colours, spacing and cursor bar as the rows above it, and chosen with the
 * pad or keys like them. Choosing it quits as closing the window does
 * (xbox_HostExit: the logs flushed, ExitProcess(0)).
 *
 * How (guest hooks, toolkit patch 0042, all in sub_00126BC0):
 * - The rows are 0..6 in [0x004D5D14] (NEW GAME, LOAD GAME, VS MODE, the
 *   debug MINIGAMES, BONUS CONTENT, XBOX LIVE, OPTIONS); EXIT is row 7.
 *   Each row looks its text up in the string table (sub_0013E4F0, which
 *   fills the text buffer 0x004AA548) and draws it: the chosen row with
 *   sub_00125DE0 (the bar, the text), the others with sub_000E0EA0 and
 *   sub_000E11B0. At 0x00126F4E, after OPTIONS and before the frame's text
 *   goes out, EXIT is drawn the same way one row lower: edi is OPTIONS' row
 *   (5, or 6 with MINIGAMES), y = (row * 5 + 20) * 8, x in ebp. The text is
 *   the table's own "Exit" (Quitter, Beenden, Salir, Esci), in capitals like
 *   the other rows.
 * - Up from NEW GAME the game wraps to OPTIONS (0x00126FD4), down from
 *   OPTIONS to NEW GAME (0x00127054): both go to EXIT instead. From EXIT
 *   the game's own steps already land right (up: 6; down: past 6, so 0),
 *   and on row 7 every other row is drawn unchosen.
 * - A (event 0xD) on row 7: the game plays its confirm sound and finds no
 *   row to start (row > 6); the hook at 0x001270B0 quits. */
extern void sub_0013E4F0(void);
extern void sub_00125DE0(void);
extern void sub_000E0EA0(void);
extern void sub_000E11B0(void);
extern void xbox_HostExit(const char *why);

#define PM_SEL       0x004D5D14u   /* the main menu's row */
#define PM_TEXT      0x004AA548u   /* the menu's text buffer */
#define PM_STRINGS   0x004F8FE8u   /* the string table (esi for sub_0013E4F0) */
#define PM_FONT      0x004D5444u
#define PM_KEY_EXIT  0x003E841Cu   /* "Exit", a key of the string table */
#define PM_ROW_LAST  6             /* OPTIONS, the game's last row */
#define PM_ROW_EXIT  7

#define PM_MEM32(a) (*(volatile uint32_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))
#define PM_MEM8(a)  (*(volatile uint8_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

static int g_menu_row_before = -1;   /* the row as this frame's input came in */

static void pm_push(uint32_t v)
{
    g_esp -= 4;
    PM_MEM32(g_esp) = v;
}

/* A guest call from a hook: the return address the callee's ret pops. */
static void pm_call(void (*fn)(void), uint32_t ret_va)
{
    pm_push(ret_va);
    fn();
}

static void menu_draw_exit(void)
{
    uint32_t sv_eax = g_eax, sv_ebx = g_ebx, sv_ecx = g_ecx, sv_edx = g_edx;
    uint32_t sv_esi = g_esi, sv_edi = g_edi, sv_esp = g_esp;
    uint32_t sv_ebp = g_ebp, sv_seh = g_seh_ebp;
    uint32_t x = g_ebp;
    uint32_t y = ((g_edi + 1u) * 5u + 20u) * 8u;
    int chosen = PM_MEM32(PM_SEL) == PM_ROW_EXIT;
    uint32_t text, i;

    pm_push(PM_KEY_EXIT);
    g_esi = PM_STRINGS;
    pm_call(sub_0013E4F0, 0x00126F4Eu);
    g_esp += 4;
    text = g_eax;
    /* Capitals, as the other rows. The rows draw from the text buffer the
     * lookup copied into (PM_TEXT), so that copy is the one changed; the
     * string the lookup returns is the table's own and stays as it is. */
    for (i = 0; i < 255u; i++) {
        uint8_t c = PM_MEM8(PM_TEXT + i);
        if (!c)
            break;
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0u && c <= 0xFEu && c != 0xF7u))
            PM_MEM8(PM_TEXT + i) = (uint8_t)(c - 0x20u);
    }
    {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[PIRATES] main menu: EXIT row added (\"%.32s\")\n",
                    (const char *)((uintptr_t)PM_TEXT + g_xbox_mem_offset));
            fflush(stderr);
        }
    }

    if (chosen) {
        pm_push(0x64u);
        pm_push(x);
        pm_push(PM_TEXT);
        g_eax = text;
        g_ebx = y;
        pm_call(sub_00125DE0, 0x00126F4Eu);
        g_esp += 0xCu;
    } else {
        uint32_t r;
        pm_push(0x64u);
        pm_push(PM_MEM32(PM_FONT));
        pm_push(0x23u);
        pm_push(0x136u);
        g_eax = text;
        pm_call(sub_000E0EA0, 0x00126F4Eu);
        r = g_eax;
        pm_push(0u);
        pm_push(0xFFFFFFFFu);
        pm_push(PM_MEM32(PM_FONT));
        pm_push(4u);
        pm_push(0xFFE8E8E8u);              /* the unchosen rows' colour */
        pm_push(r);
        pm_push(PM_TEXT);
        g_eax = y;
        g_ecx = x;
        pm_call(sub_000E11B0, 0x00126F4Eu);
        g_esp += 0x2Cu;
    }

    g_eax = sv_eax; g_ebx = sv_ebx; g_ecx = sv_ecx; g_edx = sv_edx;
    g_esi = sv_esi; g_edi = sv_edi; g_esp = sv_esp;
    g_ebp = sv_ebp; g_seh_ebp = sv_seh;
}

/* The rows a little closer together. At the game's 40 lines a row the
 * eighth row's cursor bar (y 400) covered the A SELECT prompt at the bottom
 * left; at 34 (30 with MINIGAMES, one row more) every row, EXIT's bar
 * included, ends above it. NEW GAME stays where it was, under the logo.
 * Applied where the two row-drawing functions start, only when the main
 * menu called them (their return address inside sub_00126BC0): y is ebx for
 * sub_00125DE0, eax for sub_000E11B0. */
#define PM_FUNC_START 0x00126BC0u
#define PM_FUNC_END   0x00127216u
#define PM_ROW0_Y     160u
#define PM_ROW_STEP   40u

static uint32_t menu_row_y(uint32_t y)
{
    uint32_t ret = PM_MEM32(g_esp);
    uint32_t step = PM_MEM8(0x004D69C0u) ? 30u : 34u;   /* MINIGAMES shown: 8 rows */

    if (ret < PM_FUNC_START || ret >= PM_FUNC_END || y < PM_ROW0_Y ||
        (y - PM_ROW0_Y) % PM_ROW_STEP != 0)
        return y;
    return PM_ROW0_Y + (y - PM_ROW0_Y) / PM_ROW_STEP * step;
}

/* The main menu's hooks; 0 for an address that is not one of them. */
static int menu_guest_hook(uint32_t va)
{
    switch (va) {
    case 0x00125DE0u:                   /* the chosen row: bar and text */
        g_ebx = menu_row_y(g_ebx);
        return 1;
    case 0x000E11B0u:                   /* a row's text */
        g_eax = menu_row_y(g_eax);
        return 1;
    case 0x00126F4Eu:                   /* the rows are drawn; input next */
        g_menu_row_before = (int)PM_MEM32(PM_SEL);
        menu_draw_exit();
        return 1;
    case 0x00126FD4u:                   /* up, the row moved */
        if (g_menu_row_before == 0 && PM_MEM32(PM_SEL) == PM_ROW_LAST)
            PM_MEM32(PM_SEL) = PM_ROW_EXIT;
        return 1;
    case 0x00127054u:                   /* down, the row moved */
        if (g_menu_row_before == PM_ROW_LAST && PM_MEM32(PM_SEL) == 0)
            PM_MEM32(PM_SEL) = PM_ROW_EXIT;
        return 1;
    case 0x001270B0u:                   /* A: start the row */
        if (PM_MEM32(PM_SEL) == PM_ROW_EXIT) {
            fprintf(stderr, "[PIRATES] EXIT chosen on the main menu\n");
            fflush(stderr);
            xbox_HostExit("EXIT on the main menu");
        }
        return 1;
    }
    return 0;
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

/* Town menu calls running (sub_00021D70): the town visit, every screen of
 * it (the menu, the merchant, the shipwright, the plunder, the status, the
 * rooms) runs inside it. town_fps paces them all (sub_002C0F70 below). */
static int g_town_menu_depth;

void sub_00021D70(void)
{
    pirates_front_end(0);                /* a town: the game world */
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
    g_town_menu_depth++;
    sub_00021D70_gen();
    g_town_menu_depth--;
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
 *   teleport.txt  written by the script: "x y" in map units (fractions
 *               allowed, to place the view finely) moves the
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
    int i, n; double tx, ty;

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
            if (fscanf(fp, "%lf %lf", &tx, &ty) == 2) {
                *(volatile int32_t *)((uintptr_t)0x004564BCu + g_xbox_mem_offset) = (int32_t)(tx * 4000000.0);
                *(volatile int32_t *)((uintptr_t)0x004564C0u + g_xbox_mem_offset) = (int32_t)(ty * 4000000.0);
                fprintf(stderr, "[PIRATES] test probe: ship moved to %.2f,%.2f\n", tx, ty);
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
 * start (sea: 0x00091DC2-0x00091DF5 in sub_00091170; the town sneak at
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
/* ── Sid Meier's Pirates!: sailing faster than 30 (sea_fps) ─────────────
 *
 * sea_fps = 60, 90, 120, 144, 240 or 0 for unlocked (RECOMP_PIRATES_SEA_FPS,
 * or the F2 menu's "Sailing frame rate", applies at once; 30 by default, and
 * sea_60 = 1, the setting before it, still means 60) runs the sea's own game
 * logic that many times a second at the speed it has at 30. Battles, duels,
 * towns, the town sneak, the map and the menus keep their pacing. At 30,
 * nothing below runs and the game is as before.
 *
 * Pacing (sub_001A958A): at 60 the sea's frame wait (0x91DE1) passes after
 * SEA60_WAIT_MS instead of 31 and the flip gate's one Swap a vblank holds it
 * to 60; above 60 and unlocked the wait passes at once and the gate paces the
 * sea by the host clock at the rate (toolkit patch 0044), renewed every sea
 * frame and lapsing 100 ms after the last, so a dialog or another scene gets
 * its own pacing back. Frame interpolation is not used at sea then; battles
 * and duels still use it when it is on. Only sub_00091170, the sea: the
 * other "sea" wait, 0xA4AEE, belongs to the town sneak (sub_000A1E10,
 * town.cpp), which has sneak_fps.
 *
 * Nothing below depends on the rate: frame times are the host clock's (in
 * microseconds), the logic tick comes when 33.3 ms have passed (every 2nd
 * frame at 60, every 8th at 240), and steps scaled by the frame time carry
 * their remainders (sea60_scale), so small steps at high rates still add up.
 *
 * Every change below goes through guest hooks (toolkit patch 0042; the list
 * is config/hooks/5454008F.json), so a clean lift reproduces it.
 *
 * 1. Frame time. The sea scales motion by its frame time (0x4C72F8, ms) but
 *    clamps it to 33..100 ms in three places in sub_00085980, so at 16 ms
 *    every step was a 33 ms step. After each clamp the real frame time is put
 *    back: 0x86078 (time acceleration 0x40D4C4, which feeds positions and
 *    the calendar), 0x864FE (speed), 0x86588 (turning). The frame time is
 *    taken from the host clock between sea frames (the engine's is whole ms
 *    truncated: 60 frames of 16.67 ms read 960 ms), given to the game in
 *    whole ms with the fraction carried (4, 4, 4, 5 at 240) and written to
 *    0x4C72F8 too, for the sea's other readers (sub_00077680's timers and
 *    chances).
 *
 * 2. The 30 Hz logic tick. What the sea counts in frames runs on a tick: a
 *    sea frame ticks when the frame time summed since the last tick reaches
 *    33.3 ms (at 30 every frame, at 60 every other one). On a frame that does
 *    not tick each of these is held back:
 *      0x9259F  the frame counter 0x49C288 (its readers: timestamps in
 *               0x4C70F0 and ages, which then count at 30 as they should)
 *      0x85BC0, 0x91C19, 0x91C41  countdowns 0x4C72FC, 0x4D65B8, 0x4D6600
 *      0x79433, 0x87240, 0x86387  ship words +0x4564D6, +0x45650E, +0x4564D0
 *      0x80958  ship word +0x4564FC (a sighting's stage, frames/40)
 *      0x92590  countdown 0x4D65EC (40 frames)
 *      0x86891  a ship's speed easing toward its target (0x4564C8)
 *      0x785E7  the lifetime of the sea objects in 0x4B6434 (sub_000785C0)
 *      "every Nth frame" tests on the counter, which would pass on both
 *      frames of a tick: 0x8216B, 0x83AF1, 0x84380, 0x8448A, 0x845FD,
 *      0x84720, 0x848C2, 0x87537 (bit 0 set in the tested register), and
 *      0x7FE92 / 0x86702 (the counter bumped for the test only when it would
 *      pass, put back at 0x7FF16 / 0x86711)
 *      0x77C81  sub_00077680's one chance in 150 a frame
 *    The calendar (0x49C284) advances [0x40D4C4]/100 ticks a frame, the
 *    remainder dropped, as the console does at 30: on the tick frame it is
 *    given the 30 fps time acceleration (5 below), and nothing between
 *    (0x85AAD, and 0x85B73 after the loop and its 4000-tick check).
 *
 * 3. Phases made as frame counter x step move by the part of a step not yet
 *    reached on a frame that does not tick, so they turn smoothly 60 times a
 *    second: 0x7F892 (our ship's bob), 0x85202 (the AI ships'), 0x83F9F,
 *    and the imul [0x49C288] pairs 0x81CB6/0x81CBD, 0x82233/0x8223A,
 *    0x8272A/0x82731.
 *
 * 4. Steps eased or moved every frame, scaled by the frame time (33.3 ms =
 *    one console frame): the sea objects' 100 units (0x7860C, 0x78620); the
 *    1/32 easing of 0x40D018 (0x7FE90); sub_00078EC0's easing of each ship's
 *    value, 1/16 or 1/8 toward a random target (0x78F69, 0x78F9B).
 *
 * 5. The time acceleration 0x40D4C4 (s += (t - s)/16 +- 1 a frame, sometimes
 *    forced to 100, 50 or 1) is run as at 30 on the ticks and stored as this
 *    frame's share of it (0x86015 reads it back in 30 fps units, so the
 *    game's cap of 200 and its forced values apply as at 30; 0x86078 takes M
 *    and s, 0x860B0 stores); a value
 *    the game sets itself (500 leaving a town, 100, the floor of 150) is a
 *    30 fps value, and the floor's test compares the 30 fps value (0x84D90).
 *    Readers that multiply it by the frame time again or cap it at a 30 fps
 *    limit get the 30 fps value: sub_00077680 (0x7768E) and turning
 *    (0x86568; with the share, turning near other ships ran at half speed).
 *    A ship word taking it/100 a frame takes the 30 fps amount on the tick
 *    (0x86D52). Readers that step by it each frame keep the share: positions
 *    (0x868DC, 0x86929, sub_00094270), 0x81187.
 *
 * Going to a battle, a town, the map or a menu and back: a sea frame that
 * comes more than 250 ms after the last one starts a fresh tick with nothing
 * carried in. Nothing of this is saved; saves are the same either way. */
#define SEA60_WAIT_MS 15u
#define SEA60_TICK_US 33333       /* one console frame: the logic tick */
#define SEA60_DT_MAX  100
static struct {
    int setting;          /* the sailing rate as last read (30 = off) */
    int rate;             /* this sea frame's rate: 60..1000, or -1 unlocked */
    int active;           /* this sea frame runs faster than 30 */
    int tick;             /* this sea frame advances the 30 Hz logic */
    int dt;               /* the frame time the game is given (whole ms, carried) */
    int dt_us;            /* the frame time, microseconds */
    int acc_us;           /* time since the last tick, toward SEA60_TICK_US */
    int ms_acc_us;        /* what the whole-ms frame times have not given yet */
    int64_t last_us;      /* host time of the last sea frame start */
    int32_t cal_acc;      /* time scale summed since the last tick */
    int32_t cal_saved;    /* 0x40D4C4 while the calendar loop runs */
    int cal_live;
    int frac;             /* hundredths of a tick to the next one, <= 0 */
    uint32_t phase_k;     /* a phase step saved by a hook before its imul */
    uint32_t ctr_pending; /* frame counter bumped for one test, to put back */
    uint32_t ts_m, ts_sin;          /* time acceleration: M and s coming in */
    int32_t ts_x, ts_rem, ts_last;  /* its 30 fps value, remainder, last stored */
    int ts_valid;
    int in_loop;          /* inside sub_00091170's frame (0x9190F .. 0x92681) */
    int gate_rate;        /* the rate the flip gate was last given (0: its own) */
    int32_t rem[8];       /* the remainders of the scaled steps (sea60_scale) */
} g_sea60;

static int64_t pirates_host_us(void)
{
    return host_time_ns() / 1000;
}

/* A frame-rate setting (sea_fps, battle_fps, town_fps): a whole number, or
 * "unlocked" (also 0), spaces around it allowed. Anything else -- unset, a
 * typo, "default" -- is the console's 30, never unlocked by accident.
 * Returns -1 for unlocked, else the number. */
static int pirates_parse_fps(const char *v)
{
    const char *p = v;
    char *end;
    long r;

    if (!p)
        return 30;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "unlocked", 8) == 0) {
        r = 0;
        end = (char *)p + 8;
    } else {
        r = strtol(p, &end, 10);
        if (end == p)
            end = (char *)"?";               /* not a number */
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        end++;
    if (*end || r < 0) {
        if (*p) {                            /* said once per bad text */
            static char said[64];
            if (strncmp(said, v, sizeof said - 1) != 0) {
                snprintf(said, sizeof said, "%s", v);
                fprintf(stderr, "[PIRATES] frame rate \"%s\" not understood: 30\n", v);
            }
        }
        return 30;
    }
    if (r == 0)
        return -1;
    return r > 1000 ? 1000 : (int)r;
}

/* Whether a setting's text is the word w, spaces around it allowed. */
static int pirates_word_is(const char *v, const char *w)
{
    size_t n = strlen(w);

    if (!v)
        return 0;
    while (*v == ' ' || *v == '\t')
        v++;
    if (strncmp(v, w, n) != 0)
        return 0;
    for (v += n; *v == ' ' || *v == '\t' || *v == '\r' || *v == '\n'; v++)
        ;
    return *v == '\0';
}

/* A scene's frame-rate setting, as its reader parses it. The scene's own key
 * (sea_fps, battle_fps, ... or its environment variable) when it says a rate;
 * when it is "default" (the F2 menu's No override, on its Debug tab) or
 * empty, the game's frame rate, game_fps (RECOMP_PIRATES_GAME_FPS, the F2
 * menu's Display > Frame rate), which is the same for every scene. game_fps
 * at 30 or "console" (its default) gives NULL: each reader's own console
 * rate, 30 at sea, 33 in towns, 60 in the menus and the dancing. */
static const char *pirates_scene_fps(const char *env, const char *key)
{
    const char *v = recomp_config_lookup(env, key);

    if (v && *v && !pirates_word_is(v, "default"))
        return v;
    v = recomp_config_lookup("RECOMP_PIRATES_GAME_FPS", "game_fps");
    if (!v || !*v || pirates_word_is(v, "console") || pirates_word_is(v, "30") ||
        pirates_word_is(v, "default"))
        return NULL;
    return v;
}

/* A settings file from before game_fps: every scene had its own rate, and
 * the launcher wrote each one out at its default (30, or 60 for the menus and
 * the dancing). Read as overrides, those would keep every scene at the
 * console's rate whatever game_fps says, so a scene still at its old default
 * becomes "default" (No override) once, at start. A rate the player chose
 * stays an override. Nothing is written when the file already has game_fps
 * or no scene is at its old default. */
void pirates_fps_settings_upgrade(void)
{
    static const struct { const char *key, *old_def; } scenes[] = {
        { "sea_fps", "30" }, { "battle_fps", "30" }, { "land_fps", "30" },
        { "duel_fps", "30" }, { "town_fps", "30" }, { "sneak_fps", "30" },
        { "dance_fps", "60" }, { "menu_fps", "60" },
    };
    size_t i;

    if (recomp_config_lookup(NULL, "game_fps"))
        return;
    for (i = 0; i < sizeof scenes / sizeof scenes[0]; i++) {
        const char *v = recomp_config_lookup(NULL, scenes[i].key);

        if (v && pirates_word_is(v, scenes[i].old_def)) {
            recomp_config_set(scenes[i].key, "default");
            fprintf(stderr, "[PIRATES] %s = %s was the old default: now default "
                    "(follows game_fps)\n", scenes[i].key, scenes[i].old_def);
        }
    }
}

/* step * frac: how far a phase built as counter * step is from where the
 * frame's time puts it (0 on a tick frame). */
static uint32_t sea60_phase(uint32_t step)
{
    return (uint32_t)(int32_t)((int64_t)(int32_t)step * g_sea60.frac / 100);
}

/* A step the game takes once a frame, for one console frame: this frame's
 * share of it (dt / 33.3 ms), the remainder carried in its own slot so that
 * small steps at high frame rates still add up. */
static int32_t sea60_scale(int slot, int32_t step)
{
    int64_t t = (int64_t)step * g_sea60.dt_us + g_sea60.rem[slot];
    g_sea60.rem[slot] = (int32_t)(t % SEA60_TICK_US);
    return (int32_t)(t / SEA60_TICK_US);
}

#define S60_MEM32(a)  (*(volatile int32_t *)((uintptr_t)(a) + g_xbox_mem_offset))
#define S60_MEM16(a)  (*(volatile int16_t *)((uintptr_t)(a) + g_xbox_mem_offset))
#define S60_MEMF(a)   (*(volatile float *)((uintptr_t)(a) + g_xbox_mem_offset))

/* The test harness for sailing faster (src/sea60_test.c, tools/sea60):
 * nothing unless RECOMP_PIRATES_SEA60_TEST names a folder. */
void sea60_test_frame(int active, int tick, int dt, int acc3);
void btl_test_frame(int tick, int dt_us);   /* the battle spike, RECOMP_PIRATES_BTL_TEST */

/* The flip gate's pacing for the sea (toolkit patch 0044): hz Swaps a second
 * by the host clock, -1 none, 0 the gate's own (the vblank and the cap). The
 * gate also lets it lapse by itself when it is not renewed for 100 ms: a
 * dialog, the map or a battle that interrupts the sea. */
void xbox_Nv2aFlipGateSetTitleRate(int hz);
static void sea60_set_gate(int rate)
{
    g_sea60.gate_rate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}


/* ---- Ship battles faster than 30 (battle_fps, experimental) ----
 *
 * The battle (sub_000C3950, battle object 0x4D6B80) steps everything once a
 * frame, at 30 a second (10 in its slow motion, the 100 ms wait). Faster,
 * its frame runs every frame the PC draws and a logic tick on real time
 * keeps its timing: g_btl.tick says whether this frame advances a whole
 * console frame (counters, timers, chances; undone by the hooks otherwise),
 * and steps that move something continuously take this frame's share of
 * them (btl_scale). The tick is 33.3 ms, or 100 ms in slow motion. */
#define BTL_OBJ        0x004D6B80u
#define BTL_TICK_US    33333
#define BTL_SLOW_US    100000
static struct {
    int setting;          /* the rate as last read (30 = off) */
    int rate;             /* 60..1000, -1 unlocked */
    int active;           /* this battle frame runs faster than 30 */
    int tick;             /* this frame advances a console frame */
    int tick_us;          /* a console frame now: 33.3 ms, 100 ms slowed */
    int dt_us;            /* this frame's length */
    int acc_us;           /* time since the last tick */
    int64_t last_us;
    int in_frame;         /* between 0xC397F and the frame's end */
    int gate_rate;
    int32_t rem[16];      /* btl_scale's remainders */
    int32_t srem[8][16];  /* btl_step's, a ship (btl_ship) and a kind each */
    float touch_v[3];     /* a body's velocity and spin before a touch's push */
    double anim_rem[8][2];/* what the sinking's frame-time steps truncated, a ship */
    int touch_live;
    int cam_have;         /* cam_prev/cam_cur hold the last two ticks' camera */
    int32_t cam_prev[3], cam_cur[3];  /* x, y, height (0x49C26C, 0x49C268, 0x49C260) */
    int32_t cam_out[3];   /* the camera drawn this frame (the camera log's) */
    int cam_unlogged;     /* battle frames the camera log missed (no widescreen) */
} g_btl;

/* battle_fps (RECOMP_PIRATES_BATTLE_FPS): 30 (the default) as the console,
 * 60, 90, 120, 144 or 240, 0 unlocked (-1 here). */
static int btl_read_rate(void)
{
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_BATTLE_FPS", "battle_fps"));

    if (r < 0)
        return -1;
    if (r <= 30)
        return 30;
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

/* The camera log's tail (pirates_cam_log): "| x y height tick dt_us view
 * x y z drawn x y height" -- the console's camera (sub_000C4220), whether
 * this frame ticked, the view's position as set (sub_000DCC50) and the
 * camera drawn this frame (battle_fps, the C45CF hook). */
static void btl_cam_extra(FILE *f)
{
    fprintf(f, " | %d %d %d %d %d", S60_MEM32(0x0049C26Cu), S60_MEM32(0x0049C268u),
            S60_MEM32(0x0049C260u), g_btl.tick, g_btl.dt_us);
    fprintf(f, " view %d %d %d drawn %d %d %d\n", S60_MEM32(0x0040E8A4u), S60_MEM32(0x004D6748u),
            S60_MEM32(0x004D674Cu), g_btl.cam_out[0], g_btl.cam_out[1], g_btl.cam_out[2]);
}

/* A step taken once a console frame: this frame's share of it. */
static int32_t btl_scale(int slot, int32_t step)
{
    int64_t t = (int64_t)step * g_btl.dt_us + g_btl.rem[slot];
    g_btl.rem[slot] = (int32_t)(t % g_btl.tick_us);
    return (int32_t)(t / g_btl.tick_us);
}
/* The ships: 0x177C apart from 0x4D7D48 (side 0 ship 0), two a side. */
static int btl_ship(uint32_t p)
{
    return (int)((p - 0x004D7D48u) / 0x177Cu) & 7;
}
/* A value the console moves from old to new in one frame: this frame's
 * share of the move, remainder kept for that ship and kind. */
enum { BS_RATE, BS_HEAD, BS_HEAD2, BS_HEAD3, BS_SPEED, BS_X, BS_Y, BS_BHEAD,
       BS_BSPEED, BS_LAT, BS_RUDDER, BS_KINDS };
static int32_t btl_step(uint32_t ship, int kind, int32_t old, int32_t nw)
{
    int32_t *rem = &g_btl.srem[btl_ship(ship)][kind];
    /* The guest's own 32-bit wrapping move (a heading crossing 0x80000000
     * is a small turn), in unsigned arithmetic: no signed overflow. */
    int64_t t = (int64_t)(int32_t)((uint32_t)nw - (uint32_t)old) * g_btl.dt_us + *rem;
    *rem = (int32_t)(t % g_btl.tick_us);
    return (int32_t)((uint32_t)old + (uint32_t)(int32_t)(t / g_btl.tick_us));
}

/* The same for a float step. */
static float btl_scalef(float step)
{
    return step * (float)g_btl.dt_us / (float)g_btl.tick_us;
}

static void btl_set_gate(int rate)
{
    g_btl.gate_rate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}

/* Once a battle frame, at its start (the tick read at 0xC397A). */
static void btl_begin_frame(void)
{
    int64_t now = pirates_host_us();
    int rate = btl_read_rate(), on = rate != 30, gap;
    int slow = (S60_MEM32(0x0049C298u) & 0x20) != 0;

    if (rate != g_btl.setting) {
        g_btl.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[BTL60] battles unlocked (battle_fps = 0)\n");
        else
            fprintf(stderr, "[BTL60] battles at %d frames a second\n", rate);
        fflush(stderr);
    }
    gap = g_btl.last_us == 0 || now - g_btl.last_us > 250000;
    g_btl.tick_us = slow ? BTL_SLOW_US : BTL_TICK_US;
    g_btl.dt_us = gap ? g_btl.tick_us : (int)(now - g_btl.last_us);
    if (g_btl.dt_us > g_btl.tick_us * 3) g_btl.dt_us = g_btl.tick_us * 3;
    if (g_btl.dt_us < 1) g_btl.dt_us = 1;
    g_btl.last_us = now;
    g_btl.rate = rate;
    g_btl.active = on;
    if (!on || gap) {
        g_btl.tick = 1;
        g_btl.acc_us = 0;
        g_btl.dt_us = g_btl.tick_us;
        if (!on && g_btl.gate_rate)
            btl_set_gate(0);
    } else {
        g_btl.acc_us += g_btl.dt_us;
        if (g_btl.acc_us >= g_btl.tick_us) {
            g_btl.tick = 1;
            g_btl.acc_us -= g_btl.tick_us;
            if (g_btl.acc_us > g_btl.tick_us)
                g_btl.acc_us = g_btl.tick_us;
        } else {
            g_btl.tick = 0;
        }
    }
    g_btl.in_frame = 1;
    /* The camera log is written from the HUD's edge test, which the toolkit
     * runs only in widescreen: say so once, rather than leave it empty. */
    if (g_cam_new_frame && ++g_btl.cam_unlogged == 300 && getenv("RECOMP_PIRATES_CAM_LOG"))
        fprintf(stderr, "[PIRATES] RECOMP_PIRATES_CAM_LOG missed 300 battle frames: it is "
                        "written only in widescreen (RECOMP_WIDESCREEN=1)\n");
    g_cam_new_frame = 1;
}

/* ---- Land battles faster than 30 (land_fps, experimental) ----
 *
 * The land battle (sub_0004D340, lbattle.cpp) runs its frame and then waits
 * until 30 ms have passed since it began (0x4DF70..0x4DFA3). Its units, the
 * men's walking and their animations and the fighting already move by the
 * frame's length in ms, which the loop measures and stores at 0x4DFCC
 * ([0x40D188]): at 30 and uncapped (~139) a scripted battle ended with the
 * same positions and the same AI draws. Faster than 30 the wait is passed
 * and the flip gate paces the frames (as at sea and in ship battles), the
 * frame's length is given in whole ms with the remainder carried (cut to
 * whole ms the battle ran 2% slow at 140, 4% at 240), and the few things
 * the console does once a frame keep to a 30 Hz tick (g_land.tick): the
 * frame counter [0x4D6574] (the flags' wave, the target highlight's blink;
 * the flags are given this frame's share of a tick so they wave smoothly),
 * the countdown [0x4D657C] that ends the battle a while after it is won or
 * lost, and the men's per-frame chances (a fidget, a re-aim, the flag's
 * height), whose random draws off the tick take no number from the game's
 * own sequence (sub_002E0424). */
#define LAND_TICK_US 33333
static struct {
    int setting;          /* the rate as last read (30 = off) */
    int rate;             /* 60..1000, -1 unlocked */
    int active;           /* this frame runs faster than 30 */
    int tick;             /* this frame advances a console frame */
    int acc_us;           /* time since the last tick */
    int dt_us;
    int64_t last_us;      /* host time of the last frame start */
    int in_frame;         /* between the frame's start and its wait */
    int gate_rate;
    int64_t ft_last_us;   /* host time of the last frame length stored */
    int ft_rem_us;        /* what the stored ms left over */
    int march_rem[32];    /* each unit's march step, what the scaling left */
    int cam_rem[4];       /* the camera's speed, x, y and zoom, the same */
    FILE *log;            /* RECOMP_PIRATES_LAND_LOG: a line a frame */
} g_land;

/* This frame's share of a console frame, in us of 33333: the frame's
 * length in ms ([0x40D188], its remainder carried), at most one console
 * frame (a hitch runs the battle slower, as the console does). */
static int land_frame_us(void)
{
    int us = S60_MEM32(0x0040D188u) * 1000;
    return us < 0 ? 0 : us > LAND_TICK_US ? LAND_TICK_US : us;
}

/* v, a step the console takes once a frame, for this frame; what the
 * scaling leaves over is carried in *rem. */
static int land_scale(int v, int *rem)
{
    int64_t n = (int64_t)v * land_frame_us() + *rem;
    int r = (int)(n / LAND_TICK_US);
    *rem = (int)(n - (int64_t)r * LAND_TICK_US);
    return r;
}

/* land_fps (RECOMP_PIRATES_LAND_FPS): 30 (the default) as the console, 60,
 * 90, 120, 144 or 240, 0 unlocked (-1 here). */
static int land_read_rate(void)
{
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_LAND_FPS", "land_fps"));

    if (r < 0)
        return -1;
    if (r <= 30)
        return 30;
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

static void land_set_gate(int rate)
{
    g_land.gate_rate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}

/* Once a land battle frame, at its start (the tick read at 0x4D39E). */
static void land_begin_frame(void)
{
    int64_t now = pirates_host_us();
    int rate = land_read_rate(), on = rate != 30, gap;

    if (rate != g_land.setting) {
        g_land.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[LAND] land battles unlocked (land_fps = 0)\n");
        else
            fprintf(stderr, "[LAND] land battles at %d frames a second\n", rate);
        fflush(stderr);
    }
    gap = g_land.last_us == 0 || now - g_land.last_us > 250000;
    g_land.dt_us = gap ? LAND_TICK_US : (int)(now - g_land.last_us);
    g_land.last_us = now;
    g_land.rate = rate;
    g_land.active = on;
    if (!on || gap) {
        g_land.tick = 1;
        g_land.acc_us = 0;
        if (!on && g_land.gate_rate)
            land_set_gate(0);
    } else {
        /* A late frame is one tick, with at most one more owed to the
         * next: the console's counter and countdown also take one step
         * for a frame however long it ran (what moves by time takes the
         * frame's whole length, as on the console), and catching up on
         * every missed tick would run the flags and the countdown on in a
         * burst after a hitch. Ship battles do the same (btl_begin_frame). */
        g_land.acc_us += g_land.dt_us;
        if (g_land.acc_us >= LAND_TICK_US) {
            g_land.tick = 1;
            g_land.acc_us -= LAND_TICK_US;
            if (g_land.acc_us > LAND_TICK_US)
                g_land.acc_us = LAND_TICK_US;
        } else {
            g_land.tick = 0;
        }
    }
    g_land.in_frame = 1;
    {   /* the test log: "us tick dt_us frames end_count frame_ms cam_x cam_y
         * zoom men0 men1 | unit x y ..."
         * (the first man of each side's first four units) */
        static int opened;
        if (!opened) {
            const char *v = getenv("RECOMP_PIRATES_LAND_LOG");
            opened = 1;
            if (v && *v)
                g_land.log = fopen(v, "w");
        }
        if (g_land.log) {
            int s, u;
            /* RECOMP_PIRATES_LAND_TEST_WIN=<s>, with the log only: the
             * battle counts as won s seconds in ([0x4D657C] = 1), to time
             * the countdown that ends it */
            static int64_t first_us;
            static int won;
            const char *w = getenv("RECOMP_PIRATES_LAND_TEST_WIN");
            if (!first_us)
                first_us = now;
            if (w && !won && now - first_us >= (int64_t)(atof(w) * 1e6)) {
                won = 1;
                S60_MEM32(0x004D657Cu) = 1;
                fprintf(g_land.log, "# won\n");
            }
            {   /* then the camera (x, y, zoom) and each side's men ([unit+2]) */
                int men[2] = {0, 0};
                for (s = 0; s < 2; s++)
                    for (u = 0; u < 16; u++)
                        if (S60_MEM16(0x004ADC10u + (uint32_t)(s * 16 + u) * 0x14Cu) != -1)
                            men[s] += S60_MEM16(0x004ADC12u + (uint32_t)(s * 16 + u) * 0x14Cu);
                fprintf(g_land.log, "%lld %d %d %d %d %d %d %d %d %d %d |", (long long)now, g_land.tick,
                        g_land.dt_us, S60_MEM32(0x004D6574u), S60_MEM32(0x004D657Cu),
                        S60_MEM32(0x0040D188u), S60_MEM32(0x004AB7A8u), S60_MEM32(0x004AB7A4u),
                        S60_MEM32(0x004AB790u), men[0], men[1]);
            }
            for (s = 0; s < 2; s++)
                for (u = 0; u < 4; u++) {
                    uint32_t p = 0x004ADC10u + (uint32_t)(s * 16 + u) * 0x14Cu;
                    fprintf(g_land.log, " %d %d", S60_MEM32(p + 0x74u), S60_MEM32(p + 0xA4u));
                }
            fprintf(g_land.log, "\n");
            fflush(g_land.log);
        }
    }
}

/* The land battle's guest hooks; 1 when va is one of them. */
static int land_guest_hook(uint32_t va)
{
    switch (va) {
    case 0x0004DFCCu: {     /* the frame's length in ms, stored to [0x40D188] */
        int64_t now = pirates_host_us();
        if (g_land.active && g_land.ft_last_us != 0 && now - g_land.ft_last_us <= 250000) {
            int64_t t = now - g_land.ft_last_us + g_land.ft_rem_us;
            g_eax = (uint32_t)(t / 1000);
            g_land.ft_rem_us = (int)(t % 1000);
        } else {
            g_land.ft_rem_us = 0;
        }
        g_land.ft_last_us = now;
        return 1;
    }
    case 0x0004DF60u:       /* inc [0x4D6574], the frame counter: a tick's */
        if (g_land.active && g_land.in_frame && !g_land.tick)
            S60_MEM32(0x004D6574u) -= 1;
        return 1;
    case 0x0004B57Bu:       /* [0x4D657C] += 1 or -1 (won, lost): a tick's */
        if (g_land.active && g_land.in_frame && !g_land.tick)
            g_eax = 0;
        return 1;
    case 0x00049C5Cu:       /* a flag's wave: (frames + 3 n) * 30 ms */
        /* The counter moves at the end of a tick frame, after the drawing:
         * a tick frame draws the next 30 already, so the wave goes on
         * smoothly through it. 0 is a flag that does not wave. */
        if (g_land.active && g_land.in_frame && g_ecx != 0)
            g_ecx += (uint32_t)((g_land.tick ? 30 : 0) +
                                (int)((int64_t)g_land.acc_us * 30 / LAND_TICK_US));
        return 1;
    case 0x0004C864u:       /* a unit's march: the slope's slowing, x * frame ms */
        /* The march step (below) is taken once a frame; the slope takes
         * off a share of it in proportion to the frame's ms. Given for
         * a console frame here, the whole step is then scaled. */
        if (g_land.active && g_land.in_frame) {
            int ms = S60_MEM32(0x0040D188u);
            if (ms > 0)
                g_ecx = (uint32_t)(int32_t)((int64_t)(int32_t)g_ecx * LAND_TICK_US / ((int64_t)ms * 1000));
        }
        return 1;
    case 0x0004C8D4u: {     /* a unit's march step a frame (edi, [esp+0x20]) */
        /* The men walk this far a frame toward their places (0x4CD21),
         * and a man or the unit nearer than it is there: the step for
         * this frame's length, its remainder kept for each unit. */
        if (g_land.active && g_land.in_frame) {
            uint32_t u = (uint32_t)g_ebp / 0x14Cu;
            if (u < 32u) {
                int s = land_scale((int)g_edi, &g_land.march_rem[u]);
                g_edi = (uint32_t)s;
                S60_MEM32(g_esp + 0x20u) = s;
            }
        }
        return 1;
    }
    case 0x0004CE30u: {     /* a man's fighting clock: counter (ax) * 30 ms in esi */
        /* The counter goes up once a frame (stored at 0x4CE37) and times
         * a man's aim, shot or blow and his reload: a tick's, given this
         * frame's share of a tick so his animation runs smoothly. A tick
         * frame reads c and stores c + 1; the frames after it read c + 1.
         * 0 stays 0 (the shot is cleared to be fired again there). */
        if (g_land.active && g_land.in_frame) {
            int c = (int16_t)(g_eax & 0xFFFFu);
            int f = (int)((int64_t)g_land.acc_us * 30 / LAND_TICK_US);
            if (!g_land.tick)
                g_eax -= 1u;            /* the inc then stores c again */
            if (c > 0)
                g_esi = (uint32_t)((g_land.tick ? c : c - 1) * 30 + f);
        }
        return 1;
    }
    case 0x00049DD8u:       /* a fleeing man's fade, a step a frame: a tick's */
        if (g_land.active && g_land.in_frame && !g_land.tick)
            g_ecx -= 1u;
        return 1;
    case 0x0004AE60u:       /* dec [0x40D18C], a message's frames: a tick's */
        if (g_land.active && g_land.in_frame && !g_land.tick)
            S60_MEM32(0x0040D18Cu) += 1;
        return 1;
    case 0x0004AFF0u:       /* the camera's speed, +-50 a frame (ebx) */
        /* The camera pans toward its target at a speed (0..400 a frame,
         * [0x4AB79C]) that gains or loses 50 a frame: the change and the
         * pan (0x4B005, 0x4B021) for this frame's length. */
        if (g_land.active && g_land.in_frame) {
            int old = S60_MEM32(g_esp + 0x40u);
            g_ebx = (uint32_t)(old + land_scale((int)g_ebx - old, &g_land.cam_rem[0]));
        }
        return 1;
    case 0x0004B005u:       /* the pan in x */
        if (g_land.active && g_land.in_frame)
            g_eax = (uint32_t)land_scale((int)g_eax, &g_land.cam_rem[1]);
        return 1;
    case 0x0004B021u:       /* and in y */
        if (g_land.active && g_land.in_frame)
            g_eax = (uint32_t)land_scale((int)g_eax, &g_land.cam_rem[2]);
        return 1;
    case 0x0004B0C1u: {     /* the zoom toward [0x4AB7A0] (eax), 500 a frame */
        if (g_land.active && g_land.in_frame) {
            int old = S60_MEM32(0x004AB790u), d = (int)g_eax - old, m;
            if (d != 0) {
                m = land_scale(500, &g_land.cam_rem[3]);
                g_esi = (uint32_t)(d > m ? old + m : d < -m ? old - m : (int)g_eax);
            }
        }
        return 1;
    }
    case 0x0004DFEFu:       /* the battle is over: the gate back to its own */
        g_land.in_frame = 0;
        memset(g_land.march_rem, 0, sizeof g_land.march_rem);   /* no share */
        memset(g_land.cam_rem, 0, sizeof g_land.cam_rem);       /* carried on */
        if (g_land.gate_rate)
            land_set_gate(0);
        return 1;
    }
    return 0;
}

/* ---- The town sneak faster than 30 (sneak_fps, experimental) ----
 *
 * The town sneak (sub_000A1E10, town.cpp) runs its frame from the tick read
 * at 0xA43F5 and then waits until 31 ms have passed since (0xA4AE9): 30 a
 * second, as on the console. Nothing in it keeps time: the player (record 0
 * at 0x4CDC48, sub_0009F8B0) and the guards (records 1.. of 0x44 bytes,
 * sub_000A07E0) step, turn and animate by a fixed amount a frame, the
 * camera (sub_000A55B0) eases toward the player a step a frame, and every
 * count -- the clock [0x4CE71C], the alert [0x4CE740] and its hold, the
 * hold meter [0x4CE6F0] when a guard has the player, the countdowns --
 * goes by the frame. Faster than 30 the wait is passed and the flip gate
 * paces the frames (as at sea and in battles), and each of those steps is
 * either given this frame's share of a console frame (sneak_scale, the
 * remainder carried for each record) or kept to a 30 Hz tick (g_sneak.tick):
 *  - shared out: the steps in x and y (the step the console takes, cut as
 *    sub_00103A90 cuts it, not a step from a share of the speed: that
 *    rounds down four times as often), the animation clocks, the turns,
 *    the player's idle clock, the camera's pan and zoom, the pointer's fade;
 *  - a tick's: the turn and camera speeds (sub_0009F090 eases them by a
 *    step a frame, so they change once a tick and are moved by a frame's
 *    share), the counters and countdowns, the guards' paths to the player
 *    and the music's pick (every other frame on the console), and the hold
 *    meter's fall, whose random draw (sub_002E0424) off the tick takes no
 *    number from the game's sequence. A press of A while held still adds
 *    at once, and spares that tick's fall as a press's frame does.
 * What the guards decide (a turn at a crossing, a look around) happens at
 * the end of an animation cycle or on a tile, so it comes as often as on
 * the console; a guard looks for the player every frame, at most a tick
 * sooner than the console would see him. The pause menu opened in the
 * sneak follows the rate too (sub_00131170). */
#define SNEAK_TICK_US 33333
static struct {
    int setting;          /* the rate as last read (30 = off) */
    int rate;             /* 60..1000, -1 unlocked */
    int active;           /* this frame runs faster than 30 */
    int tick;             /* this frame advances a console frame */
    int acc_us;           /* time since the last tick */
    int dt_us;            /* this frame's length, at most a console frame */
    int64_t last_us;      /* host time of the last frame start */
    int in_frame;         /* between the frame's start and its wait */
    int gate_rate;
    int anim_rem[32];     /* each record's animation clock, what the scaling left */
    int turn_rem[32];     /* each guard's turn */
    int walk_rem[32][2];  /* each record's step in x and y */
    int rem[16];          /* the single steps: see sneak_guest_hook */
    uint32_t saved;       /* a register a hook changed, given back after */
    int decay;            /* the hold meter's fall is held back this frame */
    int pressed;          /* a press of A off the tick: the next tick does not fall */
    FILE *log;            /* RECOMP_PIRATES_SNEAK_LOG: a line a frame */
} g_sneak;

static int sneak_read_rate(void)
{
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_SNEAK_FPS", "sneak_fps"));

    if (r < 0)
        return -1;
    if (r <= 30)
        return 30;
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

static void sneak_set_gate(int rate)
{
    g_sneak.gate_rate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}

/* The sneak is left (another scene's frame, the front end): no frame of
 * it is open, and the next one starts afresh, as after a gap. */
static void sneak_leave(void)
{
    g_sneak.in_frame = 0;
    g_sneak.last_us = 0;
}

/* Once a town sneak frame, at its start (the tick read at 0xA43F5). */
static void sneak_begin_frame(void)
{
    int64_t now = pirates_host_us();
    int rate = sneak_read_rate(), on = rate != 30, gap;

    if (rate != g_sneak.setting) {
        g_sneak.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[SNEAK] town sneak unlocked (sneak_fps = 0)\n");
        else
            fprintf(stderr, "[SNEAK] town sneak at %d frames a second\n", rate);
        fflush(stderr);
    }
    gap = g_sneak.last_us == 0 || now - g_sneak.last_us > 250000;
    g_sneak.dt_us = gap ? SNEAK_TICK_US : (int)(now - g_sneak.last_us);
    if (g_sneak.dt_us > SNEAK_TICK_US)
        g_sneak.dt_us = SNEAK_TICK_US;
    g_sneak.last_us = now;
    g_sneak.rate = rate;
    g_sneak.active = on;
    if (!on || gap) {
        g_sneak.tick = 1;
        g_sneak.acc_us = 0;
        if (!on && g_sneak.gate_rate)
            sneak_set_gate(0);
    } else {
        g_sneak.acc_us += g_sneak.dt_us;
        if (g_sneak.acc_us >= SNEAK_TICK_US) {
            g_sneak.tick = 1;
            g_sneak.acc_us -= SNEAK_TICK_US;
            if (g_sneak.acc_us > SNEAK_TICK_US)
                g_sneak.acc_us = SNEAK_TICK_US;
        } else {
            g_sneak.tick = 0;
        }
    }
    if (gap) {
        memset(g_sneak.anim_rem, 0, sizeof g_sneak.anim_rem);
        memset(g_sneak.turn_rem, 0, sizeof g_sneak.turn_rem);
        memset(g_sneak.walk_rem, 0, sizeof g_sneak.walk_rem);
        memset(g_sneak.rem, 0, sizeof g_sneak.rem);
        g_sneak.pressed = 0;
    }
    g_sneak.in_frame = 1;
    {   /* the test log: "us tick dt | the 0x4CE71C clock, alert 0x4CE740,
         * its hold 0x4CE744, meter 0x4CE6F0, 0x4CE6F8, 0x4CE6F4, 0x4119EC,
         * 0x4D66F4 | camera x y zoom | each record: x y angle anim time" */
        static int opened;
        if (!opened) {
            const char *v = getenv("RECOMP_PIRATES_SNEAK_LOG");
            opened = 1;
            if (v && *v)
                g_sneak.log = fopen(v, "w");
        }
        if (g_sneak.log) {
            int r, n = S60_MEM32(0x0040DE44u);
            /* RECOMP_PIRATES_SNEAK_TEST_NOGUARDS=1, with the log only: the
             * guards are left out (their records off), to time the
             * player's own moves */
            if (getenv("RECOMP_PIRATES_SNEAK_TEST_NOGUARDS"))
                for (r = 1; r <= n && r < 32; r++)
                    S60_MEM32(0x004CDC48u + (uint32_t)r * 0x44u) = 0;
            {   /* RECOMP_PIRATES_SNEAK_TEST_GRAB=<s>, with the log only: s
                 * seconds in, the first guard is put at 16000,1700 and the
                 * player 400 short of him (a guard that near takes hold),
                 * the other guards left out, to time a hold and a catch */
                static int64_t first_us;
                static int done;
                const char *w = getenv("RECOMP_PIRATES_SNEAK_TEST_GRAB");
                if (!first_us)
                    first_us = now;
                if (w && done)
                    for (r = 2; r <= n && r < 32; r++)
                        S60_MEM32(0x004CDC48u + (uint32_t)r * 0x44u) = 0;
                if (w && !done && n >= 1 && now - first_us >= (int64_t)(atof(w) * 1e6)) {
                    done = 1;
                    S60_MEM32(0x004CDC90u) = 16000;      /* the guard: x, y, */
                    S60_MEM32(0x004CDC94u) = 1700;
                    S60_MEM32(0x004CDCA8u) = 4;          /* facing the player (-y), */
                    S60_MEM32(0x004CDCB4u) = 4;
                    S60_MEM32(0x004CDCC8u) = (int32_t)0x80000000u;
                    S60_MEM32(0x004CDCCCu) = 0;
                    S60_MEM32(0x004CDCB0u) = 0;          /* no countdowns, */
                    S60_MEM32(0x004CDCC4u) = 0;
                    S60_MEM32(0x004CDCB8u) = 1;          /* walking */
                    S60_MEM32(0x004CDCBCu) = 4;
                    S60_MEM32(0x004CDCC0u) = 0;
                    S60_MEM32(0x004CE740u) = 0;          /* nobody alerted */
                    S60_MEM32(0x004CE744u) = 0;
                    S60_MEM32(0x004CE6F0u) = 0;
                    S60_MEM32(0x004CE6F8u) = 0;
                    S60_MEM32(0x004CE6F4u) = 0;
                    S60_MEM32(0x004CDC74u) = 0;
                    S60_MEM32(0x004CDC4Cu) = 16000;
                    S60_MEM32(0x004CDC50u) = 1300;
                    fprintf(g_sneak.log, "# grab\n");
                }
            }
            fprintf(g_sneak.log, "%lld %d %d | %d %d %d %d %d %d %d %d %d %d | %d %d %d |", (long long)now,
                    g_sneak.tick, g_sneak.dt_us, S60_MEM32(0x004CE71Cu), S60_MEM32(0x004CE740u),
                    S60_MEM32(0x004CE744u), S60_MEM32(0x004CE6F0u), S60_MEM32(0x004CE6F8u),
                    S60_MEM32(0x004CE6F4u), S60_MEM32(0x004119ECu), S60_MEM32(0x004D66F4u),
                    S60_MEM32(0x004D66FCu), S60_MEM32(0x004CDC74u),
                    S60_MEM32(0x0049C26Cu), S60_MEM32(0x0049C268u), S60_MEM32(0x004CE724u));
            for (r = 0; r <= n && r < 32; r++) {
                uint32_t b = 0x004CDC48u + (uint32_t)r * 0x44u;
                fprintf(g_sneak.log, " %d %d %d %d %d", S60_MEM32(b + 4u), S60_MEM32(b + 8u),
                        r ? S60_MEM32(b + 0x3Cu) : S60_MEM32(0x004CDC64u), S60_MEM32(b + 0x30u),
                        S60_MEM32(b + 0x34u));
            }
            fprintf(g_sneak.log, "\n");
            fflush(g_sneak.log);
        }
    }
}

/* v, a step the console takes once a frame, for this frame (its length,
 * at most a console frame); what the scaling leaves is carried in *rem. */
static int sneak_scale(int v, int *rem)
{
    int64_t n = (int64_t)v * g_sneak.dt_us + *rem;
    int r = (int)(n / SNEAK_TICK_US);
    *rem = (int)(n - (int64_t)r * SNEAK_TICK_US);
    return r;
}

/* A value the console moves by (reg - mem) a frame, stored from reg to
 * mem: the move for this frame's length. Angles wrap: the move is taken
 * modulo 2^32. */
static void sneak_scale_store(uint32_t *reg, uint32_t mem, int *rem)
{
    uint32_t old = (uint32_t)S60_MEM32(mem);
    *reg = old + (uint32_t)sneak_scale((int32_t)(*reg - old), rem);
}

/* The town sneak's guest hooks; 1 when va is one of them. */
static int sneak_guest_hook(uint32_t va)
{
    int on = g_sneak.active && g_sneak.in_frame, off = on && !g_sneak.tick;
    uint32_t r = (uint32_t)g_ebp / 0x44u;   /* a guard's record (sub_000A07E0) */

    switch (va) {
    /* The loop (sub_000A1E10): past its wait (every read of the wait's
     * tick, at 60 more than one, is passed until here) */
    case 0x000A4B0Bu: g_sneak.in_frame = 0; return 1;
    /* its counters, a tick's */
    case 0x000A4477u: if (off) g_edi += 1u; return 1;          /* dec edi: [esp+0x30] */
    case 0x000A4487u: if (off) g_ecx -= 1u; return 1;          /* inc [0x4CE71C], the clock */
    /* The world (sub_000A1C00) */
    case 0x000A1CBAu:                                          /* inc [0x4D66F4], every 34th */
        if (off) { g_eax -= 1u; g_ecx = 0; }
        return 1;
    case 0x000A1D58u: if (off) g_ecx += 1u; return 1;          /* dec [0x4CE740], the alert */
    case 0x000A1D78u: if (off) g_esi += 1u; return 1;          /* dec [0x4CE744], its hold */
    case 0x000A1B92u:                                          /* the pointer's fade, -4 a frame */
    case 0x000A1BD1u:                                          /* and +4 */
        if (on) {
            g_sneak.saved = g_eax;
            g_eax = (uint32_t)sneak_scale((int)g_eax, &g_sneak.rem[0]);
        }
        return 1;
    case 0x000A1B94u: if (on) g_eax = g_sneak.saved; return 1;
    /* A guard (sub_000A07E0, ebp = its record's offset) */
    case 0x000A09B4u: if (off) g_edx += 1u; return 1;          /* dec [rec+0x38] */
    case 0x000A09E5u:                                          /* its animation clock +0x21/+0x42 */
        if (on && r < 32u)
            sneak_scale_store(&g_edx, 0x004CDC7Cu + g_ebp, &g_sneak.anim_rem[r]);
        return 1;
    case 0x000A1184u:                                          /* its turn's speed (sub_0009F090) */
        if (off) g_eax = (uint32_t)S60_MEM32(0x004CDC88u + g_ebp);
        return 1;
    case 0x000A11A5u:                                          /* the turn */
        if (on && r < 32u)
            sneak_scale_store(&g_ecx, 0x004CDC84u + g_ebp, &g_sneak.turn_rem[r]);
        return 1;
    case 0x000A11C8u:                                          /* its step in x (speed * cos) */
    case 0x000A11EBu:                                          /* and in y */
        /* The step, not the speed, is scaled: sub_00103A90 rounds down,
         * so a step from a quarter of the speed would lose up to a unit
         * a frame, four times as often. */
        if (on && r < 32u)
            g_eax = (uint32_t)sneak_scale((int)g_eax, &g_sneak.walk_rem[r][va == 0x000A11EBu]);
        return 1;
    case 0x000A140Cu: if (off) g_eax += 1u; return 1;          /* dec [rec+0x24] */
    /* The player (sub_0009F8B0, record 0) */
    case 0x0009F968u:                                          /* the animation clock */
    case 0x0009FE57u:                                          /* +5, turning */
    case 0x0009FEB6u:
        if (on)
            sneak_scale_store(&g_edi, 0x004CDC7Cu, &g_sneak.anim_rem[0]);
        return 1;
    case 0x0009FA1Bu: if (off) g_eax -= 1u; return 1;          /* inc [0x4CE6F8] */
    case 0x0009FB16u: if (off) g_ecx -= 1u; return 1;          /* inc [0x4CE6F4] */
    case 0x0009FF9Eu: if (off) S60_MEM32(0x004CDC80u) += 1; return 1;   /* dec [0x4CDC80] */
    case 0x000A022Au:                                          /* [0x4CDC44] += 30 */
    case 0x000A02D6u:                                          /* and 10 */
        if (on)
            sneak_scale_store(&g_eax, 0x004CDC44u, &g_sneak.rem[1]);
        return 1;
    case 0x000A034Cu:                                          /* the turn, by the stick */
    case 0x000A0387u:
    case 0x000A03A6u:
    case 0x000A03BDu:
        if (on)
            sneak_scale_store(&g_ebp, 0x004CDC64u, &g_sneak.rem[2]);
        return 1;
    case 0x000A039Cu: if (off) g_edx <<= 1; return 1;          /* the turn eases off, sar 1 a tick */
    case 0x000A0585u:                                          /* the step in x (speed * cos) */
    case 0x000A05C9u:
    case 0x000A059Eu:                                          /* and in y */
    case 0x000A05E2u:
        if (on)
            g_eax = (uint32_t)sneak_scale((int)g_eax,
                                          &g_sneak.walk_rem[0][va == 0x000A059Eu || va == 0x000A05E2u]);
        return 1;
    case 0x000A077Bu: if (off) g_ecx -= 1u; return 1;          /* inc [0x4119EC] */
    /* The camera (sub_000A55B0): speeds (sub_0009F090) a tick, moves a frame */
    case 0x000A55F0u: if (off) g_eax = (uint32_t)S60_MEM32(0x004CE734u); return 1;
    case 0x000A5608u: if (on) sneak_scale_store(&g_ecx, 0x0049C26Cu, &g_sneak.rem[3]); return 1;
    case 0x000A561Au: if (off) g_eax = (uint32_t)S60_MEM32(0x004CE730u); return 1;
    case 0x000A563Au: if (on) sneak_scale_store(&g_eax, 0x0049C268u, &g_sneak.rem[4]); return 1;
    case 0x000A5652u: if (off) g_eax = (uint32_t)S60_MEM32(0x004CE728u); return 1;
    case 0x000A56E4u: if (off) g_eax = (uint32_t)S60_MEM32(0x004CE720u); return 1;
    case 0x000A5702u: if (on) sneak_scale_store(&g_esi, 0x004CE724u, &g_sneak.rem[5]); return 1;
    /* Every other frame (sub_000BC220, sub_000BC460): [0x4D2DF4] and
     * [0x4D2DF0] add the tick count since the limiter's base, so pass 5000
     * at once and are cleared on the next frame -- the guards' paths to
     * the player (sub_000BC690) and the music's pick (sub_000BC740, a
     * random draw), 15 times a second on the console: on the tick only. */
    case 0x000BC287u: if (off) g_ecx = (uint32_t)S60_MEM32(0x004D2DF4u); return 1;
    case 0x000BC4DEu: if (off) g_ecx = (uint32_t)S60_MEM32(0x004D2DF0u); return 1;
    /* The escape meter (sub_000A8300): its decay a tick */
    /* A press of A (key 0x20) adds to it (0xA8308); a frame without one
     * takes off 1..3 and more (0xA833B). On the console a frame with a
     * press does not fall: a press off the tick spares the next tick's
     * fall, and its draw. */
    case 0x000A8308u:
        g_sneak.decay = 0;
        if (off)
            g_sneak.pressed = 1;
        return 1;
    case 0x000A833Bu:
        g_sneak.decay = off || (on && g_sneak.pressed);
        if (on && g_sneak.tick)
            g_sneak.pressed = 0;
        return 1;
    case 0x000A835Cu:
        if (on && g_sneak.decay)
            g_eax = (uint32_t)S60_MEM32(0x004CE6F0u);
        g_sneak.decay = 0;
        return 1;
    }
    return 0;
}

/* ---- Town scenes faster than 33 (town_fps, experimental) ----
 *
 * The tavern, the governor's room and the other rooms of a town draw their
 * frames in sub_00099280, which ends waiting until 30 ms have passed since
 * the frame began (the tick read at 0x99C4E): 33 frames a second, frame cap
 * or not. Everything in them that moves runs on the engine's clock, real
 * time (0x4D3A10 advanced 1.00 s a second at 33, 120 and unlocked in
 * scripted runs), so faster only needs the wait passed -- the flip gate
 * paces the frames, by the vblank at 60 and the host clock above, as at sea
 * and in battles -- and the short-lived effects' ages (0x4D4E38, a year a
 * frame) held to a 33 Hz tick. */
#define TOWN_TICK_US 30000
static struct {
    int setting;          /* the rate as last read (30 = off) */
    int rate;             /* 60..1000, -1 unlocked */
    int tick;             /* the coming frame advances a console frame */
    int acc_us;
    int64_t last_us;      /* host time of the last frame wait */
    uint32_t frame_start; /* [0x5061F8] at it: the wait loops, one frame each */
    int gate_rate;
    int64_t seen_us;      /* host time of the last call of the frame wait */
} g_town;

/* town_fps (RECOMP_PIRATES_TOWN_FPS): 30 (the default) as the console (33
 * a second), 60, 90, 120, 144 or 240, 0 unlocked (-1 here). */
static int town_read_rate(void)
{
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_TOWN_FPS", "town_fps"));

    if (r < 0)
        return -1;
    if (r <= 30)
        return 30;
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

/* A town frame's wait has run within the last 100 ms -- by its last call,
 * not last_us: the governor's dialog loop
 * passes through the wait again and again within one frame of the room
 * (the same frame start), which leaves last_us where it was, and the test
 * went stale 100 ms into it -- so the dialog's Sleep(10) came back on
 * every other stretch of passes and the room ran at ~90 instead of
 * unlocked. */
static int town_active(void)
{
    return g_town.rate != 30 && g_town.seen_us &&
           pirates_host_us() - g_town.seen_us < 100000;
}

void xbox_Nv2aFlipGateSetTitleRate(int hz);

/* At every town frame's wait (sub_001A958A, ra 0x99C4E). */
static void town_frame_wait(void)
{
    int64_t now = pirates_host_us();
    int rate = town_read_rate();
    int gap = !g_town.last_us || now - g_town.last_us > 250000;
    extern void tav_test_frame(void);

    tav_test_frame();
    g_town.seen_us = now;
    if (rate != g_town.setting) {
        g_town.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[TOWN60] towns unlocked (town_fps = 0)\n");
        else
            fprintf(stderr, "[TOWN60] towns at %d frames a second\n", rate);
        fflush(stderr);
    }
    g_town.rate = rate;
    if (rate == 30) {
        if (g_town.gate_rate) {
            g_town.gate_rate = 0;
            xbox_Nv2aFlipGateSetTitleRate(0);
        }
        g_town.last_us = now;
        return;
    }
    /* The tick for the frame that follows: 30 ms of real time each. The
     * wait reads the tick in a loop, so once a frame (its start time). */
    if (!gap && S60_MEM32(0x005061F8u) == (int32_t)g_town.frame_start) {
        now = g_town.last_us;               /* the same frame's wait again */
    } else if (gap) {
        g_town.acc_us = 0;
        g_town.tick = 1;
    } else {
        g_town.acc_us += (int)(now - g_town.last_us);
        g_town.tick = g_town.acc_us >= TOWN_TICK_US;
        if (g_town.tick) {
            g_town.acc_us -= TOWN_TICK_US;
            if (g_town.acc_us > TOWN_TICK_US)
                g_town.acc_us = TOWN_TICK_US;
        }
    }
    g_town.last_us = now;
    g_town.frame_start = (uint32_t)S60_MEM32(0x005061F8u);
    if (rate == 60) {                      /* 15 ms, then the vblank paces it */
        g_eax += 30u - SEA60_WAIT_MS;
        if (g_town.gate_rate) {
            g_town.gate_rate = 0;
            xbox_Nv2aFlipGateSetTitleRate(0);
        }
    } else {
        g_eax += 30u;
        g_town.gate_rate = rate;
        xbox_Nv2aFlipGateSetTitleRate(rate);
    }
}

/* ---- Sword duels faster than 30 (duel_fps, experimental) ----
 *
 * A duel (sub_0002D800, one loop: frame top 0x31508, the tick read at
 * 0x3165F, the frame wait at 0x36C5B) steps everything once a frame and
 * waits until 31 ms have passed since the last (120 when slowed, flag 0x20
 * of 0x49C298). Faster, the loop runs every frame the PC draws and a logic
 * tick on real time keeps the console's timing: g_duel.tick says whether
 * this frame advances a console frame (what the duel decides once a frame,
 * by chance or by count, waits for it), and steps that move something
 * continuously take this frame's share of a console frame's (duel_scale). */
#define DUEL_TICK_US 31000
#define DUEL_SLOW_US 120000
/* The clock step a console frame gets: 0.4 a millisecond of the engine's
 * clock, clamped to 5..20 (0x31671): 12 for a frame of 31 ms. */
#define DUEL_CLOCK_STEP 12
static struct {
    int setting;          /* the rate as last read (30 = off) */
    int rate;             /* 60..1000, -1 unlocked */
    int active;           /* this duel frame runs faster than 30 */
    int tick;             /* this frame advances a console frame */
    int tick_us, dt_us, acc_us;
    int64_t last_us;
    int gate_rate;
    int32_t rem[16];      /* duel_scale's remainders */
    int32_t frem[2][8];   /* duel_fscale's, a fighter and a kind each */
} g_duel;

/* duel_fps (RECOMP_PIRATES_DUEL_FPS): 30 (the default) as the console, 60,
 * 90, 120, 144 or 240, 0 unlocked (-1 here). */
static int duel_read_rate(void)
{
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_DUEL_FPS", "duel_fps"));

    if (r < 0)
        return -1;
    if (r <= 30)
        return 30;
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

/* A step taken once a console frame: this frame's share of it. */
static int32_t duel_scale(int slot, int32_t step)
{
    int64_t t = (int64_t)step * g_duel.dt_us + g_duel.rem[slot];
    g_duel.rem[slot] = (int32_t)(t % g_duel.tick_us);
    return (int32_t)(t / g_duel.tick_us);
}
/* The same, a fighter's own (ebp 0 or 0x4C: the records at 0x424D88). */
static int32_t duel_fscale(uint32_t rec, int kind, int32_t step)
{
    int32_t *rem = &g_duel.frem[rec ? 1 : 0][kind & 7];
    int64_t t = (int64_t)step * g_duel.dt_us + *rem;
    *rem = (int32_t)(t % g_duel.tick_us);
    return (int32_t)(t / g_duel.tick_us);
}

/* The duel frame's top (the tick read, ra 0x31664). */
static void duel_begin_frame(void)
{
    int64_t now = pirates_host_us();
    int rate = duel_read_rate();
    int slow = (S60_MEM32(0x0049C298u) & 0x20) != 0;
    int gap;

    if (rate == 30 && g_duel.gate_rate) {  /* turned back to 30: the gate as before */
        g_duel.gate_rate = 0;
        xbox_Nv2aFlipGateSetTitleRate(0);
    }
    if (rate != g_duel.setting) {
        g_duel.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[DUEL60] duels unlocked (duel_fps = 0)\n");
        else
            fprintf(stderr, "[DUEL60] duels at %d frames a second\n", rate);
        fflush(stderr);
    }
    gap = g_duel.last_us == 0 || now - g_duel.last_us > 250000;
    g_duel.tick_us = slow ? DUEL_SLOW_US : DUEL_TICK_US;
    g_duel.dt_us = gap ? g_duel.tick_us : (int)(now - g_duel.last_us);
    if (g_duel.dt_us > g_duel.tick_us * 3) g_duel.dt_us = g_duel.tick_us * 3;
    if (g_duel.dt_us < 1) g_duel.dt_us = 1;
    g_duel.last_us = now;
    g_duel.rate = rate;
    g_duel.active = rate != 30;
    if (!g_duel.active || gap) {
        g_duel.tick = 1;
        g_duel.acc_us = 0;
        g_duel.dt_us = g_duel.tick_us;
    } else {
        g_duel.acc_us += g_duel.dt_us;
        if (g_duel.acc_us >= g_duel.tick_us) {
            g_duel.tick = 1;
            g_duel.acc_us -= g_duel.tick_us;
            if (g_duel.acc_us > g_duel.tick_us)
                g_duel.acc_us = g_duel.tick_us;
        } else {
            g_duel.tick = 0;
        }
    }
}

/* The duel frame's wait (ra 0x36C60), faster than 30: passed at once (the
 * elapsed time it compares, eax, made the largest -- its target moves
 * with the duel's own schedule, so a fixed 31 was not always enough); the
 * flip gate paces the frames, by the vblank at 60 and the host clock above. */
static void duel_frame_wait(void)
{
    g_eax = 0x7FFFFFFFu;
    if (g_duel.rate == 60) {
        if (g_duel.gate_rate) {
            g_duel.gate_rate = 0;
            xbox_Nv2aFlipGateSetTitleRate(0);
        }
    } else {
        g_duel.gate_rate = g_duel.rate;
        xbox_Nv2aFlipGateSetTitleRate(g_duel.rate);
    }
}

/* The duel's hooks; 0 for an address that is not one of them. */
static int duel_guest_hook(uint32_t va)
{
    /* Only inside a duel faster than 30 (its frame top ran in the last
     * 250 ms): anything else at these addresses is left to the other hooks. */
    if (!(va >= 0x0002D800u && va < 0x00037000u) || !g_duel.active ||
        pirates_host_us() - g_duel.last_us > 250000)
        return 0;
    switch (va) {
    /* The clock step ([esp+0xB4], and twice it at 0x40F62C): how far the
     * duel's clock-timed motions (the crowd, the ships, the fighters) move
     * this frame: 0.4 a millisecond of the engine's clock but never less
     * than 5, so frames shorter than 12 ms moved them too far (0 when the
     * clock has not moved). Faster than 30, each frame its share of a
     * console frame's 12. */
    case 0x000316BAu: {
        int32_t v = duel_scale(1, DUEL_CLOCK_STEP);
        S60_MEM32(g_esp + 0xB4u) = v;
        S60_MEM32(0x0040F62Cu) = 2 * v;
        break;
    }
    /* A fighter's animation step (from 0x34B7A, ebp 0 or 0x4C: the records
     * at 0x424D88; its time, +8, moves by it, twice or half of it a frame):
     * its move's own pace plus the clock step less 3, held between bounds
     * the clock step sets. The step as a console frame has it (clock step
     * 12), this frame's share of it at 0x34D9A. */
    case 0x00034B81u: g_edi = DUEL_CLOCK_STEP; break;
    case 0x00034D9Au: g_ecx = (uint32_t)duel_fscale(g_ebp, 0, (int32_t)g_ecx); break;
    /* The duel's frame count ([esp+0x90], inc at 0x36BF7): the crew fight
     * behind the duellists (0x3172D) strikes every 100 of them, and
     * others time by it. Once a tick. */
    case 0x00036BF7u: if (!g_duel.tick) S60_MEM32(g_esp + 0x90u) -= 1; break;
    /* The opponent's next move (the second record, ebp 0x4C: +0x20, +0x24,
     * stored at 0x323F0): its AI picks one each frame, by chance; faster
     * than 30 it picked 3.5 times as often. Taken once a tick (the one
     * before is stored again on other frames). The player's own record is
     * left alone, so no press is lost. */
    case 0x000323F0u:
        if (!g_duel.tick && g_ebp == 0x4Cu) {
            g_edi = (uint32_t)S60_MEM32(0x00424DA8u + g_ebp);
            g_ecx = (uint32_t)S60_MEM32(0x00424DACu + g_ebp);
        }
        break;
    /* Short effects around the duel ([esp+0x130 + 4n] each, 0x357D0): an
     * idle one starts by a 1 in 200 chance a frame (rand at 0x35840); faster
     * than 30 that is drawn once a tick (other frames the roll misses). */
    case 0x00035845u: if (!g_duel.tick) g_eax = 1; break;
    /* The fade-in's frame count (ebx, [esp+0x13C]; to 70): once a tick. */
    case 0x00031546u: if (!g_duel.tick) g_ebx -= 1; break;

    /* The camera (0x35F27..0x35FF6): its x (0x49C26C) and its turn
     * ([esp+0xC4]) each move by a speed that sub_0009F090 eases toward
     * the target every frame (sped up by 1 a frame to at most 20, slowed
     * near it), the speeds kept at [esp+0x198] and [esp+0x88]. The speed
     * changes once a tick (the one before is kept on other frames); the
     * move is this frame's share of it. After each call the 4 arguments
     * are still on the stack. */
    case 0x00035F41u: if (!g_duel.tick) g_eax = (uint32_t)S60_MEM32(g_esp + 0x1A8u); break;
    case 0x00035F49u:   /* ecx = x + speed: x + this frame's share */
        g_ecx = g_ecx - g_eax + (uint32_t)duel_scale(2, (int32_t)g_eax);
        break;
    case 0x00035FCCu: if (!g_duel.tick) g_eax = (uint32_t)S60_MEM32(g_esp + 0x98u); break;
    case 0x00035FD5u:   /* ecx = turn + speed */
        g_ecx = g_ecx - g_eax + (uint32_t)duel_scale(3, (int32_t)g_eax);
        break;
    /* And its height (0x49C260) coming in at the start, 2 a frame to 160. */
    case 0x00035F68u: g_eax = g_eax - 2u + (uint32_t)duel_scale(4, 2); break;
    default: break;
    }
    return 1;
}

/* ---- Dancing faster than 60 (dance_fps, experimental) ----
 *
 * The dance (sub_00027450) waits at least 10 ms a frame (the tick read at
 * 0x2A579, against the frame's start in [esp+0xC0]) and the vblank holds it
 * to 60; its steps, prompts and camera follow the music and the engine's
 * clock, so with the wait passed and the frame rate unlocked (80 frames a
 * second headless) the same prompts and camera came at the same moments.
 * dance_fps (RECOMP_PIRATES_DANCE_FPS): 60 (the default) as the console;
 * 90, 120, 144 or 240 pass the wait and let the flip gate pace the frames
 * by the host clock, 0 unlocked. */
static struct {
    int setting;          /* the rate as last read */
    int gate_rate;        /* the rate this module set on the gate, 0 none */
    int64_t last_us;      /* the last dance frame's wait */
    int active;           /* faster than 60 */
    int dt_us;            /* this frame's length */
    int32_t rem;          /* dance_scale's remainder */
} g_dance;

#define DANCE_FRAME_US 16667               /* a console frame: 60 a second */

/* A step taken once a console frame: this frame's share of it. */
static int32_t dance_scale(int32_t step)
{
    int64_t t = (int64_t)step * g_dance.dt_us + g_dance.rem;
    g_dance.rem = (int32_t)(t % DANCE_FRAME_US);
    return (int32_t)(t / DANCE_FRAME_US);
}

static int dance_read_rate(void)
{
    const char *v = pirates_scene_fps("RECOMP_PIRATES_DANCE_FPS", "dance_fps");
    int r;

    if (!v || !*v)
        return 60;
    r = pirates_parse_fps(v);
    if (r < 0)
        return -1;
    if (r <= 60)
        return 60;
    return r > 1000 ? 1000 : r;
}

/* At the dance frame's wait (ra 0x2A57E). */
static void dance_frame_wait(void)
{
    int rate = dance_read_rate();
    int64_t now = pirates_host_us();

    g_dance.dt_us = !g_dance.last_us || now - g_dance.last_us > 250000 ? DANCE_FRAME_US
                    : (int)(now - g_dance.last_us);
    if (g_dance.dt_us > 3 * DANCE_FRAME_US)
        g_dance.dt_us = 3 * DANCE_FRAME_US;
    g_dance.last_us = now;
    g_dance.active = rate != 60;
    if (rate != g_dance.setting) {
        g_dance.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[DANCE60] dancing unlocked (dance_fps = 0)\n");
        else
            fprintf(stderr, "[DANCE60] dancing at %d frames a second\n", rate);
        fflush(stderr);
    }
    if (rate == 60) {
        if (g_dance.gate_rate) {
            g_dance.gate_rate = 0;
            xbox_Nv2aFlipGateSetTitleRate(0);
        }
        return;
    }
    g_eax += 1000u;                        /* "10 ms have passed" */
    g_dance.gate_rate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}
/* sea_fps (RECOMP_PIRATES_SEA_FPS): 30 (the default) sails as the console
 * does, 60, 90, 120, 144 or 240 frames a second, 0 unlocked. sea_60 = 1, the
 * setting before it, still means 60 while sea_fps is 30. */
static int sea60_read_rate(void)
{
    const char *v;
    int r = pirates_parse_fps(pirates_scene_fps("RECOMP_PIRATES_SEA_FPS", "sea_fps"));

    if (r < 0)
        return -1;
    if (r <= 30) {
        v = recomp_config_lookup("RECOMP_PIRATES_SEA_60", "sea_60");
        return v && *v && strcmp(v, "0") != 0 ? 60 : 30;
    }
    return r < 60 ? 60 : r > 1000 ? 1000 : r;
}

/* Once a sea frame, at its start (the tick read at 0x9190F). */
static void sea60_begin_frame(void)
{
    int64_t now = pirates_host_us();
    int dt_us, gap, rate = sea60_read_rate(), on = rate != 30;
    int was_on = g_sea60.active;

    if (rate != g_sea60.setting) {
        g_sea60.setting = rate;
        if (rate < 0)
            fprintf(stderr, "[SEA60] sailing unlocked (sea_fps = 0)\n");
        else
            fprintf(stderr, "[SEA60] sailing at %d frames a second%s\n", rate,
                    rate == 30 ? " (sea_fps = 30, as the console)" : "");
        fflush(stderr);
    }
    gap = g_sea60.last_us == 0 || now - g_sea60.last_us > 250000;
    /* The last frame's length from the host clock, not the engine's whole
     * milliseconds (16.67 ms read 16, so 60 frames made 960 ms). */
    dt_us = gap ? SEA60_TICK_US : (int)(now - g_sea60.last_us);
    if (dt_us > SEA60_DT_MAX * 1000) dt_us = SEA60_DT_MAX * 1000;
    if (dt_us < 1) dt_us = 1;
    g_sea60.last_us = now;
    g_sea60.rate = rate;
    g_sea60.active = on;
    g_sea60.dt_us = dt_us;
    /* The game's own frame time is whole ms: carried, so they add up to the
     * real time (4, 4, 4, 5 at 240), and at least 1. */
    g_sea60.ms_acc_us += dt_us;
    g_sea60.dt = g_sea60.ms_acc_us / 1000;
    g_sea60.ms_acc_us -= g_sea60.dt * 1000;
    if (g_sea60.dt < 1) {
        g_sea60.dt = 1;
        g_sea60.ms_acc_us -= 1000;
    }
    /* 0x4C72F8, read by the sea's timers and chances (sub_00077680). */
    if (on && !gap)
        S60_MEM32(0x004C72F8u) = g_sea60.dt;
    if (!on || gap) {
        /* Off, or a new stretch of sailing (after a dialog, a battle, a town,
         * the map, a menu): this frame is a whole tick, as at 30, and no part
         * of a tick is carried in. The time acceleration's 30 fps value and
         * the calendar's sum stay (a value the game set meanwhile, 500 leaving
         * a town or 100 after a load, is seen at 0x860B0): restarting them at
         * every crew dialog made the date fall 1% behind over ten minutes. */
        g_sea60.tick = 1;
        g_sea60.acc_us = 0;
        if (!on) {
            /* Turned off while sailing: the game gets its 30 fps value back. */
            /* And a console frame's time until the game writes its own at
             * 0x91DBC: the AI before that (sub_00084E60 at 0x9194C) would
             * step the dock word by the last fast frame's time / 10, 0. */
            if (was_on)
                S60_MEM32(0x004C72F8u) = SEA60_TICK_US / 1000;
            if (g_sea60.ts_valid && !gap && S60_MEM32(0x0040D4C4u) == g_sea60.ts_last)
                S60_MEM32(0x0040D4C4u) = g_sea60.ts_x;
            g_sea60.cal_acc = 0;
            g_sea60.ts_valid = 0;
            if (g_sea60.gate_rate)
                sea60_set_gate(0);
        }
    } else {
        g_sea60.acc_us += dt_us;
        if (g_sea60.acc_us >= SEA60_TICK_US) {
            g_sea60.tick = 1;
            g_sea60.acc_us -= SEA60_TICK_US;
            if (g_sea60.acc_us > SEA60_TICK_US)   /* a long frame: no double debt */
                g_sea60.acc_us = SEA60_TICK_US;
        } else {
            g_sea60.tick = 0;
        }
    }
    /* Where this frame sits between two ticks, for phases that are the frame
     * counter times a step: the counter went up at the end of the last tick
     * frame, so a frame that does not tick is that far before the next one
     * (in hundredths of a tick, 0 or negative). */
    g_sea60.frac = g_sea60.tick ? 0
                 : (int)((int64_t)(g_sea60.acc_us - SEA60_TICK_US) * 100 / SEA60_TICK_US);
    g_sea60.in_loop = 1;
}

static int sea60_clamp_dt(void)
{
    return g_sea60.dt;
}

/* The title's guest hooks (toolkit patch 0042): run just before the guest
 * instruction at va. Each does nothing unless sea_60 is on at sea. */
/* The battle's hooks; 0 for an address that is not one of them. */
static int btl_guest_hook(uint32_t va)
{
    int skip = !g_btl.tick;

    switch (va) {
    /* The battle frame's ends: the loop's test for another frame (0xC3C25,
     * every frame), and both ways out of sub_000C3950 -- 0xC3C5C, and the
     * return at 0xC3C32 when [0x4D67C7] is set, which a career battle
     * takes. Missing that one left the battle's hooks on after it, with a
     * stale tick, through the plunder scene and back at sea. */
    case 0x000C3C25u:
        g_btl.in_frame = 0;
        return 1;
    case 0x000C3C32u:
    case 0x000C3C38u:
    case 0x000C3C5Cu:
        g_btl.in_frame = 0;
        if (g_btl.gate_rate)
            btl_set_gate(0);
        return 1;
    }
    if (!(va >= 0x000C3000u && va < 0x000DA000u) && va != 0x000FE44Au &&
        va != 0x00078F69u && va != 0x00078F9Bu)
        return 0;                           /* not the battle's */
    /* sub_00078EC0's two sail steps are the sea's too: outside a battle
     * frame they are left to the sea's hooks (sea60_scale 3 and 4). */
    if ((va == 0x00078F69u || va == 0x00078F9Bu) && !(g_btl.active && g_btl.in_frame))
        return 0;
    if (!g_btl.active || !g_btl.in_frame) {
        g_btl.cam_have = 0;                 /* start the camera afresh when it is on again */
        return 1;
    }
    switch (va) {
    /* Short-lived effects drawn by the battle (sub_000FDE20: 256 of them at
     * 0x4D5038, their ages at 0x4D4E38, gone after 5): a year a tick, but
     * never held at 0 -- at age 0 an effect is set up (its +0x70 call), and
     * held there it was set up again every frame until the next tick, which
     * left the characters of every later scene (the plunder deck, the
     * governor, the tavern) drawn black. */
    case 0x000FE44Au:                                             /* inc word [ebx] */
        if (skip && S60_MEM16(g_ebx) > 0)
            S60_MEM16(g_ebx) -= 1;
        break;

    /* Frame counts: undone on a frame that does not tick. */
    case 0x000C3B93u: if (skip) g_ecx -= 1; break;                       /* [obj+CDE0] */

    /* A ship's physics (sub_000D3EC0, ebp the ship): each frame's move,
     * this frame's share of it. */
    case 0x000D416Eu:   /* turn rate eased toward its target: ebx new, esi old */
        g_ebx = (uint32_t)btl_step(g_ebp, BS_RATE, (int32_t)g_esi, (int32_t)g_ebx);
        break;
    case 0x000D4177u:   /* heading += rate / 2: ecx new */
        g_ecx = (uint32_t)btl_step(g_ebp, BS_HEAD, S60_MEM32(g_ebp + 0x11D0u), (int32_t)g_ecx);
        break;
    case 0x000D419Cu:   /* and + rate / 3 with flag 1 of +0x123C: eax new, ecx before */
        g_eax = (uint32_t)btl_step(g_ebp, BS_HEAD2, (int32_t)g_ecx, (int32_t)g_eax);
        break;
    case 0x000D422Du:   /* heading = rate + heading: ebx the rate */
        g_ebx = (uint32_t)btl_step(g_ebp, BS_HEAD3, 0, (int32_t)g_ebx);
        break;
    case 0x000D43CDu:   /* speed eased toward its target: edi new */
        g_edi = (uint32_t)btl_step(g_ebp, BS_SPEED, S60_MEM32(g_ebp + 0x11D4u), (int32_t)g_edi);
        break;
    case 0x000D4485u:   /* sailing: x, y += speed along the heading */
        g_esi = (uint32_t)btl_step(g_ebp, BS_X, S60_MEM32(g_ebp + 0x11C8u), (int32_t)g_esi);
        g_edi = (uint32_t)btl_step(g_ebp, BS_Y, S60_MEM32(g_ebp + 0x11CCu), (int32_t)g_edi);
        break;
    case 0x000D44C0u:   /* stopped: drifting with the wind, eax the x step */
        g_eax = (uint32_t)btl_step(g_ebp, BS_X, 0, (int32_t)g_eax);
        break;
    case 0x000D44E7u:   /* and the y step */
        g_eax = (uint32_t)btl_step(g_ebp, BS_Y, 0, (int32_t)g_eax);
        break;
    /* A ship made a float body (sub_000C60E0, esi the ship): its position
     * from the one before this frame's physics (+0x1754), in body units of
     * 400, truncated -- which on the console moves every ship a little each
     * frame. Exact, less this frame's share of what the truncation takes. */
    case 0x000C6150u: {
        float bx = (float)S60_MEM32(g_esi + 0x1754u) / 400.0f + (float)S60_MEM32(0x004CBE94u);
        float by = (float)S60_MEM32(g_esi + 0x1758u) / 400.0f + (float)S60_MEM32(0x004CBE90u);
        float tx, ty, f = btl_scalef(1.0f);
        memcpy(&tx, &g_eax, 4);
        memcpy(&ty, &g_ecx, 4);
        tx = bx - f * (bx - tx);
        ty = by - f * (by - ty);
        memcpy(&g_eax, &tx, 4);
        memcpy(&g_ecx, &ty, 4);
        break;
    }

    /* The ships' bodies (sub_000C6430): each ship as a float body, moved
     * from where it was before this frame's physics by its velocity, turned
     * by its spin, and written back (sub_000C6320). The move and the turn,
     * this frame's share. */
    case 0x000C69BDu: S60_MEMF(g_esp + 0x98u) *= btl_scalef(1.0f); break;  /* x += dx (sailing) */
    case 0x000C69D1u: S60_MEMF(g_esp + 0x9Cu) *= btl_scalef(1.0f); break;  /* y += dy (before it is loaded) */
    case 0x000C688Fu: S60_MEMF(g_esp + 0x88u) *= btl_scalef(1.0f); break;  /* x += dx (touching) */
    case 0x000C68A3u: S60_MEMF(g_esp + 0x8Cu) *= btl_scalef(1.0f); break;  /* y += dy (before it is loaded) */
    case 0x000C69F2u:   /* heading += spin: eax new, [ebp-8] old, [ebp-0x1C] the ship */
        g_eax = (uint32_t)btl_step((uint32_t)S60_MEM32(g_ebp - 0x1Cu), BS_BHEAD,
                                   S60_MEM32(g_ebp - 8u), (int32_t)g_eax);
        break;
    case 0x000C68C4u:   /* the same, touching: ecx new */
        g_ecx = (uint32_t)btl_step((uint32_t)S60_MEM32(g_ebp - 0x1Cu), BS_BHEAD,
                                   S60_MEM32(g_ebp - 8u), (int32_t)g_ecx);
        break;
    case 0x000C63C4u:   /* back to the ship: speed along the heading, edx new, edi the ship */
        g_edx = (uint32_t)btl_step(g_edi, BS_BSPEED, S60_MEM32(g_edi + 0x11D4u), (int32_t)g_edx);
        break;
    case 0x000C63F8u:   /* and across it, ecx new */
        g_ecx = (uint32_t)btl_step(g_edi, BS_LAT, S60_MEM32(g_edi + 0x175Cu), (int32_t)g_ecx);
        break;

    /* Touching: the push each frame on a body's velocity and spin
     * (0xC6601 .. 0xC679E), this frame's share of it. */
    case 0x000C6601u:
        g_btl.touch_v[0] = S60_MEMF(g_ebp - 0x10u);
        g_btl.touch_v[1] = S60_MEMF(g_ebp - 0x0Cu);
        g_btl.touch_v[2] = S60_MEMF(g_ebp - 4u);
        g_btl.touch_live = 1;
        break;
    case 0x000C679Eu:
        if (g_btl.touch_live) {
            float f = btl_scalef(1.0f);
            S60_MEMF(g_ebp - 0x10u) = g_btl.touch_v[0] + (S60_MEMF(g_ebp - 0x10u) - g_btl.touch_v[0]) * f;
            S60_MEMF(g_ebp - 0x0Cu) = g_btl.touch_v[1] + (S60_MEMF(g_ebp - 0x0Cu) - g_btl.touch_v[1]) * f;
            S60_MEMF(g_ebp - 4u) = g_btl.touch_v[2] + (S60_MEMF(g_ebp - 4u) - g_btl.touch_v[2]) * f;
            g_btl.touch_live = 0;
        }
        break;

    /* A cannonball in flight (sub_000CAF60, sub_000CB220; esi/edi the
     * ball): its countdown once a tick, its x, y steps a frame's share, and
     * its height -- a sine of the countdown -- from the countdown less the
     * part of a tick gone by, so the arc stays smooth. */
    case 0x000CAFA6u: if (skip) S60_MEM32(g_edi + 0x38u) += 1; break;   /* dec [edi+0x38] */
    case 0x000CB26Eu: g_eax = (uint32_t)btl_scale(0, (int32_t)g_eax); break;  /* x += */
    case 0x000CB291u: g_eax = (uint32_t)btl_scale(1, (int32_t)g_eax); break;  /* y += */
    case 0x000CB2B6u:   /* ecx = (2^31 / flight) * countdown; eax = 2^31 / flight */
        g_ecx -= (uint32_t)((uint64_t)g_eax * (uint32_t)g_btl.acc_us / (uint32_t)g_btl.tick_us);
        break;

    /* A sail's angle (sub_00078EC0, the sea's own, here for a battle):
     * eased a sixteenth of the way to its target a frame, or fluttering by
     * a random step; ecx the step, this frame's share. */
    case 0x00078F69u: g_ecx = (uint32_t)btl_scale(4, (int32_t)g_ecx); break;
    case 0x00078F9Bu: g_ecx = (uint32_t)btl_scale(5, (int32_t)g_ecx); break;

    /* A ship blown up and going down (sub_000D9580): its sinking, +5 a
     * frame, once a tick. */
    case 0x000D98EEu: if (skip) g_eax -= 5; break;

    /* The rudder as drawn (sub_000D4680, ebp the ship): a step a frame
     * toward 16 x the rudder; ecx new. */
    case 0x000D4B73u:
        g_ecx = (uint32_t)btl_step(g_ebp, BS_RUDDER, S60_MEM32(g_ebp + 0x11FCu), (int32_t)g_ecx);
        break;

    /* A ship going down (sub_000D4680): its two animation counters (+0x122C,
     * +0x122E) step by the frame time x 30, truncated to whole frames -- at
     * 120 a frame lost 6% of it. The fraction carried a ship. */
    case 0x000D4EEDu:
    case 0x000D512Du: {
        double *rem = &g_btl.anim_rem[btl_ship(g_ebp)][va == 0x000D512Du];
        double t = *(volatile double *)((uintptr_t)0x004D39F8u + g_xbox_mem_offset) *
                   (double)S60_MEMF(0x003FF3FCu) + *rem;
        int32_t n = (int32_t)t;
        *rem = t - n;
        g_eax = (g_eax & 0xFFFF0000u) | ((uint32_t)n & 0xFFFFu);
        break;
    }

    case 0x000D4615u:   /* aground: damage +1 a frame, once a tick */
        if (skip) g_eax -= 1;
        break;
    case 0x000D4625u:   /* and its knock every 20th: not off the tick */
        if (skip) g_edx = 0;
        break;
    case 0x000C3B9Bu: if (skip) S60_MEM32(BTL_OBJ + 0xCDE8u) -= 1; break; /* inc [obj+CDE8] */
    /* A point drifting against the wind, 20 a frame (sub_000C4700, obj
     * +0xCDEC, +0xCDF0: the clouds' or the sea's scroll): a frame's share. */
    case 0x000C472Cu: g_eax = (uint32_t)btl_scale(2, (int32_t)g_eax); break;
    case 0x000C4744u: g_eax = (uint32_t)btl_scale(3, (int32_t)g_eax); break;
    case 0x000C49D4u: if (skip) S60_MEM32(g_edi + 0xCDD8u) -= 1; break;   /* inc [edi+CDD8] (to 30) */

    /* The camera (sub_000C4220): each frame it heads for the middle of the
     * ships (x 0x49C26C, y 0x49C268) at up to 30 a frame and for a height
     * (0x49C260) that keeps them all in view, at up to 80, and takes them
     * once within that. All three are whole numbers worked out from the
     * ships' box in coarse steps -- the height in steps of about 3 -- so a
     * frame faster than 30 moved by 0, 3, 6 or 10 at random and the view
     * jolted. The camera keeps the console's step once a tick (from the
     * ships where they are then), and each frame is drawn that far into the
     * move from the tick before to it: a tick's lag, and smooth. At 0xC45CF
     * the new camera is in ebx, edi and esi (stored after, at 0xC45D6),
     * x and y already stored; on a cut (obj+0xCE36) it is taken at once.
     * At 0xC467E, once the view is set (sub_000DCC50), the console's camera
     * is put back for the next frame to step from. */
    case 0x000C45CFu: {
        int32_t now[3] = { (int32_t)g_ebx, (int32_t)g_edi, (int32_t)g_esi }, out[3];
        double f;
        int k;
        if (!skip || !g_btl.cam_have) {
            int cut = *(volatile uint8_t *)((uintptr_t)(uint32_t)S60_MEM32(g_esp + 0x20u) + 0xCE36u +
                                            g_xbox_mem_offset) != 0;
            for (k = 0; k < 3; k++) {
                g_btl.cam_prev[k] = g_btl.cam_have && !cut ? g_btl.cam_cur[k] : now[k];
                g_btl.cam_cur[k] = now[k];
            }
            g_btl.cam_have = 1;
        }
        f = (double)g_btl.acc_us / g_btl.tick_us;
        if (f > 1.0) f = 1.0;
        for (k = 0; k < 3; k++) {
            double v = g_btl.cam_prev[k] + (g_btl.cam_cur[k] - g_btl.cam_prev[k]) * f;
            out[k] = (int32_t)(v >= 0 ? v + 0.5 : v - 0.5);
        }
        memcpy(g_btl.cam_out, out, sizeof out);
        g_ebx = (uint32_t)out[0];
        g_edi = (uint32_t)out[1];
        g_esi = (uint32_t)out[2];
        S60_MEM32(0x0049C26Cu) = out[0];
        S60_MEM32(0x0049C268u) = out[1];
        break;
    }
    /* And its offset back from the ships, [esp+0x20] (the height x sin 60),
     * times sin 90 (sub_00103A90): the sine table (0x4D26B8) has 256
     * entries to 90 degrees, and the 90 itself reads the word after it
     * (0x4D2AB8, a value the battle changes as it goes), overflowing into a
     * factor up to 1.6% off 1 that wraps whenever that word passes a
     * multiple of 1024: a hop of up to 40 in one frame. Between 15-25 a
     * frame at 30 it passes; at 120 it was the remaining jolt. The last
     * entry, 65535, as the table means. */
    case 0x000C462Bu:
        g_eax = (uint32_t)(int32_t)(((int64_t)S60_MEM32(g_esp + 0x20u) * 65535) >> 16);
        break;
    case 0x000C467Eu:
        if (g_btl.cam_have) {
            S60_MEM32(0x0049C26Cu) = g_btl.cam_cur[0];
            S60_MEM32(0x0049C268u) = g_btl.cam_cur[1];
            S60_MEM32(0x0049C260u) = g_btl.cam_cur[2];
        }
        break;
    default: break;
    }
    return 1;
}

/* ── Sid Meier's Pirates!: the sea's parchment map, scrolled smoothly ──
 *
 * The map at sea (BLACK; sub_00091170 from 0x91A3B, drawn by sub_0005BCC0)
 * was centred on the ship's whole map cell (0x4C70EC/E8, the position /
 * 4,000,000) times 2000, while the camera follows the position / 2000: the
 * map stood still, then jumped a cell (about 1% of the screen) twice a
 * second when sailing fast, at any frame rate.
 *
 * 1. 0x91A65, the call to sub_00059D40 (x in eax, y on top of the stack):
 *    the position / 2000 instead, from the source the cell was taken from
 *    (0x85A38: bit 0x80 of 0x49C298 follows 0x49D040/60, else our ship).
 *
 * 2. That still moves in whole pixels: sub_00059D40 rounds the map's
 *    centre, 0x91A84 turns it into the scroll (ecx, edi at 0x91B32), and
 *    sub_0005AEA0 places the 256-pixel map tiles from it with integer
 *    divisions -- steps of 1-3 pixels at 480, 13 at 2160. At 0x91B32 the
 *    same sums are done without the rounding (parch_frac); the difference
 *    from the tiles' integer place is the part of a pixel the game dropped.
 *    Every sprite sub_000DFDF0 sets up during that call (the tiles) is
 *    moved by it where it stores its place, 0xE0439 ([esp+0x3C] x - W/2,
 *    [esp+0x44] -(y - H/2), floats), so the map moves every frame, by as
 *    little as the frame needs.
 *
 * 3. Everything else sub_0005BCC0 draws (the towns' names and icons, the
 *    dots, the treasure X) is placed through sub_00059D40 from the same
 *    whole scroll, and that call rounds again: f1/f2 (sub_00061660) and the
 *    scaling to the screen. Left whole, they stepped against the smooth
 *    tiles (up to 3 px in y at 480). So inside the map's call each
 *    sub_00059D40 is redone in double at its end (0x59DCB: x saved at its
 *    entry, y, the scroll on the stack, cx stored at [edi], cy in ecx), and
 *    what it dropped moves the draws placed from it until the next one:
 *    sub_000E11B0 (text; 0xE130B, [esp+0x24]/[esp+0x2C]) and sub_000E5470
 *    (icons; 0xE5579, its object's +0x70/+0x78), as well as the tiles.
 *
 * parch_snap = 1 (RECOMP_PIRATES_SEA_MAP_SNAP) keeps the console's whole
 * cells and pixels. */
static struct {
    int in_draw;          /* inside sub_0005BCC0's call at 0x91B32 */
    float fx, fy;         /* the part of a pixel the current draws get */
    int32_t sx, sy;       /* the scroll as the game made it */
    double sxe, sye;      /* the same unrounded */
    int okx, oky;         /* the two are near (no clamp at the map's edge) */
    int32_t px;           /* sub_00059D40's x (eax), saved at its entry,
                             before its own calls can change eax */
    int has_px;           /* ... and not yet used by its end */
} g_seamap;

static int parch_snap(void)
{
    static int snap = -1;
    if (snap < 0)
        snap = recomp_config_bool("RECOMP_PIRATES_SEA_MAP_SNAP", "parch_snap", 0);
    return snap;
}

static int parch_log(void)
{
    static int log = -1;    /* RECOMP_PIRATES_SEA_MAP_LOG=1 (test) */
    if (log < 0)
        log = recomp_config_bool("RECOMP_PIRATES_SEA_MAP_LOG", "parch_log", 0);
    return log;
}

/* The source of the map's centre, as 0x85A38 takes the cell from. */
static void parch_pos(int32_t *x, int32_t *y)
{
    int follow = (S60_MEM32(0x0049C298u) & 0x80) != 0;
    *x = S60_MEM32(follow ? 0x0049D040u : 0x004564BCu);
    *y = S60_MEM32(follow ? 0x0049D060u : 0x004564C0u);
}

/* sub_00061660 without its rounding: f1 for x, f2 for y. */
static double parch_f1(double x) { return x / 462000.0 * 800.0 + 100.0; }
static double parch_f2(double y) { return (292000.0 - y) / 292000.0 * 640.0 + 5.0; }

/* A dropped part of a pixel, or 0 when it is not small (a jump). */
static float parch_small(double d)
{
    return fabs(d) < 64.0 ? (float)d : 0.0f;
}

/* One axis of sub_0005AEA0's first tile: the scroll s (0x91B32's ecx or
 * edi), b the tile's size factor (Bx or By): the game's place,
 * -(floor(r * b / 100)) less whole tiles, r = 2s mod 256; and the exact
 * one, r taken from the unrounded scroll se. Their difference. */
static float parch_axis(int32_t s, double se, int32_t b)
{
    int32_t s2 = s * 2, r = s2 & 255, col = (s2 - r) / 256;
    double re = se * 2.0 - (double)col * 256.0;

    if (b <= 0)
        return 0.0f;
    return parch_small(-(re * (double)b / 100.0) + (double)((int64_t)r * b / 100));
}

/* At 0x91B32: esi the zoom, ecx and edi the scroll as the game made it.
 * The same from the unrounded centre: sub_00059D40's f1 and f2 without
 * their rounding, 0x91A84's sums without their divisions. */
static void parch_frac(void)
{
    int32_t px, py, z = (int32_t)g_esi;
    int32_t w = S60_MEM32(0x0049C3A4u), h = S60_MEM32(0x0049C3A0u), k, a, ay;

    g_seamap.fx = g_seamap.fy = 0.0f;
    g_seamap.okx = g_seamap.oky = 0;
    g_seamap.sx = (int32_t)g_ecx;
    g_seamap.sy = (int32_t)g_edi;
    if (z <= 0 || w <= 0 || h <= 40)
        return;
    parch_pos(&px, &py);
    k = (z - 1) / 2 - 3;
    g_seamap.sxe = parch_f1((double)px / 2000.0) + 87.0 * k;
    g_seamap.sye = parch_f2((double)py / 2000.0) * (double)(h - 40) / (double)h + 50.0 * k;
    g_seamap.okx = fabs(g_seamap.sxe - (double)g_seamap.sx) <= 2.0;
    g_seamap.oky = fabs(g_seamap.sye - (double)g_seamap.sy) <= 2.0;
    a = 0x800 / z;
    ay = 0x50C / z;
    if (a <= 0 || ay <= 0)
        return;
    if (g_seamap.okx)
        g_seamap.fx = parch_axis(g_seamap.sx, g_seamap.sxe, w * 100 / a);
    if (g_seamap.oky)
        g_seamap.fy = parch_axis(g_seamap.sy, g_seamap.sye, (h - 40) * 100 / ay);
    if (parch_log())
        fprintf(stderr, "[SEAMAP] z %d scroll %d %d exact %.3f %.3f shift %.3f %.3f\n",
                z, g_seamap.sx, g_seamap.sy, g_seamap.sxe, g_seamap.sye,
                g_seamap.fx, g_seamap.fy);
}

/* At 0x59DCB, the end of a sub_00059D40 inside the map's call: ebx the
 * zoom, the y and the scroll it was given at [esp+0x1C..0x24], cx stored
 * at [edi], cy in ecx. Its unrounded place, from the unrounded scroll when
 * it was given the map's own: cx = (f1 - sx) * W * z / 1024 and
 * cy = (f2 - sy) * (H - 40) * z / 646, as the game does them. */
static void parch_place(void)
{
    int32_t z = (int32_t)g_ebx, w = S60_MEM32(0x0049C3A4u), h = S60_MEM32(0x0049C3A0u);
    int32_t y = S60_MEM32(g_esp + 0x1Cu), sx = S60_MEM32(g_esp + 0x20u), sy = S60_MEM32(g_esp + 0x24u);
    int32_t cx = S60_MEM32(g_edi), cy = (int32_t)g_ecx;
    double sxe = sx, sye = sy, xe, ye;

    g_seamap.fx = g_seamap.fy = 0.0f;
    if (!g_seamap.has_px || z <= 0 || w <= 0 || h <= 40)
        return;
    g_seamap.has_px = 0;
    if (sx == g_seamap.sx && g_seamap.okx)
        sxe = g_seamap.sxe;
    if (sy == g_seamap.sy && g_seamap.oky)
        sye = g_seamap.sye;
    xe = (parch_f1((double)g_seamap.px) - sxe) * (double)w * (double)z / 1024.0;
    ye = (parch_f2((double)y) - sye) * (double)(h - 40) * (double)z / 646.0;
    g_seamap.fx = parch_small(xe - (double)cx);
    g_seamap.fy = parch_small(ye - (double)cy);
}

/* A sprite's place as the draws store it: floats x - W/2 at va, then z,
 * then -(y - H/2) at va + 8. Moved by the current part of a pixel. */
static void parch_move(uint32_t va)
{
    float f;
    uint8_t *p = (uint8_t *)((uintptr_t)va + g_xbox_mem_offset);
    memcpy(&f, p, 4);
    f += g_seamap.fx;
    memcpy(p, &f, 4);
    memcpy(&f, p + 8, 4);
    f -= g_seamap.fy;
    memcpy(p + 8, &f, 4);
}

static int parch_guest_hook(uint32_t va)
{
    switch (va) {
    case 0x00091A65u: {
        int32_t x, y;
        if (parch_snap())
            break;
        parch_pos(&x, &y);
        g_eax = (uint32_t)(x / 2000);
        S60_MEM32(g_esp) = y / 2000;
        break;
    }
    case 0x00091B32u:
        g_seamap.has_px = 0;
        g_seamap.in_draw = 0;
        if (parch_snap())
            break;
        parch_frac();
        g_seamap.in_draw = 1;
        break;
    case 0x00091B37u:
        g_seamap.in_draw = 0;
        break;
    case 0x00059D40u:   /* a place on the map: its x. sub_00059D40 runs
                         * straight from here to 0x59DCB, no early way out */
        if (g_seamap.in_draw) {
            g_seamap.px = (int32_t)g_eax;
            g_seamap.has_px = 1;
        }
        break;
    case 0x00059DCBu:                       /* ... and what it rounded away */
        if (g_seamap.in_draw)
            parch_place();
        break;
    case 0x000E0439u:                       /* the tiles (sub_000DFDF0) */
        if (g_seamap.in_draw)
            parch_move(g_esp + 0x3Cu);
        break;
    case 0x000E130Bu:                       /* text: the towns' names (sub_000E11B0) */
        if (g_seamap.in_draw)
            parch_move(g_esp + 0x24u);
        break;
    case 0x000E5579u:                       /* icons, dots, the X (sub_000E5470) */
        if (g_seamap.in_draw)
            parch_move(S60_MEM32(g_ebx + 0x1Cu) + 0x70u);
        break;
    default:
        return 0;
    }
    return 1;
}
/* ---- The front end and the sea's dialogs faster than 60 ----
 *
 * menu_fps (RECOMP_PIRATES_MENU_FPS): 60 (the default) as the console; 90,
 * 120, 144 or 240, or 0 unlocked (-1 here), for the front end -- the title,
 * the main menu, the new career pages, loading and saving -- which the game
 * runs in one loop, sub_00126190: the page of the moment (the function at
 * 0x4D5D20), a sleep that keeps the loop to 16 ms a pass (0x5061C4 holds
 * the time owed: += 16 - the last frame's ms, Sleep(that) if above 0), and
 * the frame (sub_000FD780) whose Swap waits for the vblank. Its pages and
 * animations run on the engine's clock (0x4D3A10, the time since a page
 * began at 0x4D5990), so they keep their speed at any rate. */
static int menu_read_rate(void)
{
    const char *v = pirates_scene_fps("RECOMP_PIRATES_MENU_FPS", "menu_fps");
    int r;

    if (!v || !*v)
        return 60;
    r = pirates_parse_fps(v);
    if (r < 0)
        return -1;
    if (r <= 60)
        return 60;
    return r > 1000 ? 1000 : r;
}

static int g_sea_menu_depth;     /* (defined below, with the sea's menu) */
static int g_front_loop_depth;   /* sub_00126190 running */
static int g_front_gate;         /* the rate the front end gave the gate, 0 none */

static int g_signup_depth;       /* sub_00090CC0 running */

static void front_gate_off(void)
{
    if (g_front_gate) {                  /* the game world sets its own */
        g_front_gate = 0;
        xbox_Nv2aFlipGateSetTitleRate(0);
    }
}

extern void sub_00126190_gen(void);
void sub_00126190(void)
{
    g_front_loop_depth++;
    sub_00126190_gen();
    g_front_loop_depth--;
    front_gate_off();
}

/* A new career (game.cpp, 0x3EDCB): the tavern where you sign up, the
 * difficulty, nation and skill pages and the name, which run outside
 * sub_00126190 in loops of their own -- a frame (sub_000FD780), then
 * Sleep(5) (0x8EEFC in sub_0008EB80, 0x8FD32 in sub_0008F7D0), the vblank
 * pacing them at 60. They move by the engine's clock (0x4D3A10, the last
 * frame's ms at 0x4D39F0) too, so menu_fps paces them as the front end. */
extern void sub_00090CC0_gen(void);
void sub_00090CC0(void)
{
    g_signup_depth++;
    sub_00090CC0_gen();
    g_signup_depth--;
    front_gate_off();
}

/* The front end or a new career's pages, with no town or sea menu open
 * inside them. */
static int front_running(void)
{
    return (g_front_loop_depth > 0 || g_signup_depth > 0) &&
           g_sea_menu_depth == 0 && g_town_menu_depth == 0;
}

/* Once a front-end frame (from sub_000FD780, below). Faster than 60 the
 * sleep never holds the loop: the time owed is left at -16, so the next
 * pass's += 16 - ms is at most 0 (and back at 60 it starts from there, not
 * from the seconds a fast loop would have run up), and the gate paces it
 * by the host clock at the rate, renewed every frame. */
static void front_frame(void)
{
    static int said;
    int rate = menu_read_rate();

    if (rate != said) {
        said = rate;
        if (rate < 0)
            fprintf(stderr, "[MENU] front end unlocked (menu_fps = 0)\n");
        else
            fprintf(stderr, "[MENU] front end at %d frames a second\n", rate);
        fflush(stderr);
    }
    if (rate == 60) {
        if (g_front_gate) {
            g_front_gate = 0;
            xbox_Nv2aFlipGateSetTitleRate(0);
        }
        return;
    }
    S60_MEM32(0x005061C4u) = -16;
    g_front_gate = rate;
    xbox_Nv2aFlipGateSetTitleRate(rate);
}

/* The dialogs at sea (sub_00040D50 called inside the sea loop's frame: the
 * crew's reports, a town's approach, the landing party) sleep 10 ms a pass
 * (0x420A0) and present at the vblank, 60 at most whatever sea_fps says.
 * With sea_fps above 60 or unlocked the pass only yields and the gate is
 * renewed with sea_fps, as in the sea's pause menu; their timeouts and
 * animations run on the clock. At 30 and 60 nothing changes. */
static int g_sea_dialog_depth;

extern void sub_00040D50_gen(void);
void sub_00040D50(void)
{
    int at_sea = g_sea60.in_loop && g_town_menu_depth == 0 && g_sea_menu_depth == 0 &&
                 !g_btl.in_frame;

    if (at_sea)
        g_sea_dialog_depth++;
    sub_00040D50_gen();
    if (at_sea)
        g_sea_dialog_depth--;
}

static int sea_dialog_fast(void)
{
    int rate;

    if (g_sea_dialog_depth <= 0 || g_town_menu_depth > 0)
        return 0;
    rate = sea60_read_rate();
    return rate != 30 && rate != 60 ? rate : 0;
}

static int sea_map_guest_hook(uint32_t va);

void recomp_guest_hook(uint32_t va)
{
    int skip;

    if (menu_guest_hook(va))                /* the main menu's EXIT row */
        return;
    if (duel_guest_hook(va))                /* the sword duel's */
        return;
    if (sea_map_guest_hook(va))             /* the world map's stick at sea */
        return;
    if (land_guest_hook(va))                /* land battles (land_fps) */
        return;
    if (sneak_guest_hook(va))               /* the town sneak (sneak_fps) */
        return;

    /* The short-lived effects' ages in a town scene (as btl_guest_hook does
     * in a battle): a year a tick, never held at 0. */
    if (va == 0x000FE44Au && !g_btl.in_frame && town_active()) {
        if (!g_town.tick && S60_MEM16(g_ebx) > 0)
            S60_MEM16(g_ebx) -= 1;
        return;
    }
    /* The dialog loop of a town's rooms (the governor's, sub_00041xxx: one
     * frame of the room a pass) sleeps 10 ms a pass (Sleep at 0x420A0) on
     * top of the room's 30 ms frame wait, which the console's 33 a second
     * hides. With town_fps faster than that the wait is passed, and the
     * sleep -- 10 ms or more on the PC's timer -- held the governor's room
     * to 55-60 frames a second where the tavern (another loop) ran
     * unlocked. Its speech and its people run on the engine's clock, so
     * the pass only yields (Sleep(0)). */
    /* The same loop runs the town's dialogs that have no room behind them
     * (dividing the plunder: ~75 a second on its Sleep(10)), so inside a
     * town visit town_fps alone decides. */
    /* The dance's one frame-counted motion: [esp+0x7C] += 25 a frame at
     * 0x29B7D (a prompt's or a glow's phase, drawn at 0x29B30/0x29BB5).
     * Faster than 60, this frame's share of 25 (made up before the add). */
    if (va == 0x00029B7Du) {
        if (g_dance.active && pirates_host_us() - g_dance.last_us < 250000)
            S60_MEM32(g_esp + 0x7Cu) += dance_scale(25) - 25;
        return;
    }
    /* A new career's page loops sleep 5 ms a frame: only a yield faster
     * than 60 (front_frame paces them). */
    if (va == 0x0008EEFCu || va == 0x0008FD32u) {
        if (front_running() && menu_read_rate() != 60)
            S60_MEM32(g_esp) = 0;
        return;
    }
    if (va == 0x000420A0u) {
        int sea_rate;

        if (town_active() || (g_town_menu_depth > 0 && town_read_rate() != 30) ||
            (front_running() && menu_read_rate() != 60)) {   /* or a new career's */
            S60_MEM32(g_esp) = 0;
        } else if ((sea_rate = sea_dialog_fast()) != 0) {   /* a dialog at sea */
            S60_MEM32(g_esp) = 0;
            sea60_set_gate(sea_rate);
        }
        return;
    }
    if (btl_guest_hook(va))                 /* the battle's */
        return;

    if (parch_guest_hook(va))                /* the sea's parchment map */
        return;

    /* The sea loop's frame runs from the tick read at 0x9190F past its
     * frame wait to 0x9267B; it leaves through 0x92681 (both of its exits).
     * Hooks act only in between, with sea_60 on. */
    if (va == 0x00092681u) {
        g_sea60.in_loop = 0;
        if (g_sea60.gate_rate)               /* the sea's own rate ends with it */
            sea60_set_gate(0);
        return;
    }
    /* A ship's dock/fade word (+0x456500, 0..100: the hull is not drawn
     * from 50, and at 100 in port the ship is removed, 0x88CB7) moves by
     * the frame time / 10 a frame, the remainder dropped: 3 a frame at 30,
     * but 0 under 10 ms, so above about 100 fps a ship that reached a port
     * circled among its buildings (only its flags showing) until a long
     * frame came. Given the console's 33 ms on the tick, nothing between.
     * sub_00084E60 (0x8513E) runs outside the sea loop's window, so these
     * are served before the in_loop test. A ship stepped many times in one
     * host frame -- the AI run 150 or 500 times over for a newly placed
     * ship, with 0x400 set in 0x49C298 (from a town event at 0x1DD01, or at
     * sea from sub_0007D4B7 and 0x89040) -- is given a console frame for
     * every step, as at 30, whatever the tick, and wherever the frame time
     * was last left; otherwise only the sea's own frames (one began in the
     * last 250 ms) are given the tick's. */
    if (va == 0x0008513Eu || va == 0x00088D3Au) {
        uint32_t step;
        if (!g_sea60.active)
            return;
        if (S60_MEM32(0x0049C298u) & 0x400)
            step = 33u;
        else if (g_sea60.last_us != 0 && pirates_host_us() - g_sea60.last_us <= 250000)
            step = g_sea60.tick ? 33u : 0u;
        else
            return;
        if (va == 0x0008513Eu) g_edx = step;   /* frame time read at 0x85131 */
        else                   g_ecx = step;   /* the first step on arrival */
        return;
    }
    if (!g_sea60.active || !g_sea60.in_loop)
        return;
    skip = !g_sea60.tick;
    switch (va) {
    /* The frame-time clamps: what the clamp left, replaced. */
    case 0x00086078u:
        g_sea60.ts_m = g_ecx;                 /* M, and s as it comes in (0x860B0) */
        g_sea60.ts_sin = g_esi;
        g_eax = (uint32_t)sea60_clamp_dt();
        break;
    case 0x000864FEu: S60_MEM32(g_esp + 0x24u) = sea60_clamp_dt(); break;
    case 0x00086588u: g_eax = (uint32_t)sea60_clamp_dt(); break;

    /* Frame counts: undone on a frame that does not tick. */
    case 0x0009259Fu: if (skip) S60_MEM32(0x0049C288u) -= 1; break;  /* inc [0x49C288] */
    case 0x00085BC0u: if (skip) g_eax += 1; break;                    /* dec eax -> 0x4C72FC */
    case 0x00091C19u: if (skip) S60_MEM32(0x004D65B8u) += 1; break;  /* dec [0x4D65B8] */
    case 0x00091C41u: if (skip) g_eax += 1; break;                    /* dec eax -> 0x4D6600 */
    case 0x00079433u: if (skip) S60_MEM16(g_esi + 0x004564D6u) += 1; break;
    case 0x00087240u: if (skip) S60_MEM16(g_ebx + 0x0045650Eu) += 1; break;
    case 0x00086387u: if (skip) S60_MEM16(g_ebx + 0x004564D0u) -= 1; break;
    case 0x00080958u: if (skip) S60_MEM16(g_ebx + 0x004564FCu) -= 1; break;  /* encounter stage count */
    case 0x00092590u: if (skip) g_eax += 1; break;                    /* dec eax -> 0x4D65EC (40 frames) */
    case 0x00086891u: if (skip) g_eax = (uint32_t)S60_MEM32(g_ebx + 0x004564C8u); break;
    case 0x000785E7u: if (skip) g_eax = (g_eax & 0xFFFF0000u) | ((g_eax - 1u) & 0xFFFFu); break;

    /* The calendar: the time scale of a whole tick, applied on the tick. */
    case 0x00085AADu:
        g_sea60.cal_acc += S60_MEM32(0x0040D4C4u);
        g_sea60.cal_saved = S60_MEM32(0x0040D4C4u);
        g_sea60.cal_live = 1;
        if (skip) {
            S60_MEM32(0x0040D4C4u) = 0;          /* no ticks this frame */
        } else {
            /* The console's step: the 30 fps time acceleration, its /100
             * remainder dropped (a sum of frame shares swings with how many
             * frames a tick holds: 4 or 5 at 139 fps lost 3%). The sum, if
             * the 30 fps value is not known yet. */
            S60_MEM32(0x0040D4C4u) = g_sea60.ts_valid ? g_sea60.ts_x : g_sea60.cal_acc;
            g_sea60.cal_acc = 0;
        }
        break;
    case 0x00085B73u:
        if (g_sea60.cal_live) {
            S60_MEM32(0x0040D4C4u) = g_sea60.cal_saved;
            g_sea60.cal_live = 0;
        }
        break;

    /* "Every Nth frame" tests on the frame counter: the counter stays put on
     * a frame that does not tick, so the test would pass twice. Bit 0 set in
     * the tested register makes it fail on that frame. */
    case 0x0008216Bu: if (skip) g_ecx |= 1u; break;   /* test cl, 0x7F  */
    case 0x00083AF1u: if (skip) g_edx |= 1u; break;   /* test dl, 0x1F  */
    case 0x00084380u: if (skip) g_edx |= 1u; break;
    case 0x0008448Au: if (skip) g_edx |= 1u; break;
    case 0x000845FDu: if (skip) g_eax |= 1u; break;   /* test al, 0x1F  */
    case 0x00084720u: if (skip) g_eax |= 1u; break;
    case 0x000848C2u: if (skip) g_edx |= 1u; break;
    case 0x00087537u: if (skip) g_edx |= 1u; break;   /* and edx, 0x80000007 */
    /* The same as a memory test: bumped for the test, put back after it. */
    case 0x0007FE92u:                                  /* test [0x49C288], 0xF */
    case 0x00086702u: {                                /* test [0x49C288], 7   */
        uint32_t mask = va == 0x0007FE92u ? 0xFu : 7u;
        /* Only when the test would pass: a bump elsewhere could make it. */
        if (skip && !(S60_MEM32(0x0049C288u) & mask)) {
            S60_MEM32(0x0049C288u) += 1;
            g_sea60.ctr_pending = 1;
        }
        break;
    }
    case 0x0007FF16u:
    case 0x00086711u:
        if (g_sea60.ctr_pending) { S60_MEM32(0x0049C288u) -= 1; g_sea60.ctr_pending = 0; }
        break;

    /* Phases made as frame counter x step: on a frame that does not tick,
     * moved back by the part of a step not reached yet, so they turn
     * smoothly 60 times a second (the hull's bob among them). */
    case 0x0007F892u: g_ecx += sea60_phase(0x04444444u); break;  /* after imul ecx, 0x4444444 */
    case 0x00085202u: g_ecx += sea60_phase(0x04444444u); break;
    case 0x00083F9Fu: g_ecx += sea60_phase(0x016C16C1u); break;
    case 0x00081CB6u: case 0x00082233u: case 0x0008272Au:    /* imul eax, [0x49C288] */
        g_sea60.phase_k = g_eax; break;
    case 0x00081CBDu: case 0x0008223Au: case 0x00082731u:
        g_eax += sea60_phase(g_sea60.phase_k); break;

    /* Smooth steps, scaled by the frame time (33.3 ms = one console frame). */
    case 0x0007860Cu: g_eax = (uint32_t)sea60_scale(0, (int32_t)g_eax); break;
    case 0x00078620u: g_eax = (uint32_t)sea60_scale(1, (int32_t)g_eax); break;
    /* 0x40D018 moves 1/32 of the way to its target each frame: that step,
     * scaled, is close enough to the same curve in time ((1-1/64)^2 for
     * (1-1/32) at 60). */
    case 0x0007FE90u: g_eax = (uint32_t)sea60_scale(2, (int32_t)g_eax); break;
    /* sub_00078EC0, called every frame for each ship (the HUD for ours, the
     * AI step for theirs): a value eased 1/16 of the way to its target, or
     * 1/8 toward a random one. Both steps scaled. */
    case 0x00078F69u: g_ecx = (uint32_t)sea60_scale(3, (int32_t)g_ecx); break;
    case 0x00078F9Bu: g_ecx = (uint32_t)sea60_scale(4, (int32_t)g_ecx); break;
    /* The time acceleration 0x40D4C4: each frame the game eases it toward
     * frame time x M / 33 (M at 0x86078 in ecx: 200..599, or 100, 50 or 1
     * when it forces the value), s += (target - s)/16 + c, c = +-1, and the
     * result is a per-frame amount. Its fixed points are integer effects
     * of that formula, so it is run here as the console runs it: a 30 fps
     * value x, stepped on each logic tick with the 33 ms target M, and this
     * frame's share x * dt / 33.3 stored (remainder carried). A value the
     * game forced in (not the one stored last) restarts x. */
    case 0x000860B0u: {                                /* mov [0x40D4C4], esi */
        int32_t sin = (int32_t)g_sea60.ts_sin, m = (int32_t)g_sea60.ts_m, d, c;
        int64_t v;
        /* s comes in as the 30 fps value (0x86015); anything else the game
         * forced or capped (100, 50, 1, the cap of 200) or set elsewhere. */
        if (!g_sea60.ts_valid || sin != g_sea60.ts_x) {
            g_sea60.ts_x = sin;
            g_sea60.ts_rem = 0;
            g_sea60.ts_valid = 1;
        }
        if (g_sea60.tick) {
            d = m - g_sea60.ts_x;
            c = d > 0 ? 1 : (d == 0 ? 0 : -1);
            g_sea60.ts_x += d / 16 + c;               /* sar 4 after +15: toward zero */
        }
        v = (int64_t)g_sea60.ts_x * g_sea60.dt_us + g_sea60.ts_rem;
        g_esi = (uint32_t)(int32_t)(v / SEA60_TICK_US);
        g_sea60.ts_rem = (int32_t)(v % SEA60_TICK_US);
        g_sea60.ts_last = (int32_t)g_esi;
        break;
    }
    /* The time acceleration as it is read in (0x8600F) is this frame's
     * share; the code that follows caps it at 200 while a countdown runs
     * and forces 100, 50 or 1, all in 30 fps units: given the 30 fps value. */
    case 0x00086015u:
        if (g_sea60.ts_valid && (int32_t)g_esi == g_sea60.ts_last)
            g_esi = (uint32_t)g_sea60.ts_x;
        break;
    /* sub_00084AB0 (an event: speed halved) keeps the time acceleration at
     * 150 or more: compared as the 30 fps value, not this frame's share. */
    case 0x00084D90u: if (g_sea60.ts_valid) g_ecx = (uint32_t)g_sea60.ts_x; break;
    /* Readers that want the 30 fps time acceleration, not the frame's share:
     * they also multiply by the frame time, or cap it at a 30 fps limit. */
    case 0x0007768Eu: if (g_sea60.ts_valid) g_eax = (uint32_t)g_sea60.ts_x; break; /* sub_00077680: dt x it, cap 400 */
    case 0x00086568u: if (g_sea60.ts_valid) g_ecx = (uint32_t)g_sea60.ts_x; break; /* turning: min(it, 200|500) x dt */
    /* A ship word (+0x45650C) taking it/100 off each frame: the 30 fps
     * amount on the tick, nothing between (integer /100 of half lost most). */
    case 0x00086D52u:
        if (g_sea60.ts_valid)
            g_eax = skip ? 0u : (uint32_t)(g_sea60.ts_x / 100);
        break;
    /* sub_00077680: one chance in 150 a frame (rand() % 150 == 0). */
    case 0x00077C81u: if (skip) g_edx |= 1u; break;
    default: break;
    }
}


/* ---- Ship battles faster than 30 (battle_fps): calls run once a console frame ---- */

/* Battle code that runs once a console frame -- decisions, countdowns,
 * effects started -- on the tick only when battles run faster than 30: the
 * call is skipped as if it had run (pop: the callee's own ret N). */
static int btl_off_tick_bit(uint32_t pop, unsigned bit)
{
    static int mask = -1;   /* RECOMP_PIRATES_BTL_NOGATE: test switch, bits of calls run every frame */
    if (mask < 0) {
        const char *v = getenv("RECOMP_PIRATES_BTL_NOGATE");
        mask = v ? (int)strtoul(v, NULL, 16) : 0;
    }
    if ((unsigned)mask & bit)
        return 0;
    if (g_btl.active && g_btl.in_frame && !g_btl.tick) {
        g_esp += 4u + pop;
        return 1;
    }
    return 0;
}

/* A ship's wake (sub_000D5480, the ship pushed): its history -- arrays at
 * +0x1244..+0x167C shifted a place a frame, the newest point at the ship --
 * and the wake drawn from it. Run every frame, since it draws (as its
 * caller, sub_000D4680, places the ship's model: skipping either left the
 * ships or their wakes undrawn every other frame); off the tick the history
 * is put back as it was after the drawing, so it moves once a tick. */
#define BTL_WAKE_LO 0x1244u
#define BTL_WAKE_HI 0x1680u
extern void sub_000D5480_gen(void);
void sub_000D5480(void)
{
    static uint8_t saved[BTL_WAKE_HI - BTL_WAKE_LO];
    uint32_t ship = (uint32_t)S60_MEM32(g_esp + 4u);
    int keep = g_btl.active && g_btl.in_frame && !g_btl.tick;
    void *hist = (void *)((uintptr_t)(ship + BTL_WAKE_LO) + g_xbox_mem_offset);

    if (keep)
        memcpy(saved, hist, sizeof saved);
    sub_000D5480_gen();
    if (keep)
        memcpy(hist, saved, sizeof saved);
}
extern void sub_000D0B90_gen(void);
void sub_000D0B90(void) { if (!btl_off_tick_bit(0u, 2u)) sub_000D0B90_gen(); }  /* the AI's decisions */
extern void sub_000D6270_gen(void);
void sub_000D6270(void) { if (!btl_off_tick_bit(0u, 4u)) sub_000D6270_gen(); }  /* countdowns, damage effects */
extern void sub_000D6340_gen(void);
void sub_000D6340(void) { if (!btl_off_tick_bit(0u, 8u)) sub_000D6340_gen(); }  /* countdowns, fires started */
/* A cannonball's splash warning (sub_000CAF60). Not its launch smoke,
 * sub_000CB030, which the console runs every frame of the launch: held to
 * the tick, it left the particle emitters it feeds (0x4E6B80, 0x4E66A0)
 * so that every later scene drew its characters black (the plunder
 * deck, the governor, the tavern). */
extern void sub_000CBEE0_gen(void);
void sub_000CBEE0(void) { if (!btl_off_tick_bit(4u, 0x20u)) sub_000CBEE0_gen(); }

/* ---- Land battles faster than 30 (land_fps): the men's chances a tick ----
 *
 * rand() (the C library's, sub_002E0424). A land battle frame off the tick
 * gives the calls that the console makes once a frame for show -- a man's
 * fidget (1 in 50 at 0x4D1F9), a man's re-aim (1 in 2 at 0x4CCF2) and a
 * flag's height (1 in 2 at 0x4992D, 0x49953), a fighter's new cycle (1 in
 * 16 at 0x4D17C), a man's fade as his unit leaves the field (1 in 10 at
 * 0x4B54B) -- the answer that does
 * nothing, without taking a number from the game's sequence: the fights'
 * and the AI's draws then come as they do at 30. */
extern void sub_002E0424_gen(void);
void sub_002E0424(void)
{
    if (g_land.active && g_land.in_frame && !g_land.tick) {
        uint32_t ra = (uint32_t)S60_MEM32(g_esp);
        int v = -1;
        switch (ra) {
        case 0x0004D1FEu: v = 1; break;   /* % 50 != 0: no fidget */
        case 0x0004CCF7u: v = 1; break;   /* odd: no re-aim (but the man it keeps) */
        case 0x00049932u:                 /* even: the flag stays */
        case 0x00049958u: v = 0; break;
        case 0x0004D181u: v = 1; break;   /* & 15 != 0: a fighter's cycle goes on */
        case 0x0004B550u: v = 1; break;   /* % 10 != 0: a man does not start to fade */
        }
        if (v >= 0) {
            g_eax = (uint32_t)v;
            g_esp += 4u;                  /* the return the call would take */
            return;
        }
    }
    if (g_sneak.active && g_sneak.in_frame && g_sneak.decay &&
        (uint32_t)S60_MEM32(g_esp) == 0x000A8340u) {
        g_eax = 0;                        /* the hold meter's fall: its draw a tick */
        g_esp += 4u;
        return;
    }
    sub_002E0424_gen();
}

/* ── Sid Meier's Pirates!: the pause menu at sea follows sea_fps ─────────
 *
 * Start at sea opens the pause menu (status, fleet, ship, log, the world
 * map and its "Select City"): the sea loop (sub_00091170) calls the tab
 * dispatcher sub_00131170 (returning to 0x923FD or 0x92581) or sub_001313F0,
 * which calls it (returning to 0x92562), and is blocked until it returns.
 * Each tab runs its own loop, and each of its frames is drawn by
 * sub_000FD780, the renderer the sea uses too, with the world switched off
 * (0x40EA78 bit 0x1000: the int_pause_background.dds backdrop and the menu,
 * nothing simulated). No frame of it waits on the game's clock, so the flip
 * gate's one Swap a vblank held it to 60 whatever sea_fps said.
 *
 * So while the menu opened from the sea runs, every frame renews the
 * gate's title rate with the sailing rate (toolkit patch 0044), as the sea
 * frames do: 90..240 by the host clock, unlocked with no wait. At 30 and
 * 60 nothing changes (the vblank, 60, as before). Once it closes the sea's
 * next frame sets its own rate, and the gate drops a rate nobody renews
 * after 100 ms.
 *
 * Only the menu called from the sea loop itself, by those return
 * addresses: towns, ship battles and the town sneak run inside the sea's
 * frame too (sub_00085980 -> the town menu sub_00021D70, sub_000C3950 ->
 * sub_000C72F0), and their pause menus keep the vblank's 60 -- but the
 * town sneak's (its Start, 0xA4948), which follows sneak_fps the same way
 * (g_sneak_menu_depth). A tab switch inside the menu calls sub_00131170
 * again and stays counted.
 *
 * The world map tab (sub_0005D8C0) runs at the sailing rate too when it is
 * above 60 (sea_map_guest_hook below keeps its stick at the speed it has
 * at 60). At 30 and 60 it is held at 60 by the gate's title rate, as
 * before, so a frame cap that is off or above 60 does not lift it either.
 * Its cursor's pulse runs on the clock (sub_0005BCC0 reads the tick). */
static int g_sea_menu_depth;   /* menu calls running that were opened from the sea loop */
static int g_sea_menu_map;     /* the world map tab is running inside it */
static int g_sneak_menu_depth; /* and from the town sneak (0xA4948, its Start) */

static uint32_t pirates_return_address(void)
{
    return *(volatile uint32_t *)((uintptr_t)g_esp + g_xbox_mem_offset);
}

extern void sub_00131170_gen(void);
void sub_00131170(void)
{
    uint32_t ra = pirates_return_address();
    int at_sea = g_sea_menu_depth > 0 ||
                 (g_sea60.in_loop && (ra == 0x000923FDu || ra == 0x00092581u));

    /* The town sneak's own Start (sub_000A1E10, at 0xA4948) opens the same
     * menu inside its frame: it follows sneak_fps the same way. */
    int in_sneak = !at_sea && (g_sneak_menu_depth > 0 || (g_sneak.in_frame && ra == 0x000A494Du));

    if (at_sea)
        g_sea_menu_depth++;
    if (in_sneak)
        g_sneak_menu_depth++;
    sub_00131170_gen();
    if (at_sea)
        g_sea_menu_depth--;
    if (in_sneak)
        g_sneak_menu_depth--;
}

extern void sub_001313F0_gen(void);
void sub_001313F0(void)
{
    int at_sea = g_sea60.in_loop && g_sea_menu_depth == 0 &&
                 pirates_return_address() == 0x00092562u;

    if (at_sea)
        g_sea_menu_depth++;
    sub_001313F0_gen();
    if (at_sea)
        g_sea_menu_depth--;
}

static int32_t g_sea_map_x, g_sea_map_y;   /* the view the stick last left (clamped at the next frame) */
static int g_sea_map_frames;                /* frames the stick moved it, this visit */

static struct {
    int armed;                 /* 0x5E3D0 passed this frame */
    int32_t x0, y0;            /* the view before the stick's step */
    double fx, fy;             /* the step owed, in pixels, not yet taken */
    int64_t last_ns;           /* host time of the last stick pass */
} g_sea_map;

/* A visit to the map starts and ends with nothing owed and no last frame. */
static void sea_map_reset(void)
{
    memset(&g_sea_map, 0, sizeof g_sea_map);
    g_sea_map_frames = 0;
}

extern void sub_0005D8C0_gen(void);
void sub_0005D8C0(void)
{
    static int trace = -1;

    if (trace < 0) {
        const char *v = getenv("RECOMP_PIRATES_MAP_TRACE");
        trace = v && *v && strcmp(v, "0") != 0;
    }
    sea_map_reset();                   /* nothing carried from another visit */
    g_sea_menu_map++;
    sub_0005D8C0_gen();
    g_sea_menu_map--;
    if (trace && (g_sea_menu_depth > 0 || g_town_menu_depth > 0) &&
        g_sea_map_frames > 0) {   /* for the tests */
        fprintf(stderr, "[%s] world map closed: view %d,%d after %d stick frames\n",
                g_sea_menu_depth > 0 ? "SEA60" : "TOWN60",
                g_sea_map_x, g_sea_map_y, g_sea_map_frames);
        fflush(stderr);
    }
    sea_map_reset();
}

/* The world map's stick, faster than 60 (sea_fps above 60 or unlocked).
 *
 * Each frame the map is not choosing a city ([esp+0x150] set), the loop of
 * sub_0005D8C0 moves its view (x in ebp, y in edi and [esp+0x18]) at
 * 0x5E3D0..0x5E44D by the pad's axes for this controller (0x4D6734's
 * table, 0x4D4170..0x4D4280) times 12 / zoom, each product cut to a whole
 * number: a fixed step a frame, at the 60 a second the console draws it.
 * It is clamped to the map from 0x5E451 on.
 *
 * So the step the console takes is kept, cut as it cuts it (a slight push
 * still moves nothing), and only how much of it a frame gets changes: the
 * step times the frame's time over a 60th of a second, the part of a pixel
 * left over carried to the next frame. A frame after a gap of more than
 * 100 ms (the map just opened, or back from choosing a city) counts as a
 * 60th; a slow frame counts as at most three. At 30 and 60 the map runs at
 * 60 and nothing changes. */

/* The town's world map tab (its status pages, the town menu sub_00021D70
 * with no sea menu open) does the same at town_fps above 60: the town menu's
 * Swaps (sub_002C0F70) pace it at that rate, and this keeps its stick at
 * the speed it has at 60. Not while a scene with its own frame wait runs
 * inside the town. */
static int town_map_fast(void)
{
    int rate;

    if (g_town_menu_depth <= 0 || g_sea_menu_depth > 0 || g_sea_menu_map <= 0 ||
        g_btl.in_frame ||
        (g_pirates_world_seen &&
         pirates_host_ms() - g_pirates_world_ms <= PIRATES_WORLD_RECENT_MS))
        return 0;
    rate = town_read_rate();
    return rate != 30 && rate != 60;
}

static int sea_map_fast(void)
{
    int rate;

    if (g_sea_menu_map <= 0)
        return 0;
    if (g_sneak_menu_depth > 0) {
        rate = sneak_read_rate();
        return rate != 30 && rate != 60;
    }
    if (g_sea_menu_depth <= 0)
        return town_map_fast();
    rate = sea60_read_rate();
    return rate != 30 && rate != 60;
}

static int32_t sea_map_take(double *owed, int32_t step, double frames)
{
    int32_t whole;

    if (step == 0) {                   /* the stick let go on this axis */
        *owed = 0.0;
        return 0;
    }
    *owed += step * frames;
    whole = (int32_t)*owed;            /* toward 0, as the console cuts */
    *owed -= whole;
    return whole;
}

static int sea_map_guest_hook(uint32_t va)
{
    if (va == 0x0005E3D0u) {
        g_sea_map.armed = sea_map_fast();
        if (g_sea_map.armed) {
            g_sea_map.x0 = (int32_t)g_ebp;
            g_sea_map.y0 = (int32_t)g_edi;
        }
        return 1;
    }
    if (va == 0x0005E451u) {
        int64_t now;
        double frames;

        if ((g_sea_menu_depth > 0 || g_town_menu_depth > 0) && g_sea_menu_map > 0 &&
            !g_sea_map.armed) {
            if ((int32_t)g_ebp != g_sea_map_x || (int32_t)g_edi != g_sea_map_y)
                g_sea_map_frames++;
            g_sea_map_x = (int32_t)g_ebp;
            g_sea_map_y = (int32_t)g_edi;
        }
        if (!g_sea_map.armed)
            return 1;
        g_sea_map.armed = 0;
        now = host_time_ns();
        frames = 1.0;
        if (g_sea_map.last_ns && now - g_sea_map.last_ns <= 100000000) {
            frames = (double)(now - g_sea_map.last_ns) * 60.0 / 1e9;
            if (frames > 3.0)
                frames = 3.0;
        } else {
            g_sea_map.fx = g_sea_map.fy = 0.0;
        }
        g_sea_map.last_ns = now;
        g_ebp = (uint32_t)(g_sea_map.x0 +
                           sea_map_take(&g_sea_map.fx, (int32_t)g_ebp - g_sea_map.x0, frames));
        g_edi = (uint32_t)(g_sea_map.y0 +
                           sea_map_take(&g_sea_map.fy, (int32_t)g_edi - g_sea_map.y0, frames));
        S60_MEM32(g_esp + 0x18u) = (int32_t)g_edi;
        if ((int32_t)g_ebp != g_sea_map_x || (int32_t)g_edi != g_sea_map_y)
            g_sea_map_frames++;
        g_sea_map_x = (int32_t)g_ebp;
        g_sea_map_y = (int32_t)g_edi;
        return 1;
    }
    return 0;
}

extern void sub_000FD780_gen(void);
void sub_000FD780(void)
{
    if (front_running()) {
        front_frame();                     /* the front end (menu_fps) */
        sneak_leave();
    }
    if (g_sea_menu_depth > 0) {
        static int said = -2, said_map = -1;
        int setting = sea60_read_rate();
        int rate = setting;
        int on_map = g_sea_menu_map > 0;

        if (g_sea_menu_map > 0 && (rate == 30 || rate == 60))
            rate = 60;                     /* the map: 60, as before */
        if (rate != said || on_map != said_map) {
            said = rate;
            said_map = on_map;
            if (rate == 30 || rate == 60)
                fprintf(stderr, "[SEA60] pause menu at sea: 60 (sea_fps = %d%s)\n",
                        setting < 0 ? 0 : setting, g_sea_menu_map > 0 ? ", world map" : "");
            else if (rate < 0)
                fprintf(stderr, "[SEA60] pause menu at sea: unlocked (sea_fps = 0%s)\n",
                        g_sea_menu_map > 0 ? ", world map" : "");
            else
                fprintf(stderr, "[SEA60] pause menu at sea: %d frames a second%s\n", rate,
                        g_sea_menu_map > 0 ? " (world map)" : "");
            fflush(stderr);
        }
        if (g_sea_menu_map > 0 && rate == 60)
            sea60_set_gate(60);            /* 60 by the host clock, over any frame cap */
        else if (rate != 30 && rate != 60)
            sea60_set_gate(rate);
        else if (g_sea60.gate_rate)
            sea60_set_gate(0);
    } else if (g_sneak_menu_depth > 0) {   /* the town sneak's: sneak_fps, the same way */
        int rate = sneak_read_rate();

        if (g_sea_menu_map > 0 && (rate == 30 || rate == 60))
            sneak_set_gate(60);
        else if (rate != 30 && rate != 60)
            sneak_set_gate(rate);
        else if (g_sneak.gate_rate)
            sneak_set_gate(0);
    }
    sub_000FD780_gen();
}

/* The town menu and its screens faster than 60 (town_fps above 60 or
 * unlocked). The rooms wait for their 30 ms frame and town_frame_wait sets
 * the flip gate's rate from it, but the town menu (sub_00021D70) and the
 * screens it opens without a room -- the merchant, the shipwright, the
 * plunder, the status pages -- have no frame wait of their own: on the
 * console the vblank paces them, and here the gate's one Swap a vblank
 * (frame cap adaptive, the default) held them to 60 whatever town_fps
 * said. Their pictures move on the engine's clock, not by the frame.
 *
 * So at every Swap (sub_002C0F70, the render target's present) made while
 * a town menu call runs, the gate's title rate is renewed with town_fps,
 * as the rooms do: 90..240 by the host clock, unlocked with no wait. Not
 * while a scene with its own frame wait runs inside the town (a duel or
 * a battle: the world frame waits, sub_001A958A), which sets its own rate
 * or the vblank's; the dance paces itself. The world map tab of the
 * status pages runs at town_fps above 60 too, its stick kept at the speed
 * it has at 60 (town_map_fast); at 30 and 60 it is held at 60 by the host
 * clock whatever the frame cap says, as at sea. Elsewhere at 30 and 60 nothing changes. Once the town is left nobody renews it and the gate drops it
 * after 100 ms. */
extern void sub_002C0F70_gen(void);
void sub_002C0F70(void)
{
    if (g_town_menu_depth > 0 && !g_btl.in_frame &&
        !(g_pirates_world_seen &&
          pirates_host_ms() - g_pirates_world_ms <= PIRATES_WORLD_RECENT_MS)) {
        int rate = town_read_rate();
        if (g_sea_menu_map > 0 && (rate == 30 || rate == 60)) {
            g_town.gate_rate = 60;         /* the world map tab: 60 by the host */
            xbox_Nv2aFlipGateSetTitleRate(60);   /* clock at 30 and 60, as before */
        } else if (rate != 30 && rate != 60) {   /* the map too: town_map_fast */
            g_town.gate_rate = rate;
            xbox_Nv2aFlipGateSetTitleRate(rate);
        }
    }
    sub_002C0F70_gen();
}

extern void sub_001A958A_gen(void);
extern int recomp_config_int(const char *env_name, const char *key, int fallback);
extern int recomp_settings_default_frame_interp(void);
extern int hle_frame_interp(void);   /* toolkit: frames shown per frame drawn, now */
extern void xbox_Nv2aFlipGateSetTitleInterval(uint32_t interval);
extern int xbox_Nv2aFlipGateVblanksPerSwap(void);
#define PIRATES_LIMITER_SEA  0x00091DE1u     /* after the limiters' tick reads: */
#define PIRATES_LIMITER_SEA2 0x000A4AEEu     /* the town sneak (sub_000A1E10, town.cpp; long taken for a second sea loop; sneak_fps), */
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
    /* Another scene's frame: no town sneak frame is open (its wait clears
     * it, but a sneak frame that skips the wait -- 0x49C298 bit 8 -- and
     * leaves the loop would leave it set), and the next sneak starts
     * afresh (sneak_begin_frame). */
    if (ra == 0x0009190Fu || ra == 0x000C397Fu || ra == 0x0004D3A3u || ra == 0x00031664u ||
        ra == 0x00099C4Eu || ra == 0x0002A57Eu)
        sneak_leave();
    if (g_pirates_front < 0)
        pirates_front_end(1);            /* start-up is the front end */
    if (ra == 0x00091DE1u || ra == 0x000A4AEEu || ra == 0x000C3B4Eu ||
        ra == 0x00036C60u || ra == 0x0004DF8Fu) {
        pirates_front_end(0);            /* sea, battle, duel: the game world */
        g_pirates_world_ms = pirates_host_ms();
        g_pirates_world_seen = 1;
        if (ra == 0x00091DE1u || ra == 0x000A4AEEu) {   /* the sea */
            if (ra == 0x00091DE1u) {     /* sailing: LB/RB are Black/White; */
                g_pirates_sea_ms = g_pirates_world_ms;   /* not in the town sneak */
                g_pirates_sea_seen = 1;
            }
            g_pirates_in_sea_frame = 0;          /* its frame is drawn */
        } else {
            g_pirates_battle_ms = g_pirates_world_ms;
            g_pirates_battle_seen = 1;
            if (ra == 0x000C3B4Eu) {     /* a ship battle */
                g_pirates_ship_battle_ms = g_pirates_world_ms;
                g_pirates_ship_battle_seen = 1;
            }
        }
    }
    /* A duel left with its rate on the gate: the gate as before (it also
     * drops a rate nobody renews within 100 ms, but not at once). */
    if (g_duel.gate_rate && ra != 0x00031664u && ra != PIRATES_LIMITER_DUEL &&
        pirates_host_us() - g_duel.last_us > 150000) {
        g_duel.gate_rate = 0;
        xbox_Nv2aFlipGateSetTitleRate(0);
    }
    if (ra == 0x00031664u) {              /* a duel frame's top */
        duel_begin_frame();
        {   /* RECOMP_PIRATES_DUEL_DUMP=<file> (test): a record a duel frame --
             * the host time in us (8 bytes), then 0x424C00..0x425000 (the
             * fighters' records at 0x424D88, stride 0x4C), 0x40F600..0x40F700
             * and the loop's locals ([esp]..[esp+0x600]) -- to compare runs
             * at 30 and faster. */
            static FILE *df;
            static int dopen;
            if (!dopen) {
                const char *dv = getenv("RECOMP_PIRATES_DUEL_DUMP");
                dopen = 1;
                if (dv && *dv)
                    df = fopen(dv, "wb");
            }
            if (df) {
                int64_t us = pirates_host_us();
                fwrite(&us, 8, 1, df);
                fwrite((const void *)((uintptr_t)0x00424C00u + g_xbox_mem_offset), 1, 0x400, df);
                fwrite((const void *)((uintptr_t)0x0040F600u + g_xbox_mem_offset), 1, 0x100, df);
                fwrite((const void *)((uintptr_t)g_esp + 4u + g_xbox_mem_offset), 1, 0x600, df);
                fflush(df);   /* the test stops the game without closing it */
            }
        }
    }
    if (ra == 0x0002A57Eu)                /* a dance frame's wait */
        dance_frame_wait();
    else if (g_dance.gate_rate && pirates_host_us() - g_dance.last_us > 150000) {
        g_dance.gate_rate = 0;            /* the dance is over: the gate as before */
        xbox_Nv2aFlipGateSetTitleRate(0);
    }    if (ra == 0x00099C4Eu)                /* a town scene's frame wait */
        town_frame_wait();
    /* RECOMP_PIRATES_SRAND=<n>: the game's srand(timeGetTime()) calls (at
     * start-up, a land battle's start, sub_000E2020) get n instead, so a
     * scripted test repeats: the same start location and the same draws. */
    if (ra == 0x0003E518u || ra == 0x001010BBu || ra == 0x000E202Fu) {
        const char *sd = getenv("RECOMP_PIRATES_SRAND");
        if (sd && *sd)
            g_eax = (uint32_t)strtoul(sd, NULL, 0);
    }
    if (ra == 0x0004D3A3u)                /* a land battle frame begins */
        land_begin_frame();
    if (ra == 0x0004DFC4u)                /* and its wait is over */
        g_land.in_frame = 0;
    if (ra == 0x000C397Fu) {              /* a battle frame begins */
        btl_begin_frame();
        btl_test_frame(g_btl.tick, g_btl.dt_us);
    }
    if (ra == 0x0009190Fu || ra == 0x000A43FAu) { /* a sea frame begins */
        g_pirates_in_sea_frame = 1;
        if (ra == 0x000A43FAu)           /* the town sneak (sub_000A1E10) */
            sneak_begin_frame();
        if (ra == 0x0009190Fu) {         /* sub_00091170, the sea; 0xA43FA is the town sneak */
            sea60_begin_frame();
            sea60_test_frame(g_sea60.active, g_sea60.tick, g_sea60.dt,
                             (int)((int64_t)g_sea60.acc_us * 100 / SEA60_TICK_US));
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
    /* Sailing faster than 30 (sea_fps). Only the sea's loop (sub_00091170):
     * the second "sea" wait, 0xA4AEE, is the town sneak's loop
     * (sub_000A1E10, town.cpp), which has sneak_fps. At 60 the sea's own wait
     * passes at SEA60_WAIT_MS and the gate's one Swap a vblank paces it;
     * above 60 (and unlocked) the wait passes at once and the gate paces it
     * by the host clock at the rate (toolkit patch 0044) for as long as sea
     * frames keep coming. No interpolation at sea either way. */
    /* Battles faster than 30 (battle_fps): the battle's wait (31 ms, 100 in
     * slow motion) passes at once and the gate paces the frames, by the
     * vblank at 60 and the host clock above it, as the sea does. */
    /* Land battles faster than 30 (land_fps): the battle's 30 ms wait passes
     * at once, paced the same way. */
    if (g_land.active && g_land.in_frame && ra == PIRATES_LIMITER_30MS) {
        if (g_land.rate == 60) {
            g_eax += 30u - SEA60_WAIT_MS;
            if (g_land.gate_rate)
                land_set_gate(0);
        } else {
            g_eax += 30u;
            land_set_gate(g_land.rate);
        }
        if (at30) {
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
    /* The town sneak faster than 30 (sneak_fps): its 31 ms wait passes. */
    if (g_sneak.active && g_sneak.in_frame && ra == PIRATES_LIMITER_SEA2) {
        if (g_sneak.rate == 60) {
            g_eax += 31u - SEA60_WAIT_MS;
            if (g_sneak.gate_rate)
                sneak_set_gate(0);
        } else {
            g_eax += 31u;
            sneak_set_gate(g_sneak.rate);
        }
        if (at30) {
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
    if (g_btl.active && g_btl.in_frame && ra == PIRATES_LIMITER_BTL) {
        uint32_t wait = (S60_MEM32(PIRATES_SLOW_FLAGS) & 0x20) ? 100u : 31u;
        if (g_btl.rate == 60) {
            g_eax += wait - SEA60_WAIT_MS;
            if (g_btl.gate_rate)
                btl_set_gate(0);
        } else {
            g_eax += wait;
            btl_set_gate(g_btl.rate);
        }
        if (at30) {
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
    /* Duels faster than 30 (duel_fps): the same (duel_frame_wait). */
    if (g_duel.active && ra == PIRATES_LIMITER_DUEL) {
        duel_frame_wait();
        if (at30) {
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
    if (g_sea60.active && g_sea60.in_loop && ra == PIRATES_LIMITER_SEA) {
        if (g_sea60.rate == 60) {
            g_eax += 31u - SEA60_WAIT_MS;
            if (g_sea60.gate_rate)
                sea60_set_gate(0);
        } else {
            g_eax += 31u;
            sea60_set_gate(g_sea60.rate);
        }
        if (at30) {
            at30 = 0;
            xbox_Nv2aFlipGateSetTitleInterval(1);
        }
        return;
    }
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

