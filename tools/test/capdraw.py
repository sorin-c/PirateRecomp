"""Inspect one draw of a .d3dcap capture: the vertex-program constants in force,
the vertex declaration, and the draw's vertices decoded by that declaration.

usage: capdraw.py <capture> <draw index> [--consts a-b] [--verts n] [--json out]

Draws are counted as d3d8_replay --list-draws counts them (every DRAW_UP and
DRAW_INDEXED_UP chunk, from 0). Prints the constants in the given register
range and the first n vertices; --json writes everything for a script that
evaluates the program by hand."""
import json, struct, sys

DRAW_UP, DRAW_INDEXED_UP = 9, 10
SET_VS, VS_DECL, VS_CONST = 8, 16, 17

# DXGI formats a vertex declaration uses here: (struct code, count, scale).
DXGI = {
    2: ("f", 4, None), 6: ("f", 3, None), 16: ("f", 2, None), 41: ("f", 1, None),
    28: ("B", 4, 255.0),        # R8G8B8A8_UNORM
    87: ("B", 4, 255.0),        # B8G8R8A8_UNORM (swizzled below)
    13: ("h", 4, 32767.0),      # R16G16B16A16_SNORM
    37: ("h", 2, 32767.0),      # R16G16_SNORM
    31: ("b", 4, 127.0),        # R8G8B8A8_SNORM
    14: ("h", 4, None),         # R16G16B16A16_SINT (as is)
    38: ("h", 2, None),         # R16G16_SINT
}


def main():
    path, want = sys.argv[1], int(sys.argv[2])
    crange = (96, 120)
    nverts = 8
    out_json = None
    a = sys.argv[3:]
    for i, x in enumerate(a):
        if x == "--consts":
            lo, hi = a[i + 1].split("-"); crange = (int(lo), int(hi))
        if x == "--verts":
            nverts = int(a[i + 1])
        if x == "--json":
            out_json = a[i + 1]
    data = open(path, "rb").read()
    hdr_bytes = struct.unpack_from("<I", data, 12)[0]
    pos = hdr_bytes
    consts = [[0.0] * 4 for _ in range(192)]
    decls = {}
    cur_vs = None
    draw = -1
    while pos + 8 <= len(data):
        typ, n = struct.unpack_from("<II", data, pos)
        body = data[pos + 8: pos + 8 + n]
        pos += 8 + ((n + 3) & ~3)          # payloads are padded to 4 bytes
        if typ == VS_CONST:
            first, count = struct.unpack_from("<II", body, 0)
            for k in range(count):
                consts[first + k] = list(struct.unpack_from("<4f", body, 8 + 16 * k))
        elif typ == VS_DECL:
            handle, count = struct.unpack_from("<II", body, 0)
            decls[handle] = [struct.unpack_from("<iII", body, 8 + 12 * k) for k in range(count)]
        elif typ == SET_VS:
            cur_vs = struct.unpack_from("<I", body, 0)[0]
        elif typ in (DRAW_UP, DRAW_INDEXED_UP):
            draw += 1
            if draw != want:
                continue
            if typ == DRAW_INDEXED_UP:
                prim, mn, nv, pc, ifmt, ib, stride, vb = struct.unpack_from("<8I", body, 0)
                idx = body[32:32 + ib]
                verts = body[32 + ib:32 + ib + vb]
                isz = 2 if ifmt == 101 else 4
                indices = [struct.unpack_from("<H" if isz == 2 else "<I", idx, k * isz)[0]
                           for k in range(ib // isz)]
            else:
                prim, pc, stride, vb = struct.unpack_from("<4I", body, 0)
                verts = body[16:16 + vb]
                indices = None
            decl = decls.get(cur_vs, [])
            print(f"draw {want}: prim {prim} count {pc} stride {stride} vertices {len(verts)//stride} vs 0x{cur_vs:X}")
            print("declaration:", [(r, f, o) for r, f, o in decl])
            vlist = []
            for vi in range(len(verts) // stride):
                v = {}
                for reg, fmt, off in decl:
                    code, cnt, scale = DXGI.get(fmt, ("f", 4, None))
                    vals = list(struct.unpack_from("<%d%s" % (cnt, code), verts, vi * stride + off))
                    if scale:
                        vals = [x / scale for x in vals]
                    if fmt == 87:
                        vals = [vals[2], vals[1], vals[0], vals[3]]
                    while len(vals) < 4:
                        vals.append(1.0 if len(vals) == 3 else 0.0)
                    v[reg] = vals
                vlist.append(v)
            for vi in range(min(nverts, len(vlist))):
                print(f"v[{vi}]", {k: [round(x, 4) for x in val] for k, val in vlist[vi].items()})
            for r in range(crange[0], crange[1] + 1):
                print(f"c{r} = {[round(x, 6) for x in consts[r]]}")
            if out_json:
                json.dump({"consts": consts, "verts": vlist, "indices": indices}, open(out_json, "w"))
            return
    print("draw not found")


if __name__ == "__main__":
    main()
