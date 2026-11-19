"""Join two wwatch.txt (30/60) by watched address + guest block; per-frame and per-second rates.
usage: wmap.py RUN30 RUN60"""
import re, sys, os
GEN = r"K:\Code\pirategame\sea60\src\recomp\gen"
cache = {}
def label(f, line):
    if f not in cache:
        p = os.path.join(GEN, f)
        cache[f] = open(p, encoding="utf-8", errors="replace").read().split("\n") if os.path.exists(p) else []
    L = cache[f]
    for i in range(min(line, len(L)) - 1, -1, -1):
        m = re.match(r"(loc_[0-9A-F]{8}):", L[i])
        if m: return m[1]
    return "?"
def load(run):
    t = open(os.path.join(run, "wwatch.txt")).read().split("\n")
    fr, ms = map(int, re.findall(r"\d+", t[0]))
    d = {}
    for l in t[1:]:
        m = re.match(r"([0-9A-F]{8}) (\S+)\+0x[0-9A-F]+ (\S+):(\d+) changes (\d+) .*sum (-?\d+)", l)
        if not m: continue
        key = (m[1], m[2].split("+")[0], label(m[3], int(m[4])))
        n, s = d.get(key, (0, 0)); d[key] = (n + int(m[5]), s + int(m[6]))
    return fr, ms / 1000, d
f3, s3, d3 = load(sys.argv[1]); f6, s6, d6 = load(sys.argv[2])
print(f"30-run {f3} frames/{s3:.0f}s = {f3/s3:.1f}/s; 60-run {f6} frames/{s6:.0f}s = {f6/s6:.1f}/s")
print("addr     function      block           chg/frame 30|60   mean delta/change 30|60   chg/s ratio  |sum|/s ratio")
for k in sorted(set(d3) | set(d6)):
    n3, x3 = d3.get(k, (0, 0)); n6, x6 = d6.get(k, (0, 0))
    if n3 + n6 < 20: continue
    md3 = x3 / n3 if n3 else 0; md6 = x6 / n6 if n6 else 0
    cr = (n6 / s6) / (n3 / s3) if n3 else float("inf")
    sr = (abs(x6) / s6) / (abs(x3) / s3) if x3 else float("nan")
    print(f"{k[0]} {k[1]:13s} {k[2]:14s} {n3/f3:6.2f} | {n6/f6:6.2f}   {md3:12.4g} | {md6:12.4g}   {cr:5.2f}   {sr:6.2f}")
