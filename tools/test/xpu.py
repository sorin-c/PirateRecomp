"""Decode the game's compiled Xbox pixel shaders (.xpu: a 4-byte header, then
the XDK's D3DPIXELSHADERDEF, 60 dwords).

usage: xpu.py <file.xpu> [...]"""
import struct, sys

FIELDS = (["PSAlphaInputs%d" % i for i in range(8)] +
          ["PSFinalCombinerInputsABCD", "PSFinalCombinerInputsEFG"] +
          ["PSConstant0_%d" % i for i in range(8)] +
          ["PSConstant1_%d" % i for i in range(8)] +
          ["PSAlphaOutputs%d" % i for i in range(8)] +
          ["PSRGBInputs%d" % i for i in range(8)] +
          ["PSCompareMode", "PSFinalCombinerConstant0", "PSFinalCombinerConstant1"] +
          ["PSRGBOutputs%d" % i for i in range(8)] +
          ["PSCombinerCount", "PSTextureModes", "PSDotMapping", "PSInputTexture",
           "PSC0Mapping", "PSC1Mapping", "PSFinalCombinerConstants"])
MODES = {0: "NONE", 1: "PROJECT2D", 2: "PROJECT3D", 3: "CUBEMAP", 4: "PASSTHRU",
         5: "CLIPPLANE", 6: "BUMPENVMAP", 7: "BUMPENVMAP_LUM", 8: "BRDF",
         9: "DOT_ST", 10: "DOT_ZW", 11: "DOT_RFLCT_DIFF", 12: "DOT_RFLCT_SPEC",
         13: "DOT_STR_3D", 14: "DOT_STR_CUBE", 15: "DPNDNT_AR", 16: "DPNDNT_GB",
         17: "DOTPRODUCT", 18: "DOT_RFLCT_SPEC_CONST"}

for path in sys.argv[1:]:
    data = open(path, "rb").read()
    head = struct.unpack_from("<I", data, 0)[0]
    words = struct.unpack_from("<%dI" % len(FIELDS), data, 4)
    d = dict(zip(FIELDS, words))
    tm = d["PSTextureModes"]
    modes = [MODES.get((tm >> (5 * s)) & 31, "?") for s in range(4)]
    print(f"{path.rsplit(chr(92), 1)[-1]}: header 0x{head:08X} count 0x{d['PSCombinerCount']:08X} "
          f"modes 0x{tm:08X} {modes} compare 0x{d['PSCompareMode']:08X} "
          f"dotmap 0x{d['PSDotMapping']:08X} input 0x{d['PSInputTexture']:08X} "
          f"final 0x{d['PSFinalCombinerInputsABCD']:08X}/0x{d['PSFinalCombinerInputsEFG']:08X}")
