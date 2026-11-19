"""Compare rand() calls per second by caller (RECOMP_PIRATES_SEA60_RANDLOG)
between a 30 and a 60 run.  usage: randcmp.py RUN30 RUN60 [min_rate]"""
import os, re, sys
from collections import defaultdict


def load(run):
    log = os.path.join(run, os.path.basename(run.rstrip("/\\")) + ".log")
    windows, cur = [], defaultdict(float)
    for line in open(log, errors="replace"):
        m = re.match(r"\[SEA60\] rand from 0x([0-9A-F]+) ([\d.]+)/s", line)
        if m:
            cur[int(m[1], 16)] = float(m[2])
        elif line.startswith("[SEA60] rand window"):
            windows.append(cur)
            cur = defaultdict(float)
    windows = windows[1:-1] if len(windows) > 2 else windows    # drop partial ones
    avg = defaultdict(float)
    for w in windows:
        for k, v in w.items():
            avg[k] += v / len(windows)
    return avg, len(windows)


a3, n3 = load(sys.argv[1])
a6, n6 = load(sys.argv[2])
lo = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3
print(f"windows: {n3} / {n6}   total rand/s {sum(a3.values()):.1f} / {sum(a6.values()):.1f}")
print("caller      30/s     60/s   ratio")
for k in sorted(set(a3) | set(a6), key=lambda k: -(a3.get(k, 0) + a6.get(k, 0))):
    v3, v6 = a3.get(k, 0), a6.get(k, 0)
    if max(v3, v6) < lo:
        continue
    r = v6 / v3 if v3 else float("inf")
    flag = "  <--" if r >= 1.5 or r <= 0.67 else ""
    print(f"0x{k:08X} {v3:8.2f} {v6:8.2f} {r:7.2f}{flag}")
