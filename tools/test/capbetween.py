"""Every chunk recorded between two draws of a .d3dcap capture, with render
and texture-stage states named.

usage: capbetween.py <capture> <first draw> <last draw>

Prints the chunks after draw <first> up to and including draw <last>."""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "xboxrecomp", "src", "d3d", "d3d8_xbox.h")


def names(prefix):
    out = {}
    text = open(HEADER, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"\b(%s\w+)\s*=\s*(\d+)" % prefix, text):
        out.setdefault(int(m.group(2)), m.group(1))
    return out


def main():
    path, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    rs_n, tss_n = names("D3DRS_"), names("D3DTSS_")
    data = open(path, "rb").read()
    pos = struct.unpack_from("<I", data, 12)[0]
    draw = -1
    while pos + 8 <= len(data):
        typ, n = struct.unpack_from("<II", data, pos)
        body = data[pos + 8: pos + 8 + n]
        pos += 8 + ((n + 3) & ~3)
        if typ in (9, 10):
            draw += 1
        inside = lo <= draw <= hi and not (draw == lo and typ in (9, 10))
        if draw > hi:
            break
        if not inside:
            continue
        if typ == 3:
            s, v = struct.unpack_from("<II", body, 0)
            print(f"  RS  {rs_n.get(s, s)} = 0x{v:08X}")
        elif typ == 4:
            st, t, v = struct.unpack_from("<III", body, 0)
            print(f"  TSS {st} {tss_n.get(t, t)} = 0x{v:08X}")
        elif typ in (9, 10):
            print(f"DRAW {draw}")
        else:
            words = struct.unpack_from("<%dI" % min(4, n // 4), body, 0)
            print(f"  chunk {typ} ({n} bytes) " + " ".join(f"{w:08X}" for w in words))


if __name__ == "__main__":
    main()
