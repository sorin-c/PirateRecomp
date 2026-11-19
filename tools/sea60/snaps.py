"""Per-second analysis of snaps.bin for one run: player/ship speeds, calendar, rare changers.
usage: snaps.py RUN t0 t1 [rare]"""
import struct, sys, os, math
LO = 0x400400
run = sys.argv[1]; t0 = float(sys.argv[2]); t1 = float(sys.argv[3])
b = open(os.path.join(run, "snaps.bin"), "rb").read()
n = struct.unpack_from("<4I", b, 0)[3]; rec = 16 + 4 * n
snaps = []
for o in range(0, len(b) - rec + 1, rec):
    _, ms, fr, _ = struct.unpack_from("<4I", b, o)
    snaps.append((ms, fr, o + 16))
T0 = snaps[0][0]
snaps = [s for s in snaps if t0 <= (s[0] - T0) / 1000 <= t1]
def rd(s, va): return struct.unpack_from("<i", b, s[2] + va - LO)[0]
def rdu(s, va): return struct.unpack_from("<I", b, s[2] + va - LO)[0]
dt = (snaps[-1][0] - snaps[0][0]) / 1000
print(f"{run}: {len(snaps)} snaps over {dt:.1f}s, fps {(snaps[-1][1]-snaps[0][1])/dt:.2f}")
# player speed: per-second displacement, skipping seconds with a pin teleport (jump > 3 units)
sp = []
for a, c in zip(snaps, snaps[1:]):
    dx = (rd(c, 0x4564BC) - rd(a, 0x4564BC)) / 4e6; dy = (rd(c, 0x4564C0) - rd(a, 0x4564C0)) / 4e6
    d = math.hypot(dx, dy) / ((c[0] - a[0]) / 1000)
    if d < 4: sp.append(d)
sp.sort()
print(f"player speed units/s median {sp[len(sp)//2]:.3f} (n={len(sp)})  heading {rdu(snaps[0],0x4564C4)*360/2**32:.1f} -> {rdu(snaps[-1],0x4564C4)*360/2**32:.1f}")
# AI ships
spd = []
for i in range(1, 128):
    base = 0x4564BC + i * 0x45C
    v = []
    for a, c in zip(snaps, snaps[1:]):
        x0, y0, x1, y1 = rd(a, base), rd(a, base + 4), rd(c, base), rd(c, base + 4)
        if not (x0 or y0) or not (x1 or y1): continue
        d = math.hypot(x1 - x0, y1 - y0) / 4e6 / ((c[0] - a[0]) / 1000)
        if 0 < d < 6: v.append(d)
    if len(v) > 10: v.sort(); spd.append((i, v[len(v)//2]))
print("AI ships moving (index, median units/s):", " ".join(f"{i}:{v:.2f}" for i, v in spd))
if spd: s = sorted(v for _, v in spd); print(f"AI median of medians {s[len(s)//2]:.3f}")
cal = (rdu(snaps[-1], 0x49C284) - rdu(snaps[0], 0x49C284)) / dt
print(f"calendar 0x49C284 {rdu(snaps[0],0x49C284)} -> {rdu(snaps[-1],0x49C284)}: {cal:.2f}/s")
if len(sys.argv) > 4:
    # rare changers: dwords that change in 2..25 of the per-second steps
    out = []
    for k in range(n):
        o = 4 * k
        vals = [struct.unpack_from("<I", b, s[2] + o)[0] for s in snaps]
        ch = sum(1 for p, q in zip(vals, vals[1:]) if p != q)
        if 2 <= ch <= 25: out.append((LO + o, ch, vals[0], vals[-1]))
    for a, ch, v0, v1 in out:
        if 0x4564B0 <= a < 0x49C0B0: continue
        print(f"rare {a:08X} changes {ch:3d}  {v0} -> {v1}")
