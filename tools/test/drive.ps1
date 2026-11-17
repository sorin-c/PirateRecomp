# Drive a live game session.
#   drive.ps1 start [-Tag t] [-KeepData] [-SaveDir d] [-Sound]   start the game in the background
#   drive.ps1 press a dpad_down:300 ...                           append presses (one per arg)
#   drive.ps1 shot [name]                                         screenshot -> runs\<tag>_<name>.png
#   drive.ps1 wait <seconds>                                      sleep
#   drive.ps1 log [pattern] [n]                                   tail / grep the run log
#   drive.ps1 stop                                                kill the game, keep the log
param([string]$Cmd, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$sp = $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) })
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$state = Join-Path $sp "drive_state.txt"
$runs = Join-Path $sp "runs"; New-Item -ItemType Directory -Force $runs | Out-Null

function Get-State { if (Test-Path $state) { $s = @{}; Get-Content $state | % { $k, $v = $_ -split '=', 2; $s[$k] = $v }; $s } else { @{} } }

switch ($Cmd) {
  "start" {
    $tag = "live"; $keep = $false; $saveDir = ""; $sound = $false; $extra = @{}
    for ($i = 0; $i -lt $Rest.Count; $i++) {
      switch ($Rest[$i]) {
        "-Tag" { $tag = $Rest[++$i] } "-KeepData" { $keep = $true } "-SaveDir" { $saveDir = $Rest[++$i] }
        "-Sound" { $sound = $true } default { $k, $v = $Rest[$i] -split '=', 2; $extra[$k] = $v }
      }
    }
    $s = Get-State; if ($s.pid) { Stop-Process -Id $s.pid -Force -ErrorAction SilentlyContinue }
    # One save folder for every run: the runtime keeps the emulated console's
    # identity (console.ini, its MAC address) there, and XAPI signs the game's
    # settings and saves per console. A folder per run is a new console per
    # run, and the game then calls the last run's settings "damaged".
    $saves = if ($saveDir) { $saveDir } else { Join-Path $sp "saves" }; New-Item -ItemType Directory -Force $saves | Out-Null
    $env:RECOMP_SAVE_DIR = $saves
    $env:RECOMP_GAME_DIR = $(if ($env:PIRATES_TEST_GAME) { $env:PIRATES_TEST_GAME } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testgame")) })
    if (-not $keep) { foreach ($d in "UDATA", "TDATA", "CACHE") { Remove-Item -Recurse -Force (Join-Path $env:RECOMP_GAME_DIR $d) -ErrorAction SilentlyContinue } }
    if ($sound) { Remove-Item Env:RECOMP_MUTE -ErrorAction SilentlyContinue } else { $env:RECOMP_MUTE = "1" }
    foreach ($k in $extra.Keys) { Set-Item "Env:$k" $extra[$k] }
    $live = Join-Path $sp "live_$tag.txt"; Set-Content -Path $live -Value $null -Encoding ascii
    $env:RECOMP_INPUT_LIVE = $live
    $log = Join-Path $runs "$tag.log"
    # PIRATES_TEST_EXE: another build to run (a second build tree, so the
    # player's own build\Release is never rebuilt under them).
    $exe = if ($env:PIRATES_TEST_EXE) { $env:PIRATES_TEST_EXE } else { Join-Path $root "build\Release\pirates_recomp.exe" }
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
         -RedirectStandardError $log -RedirectStandardOutput (Join-Path $runs "$tag.stdout") -PassThru
    "pid=$($p.Id)`ntag=$tag`nlive=$live`nlog=$log`nt0=$([DateTime]::Now.Ticks)" | Set-Content $state
    "started pid $($p.Id) tag $tag"
  }
  "press" {
    $s = Get-State
    foreach ($r in $Rest) { [IO.File]::AppendAllText($s.live, "$r`n") }
    "pressed: $($Rest -join ' ')"
  }
  "wait" { Start-Sleep -Milliseconds ([int]([double]$Rest[0] * 1000)) }
  "shot" {
    $s = Get-State
    $el = [int](([DateTime]::Now.Ticks - [long]$s.t0) / 1e7)
    $name = if ($Rest) { $Rest[0] } else { "t$el" }
    & (Join-Path $PSScriptRoot "shot.ps1") -ProcessId $s.pid -Out (Join-Path $runs "$($s.tag)_$name.png")
    "elapsed $el s"
  }
  "log" {
    $s = Get-State
    $pat = if ($Rest.Count -ge 1) { $Rest[0] } else { "" }; $n = if ($Rest.Count -ge 2) { [int]$Rest[1] } else { 20 }
    $l = Get-Content $s.log
    if ($pat) { $l | Select-String $pat | Select-Object -Last $n | % { "$_" } } else { $l | Select-Object -Last $n }
  }
  "stop" {
    $s = Get-State
    if ($s.pid) { Stop-Process -Id $s.pid -Force -ErrorAction SilentlyContinue; "stopped $($s.pid)" }
  }
  "alive" { $s = Get-State; if (Get-Process -Id $s.pid -ErrorAction SilentlyContinue) { "alive" } else { "exited" } }
}
