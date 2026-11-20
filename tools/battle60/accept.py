"""Compare ship-battle runs (tools/battle60/battle.ps1): per run, from
btl_frames.bin, our ship's mean speed and turn rate (+0x11D4, +0x11D8), its
path and turning per real second sampled every 0.5 s, the enemy's mean
speed, and the cannonballs' flights (count, seconds, distance, speed).

usage: accept.py <run folder> ... [--from ms] [--to ms]

A run at 60, 90, 120 or unlocked should match the 30 fps run (taken with
RECOMP_FRAME_INTERP=2) within a few per cent for our ship when the enemy is
pinned (RECOMP_PIRATES_BTL_PIN_ENEMY), and the balls' seconds should equal
their frames / 30 at every rate. The enemy's AI differs from run to run as
much at 30 as between rates: compare it over several runs."""
import math, struct, sys

REC = 16 + 0x177C * 2 + 40 * 0x44 + 0x100      # sea60_test.c, btl_test_frame
SHIP = (0, 0x177C)                               # ours, the enemy


def run(path, t0, t1):
    b = open(path + "/btl_frames.bin", "rb").read()
    n = len(b) // REC
    T0 = None; sv = sr = ev = cnt = 0; path_len = turn = 0.0; last = None; nxt = t0
    live = {}; done = []; prevcd = {}
    for r in range(n):
        o = r * REC
        ms = struct.unpack_from("<I", b, o + 4)[0]
        if T0 is None:
            T0 = ms
        t = ms - T0
        for s in (0, 1):                          # cannonballs: 48 a ship at +0x50
            for k in range(48):
                base = o + 16 + SHIP[s] + 0x50 + k * 0x5C
                x, y = struct.unpack_from("<2i", b, base + 0x18)
                tot, cd = struct.unpack_from("<2i", b, base + 0x34)
                key = (s, k); pc = prevcd.get(key, 0); prevcd[key] = cd
                if key not in live and x and cd > 1 and tot > 0 and cd > pc:
                    live[key] = (ms, x, y, tot)
                elif key in live and (cd <= 1 or not x):
                    m0, x0, y0, tt = live.pop(key)
                    done.append(((ms - m0) / 1000, math.hypot(x - x0, y - y0), tt))
        if not t0 <= t <= t1:
            continue
        x, y, h, v, rt = struct.unpack_from("<5i", b, o + 16 + 0x11C8)
        sv += v; sr += abs(rt) >> 16; cnt += 1
        ev += struct.unpack_from("<i", b, o + 16 + SHIP[1] + 0x11D4)[0]
        if t >= nxt:
            if last:
                path_len += math.hypot(x - last[0], y - last[1])
                turn += abs(((h - last[2] + 2**31) % 2**32) - 2**31)
            last = (x, y, h); nxt += 500
    dt = (t1 - t0) / 1000
    line = (f"{path:30s} ours speed {sv / cnt:6.0f} rate {sr / cnt:6.0f} "
            f"path/s {path_len / dt:8.0f} turn/s {turn / dt / 2**32 * 360:6.1f}  enemy speed {ev / cnt:6.0f}")
    if done:
        sec = sum(d[0] for d in done) / len(done); dist = sum(d[1] for d in done) / len(done)
        frames = sum(d[2] for d in done) / len(done)
        line += (f"  balls {len(done)}: {sec:.3f} s ({frames / 30:.3f} at 30), "
                 f"{dist / sec:7.0f} a second")
    print(line)


args = sys.argv[1:]; t0, t1 = 3000, 40000
if "--from" in args:
    i = args.index("--from"); t0 = int(args[i + 1]); del args[i:i + 2]
if "--to" in args:
    i = args.index("--to"); t1 = int(args[i + 1]); del args[i:i + 2]
for a in args:
    run(a.rstrip("/\\"), t0, t1)
