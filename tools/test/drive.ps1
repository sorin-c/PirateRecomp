# Drive a live game session.
#   drive.ps1 start [-Tag t] [-KeepData] [-SaveDir d] [-Sound] [-Visible]
#                                                                 start the game, headless
#   drive.ps1 press a dpad_down:300 ...                           append presses (one per arg)
#   drive.ps1 shot [name]                                         screenshot -> runs\<tag>_<name>.png
#   drive.ps1 screen [name]                                       the same, as presented (overlay, menu)
#   drive.ps1 capture [name]                                      the same, plus a replayable .d3dcap (as F11)
#   drive.ps1 wait <seconds>                                      sleep
#   drive.ps1 log [pattern] [n]                                   tail / grep the run log
#   drive.ps1 stop                                                kill the game, keep the log
#
# Runs are headless (RECOMP_HEADLESS=1): the window opens off the desktop, off
# the taskbar and is never active, and no real keyboard or pad is read on any
# port -- the presses above are the game's only input. Pictures come from the
# game itself (RECOMP_SHOT_REQUEST), so shot works with the window out of sight.
# -Visible is the old background mode, for watching a run: the window is on
# the desktop behind the others and never active, but real devices are read
# again on the ports the script does not own (a pad in slot 2 reaches the game).
param([string]$Cmd, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$sp = $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) })
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$state = Join-Path $sp "drive_state.txt"
$runs = Join-Path $sp "runs"; New-Item -ItemType Directory -Force $runs | Out-Null

function Get-State { if (Test-Path $state) { $s = @{}; Get-Content $state | % { $k, $v = $_ -split '=', 2; $s[$k] = $v }; $s } else { @{} } }

switch ($Cmd) {
  "start" {
    $tag = "live"; $keep = $false; $saveDir = ""; $sound = $false; $visible = $false; $extra = @{}
    for ($i = 0; $i -lt $Rest.Count; $i++) {
      switch ($Rest[$i]) {
        "-Tag" { $tag = $Rest[++$i] } "-KeepData" { $keep = $true } "-SaveDir" { $saveDir = $Rest[++$i] }
        "-Sound" { $sound = $true } "-Visible" { $visible = $true }
        default { if ($Rest[$i]) { $k, $v = $Rest[$i] -split '=', 2; $extra[$k] = $v } }   # an empty @Env splat passes $null
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
    # Never in front, never the focus; headless also off the desktop and deaf
    # to the real keyboard and pads (a player's pad in another slot included).
    $env:RECOMP_WINDOW_BACKGROUND = "1"
    if ($visible) { Remove-Item Env:RECOMP_HEADLESS -ErrorAction SilentlyContinue } else { $env:RECOMP_HEADLESS = "1" }
    $req = Join-Path $sp "shot_$tag.req"; Remove-Item $req -ErrorAction SilentlyContinue
    $env:RECOMP_SHOT_REQUEST = $req
    foreach ($k in $extra.Keys) { Set-Item "Env:$k" $extra[$k] }
    $live = Join-Path $sp "live_$tag.txt"; Set-Content -Path $live -Value $null -Encoding ascii
    $env:RECOMP_INPUT_LIVE = $live
    $log = Join-Path $runs "$tag.log"
    # PIRATES_TEST_EXE: another build to run (a second build tree, so the
    # player's own build\Release is never rebuilt under them).
    $exe = if ($env:PIRATES_TEST_EXE) { $env:PIRATES_TEST_EXE } else { Join-Path $root "build\Release\pirates_recomp.exe" }
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
         -RedirectStandardError $log -RedirectStandardOutput (Join-Path $runs "$tag.stdout") -PassThru
    "pid=$($p.Id)`ntag=$tag`nlive=$live`nlog=$log`nreq=$req`nt0=$([DateTime]::Now.Ticks)" | Set-Content $state
    "started pid $($p.Id) tag $tag$(if ($visible) { ' (visible)' } else { ' (headless)' })"
  }
  "press" {
    $s = Get-State
    foreach ($r in $Rest) { [IO.File]::AppendAllText($s.live, "$r`n") }
    "pressed: $($Rest -join ' ')"
  }
  "wait" { Start-Sleep -Milliseconds ([int]([double]$Rest[0] * 1000)) }
  { $_ -in "shot", "capture", "screen" } {
    $s = Get-State
    $el = [int](([DateTime]::Now.Ticks - [long]$s.t0) / 1e7)
    $name = if ($Rest) { $Rest[0] } else { "t$el" }
    & (Join-Path $PSScriptRoot "shot.ps1") -ProcessId $s.pid -Out (Join-Path $runs "$($s.tag)_$name.png") `
      -Request $s.req -Capture:($Cmd -eq "capture") -Screen:($Cmd -eq "screen")
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
