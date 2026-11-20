"""Calendar against sailing time over a long run (sea.ps1 -Script drift).
usage: drift.py RUN30 RUN60
Sailing time sums frame-to-frame times under 250 ms (a dialog or a menu that
stops the sea is left out). Prints the calendar (0x49C284) after each minute
of sailing in both runs, and the stops (dialogs) with the sailing time and the
calendar when each began."""
import os, sys

CAL = 3 + 32          # frames.txt column of 0x49C284 (see accept.py)


def load(run):
    rows = []
    for line in open(os.path.join(run, "frames.txt")):
        if line.startswith("#"):
            continue
        v = line.split("|")[0].split()
        rows.append((int(v[0]), int(v[CAL])))
    sail, out, stops, prev = 0.0, [], [], None
    for t, cal in rows:
        if prev is not None:
            d = ((t - prev[0]) & 0xFFFFFFFF) / 1000
            if d < 0.25:
                sail += d
            elif d > 1.0:
                stops.append((sail, prev[1], d))
        out.append((sail, cal))
        prev = (t, cal)
    return out, stops


def at(series, s):
    for t, c in series:
        if t >= s:
            return c
    return None


a, sa = load(sys.argv[1])
b, sb = load(sys.argv[2])
start3, start6 = a[0][1], b[0][1]
print(f"sailing time: {a[-1][0]:.0f} s / {b[-1][0]:.0f} s")
print("minute  calendar advanced 30 / 60   ratio")
for m in range(1, 12):
    c3, c6 = at(a, 60 * m), at(b, 60 * m)
    if c3 is None or c6 is None:
        break
    d3, d6 = c3 - start3, c6 - start6
    print(f"{m:5d}   {d3:9d} {d6:9d}   {d6 / d3 if d3 else float('nan'):.4f}")
print("stops (sailing s, calendar advanced, stop length s):")
print("  30:", ", ".join(f"{s:.0f}s/{c - start3}/{d:.0f}" for s, c, d in sa[:12]))
print("  60:", ", ".join(f"{s:.0f}s/{c - start6}/{d:.0f}" for s, c, d in sb[:12]))
