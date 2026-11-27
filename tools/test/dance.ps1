# Start the dancing minigame (Easy, Spanish ballroom) from the debug
# MINIGAMES menu (RECOMP_PIRATES_MINIGAMES=1) and photograph the dance floor.
# Leaves the game running in the dance: use drive.ps1 shot/capture/stop
# afterwards.
# usage: dance.ps1 -Tag t [VAR=value ...]
param([string]$Tag = "dance", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
& $d start -Tag $Tag RECOMP_PIRATES_MINIGAMES=1 @Env | Out-Null
& $d wait 37 | Out-Null
& $d press start | Out-Null              # title -> main menu
& $d wait 8 | Out-Null
foreach ($i in 1..2) { & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null }   # MINIGAMES (*DEBUG!*)
& $d press a | Out-Null
& $d wait 6 | Out-Null
& $d shot minigames | Out-Null
& $d press a | Out-Null                  # DANCING (the first row)
& $d wait 5 | Out-Null
& $d shot difficulty | Out-Null
& $d press a | Out-Null                  # Argh. (easy)
& $d wait 8 | Out-Null
& $d shot ballroom | Out-Null
& $d press a | Out-Null                  # Spanish
& $d wait 20 | Out-Null
& $d shot dance | Out-Null
"in the dance (tag $Tag)"
