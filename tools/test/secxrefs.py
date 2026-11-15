"""Count call/jump xrefs landing in given VA ranges.  usage: secxrefs.py xrefs.json name:lo:hi ..."""
import json, sys
xr = json.load(open(sys.argv[1]))
items = xr if isinstance(xr, list) else xr.get("xrefs", xr)
if isinstance(items, dict):
    flat = []
    for k, v in items.items():
        if isinstance(v, list):
            flat.extend(v)
    items = flat
ranges = []
for a in sys.argv[2:]:
    n, lo, hi = a.split(":")
    ranges.append((n, int(lo, 16), int(hi, 16)))
def num(x):
    return int(x, 16) if isinstance(x, str) else int(x)
print("sample:", items[0] if items else None)
for n, lo, hi in ranges:
    hits = []
    for x in items:
        t = str(x.get("type", ""))
        if "call" not in t and "jump" not in t and "jmp" not in t:
            continue
        to, fr = num(x.get("to", 0)), num(x.get("from", 0))
        if lo <= to < hi and not (lo <= fr < hi):
            hits.append((fr, to, t))
    print(n, len(hits), [f"{f:08X}->{t:08X} {ty}" for f, t, ty in hits[:8]])
