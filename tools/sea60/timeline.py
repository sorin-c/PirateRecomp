"""Per 5 s of a run: sea frames a second, calendar ticks a second, time
acceleration a second (/100), and the stops (sea loop not running > 1 s,
e.g. a town or a menu) with the calendar before and after.
usage: timeline.py RUN"""
import os, sys

CAL, TS = 3 + 32, 3 + 40
rows, marks = [], []
for line in open(os.path.join(sys.argv[1], "frames.txt")):
    if line.startswith("# flip"):
        marks.append((len(rows), line.strip()))
    if line.startswith("#"):
        continue
    rows.append([int(v) for v in line.split("|")[0].split()])
t0 = rows[0][0]
rel = lambda r: ((r[0] - t0) & 0xFFFFFFFF) / 1000
bins = {}
for a, b in zip(rows, rows[1:]):
    d = ((b[0] - a[0]) & 0xFFFFFFFF) / 1000
    if d > 1:
        print(f"stop at {rel(a):6.1f} s for {d:5.1f} s: calendar {a[CAL]} -> {b[CAL]}")
        continue
    k = int(rel(b) // 5)
    f, c, s, t = bins.get(k, (0, 0, 0, 0.0))
    bins[k] = (f + 1, c + b[CAL] - a[CAL], s + b[TS], t + d)
for i, m in marks:
    print(f"{m} at {rel(rows[min(i, len(rows) - 1)]):.1f} s")
print("  t(s)  frames/s  calendar/s  time scale/s /100")
for k in sorted(bins):
    f, c, s, t = bins[k]
    if t > 1:
        print(f"{k * 5:6d} {f / t:9.1f} {c / t:11.1f} {s / t / 100:12.1f}")
