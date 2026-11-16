"""Render and texture-stage state in force at one draw of a .d3dcap capture.

usage: capstate.py <capture> <draw index> [name filter]

Names come from the D3DRS_ enum in the toolkit's src/d3d/d3d8_xbox.h, which
is the numbering the capture records."""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "xboxrecomp", "src", "d3d", "d3d8_xbox.h")


def rs_names():
    names = {}
    text = open(HEADER, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"\b(D3DRS_\w+)\s*=\s*(\d+)", text):
        names.setdefault(int(m.group(2)), m.group(1))
    return names


def main():
    path, want = sys.argv[1], int(sys.argv[2])
    filt = sys.argv[3] if len(sys.argv) > 3 else ""
    names = rs_names()
    data = open(path, "rb").read()
    pos = struct.unpack_from("<I", data, 12)[0]
    rs, tss, draw = {}, {}, -1
    while pos + 8 <= len(data):
        typ, n = struct.unpack_from("<II", data, pos)
        body = data[pos + 8: pos + 8 + n]
        pos += 8 + ((n + 3) & ~3)
        if typ == 3:
            s, v = struct.unpack_from("<II", body, 0)
            rs[s] = v
        elif typ == 4:
            st, t, v = struct.unpack_from("<III", body, 0)
            tss[(st, t)] = v
        elif typ in (9, 10):
            draw += 1
            if draw == want:
                break
    for s in sorted(rs):
        name = names.get(s, f"RS_{s}")
        if filt.lower() in name.lower():
            print(f"{s:4d} {name:34s} 0x{rs[s]:08X}")
    if not filt:
        for (st, t) in sorted(tss):
            print(f"stage {st} type {t:2d} = 0x{tss[(st, t)]:08X}")


if __name__ == "__main__":
    main()
