# Start the game and drive it into a VS MODE ship battle (player 1 against
# one CPU ship) with no save needed: the quickest way to a 3D sea scene.
# usage: vs-battle.ps1 -Tag t [VAR=value ...]   then use drive.ps1 shot/press/stop
param([string]$Tag = "vs", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$d = Join-Path $PSScriptRoot "drive.ps1"
& $d start -Tag $Tag @Env | Out-Null
& $d wait 37
& $d press start | Out-Null        # title -> main menu
& $d wait 8
& $d press dpad_down:250 a | Out-Null   # VS MODE (LOAD GAME is greyed with no save)
& $d wait 6
& $d press start | Out-Null        # player 1 joins
& $d wait 3
& $d press a | Out-Null            # player 1 ready
& $d wait 3
& $d press a | Out-Null            # CPU 2 joins
& $d wait 2
& $d press a | Out-Null            # CPU 2 ready
& $d wait 2
& $d press start | Out-Null        # begin
& $d wait 30
"in battle (tag $Tag)"
