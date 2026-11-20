"""Save at one rate, load at the other: the sea state when the game was saved
(the last sea frame before the menu opened, the first stop after -SailFirst)
against the first sea frame after loading that save.
usage: savecheck.py SAVE_RUN LOAD_RUN"""
import os, sys

COLS = {"x": 3 + 7, "y": 3 + 8, "heading": 3 + 9, "speed": 3 + 10, "calendar": 3 + 32,
        "time scale": 3 + 40}


def rows(run):
    out = []
    for line in open(os.path.join(run, "frames.txt")):
        if not line.startswith("#"):
            out.append([int(v) for v in line.split("|")[0].split()])
    return out


s, l = rows(sys.argv[1]), rows(sys.argv[2])
# the save: the frame before the longest stop (the pause menu, the save)
saved, longest = None, 0
for a, b in zip(s, s[1:]):
    g = (b[0] - a[0]) & 0xFFFFFFFF
    if g > longest:
        saved, longest = a, g
loaded = l[0]
if saved is None:
    sys.exit("no save stop found")
print(f"{'':12s} {'saved':>14s} {'loaded':>14s}")
for k, c in COLS.items():
    print(f"{k:12s} {saved[c]:14d} {loaded[c]:14d}")
