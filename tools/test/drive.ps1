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
# The game this session started, or $null: the state's pid only counts while
# that process is still the one started (its start time and executable), so
# a pid Windows has since given to another program is never stopped.
function Get-Game($s) {
  if (-not $s.pid) { return $null }
  $p = Get-Process -Id $s.pid -ErrorAction SilentlyContinue
  if (-not $p) { return $null }
  try { $st = $p.StartTime.ToUniversalTime().Ticks; $path = $p.Path } catch { return $null }
  if (-not $s.started -or [long]$s.started -ne $st) { return $null }
  if ($s.exe -and $path -and $path -ine $s.exe) { return $null }
  $p
}
function Clear-Pid { if (Test-Path $state) { (Get-Content $state | Where-Object { $_ -notlike "pid=*" -and $_ -notlike "started=*" }) | Set-Content $state } }

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
    $s = Get-State; $old = Get-Game $s; if ($old) { $old | Stop-Process -Force -ErrorAction SilentlyContinue; $old.WaitForExit(5000) | Out-Null }
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
    $started = try { $p.StartTime.ToUniversalTime().Ticks } catch { "" }
    $exePath = try { $p.Path } catch { "" }
    "pid=$($p.Id)`nstarted=$started`nexe=$exePath`ntag=$tag`nlive=$live`nlog=$log`nreq=$req`nt0=$([DateTime]::Now.Ticks)" | Set-Content $state
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
    $s = Get-State; $p = Get-Game $s
    if ($p) { $p | Stop-Process -Force -ErrorAction SilentlyContinue; $p.WaitForExit(5000) | Out-Null; "stopped $($s.pid)" }
    elseif ($s.pid) { "not running (pid $($s.pid) is gone or another program now)" }
    Clear-Pid
  }
  "alive" { $s = Get-State; if (Get-Game $s) { "alive" } else { "exited" } }
}
