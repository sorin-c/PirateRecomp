"""Disassemble guest VAs from default.xbe.  usage: dis.py VA [count] [VA count ...]"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

XBE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "game", "default.xbe")
data = open(XBE, "rb").read()
base = struct.unpack_from("<I", data, 0x104)[0]
nsec = struct.unpack_from("<I", data, 0x11C)[0]
shdr = struct.unpack_from("<I", data, 0x120)[0] - base
secs = []
for i in range(nsec):
    o = shdr + i * 56
    flags, va, vsz, raw, rsz = struct.unpack_from("<5I", data, o)
    secs.append((va, vsz, raw, rsz))

def read(va, n):
    for sva, vsz, raw, rsz in secs:
        if sva <= va < sva + vsz:
            off = va - sva
            return data[raw + off: raw + min(rsz, off + n)]
    return b""

def dword(va):
    b = read(va, 4)
    return struct.unpack("<I", b)[0] if len(b) == 4 else None

md = Cs(CS_ARCH_X86, CS_MODE_32)
args = sys.argv[1:]
if args and args[0] == "dd":
    va = int(args[1], 16); n = int(args[2]) if len(args) > 2 else 16
    for k in range(n):
        print(f"{va + 4*k:08X}: {dword(va + 4*k):08X}")
    sys.exit()
i = 0
while i < len(args):
    va = int(args[i], 16)
    cnt = int(args[i + 1]) if i + 1 < len(args) else 40
    i += 2
    print(f"--- {va:08X}")
    for ins in md.disasm(read(va, cnt * 16), va):
        print(f"{ins.address:08X}: {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")
        cnt -= 1
        if cnt == 0:
            break
