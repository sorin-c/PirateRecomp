/*
 * sea60_test.c -- the test harness for sailing at 60 (sea_60), used by the
 * scripts in tools/sea60 (docs/sea60-testing.md). A player never needs it.
 *
 * Nothing here runs unless RECOMP_PIRATES_SEA60_TEST names a folder. Then,
 * at the start of every sea frame (the tick read at 0x9190F, called from
 * sub_001A958A in recomp_manual.c):
 *
 *   frames.txt  one line a sea frame: host ms, 0, 0x4D67D8, the dwords in
 *               WATCH below, then "| active tick dt acc3" (sea_60's state).
 *               A line starting with # marks a pin, a poke or a flip.
 *   snaps.bin   every second: 16-byte header (magic, host ms, frames,
 *               dword count) and the ship table 0x4564B0..0x49C0B0.
 *   the log     "[SEA60] clock sync host_ms H tick64 T", to put the sound
 *               log's GetTickCount64 times on the frames' clock.
 *
 * and these, each read once:
 *   RECOMP_PIRATES_SEA60_PIN="x y ms [heading speed]"  put our ship back at
 *       map x, y every ms (open water for a long test), and optionally the
 *       heading (degrees) and the speed (0x4564C8)
 *   RECOMP_PIRATES_SEA60_POKE="addr=value@ms,..."  set a dword once, ms
 *       after the harness started (start a countdown in both runs alike)
 *   RECOMP_PIRATES_SEA60_HOLD="addr=value,..."    set dwords every frame
 *   RECOMP_PIRATES_SEA60_NOSHIPS=1                no AI ships (every slot
 *       of the ship table emptied each frame): the world stays the same in
 *       both runs, as AI ships make it diverge
 *   RECOMP_PIRATES_SEA60_FLIP="ms=rate,..."       set sea_fps to the rate
 *       (30 off, 0 unlocked) at those times, as the F2 menu does
 *       (recomp_config_set)
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern ptrdiff_t g_xbox_mem_offset;
const char *recomp_config_lookup(const char *env_name, const char *key);
int recomp_config_set(const char *key, const char *value);

#define MEM32(a) (*(volatile uint32_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))
#define MEM16(a) (*(volatile int16_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

#define SHIPS_LO 0x004564B0u
#define SHIPS_HI 0x0049C0B0u

/* The dwords in frames.txt, in the order tools/sea60/accept.py reads them:
 * our ship 0x4564A0..0x45651C (x 0x4564BC, y +4, heading +8, speed +0xC),
 * then the calendar, two flag words, the frame counter, three countdowns,
 * the time acceleration and an angle. */
static const uint32_t EXTRA[] = {
    0x0049C284u, 0x0049C28Cu, 0x0049C298u, 0x004D65E8u, 0x0049C288u,
    0x004C72FCu, 0x004D65B8u, 0x004D6600u, 0x0040D4C4u, 0x004D652Cu,
};

static uint32_t host_ms(void)
{
    struct timespec ts;
    if (!timespec_get(&ts, TIME_UTC))
        return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* "a=v@t,a=v@t" style lists: up to max entries, n fields each */
static int parse_list(const char *p, const char *fmt, int fields, uint32_t (*out)[3], int max)
{
    int n = 0;
    while (p && *p && n < max) {
        unsigned a = 0, b = 0, c = 0;
        if (sscanf(p, fmt, &a, &b, &c) != fields)
            break;
        out[n][0] = a; out[n][1] = b; out[n][2] = c;
        n++;
        p = strchr(p, ',');
        if (p) p++;
    }
    return n;
}

void sea60_test_frame(int active, int tick, int dt, int acc3)
{
    static int on = -1, npoke, nhold, nflip, flip_next, noships;
    static int pin, px, py, pms, ph = -1, pv = -1;
    static char dir[400];
    static FILE *ff, *fs;
    static uint32_t t0, last_pin, last_snap, frames;
    static uint32_t poke[16][3], hold[8][3], flip[8][3];
    static char poked[16];
    uint32_t now = host_ms(), i;

    if (on < 0) {
        const char *v = getenv("RECOMP_PIRATES_SEA60_TEST");
        char fn[512];
        on = v && *v && strcmp(v, "0") != 0;
        if (!on)
            return;
        snprintf(dir, sizeof dir, "%s", v);
        snprintf(fn, sizeof fn, "%s/frames.txt", dir);
        if ((ff = fopen(fn, "w")) != NULL)
            setvbuf(ff, NULL, _IOFBF, 1 << 20);
        v = getenv("RECOMP_PIRATES_SEA60_PIN");
        pin = v && sscanf(v, "%d %d %d %d %d", &px, &py, &pms, &ph, &pv) >= 3;
        npoke = parse_list(getenv("RECOMP_PIRATES_SEA60_POKE"), "%x=%u@%u", 3, poke, 16);
        nhold = parse_list(getenv("RECOMP_PIRATES_SEA60_HOLD"), "%x=%u", 2, hold, 8);
        nflip = parse_list(getenv("RECOMP_PIRATES_SEA60_FLIP"), "%u=%u", 2, flip, 8);
        noships = getenv("RECOMP_PIRATES_SEA60_NOSHIPS") != NULL;
        t0 = last_pin = now;
        fprintf(stderr, "[SEA60] test harness writing to %s (RECOMP_PIRATES_SEA60_TEST)\n", dir);
#ifdef _WIN32
        fprintf(stderr, "[SEA60] clock sync host_ms %u tick64 %llu\n", now,
                (unsigned long long)GetTickCount64());
#endif
        fflush(stderr);
    }
    if (!on)
        return;

    if (pin && now - last_pin >= (uint32_t)pms) {
        last_pin = now;
        MEM32(0x004564BCu) = (uint32_t)(px * 4000000);
        MEM32(0x004564C0u) = (uint32_t)(py * 4000000);
        if (ph >= 0)
            MEM32(0x004564C4u) = (uint32_t)((uint64_t)ph * 0x100000000ull / 360u);
        if (pv >= 0)
            MEM32(0x004564C8u) = (uint32_t)pv;
        if (ff) fputs("# pin\n", ff);
    }
    for (i = 0; i < (uint32_t)nhold; i++)
        MEM32(hold[i][0]) = hold[i][1];
    if (noships) {
        uint32_t k;
        for (k = 1; k < 128; k++)
            MEM16(SHIPS_LO + k * 0x45Cu) = -1;     /* the slot's kind: empty */
    }
    for (i = 0; i < (uint32_t)npoke; i++)
        if (!poked[i] && now - t0 >= poke[i][2]) {
            poked[i] = 1;
            MEM32(poke[i][0]) = poke[i][1];
            if (ff) fprintf(ff, "# poke %08X=%u\n", poke[i][0], poke[i][1]);
        }
    if (flip_next < nflip && now - t0 >= flip[flip_next][0]) {
        char v[16];
        snprintf(v, sizeof v, "%u", flip[flip_next][1]);
        recomp_config_set("sea_fps", v);
        if (ff) fprintf(ff, "# flip sea_fps=%s\n", v);
        flip_next++;
    }

    frames++;
    if (ff) {
        uint32_t a;
        fprintf(ff, "%u 0 %u", now, MEM32(0x004D67D8u));
        for (a = 0x004564A0u; a < 0x00456520u; a += 4)
            fprintf(ff, " %d", (int32_t)MEM32(a));
        for (i = 0; i < sizeof EXTRA / sizeof EXTRA[0]; i++)
            fprintf(ff, " %d", (int32_t)MEM32(EXTRA[i]));
        fprintf(ff, " | %d %d %d %d\n", active, tick, dt, acc3);
    }
    if (now - last_snap >= 1000u) {
        uint32_t hdr[4] = { 0x53454138u, now, frames, (SHIPS_HI - SHIPS_LO) / 4u };
        last_snap = now;
        if (!fs) {
            char fn[512];
            snprintf(fn, sizeof fn, "%s/snaps.bin", dir);
            fs = fopen(fn, "wb");
        }
        if (fs) {
            fwrite(hdr, 4, 4, fs);
            fwrite((const void *)((uintptr_t)SHIPS_LO + g_xbox_mem_offset), 4, hdr[3], fs);
            fflush(fs);
            if (ff) fflush(ff);
        }
    }
}

/*
 * The battle spike (RECOMP_PIRATES_BTL_TEST=<folder>): at the start of every
 * ship-battle frame (the tick read at 0xC397A in sub_000C3950), a line in
 * btl_frames.txt -- host ms, the frame counter 0x4E3960 (battle object
 * 0x4D6B80 + 0xCDE0), ours 0x4D8F10..+0x60, the enemy 0x4DBE08..+0x60 --
 * and every second the whole battle object (0x4D6B80..0x4E39C0) in
 * btl_snaps.bin (16-byte header as snaps.bin).
 */
#define BTL_LO 0x004D6B80u
#define BTL_HI 0x004E39C0u
/* RECOMP_PIRATES_BTL_WIDE=1: the snapshots cover the game's whole data
 * (0x400000..0x510000) instead of the battle object. */
#define BTL_WIDE_LO 0x00400000u
#define BTL_WIDE_HI 0x00510000u
void btl_test_frame(int btl_tick, int btl_dt)
{
    static int on = -1;
    static char dir[400];
    static FILE *ff, *fs, *fb;
    static uint32_t last_snap, frames;
    uint32_t now = host_ms(), a;

    if (on < 0) {
        const char *v = getenv("RECOMP_PIRATES_BTL_TEST");
        char fn[512];
        on = v && *v && strcmp(v, "0") != 0;
        if (!on)
            return;
        snprintf(dir, sizeof dir, "%s", v);
        snprintf(fn, sizeof fn, "%s/btl_frames.txt", dir);
        if ((ff = fopen(fn, "w")) != NULL)
            setvbuf(ff, NULL, _IOFBF, 1 << 20);
        snprintf(fn, sizeof fn, "%s/btl_snaps.bin", dir);
        fs = fopen(fn, "wb");
        snprintf(fn, sizeof fn, "%s/btl_frames.bin", dir);
        if ((fb = fopen(fn, "wb")) != NULL)
            setvbuf(fb, NULL, _IOFBF, 4 << 20);
        fprintf(stderr, "[BTL] test harness writing to %s\n", dir);
        fflush(stderr);
    }
    if (!on)
        return;
    {   /* RECOMP_PIRATES_BTL_PIN_ENEMY="x y": the enemy (0x4DAC40) held at
         * x, y (map units, as x / 1000 in the logs) and stopped, every frame:
         * our ship's runs then match from one to the next. */
        static int pin = -1, px, py;
        if (pin < 0) {
            const char *v = getenv("RECOMP_PIRATES_BTL_PIN_ENEMY");
            pin = v && sscanf(v, "%d %d", &px, &py) == 2;
        }
        static int pk = -1, poked;
        static unsigned pk_addr, pk_val, pk_ms, pk_t0;
        if (pk < 0) {   /* RECOMP_PIRATES_BTL_POKE="addr=value@ms": once, ms after the battle began */
            const char *v = getenv("RECOMP_PIRATES_BTL_POKE");
            pk = v && sscanf(v, "%x=%u@%u", &pk_addr, &pk_val, &pk_ms) == 3;
            pk_t0 = now;
        }
        if (pk && !poked && now - pk_t0 >= pk_ms) {
            poked = 1;
            MEM32(pk_addr) = pk_val;
        }
        static int orset = -1;
        static unsigned or_addr, or_mask;
        if (orset < 0) {   /* RECOMP_PIRATES_BTL_OR="addr=mask": bits set every frame */
            const char *v = getenv("RECOMP_PIRATES_BTL_OR");
            orset = v && sscanf(v, "%x=%x", &or_addr, &or_mask) == 2;
        }
        if (orset)
            MEM32(or_addr) |= or_mask;
        static int pnear = -1, nx, ny, nms;
        if (pnear < 0) {   /* RECOMP_PIRATES_BTL_PIN_NEAR="dx dy [ms]": the enemy held at our ship + dx, dy (from ms on) */
            const char *v = getenv("RECOMP_PIRATES_BTL_PIN_NEAR");
            pnear = v && sscanf(v, "%d %d %d", &nx, &ny, &nms) >= 2;
        }
        if (pnear && now - pk_t0 >= (uint32_t)nms) {
            MEM32(0x004DAC40u + 0x11C8u) = MEM32(0x004D7D48u + 0x11C8u) + (uint32_t)(nx * 1000);
            MEM32(0x004DAC40u + 0x11CCu) = MEM32(0x004D7D48u + 0x11CCu) + (uint32_t)(ny * 1000);
            MEM32(0x004DAC40u + 0x11D4u) = 0;
        }
        static int pino = -1, ox, oy;
        if (pino < 0) {
            const char *v = getenv("RECOMP_PIRATES_BTL_PIN_OURS");
            pino = v && sscanf(v, "%d %d", &ox, &oy) == 2;
        }
        if (pino) {       /* RECOMP_PIRATES_BTL_PIN_OURS: the same for our ship */
            MEM32(0x004D7D48u + 0x11C8u) = (uint32_t)(ox * 1000);
            MEM32(0x004D7D48u + 0x11CCu) = (uint32_t)(oy * 1000);
            MEM32(0x004D7D48u + 0x11D4u) = 0;
            MEM32(0x004D7D48u + 0x11D8u) = 0;
        }
        if (pin) {
            MEM32(0x004DAC40u + 0x11C8u) = (uint32_t)(px * 1000);
            MEM32(0x004DAC40u + 0x11CCu) = (uint32_t)(py * 1000);
            MEM32(0x004DAC40u + 0x11D4u) = 0;
            MEM32(0x004DAC40u + 0x11D8u) = 0;
        }
    }
    frames++;
    if (fb) {
        /* btl_frames.bin: a record a frame -- magic, host ms, tick, dt us,
         * then our ship's struct (0x4D7D48), the enemy's (0x4DAC40), the 40
         * shot slots (obj+0x408) and the object's counters (obj+0xCD80). */
        uint32_t hdr[4] = { 0x42544C46u, now, (uint32_t)btl_tick, (uint32_t)btl_dt };
        fwrite(hdr, 4, 4, fb);
        fwrite((const void *)((uintptr_t)0x004D7D48u + g_xbox_mem_offset), 1, 0x177C, fb);
        fwrite((const void *)((uintptr_t)0x004DAC40u + g_xbox_mem_offset), 1, 0x177C, fb);
        fwrite((const void *)((uintptr_t)(BTL_LO + 0x408u) + g_xbox_mem_offset), 1, 40 * 0x44, fb);
        fwrite((const void *)((uintptr_t)(BTL_LO + 0xCD80u) + g_xbox_mem_offset), 1, 0x100, fb);
    }
    if (ff) {
        fprintf(ff, "%u %u", now, MEM32(BTL_LO + 0xCDE0u));
        for (a = 0x004D8F10u; a < 0x004D8F70u; a += 4)
            fprintf(ff, " %d", (int32_t)MEM32(a));
        for (a = 0x004DBE08u; a < 0x004DBE68u; a += 4)
            fprintf(ff, " %d", (int32_t)MEM32(a));
        fputc('\n', ff);
    }
    if (now - last_snap >= 1000u && fs) {
        static int wide = -1;
        uint32_t lo, hi, hdr[4];
        if (wide < 0) wide = getenv("RECOMP_PIRATES_BTL_WIDE") != NULL;
        lo = wide ? BTL_WIDE_LO : BTL_LO;
        hi = wide ? BTL_WIDE_HI : BTL_HI;
        hdr[0] = 0x42544C38u; hdr[1] = now; hdr[2] = frames; hdr[3] = (hi - lo) / 4u;
        last_snap = now;
        fwrite(hdr, 4, 4, fs);
        fwrite((const void *)((uintptr_t)lo + g_xbox_mem_offset), 4, hdr[3], fs);
        fflush(fs);
        if (ff) fflush(ff);
        if (fb) fflush(fb);
    }
}
