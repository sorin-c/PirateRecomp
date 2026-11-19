"""Compare per-dword change rates of .data between two sea runs (no numpy).
usage: stats.py RUN30 RUN60 [t0_s t1_s [mode]]  mode: 2 (ratio 1.6-2.6), x (not ~1), all
Window in seconds since logging began (stats files every 10 s)."""
import struct, sys, os, glob
from array import array

LO = 0x00400400

def load(run):
    out = []
    for fn in sorted(glob.glob(os.path.join(run, "stats_*.bin"))):
        b = open(fn, "rb").read()
        magic, ms, frames, n = struct.unpack_from("<4I", b, 0)
        o = 16
        chg = array("I"); chg.frombytes(b[o:o + 4 * n]); o += 4 * n
        ia = array("Q"); ia.frombytes(b[o:o + 8 * n]); o += 8 * n
        fa = array("d"); fa.frombytes(b[o:o + 8 * n])
        out.append((ms / 1000.0, frames, chg, ia, fa))
    return out

def pick(st, t):
    return min(st, key=lambda s: abs(s[0] - t))

r30, r60 = load(sys.argv[1]), load(sys.argv[2])
t0 = float(sys.argv[3]) if len(sys.argv) > 3 else 40
t1 = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
mode = sys.argv[5] if len(sys.argv) > 5 else "2"
a3, b3 = pick(r30, t0), pick(r30, t1); a6, b6 = pick(r60, t0), pick(r60, t1)
d3 = b3[0] - a3[0]; d6 = b6[0] - a6[0]
print(f"30-run {a3[0]:.0f}-{b3[0]:.0f}s fps {(b3[1]-a3[1])/d3:.2f}; 60-run {a6[0]:.0f}-{b6[0]:.0f}s fps {(b6[1]-a6[1])/d6:.2f}")
n = len(a3[2])
buckets = {}
rows = []
for i in range(n):
    c3 = (b3[2][i] - a3[2][i]) / d3
    if c3 < 0.3: continue
    c6 = (b6[2][i] - a6[2][i]) / d6
    i3 = (b3[3][i] - a3[3][i]) / d3; i6 = (b6[3][i] - a6[3][i]) / d6
    f3 = (b3[4][i] - a3[4][i]) / d3; f6 = (b6[4][i] - a6[4][i]) / d6
    isf = 0 < f3 < 1e7 and i3 > 1e5
    m3, m6 = (f3, f6) if isf else (i3, i6)
    if m3 <= 0: continue
    r = m6 / m3
    k = "~0.5" if 0.35 <= r < 0.7 else "~1" if 0.7 <= r <= 1.3 else "1.3-1.6" if r < 1.6 else "~2" if r <= 2.6 else ">2.6" if r > 2.6 else "<0.35"
    buckets[k] = buckets.get(k, 0) + 1
    rows.append((LO + 4 * i, r, c3, c6, m3, m6, "f" if isf else "i"))
print("dwords changing >=0.3/s in 30-run, by |delta|/s ratio:", dict(sorted(buckets.items())))
for a, r, c3, c6, m3, m6, t in rows:
    if mode == "all" or (mode == "2" and 1.6 <= r <= 2.6) or (mode == "x" and not 0.7 <= r <= 1.3):
        print(f"{a:08X} {t} ratio {r:6.2f}  chg/s {c3:5.1f}->{c6:5.1f}  |d|/s {m3:.4g} -> {m6:.4g}")
