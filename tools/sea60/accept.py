"""sea_60 acceptance: per real second, a 30 fps run against a 60 fps run.
usage: accept.py RUN30 RUN60   (folders written by sea.ps1 -Script turns|long)

Reads frames.txt (watched dwords per sea frame), marks.txt (script phases),
snaps.bin (AI ships, 1 a second) and the run log (sound events). Prints one
table: quantity, 30-run rate, 60-run rate, ratio."""
import datetime, math, os, re, struct, sys

# frames.txt columns: host ms, game tick, 0x4D67D8, then the watch list of
# sea.ps1: 0x4564A0..0x45651C (32), 49C284, 49C28C, 49C298, 4D65E8, 49C288,
# 4C72FC, 4D65B8, 4D6600, 40D4C4, 4D652C
W = {f"{0x4564A0 + 4 * k:06X}": 3 + k for k in range(32)}
for k, a in enumerate(["49C284", "49C28C", "49C298", "4D65E8", "49C288", "4C72FC",
                       "4D65B8", "4D6600", "40D4C4", "4D652C"]):
    W[a] = 35 + k
X, Y, H, SPD = W["4564BC"], W["4564C0"], W["4564C4"], W["4564C8"]


def s32(v):
    return ((v + 2 ** 31) & 0xFFFFFFFF) - 2 ** 31


def load(run):
    rows, pins, n = [], set(), 0
    for line in open(os.path.join(run, "frames.txt")):
        if line.startswith("#"):
            pins.add(n)
            continue
        rows.append([int(v) for v in line.split("|")[0].split()])
        n += 1
    marks = {}
    day = datetime.date.fromtimestamp(os.path.getmtime(os.path.join(run, "marks.txt")))
    for line in open(os.path.join(run, "marks.txt")):
        t, m = line.split()
        dt = datetime.datetime.combine(day, datetime.datetime.strptime(t, "%H:%M:%S.%f").time())
        marks[m] = int(dt.timestamp() * 1000) & 0xFFFFFFFF
    return rows, pins, marks


def window(rows, a, b):
    return [i for i, r in enumerate(rows) if s32(r[0] - a) >= 0 and s32(r[0] - b) < 0]


def rate(rows, pins, idx, col, absd=True, skip_pins=False, angle=False):
    """sum of |delta| (or delta) of a column per real second over frames idx"""
    if len(idx) < 2:
        return float("nan")
    tot = 0
    for i in idx[1:]:
        if skip_pins and i in pins:
            continue
        d = rows[i][col] - rows[i - 1][col]
        if angle:
            d = s32(d)
        tot += abs(d) if absd else d
    secs = s32(rows[idx[-1]][0] - rows[idx[0]][0]) / 1000
    return tot / secs


def speed(rows, pins, idx):
    if len(idx) < 2:
        return float("nan")
    dist = 0.0
    for i in idx[1:]:
        if i in pins:
            continue
        dx = s32(rows[i][X] - rows[i - 1][X]) / 4e6
        dy = s32(rows[i][Y] - rows[i - 1][Y]) / 4e6
        if math.hypot(dx, dy) < 1:          # a teleport the pin did
            dist += math.hypot(dx, dy)
    return dist / (s32(rows[idx[-1]][0] - rows[idx[0]][0]) / 1000)


def ai_speed(run, t0, t1):
    path = os.path.join(run, "snaps.bin")
    if not os.path.exists(path):
        return float("nan")
    b = open(path, "rb").read()
    n = struct.unpack_from("<4I", b, 0)[3]
    rec = 16 + 4 * n
    snaps = [(struct.unpack_from("<I", b, o + 4)[0], o + 16) for o in range(0, len(b) - rec + 1, rec)]
    snaps = [s for s in snaps if s32(s[0] - t0) >= 0 and s32(s[0] - t1) < 0]
    rd = lambda s, va: struct.unpack_from("<i", b, s[1] + va - 0x400400)[0]
    meds = []
    for i in range(1, 128):
        base = 0x4564BC + i * 0x45C
        v = []
        for a, c in zip(snaps, snaps[1:]):
            x0, y0, x1, y1 = rd(a, base), rd(a, base + 4), rd(c, base), rd(c, base + 4)
            if not (x0 or y0) or not (x1 or y1):
                continue
            d = math.hypot(x1 - x0, y1 - y0) / 4e6 / (s32(c[0] - a[0]) / 1000)
            if 0 < d < 6:
                v.append(d)
        if len(v) > 5:
            v.sort()
            meds.append(v[len(v) // 2])
    meds.sort()
    return meds[len(meds) // 2] if meds else float("nan"), len(meds)


def sound(run, tag, t0, t1):
    """DirectSound Play and Stop calls per second (RECOMP_DSOUND_PLAY_LOG);
    their times are GetTickCount64, put on the frames' clock by the
    "[SEA60] clock sync" line."""
    log = os.path.join(run, f"{tag}.log")
    if not os.path.exists(log):
        return float("nan")
    text = open(log, errors="replace").read()
    m = re.search(r"\[SEA60\] clock sync host_ms (\d+) tick64 (\d+)", text)
    if not m:
        return float("nan")
    off = int(m[1]) - int(m[2])
    n = 0
    for t in re.findall(r"\[DSOUND\] (\d+) ms (?:Stop|slot)", text):
        t = (int(t) + off) & 0xFFFFFFFF
        n += 1 if s32(t - t0) >= 0 and s32(t - t1) < 0 else 0
    return n / (s32(t1 - t0) / 1000)


def main():
    r3, r6 = sys.argv[1], sys.argv[2]
    A = [load(r3), load(r6)]
    phases = [("straight", "atsea", "left", 8000), ("turn L", "left", "right", 500),
              ("turn R", "right", "idle", 500), ("idle", "idle", "end", 0)]
    out = []

    def add(name, v3, v6):
        r = v6 / v3 if v3 else float("nan")
        out.append((name, v3, v6, r))

    for name, a, b, skip in phases:
        sel = []
        for rows, pins, marks in A:
            if a in marks and b in marks:
                sel.append(window(rows, marks[a] + skip, marks[b]))
            else:
                sel.append([])
        if not sel[0] or not sel[1]:
            continue
        (rows3, pins3, _), (rows6, pins6, _) = A
        fps = [len(s) / (s32(rw[s[-1]][0] - rw[s[0]][0]) / 1000) for s, (rw, _, _) in zip(sel, A)]
        add(f"{name}: frames/s", *fps)
        add(f"{name}: player speed (units/s)", speed(rows3, pins3, sel[0]), speed(rows6, pins6, sel[1]))
        add(f"{name}: turning (deg/s)", *[rate(rw, pn, s, H, angle=True) * 360 / 2 ** 32
                                          for s, (rw, pn, _) in zip(sel, A)])
        add(f"{name}: speed changes 0x4564C8 (|d|/s)", *[rate(rw, pn, s, SPD) for s, (rw, pn, _) in zip(sel, A)])
        add(f"{name}: calendar 0x49C284 (ticks/s)", *[rate(rw, pn, s, W["49C284"], absd=False) for s, (rw, pn, _) in zip(sel, A)])
        def scale(rw, s):     # the time scale 0x40D4C4 summed per second, /100
            return sum(rw[i][W["40D4C4"]] for i in s) / 100 / (s32(rw[s[-1]][0] - rw[s[0]][0]) / 1000)
        add(f"{name}: time scale sum 0x40D4C4/100 (/s)", *[scale(rw, s) for s, (rw, _, _) in zip(sel, A)])
        add(f"{name}: calendar per unit of time scale", *[rate(rw, pn, s, W["49C284"], absd=False) / scale(rw, s)
                                                          for s, (rw, pn, _) in zip(sel, A)])
        add(f"{name}: frame counter 0x49C288 (/s)", *[rate(rw, pn, s, W["49C288"], absd=False) for s, (rw, pn, _) in zip(sel, A)])
        for t in ("4C72FC", "4D65B8", "4D6600"):
            add(f"{name}: countdown 0x{t} (|d|/s)", *[rate(rw, pn, s, W[t]) for s, (rw, pn, _) in zip(sel, A)])
        add(f"{name}: angle 0x4D652C (deg/s)", *[rate(rw, pn, s, W["4D652C"], angle=True) * 360 / 2 ** 32
                                                 for s, (rw, pn, _) in zip(sel, A)])
    # AI ships and sound over the whole sea stretch after the first 8 s
    m3, m6 = A[0][2], A[1][2]
    e3 = m3.get("end", A[0][0][-1][0]); e6 = m6.get("end", A[1][0][-1][0])
    ai3 = ai_speed(r3, m3["atsea"] + 8000, e3); ai6 = ai_speed(r6, m6["atsea"] + 8000, e6)
    add(f"AI ships median speed (units/s, {ai3[1]}/{ai6[1]} ships)", ai3[0], ai6[0])
    add("sound events (play/stop per s)", sound(r3, os.path.basename(r3), m3["atsea"] + 8000, e3),
        sound(r6, os.path.basename(r6), m6["atsea"] + 8000, e6))
    # Countdown test (RECOMP_PIRATES_SEA_POKE): seconds from the poke until
    # the poked counter is back to 0.
    def countdown(run):
        rows, n, poked, addr = [], 0, None, None
        for line in open(os.path.join(run, "frames.txt")):
            if line.startswith("# poke"):
                addr = line.split()[2].split("=")[0][-6:].upper()
                poked = n
                continue
            if line.startswith("#"):
                continue
            rows.append([int(v) for v in line.split("|")[0].split()])
            n += 1
        if poked is None or addr not in W:
            return float("nan")
        for i in range(poked, len(rows)):
            if rows[i][W[addr]] == 0:
                return s32(rows[i][0] - rows[poked][0]) / 1000
        return float("nan")
    add("countdown 0x4C72FC from 150 to 0 (s)", countdown(r3), countdown(r6))
    print(f"{'quantity':52s} {'30':>12s} {'60':>12s} {'ratio':>7s}")
    for name, v3, v6, r in out:
        flag = "" if math.isnan(r) or abs(r - 1) <= 0.03 else "  <--"
        print(f"{name:52s} {v3:12.4g} {v6:12.4g} {r:7.3f}{flag}")


main()
