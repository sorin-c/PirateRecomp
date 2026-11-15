"""RMS per 5-second window of raw 16-bit PCM captures.  usage: pcmstat.py file..."""
import array, math, re, sys
for path in sys.argv[1:]:
    m = re.search(r"_(\d+)_(\d)ch_16", path)
    rate, ch = int(m.group(1)), int(m.group(2))
    a = array.array("h"); a.frombytes(open(path, "rb").read())
    win = rate * ch * 5
    rows = []
    for i in range(0, len(a), win):
        seg = a[i:i + win]
        if not seg:
            break
        rms = math.sqrt(sum(x * x for x in seg) / len(seg))
        peak = max(abs(x) for x in seg)
        rows.append(f"{i // (rate * ch)}s:{rms:.0f}/{peak}")
    print(path.rsplit("\\", 1)[-1], f"{len(a) / (rate * ch):.1f}s", " ".join(rows))
