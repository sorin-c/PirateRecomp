"""Find code that uses a 32-bit value as an operand: decode from known function
starts (functions.json) and print every instruction whose bytes hold it.
usage: findimm.py VALUE [VALUE ...]"""
import json, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
XBE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "game", "default.xbe")
FUN = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "build", "pipeline", "disasm", "functions.json")
data = open(XBE, "rb").read()
base = struct.unpack_from("<I", data, 0x104)[0]
nsec = struct.unpack_from("<I", data, 0x11C)[0]
shdr = struct.unpack_from("<I", data, 0x120)[0] - base
secs = [struct.unpack_from("<5I", data, shdr + i * 56) for i in range(nsec)]
def read(va, n):
    for f, sva, vsz, raw, rsz in secs:
        if sva <= va < sva + vsz:
            o = va - sva
            return data[raw + o: raw + min(rsz, o + n)]
    return b""
vals = [struct.pack("<I", int(v, 16)) for v in sys.argv[1:]]
md = Cs(CS_ARCH_X86, CS_MODE_32)
funcs = json.load(open(FUN))
for fn in funcs:
    s = int(fn["start"], 16); e = int(str(fn["end"]), 16) if isinstance(fn["end"], str) else int(fn["end"])
    for ins in md.disasm(read(s, e - s), s):
        if any(v in bytes(ins.bytes) for v in vals):
            print(f"{fn['start']} {ins.address:08X}: {ins.mnemonic} {ins.op_str}")
