# Start the town-sneak minigame (Sneak Into Governor's Mansion, Spanish,
# poor) from the debug MINIGAMES menu (RECOMP_PIRATES_MINIGAMES=1). Leaves
# the game running in the sneak: use drive.ps1 shot/press/stop afterwards.
# usage: townsneak.ps1 -Tag t [VAR=value ...]
param([string]$Tag = "ts", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
& $d start -Tag $Tag RECOMP_PIRATES_MINIGAMES=1 @Env | Out-Null
& $d wait 37 | Out-Null
& $d press start | Out-Null              # title -> main menu
& $d wait 8 | Out-Null
foreach ($i in 1..2) { & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null }   # MINIGAMES (*DEBUG!*): LOAD GAME is greyed and skipped
& $d press a | Out-Null
& $d wait 6 | Out-Null
foreach ($i in 1..4) { & $d press dpad_down:200 | Out-Null; & $d wait 1 | Out-Null }   # TOWN SNEAK
& $d press a | Out-Null                  # mission: Sneak Into Governor's Mansion
& $d wait 5 | Out-Null
& $d press a | Out-Null                  # town: Spanish
& $d wait 5 | Out-Null
& $d press a | Out-Null                  # prosperity: Poor
& $d wait 5 | Out-Null
& $d press a | Out-Null
& $d wait 35 | Out-Null
"in the town sneak (tag $Tag)"
