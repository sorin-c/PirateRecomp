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
 *   RECOMP_PIRATES_SEA60_FLIP="ms,ms,..."         flip sea_60 at those
 *       times, as the F2 menu does (recomp_config_set)
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
        nflip = parse_list(getenv("RECOMP_PIRATES_SEA60_FLIP"), "%u", 1, flip, 8);
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
        const char *v = recomp_config_lookup(NULL, "sea_60");
        int was = v && *v && strcmp(v, "0") != 0;
        recomp_config_set("sea_60", was ? "0" : "1");
        if (ff) fprintf(ff, "# flip sea_60=%d\n", !was);
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
