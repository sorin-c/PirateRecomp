# Ship battles faster than 30 (battle_fps), test driver: a VS MODE battle,
# headless, in which our ship circles (left stick held right from the start)
# and tries each fire button, while the test harness (src/sea60_test.c,
# RECOMP_PIRATES_BTL_TEST) logs every battle frame. docs/sea60-testing.md
# ("Ship battles") says how the runs are compared.
#
#   battle.ps1 -Tag t [-Rate 30|60|90|120|144|240|0] [-Secs 60] [VAR=value ...]
#
#   -Rate 30 is the baseline: run it with RECOMP_FRAME_INTERP=2, which holds
#   the battle at exactly 30.0 (without it the recomp's battle runs at 31).
#   Useful switches: RECOMP_PIRATES_BTL_PIN_ENEMY="x y" (the enemy held still,
#   so our ship's runs match), RECOMP_PIRATES_BTL_PIN_OURS="x y",
#   RECOMP_PIRATES_BTL_OR=49C298=20 (slow motion all along).
#
# Where things are: as tools/test/drive.ps1 (PIRATES_TEST_EXE, PIRATES_TEST_GAME,
# PIRATES_TEST_OUT); each run's logs go to $env:BATTLE60_RUNS\<Tag>
# ($env:TEMP\battle60 by default).
[CmdletBinding(PositionalBinding = $false)]
param([string]$Tag = "btl", [string]$Rate = "30", [int]$Secs = 60,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$d = Join-Path $root "tools\test\drive.ps1"
$runs = $(if ($env:BATTLE60_RUNS) { $env:BATTLE60_RUNS } else { Join-Path $env:TEMP "battle60" })
# drive.ps1 sets the run's variables in this PowerShell: clear the last run's
Get-ChildItem Env: | Where-Object { $_.Name -like "RECOMP_*" } | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$o = Join-Path $runs $Tag; Remove-Item -Recurse -Force $o -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $o | Out-Null
$all = @("RECOMP_PIRATES_BTL_TEST=$o", "RECOMP_PIRATES_BATTLE_FPS=$Rate") + $Env
& $d start -Tag $Tag @all | Out-Null
& $d wait 37 | Out-Null
& $d press start | Out-Null; & $d wait 8 | Out-Null                  # title -> main menu
& $d press dpad_down:250 a | Out-Null; & $d wait 6 | Out-Null        # VS MODE
& $d press start | Out-Null; & $d wait 3 | Out-Null                  # player 1 joins
& $d press a | Out-Null; & $d wait 3 | Out-Null                      # ready
& $d press a | Out-Null; & $d wait 2 | Out-Null                      # CPU 2 joins
& $d press a | Out-Null; & $d wait 2 | Out-Null                      # ready
& $d press start | Out-Null; & $d wait 2 | Out-Null                  # begin
# Live lines run one after another: the turn held in pieces, a fire button
# pressed with it between them.
$seq = @()
foreach ($k in 'a','x','b','y','rt','lt','a','x','a','x') {
  $seq += "lstick_right:$([int]($Secs * 1000 / 10))"; $seq += "lstick_right+$($k):300"
}
& $d press @seq | Out-Null
& $d wait ($Secs + 6) | Out-Null
& $d shot end | Out-Null
& $d stop | Out-Null
"done $Tag -> $o"
