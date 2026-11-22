# Indoor lighting regression test: start a new career (English, default name),
# photograph the tables behind the nation choice, sail into the start town
# (St. Kitts) and photograph its tavern, then compare both with xemu's
# pictures of the same screens (indoor_compare.py). Exit code 0 when they
# match, 1 when not. About four minutes, headless (drive.ps1).
#
#   indoor-lighting.ps1 [-Tag t] [-Reference <dir>] [VAR=value ...]
#
# The references (tavern.png, nation.png) are xemu screenshots of these two
# screens, 4:3. They are pictures of the game, so they live beside the
# repository, not in it: -Reference, else $env:PIRATES_INDOOR_REF, else
# ..\fixtures\xemu-indoor beside the repository. Make them in xemu with the
# same keys: new game, A, A, A, Start, Start (nation screen), A, A, wait
# about two minutes for St. Kitts, Down, A (the tavern), 20 s.
#
# What it guards against: the rooms' lightmaps read with the wrong texture
# coordinate set (toolkit patches 0036, 0049 and 0053), which made the
# tavern dark and blotchy, its floor striped and its hearth a mosaic.
param([string]$Tag = "indoor", [string]$Reference = "",
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
$sp = $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) })
if (-not $Reference) {
    $Reference = $(if ($env:PIRATES_INDOOR_REF) { $env:PIRATES_INDOOR_REF } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\fixtures\xemu-indoor")) })
}
if (-not (Test-Path (Join-Path $Reference "tavern.png")) -or -not (Test-Path (Join-Path $Reference "nation.png"))) {
    Write-Host "FAIL: no xemu references (tavern.png, nation.png) in $Reference"
    exit 1
}
# 4:3, as the references are; any VAR=value given comes after and wins.
$Env = @("RECOMP_WIDESCREEN=0") + @($Env | Where-Object { $_ })
& $d start -Tag $Tag @Env | Out-Null
try {
    & $d wait 37 | Out-Null
    & $d press start | Out-Null; & $d wait 8 | Out-Null          # title -> main menu
    & $d press a | Out-Null; & $d wait 6 | Out-Null              # NEW GAME
    & $d press a | Out-Null; & $d wait 6 | Out-Null              # tavern intro -> crew sign-up
    & $d press a | Out-Null; & $d wait 3 | Out-Null              # name keyboard
    & $d press start | Out-Null; & $d wait 3 | Out-Null          # keyboard Done
    & $d press start | Out-Null; & $d wait 8 | Out-Null          # sign-up DONE -> nation choice
    & $d screen nation | Out-Null
    & $d press a | Out-Null; & $d wait 10 | Out-Null             # English
    & $d press a | Out-Null; & $d wait 120 | Out-Null            # confirm -> St. Kitts town menu
    & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null  # VISIT THE TAVERN
    & $d press a | Out-Null; & $d wait 20 | Out-Null
    & $d screen tavern | Out-Null
} finally {
    & $d stop | Out-Null
}
$runs = Join-Path $sp "runs"
$cmp = @((Join-Path $PSScriptRoot "indoor_compare.py"), $Reference,
         (Join-Path $runs "${Tag}_tavern.png"), (Join-Path $runs "${Tag}_nation.png"))
if ($env:PYTHON) { & $env:PYTHON @cmp } else { & py -3 @cmp }
exit $LASTEXITCODE
