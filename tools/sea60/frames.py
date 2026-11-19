"""Per-phase rates of the watched dwords in two runs' frames.txt.
usage: frames.py RUN30 RUN60"""
import sys, os, time, datetime

def load(run):
    rows = [list(map(int, l.split())) for l in open(os.path.join(run, "frames.txt")) if not l.startswith("#")]
    pins = []
    n = 0
    for l in open(os.path.join(run, "frames.txt")):
        if l.startswith("#"): pins.append(n)
        else: n += 1
    marks = {}
    day = datetime.date.fromtimestamp(os.path.getmtime(os.path.join(run, "marks.txt")))
    for l in open(os.path.join(run, "marks.txt")):
        t, m = l.split()
        dt = datetime.datetime.combine(day, datetime.datetime.strptime(t, "%H:%M:%S.%f").time())
        marks[m] = int(dt.timestamp() * 1000) & 0xFFFFFFFF
    return rows, set(pins), marks

def rel(t, t0):
    return ((t - t0 + 2**31) & 0xFFFFFFFF) - 2**31

def phase_rates(rows, pins, a, b):
    """per real second: frames, |delta| sum per watch (skipping pin frames for x,y)"""
    sel = [i for i, r in enumerate(rows) if rel(r[0], a) >= 0 and rel(r[0], b) < 0]
    if len(sel) < 2: return None
    i0, i1 = sel[0], sel[-1]
    dt = rel(rows[i1][0], rows[i0][0]) / 1000.0
    nw = len(rows[0]) - 3
    tot = [0.0] * nw; chg = [0] * nw
    for i in range(i0 + 1, i1 + 1):
        if i in pins: continue          # teleport frame
        for k in range(nw):
            d = rows[i][3 + k] - rows[i - 1][3 + k]
            if k in (32,):              # clock: plain
                pass
            if d: chg[k] += 1
            tot[k] += abs(d)
    return dt, (i1 - i0) / dt, [t / dt for t in tot], [c / dt for c in chg]

names = {k: f"{0x4564A0 + 4*k:08X}" for k in range(32)}
names.update({32: "0049C284", 33: "0049C28C", 34: "0049C298", 35: "004D65E8"})
r3, p3, m3 = load(sys.argv[1]); r6, p6, m6 = load(sys.argv[2])
phases = [("straight", "atsea", "left", 5000), ("turn L", "left", "right", 0), ("turn R", "right", "idle", 0), ("idle", "idle", "end", 0)]
for name, a, b, skip in phases:
    x3 = phase_rates(r3, p3, m3[a] + skip, m3[b]); x6 = phase_rates(r6, p6, m6[a] + skip, m6[b])
    if not x3 or not x6: print(name, "no data"); continue
    print(f"== {name}: {x3[0]:.1f}s/{x6[0]:.1f}s fps {x3[1]:.1f} vs {x6[1]:.1f}")
    for k in range(len(x3[2])):
        if x3[2][k] or x6[2][k]:
            r = x6[2][k] / x3[2][k] if x3[2][k] else float('inf')
            print(f"   {names.get(k,k)}  |d|/s {x3[2][k]:14.1f} -> {x6[2][k]:14.1f}  ratio {r:5.2f}   chg/s {x3[3][k]:5.1f} -> {x6[3][k]:5.1f}")
