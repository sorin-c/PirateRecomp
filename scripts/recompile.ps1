# Lift Sid Meier's Pirates! from the game's own XBE into src/recomp/gen/, with
# the toolkit's pipeline (xboxrecomp/scripts/recompile.py). Run from anywhere;
# extra arguments go to recompile.py (--from disasm after a seed is added,
# --from lift after a recomp_manual.c override is added).
#
#   XBOXRECOMP_DIR  the toolkit (default: the xboxrecomp/ submodule)
#   PYTHON          a Python 3.10+ with capstone (default: py -3)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Toolkit = if ($env:XBOXRECOMP_DIR) { $env:XBOXRECOMP_DIR } else { Join-Path $Here "xboxrecomp" }
$Xbe = Join-Path $Here "game\default.xbe"
if (-not (Test-Path $Xbe)) {
    Write-Error "no $Xbe: put the disc's files in game\ (README.md)"
}
if (-not (Test-Path (Join-Path $Here "game\default_xdk_symbols.json"))) {
    Write-Host "game\default_xdk_symbols.json is missing: without it nothing is replaced"
    Write-Host "by name (no rendering, no sound). Make it with tools.xdk_symbols; README.md."
    exit 1
}
Push-Location $Toolkit
try {
    $py = if ($env:PYTHON) { @($env:PYTHON) } else { @("py", "-3") }
    & $py[0] @($py[1..($py.Length-1)]) scripts\recompile.py $Xbe `
        --work-dir (Join-Path $Here "build\pipeline") --project $Here @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
