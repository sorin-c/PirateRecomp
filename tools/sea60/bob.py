"""Hull bob from in-game frame bursts (sea.ps1 -Script burst): the bright
(sail) pixels' mean row near the ship, per frame, and the period of its
swing, found as the lag with the best fit of a sine.  usage: bob.py RUN..."""
import datetime, math, os, sys
from PIL import Image


def series(run):
    ts = {}
    for line in open(os.path.join(run, "burst.txt")):
        k, i, t = line.split()
        ts[(int(k), int(i))] = datetime.datetime.strptime(t, "%H:%M:%S.%f")
    out = {}
    for (k, i), t in sorted(ts.items()):
        p = os.path.join(run, f"b{k}-{i:02d}.bmp")
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("L").crop((170, 110, 340, 240))
        px = im.load(); w, h = im.size
        pts = [y for x in range(w) for y in range(h) if px[x, y] > 200]
        if pts:
            out.setdefault(k, []).append(((t - ts[(k, 1)]).total_seconds(), sum(pts) / len(pts)))
    return out


def period(sr):
    """best-fitting sine period (s) by least squares over 0.4..4 s"""
    best = None
    for p10 in range(4, 41):
        p = p10 / 10
        c = [math.cos(2 * math.pi * t / p) for t, _ in sr]
        s = [math.sin(2 * math.pi * t / p) for t, _ in sr]
        y = [v for _, v in sr]
        n = len(y); my = sum(y) / n
        # project onto cos/sin (approximate, unweighted)
        a = sum((yy - my) * cc for yy, cc in zip(y, c)) * 2 / n
        b = sum((yy - my) * ss for yy, ss in zip(y, s)) * 2 / n
        res = sum((yy - my - a * cc - b * ss) ** 2 for yy, cc, ss in zip(y, c, s))
        if best is None or res < best[0]:
            best = (res, p, math.hypot(a, b))
    return best[1], best[2]


for run in sys.argv[1:]:
    for k, sr in series(run).items():
        p, amp = period(sr)
        print(f"{os.path.basename(run)} burst {k}: {len(sr)} frames over {sr[-1][0]:.2f} s, "
              f"bob period {p:.1f} s, amplitude {amp:.2f} px")
