#!/bin/bash
# Lift Sid Meier's Pirates! from the game's own XBE into src/recomp/gen/, with
# the toolkit's pipeline (xboxrecomp/scripts/recompile.py). Run from anywhere;
# extra arguments go to recompile.py (--from disasm after a seed is added,
# --from lift after a recomp_manual.c override is added).
#
#   XBOXRECOMP_DIR  the toolkit (default: the xboxrecomp/ submodule)
#   PYTHON          a Python 3.10+ with capstone
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
TOOLKIT="${XBOXRECOMP_DIR:-$HERE/xboxrecomp}"
PY="${PYTHON:-$TOOLKIT/.venv/bin/python}"
[ -x "$PY" ] || PY=python3
XBE="$HERE/game/default.xbe"
[ -f "$XBE" ] || { echo "no $XBE: put the disc's files in game/ (README.md)" >&2; exit 1; }
cd "$TOOLKIT"
if [ ! -f "$HERE/game/default_xdk_symbols.json" ]; then
    echo "game/default_xdk_symbols.json is missing: without it nothing is replaced"
    echo "by name (no rendering, no sound). Make it with tools.xdk_symbols; README.md."
    exit 1
fi
exec "$PY" scripts/recompile.py "$XBE" --work-dir "$HERE/build/pipeline" \
    --project "$HERE" "$@"
