# Start a sword duel (Garden/Fountain, Easy, Spanish) from the debug
# MINIGAMES menu (RECOMP_PIRATES_MINIGAMES=1). Leaves the game running in the
# duel: use drive.ps1 shot/press/stop afterwards.
# usage: duel.ps1 -Tag t [VAR=value ...]
param([string]$Tag = "duel", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
& $d start -Tag $Tag RECOMP_PIRATES_MINIGAMES=1 @Env | Out-Null
& $d wait 37 | Out-Null
& $d press start | Out-Null              # title -> main menu
& $d wait 8 | Out-Null
foreach ($i in 1..2) { & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null }   # MINIGAMES (*DEBUG!*)
& $d press a | Out-Null
& $d wait 6 | Out-Null
& $d press dpad_down:200 | Out-Null      # DUELING
& $d wait 1 | Out-Null
& $d press a | Out-Null
& $d wait 5 | Out-Null
& $d shot pick | Out-Null
foreach ($i in 1..3) { & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null }   # Garden/Fountain
foreach ($i in 1..4) { & $d press a | Out-Null; & $d wait 4 | Out-Null; & $d shot "step$i" | Out-Null }   # Garden/Fountain, Easy, Spanish, then on
& $d wait 20 | Out-Null
"in the duel (tag $Tag)"
