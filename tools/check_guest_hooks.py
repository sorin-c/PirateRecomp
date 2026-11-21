"""Check that the guest hook list and the code that serves it agree.

usage: check_guest_hooks.py config/hooks/<TITLEID>.json src/recomp_manual.c

Every address in the JSON (toolkit patch 0042: the lift puts a call to
recomp_guest_hook() before that instruction) needs a `case 0x...u:` in
recomp_guest_hook() or a <scene>_guest_hook() it calls (btl_guest_hook), and
every case needs its address in the JSON. Exits 1
and names the difference otherwise. Run by the build (CMakeLists.txt)."""
import json
import re
import sys

hooks_json, manual = sys.argv[1], sys.argv[2]
listed = {int(h["address"], 16) for h in json.load(open(hooks_json, encoding="utf-8"))["hooks"]}
src = open(manual, encoding="utf-8").read()
# recomp_guest_hook() and the functions it hands a scene's hooks to (named
# <scene>_guest_hook, as btl_guest_hook for ship battles).
src = src.replace("\r\n", "\n")
body = ""
for m in re.finditer(r"^(?:static )?(?:void|int) \w*guest_hook\(uint32_t va\)$", src, re.M):
    body += src[m.start():src.index("\n}\n", m.start())]
served = {int(a, 16) for a in re.findall(r"case (0x[0-9A-Fa-f]+)u?:", body)}
served |= {int(a, 16) for a in re.findall(r"va == (0x[0-9A-Fa-f]+)u?", body)}
missing_case = sorted(listed - served)
missing_json = sorted(served - listed)
for a in missing_case:
    print(f"check_guest_hooks: 0x{a:08X} is in {hooks_json} but recomp_guest_hook has no case for it")
for a in missing_json:
    print(f"check_guest_hooks: recomp_guest_hook handles 0x{a:08X} but {hooks_json} does not list it")
if missing_case or missing_json:
    sys.exit(1)
print(f"check_guest_hooks: {len(listed)} hooks, all served")
