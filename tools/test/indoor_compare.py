"""Compare indoor screenshots with xemu references: the tavern and the tables
behind the new-career nation choice (indoor-lighting.ps1 takes them).

usage: indoor_compare.py <reference dir> <tavern.png> <nation.png>

The references are xemu's pictures of the same two screens (tavern.png,
nation.png in <reference dir>). They are pictures of the game, so they are
kept beside the repository, never in it.

Characters stand in different places from run to run, so the frames are not
compared pixel for pixel. Each check is a region of the room that the
characters leave alone: its mean brightness against xemu's, and the
correlation of a coarse (24x12) brightness map of it with xemu's, which is
what catches a lightmap read with the wrong coordinates (a mosaic, or a
striped floor) even when the overall brightness is close. Prints one line
per region and exits 1 when any is out of bounds.

Measured on 21-22 Nov 2026 (correlation): a correct build 0.96-1.00 on the
tavern's walls, hearth and floor, 0.86 on the nation tables; patch 0036's
rule 0.23 on the floor and 0.42 on the hearth; 0049's 0.23 on the floor;
coordinates taken literally everywhere 0.29 on the hearth; the nation
tables as a mosaic 0.71."""
import os
import sys

from PIL import Image, ImageStat

W, H = 640, 480

# name: (box in a 640x480 frame, minimum correlation, brightness bounds)
TAVERN = {
    "walls":   ((0, 60, 200, 200),  0.90, (0.85, 1.20)),
    "hearth":  ((20, 60, 170, 170), 0.90, (0.85, 1.20)),
    "floor":   ((150, 280, 450, 360), 0.70, (0.85, 1.25)),
    "ceiling": ((200, 0, 640, 60),  0.70, (0.85, 1.25)),
}
NATION = {
    "tables":  ((0, 300, 640, 420), 0.80, (0.90, 1.15)),
    "back":    ((0, 0, 640, 120),   0.85, (0.90, 1.15)),
}


def load(path):
    return Image.open(path).convert("L").resize((W, H))


def coarse(im, box):
    small = im.crop(box).resize((24, 12))
    return [small.getpixel((x, y)) for y in range(12) for x in range(24)]


def corr(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    sa = sum((x - ma) ** 2 for x in a) ** 0.5
    sb = sum((y - mb) ** 2 for y in b) ** 0.5
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb + 1e-9)


def check(name, ref, shot, regions):
    ok = True
    for region, (box, min_corr, (lo, hi)) in regions.items():
        rm = ImageStat.Stat(ref.crop(box)).mean[0]
        sm = ImageStat.Stat(shot.crop(box)).mean[0]
        ratio = sm / rm if rm else 0.0
        c = corr(coarse(shot, box), coarse(ref, box))
        good = c >= min_corr and lo <= ratio <= hi
        ok = ok and good
        print(f"{'ok  ' if good else 'FAIL'} {name:7s} {region:8s} brightness {ratio * 100:5.1f}% of xemu "
              f"({lo * 100:.0f}-{hi * 100:.0f}), pattern {c:.2f} (>= {min_corr:.2f})")
    return ok


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    refdir, tavern, nation = sys.argv[1:]
    for p in (os.path.join(refdir, "tavern.png"), os.path.join(refdir, "nation.png"), tavern, nation):
        if not os.path.exists(p):
            print(f"FAIL missing {p}")
            return 1
    ok = check("tavern", load(os.path.join(refdir, "tavern.png")), load(tavern), TAVERN)
    ok = check("nation", load(os.path.join(refdir, "nation.png")), load(nation), NATION) and ok
    print("PASS: indoor lighting matches xemu" if ok else "FAIL: indoor lighting differs from xemu")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
