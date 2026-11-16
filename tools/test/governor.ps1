# Start a new career (English, default name) and walk into the start town's
# governor's welcome, pressing A through his speech. Reproduces the governor
# hang (the picture stops on "Good day Mr. Incognito." while the game keeps
# drawing). Leaves the game running: use drive.ps1 shot/log/stop afterwards.
# usage: governor.ps1 -Tag t [VAR=value ...]
param([string]$Tag = "gov", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
if (-not $Env) { $Env = @("RECOMP_NONE=1") }
& $d start -Tag $Tag @Env | Out-Null
& $d wait 37
& $d press start | Out-Null        # title -> main menu
& $d wait 8
& $d press a | Out-Null            # NEW GAME
& $d wait 6
& $d press a | Out-Null            # tavern intro -> crew sign-up
& $d wait 6
& $d press a | Out-Null            # opens the name keyboard (cursor is on NAME)
& $d wait 3
& $d press start | Out-Null        # keyboard Done
& $d wait 3
& $d press start | Out-Null        # sign-up DONE -> nation select
& $d wait 8
& $d press a | Out-Null            # English
& $d wait 10
& $d press a | Out-Null            # confirm -> loading the start town
& $d wait 120
& $d shot town | Out-Null
& $d press a | Out-Null            # TALK TO THE GOVERNOR
& $d wait 15
for ($i = 0; $i -lt 30; $i++) { & $d press a | Out-Null; & $d wait 2 }
& $d shot gov | Out-Null
"at the governor (tag $Tag)"
