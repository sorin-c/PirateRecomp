# Sailing at 60 (sea_60), test driver: load a saved game at sea, headless,
# then play a fixed script while the test harness (src/sea60_test.c) logs
# every sea frame. docs/sea60-testing.md says how the runs are compared.
#
#   sea.ps1 -Tag t [-Rate 60|90|120|144|240|0] [-Sea60] [-Interp 2] [-Script accept|drift|burst|keys|idle]
#           [-Pin "x y ms [heading speed]"] [-Poke "addr=value@ms,..."]
#           [-NoShips] [-Flip "ms,..."] [-Keys ...] [-SailFirst s] [-Save dir]
#           [-Sound] [VAR=value ...]
#
#   -Rate N    sail with sea_fps = N (0: unlocked); -Sea60 is -Rate 60; with
#              neither the run is the 30 fps baseline
#   -Interp 2  frame interpolation: holds a 30 fps sea at exactly 30.0
#              (without it the recomp's 30 fps sea runs about 31.3)
#   -Pin, -Poke, -NoShips, -Flip  the harness switches of the same names
#
# Where things are (environment, else the defaults):
#   PIRATES_TEST_EXE   the build (build\Release\pirates_recomp.exe)
#   PIRATES_TEST_GAME  a copy of the game folder (..\testgame, as drive.ps1)
#   PIRATES_TEST_OUT   drive.ps1's work folder (..\testrun)
#   SEA60_FIXTURE      the UDATA folder of a save at sea (required)
#   SEA60_RUNS         where each run's folder goes ($env:TEMP\sea60)
[CmdletBinding(PositionalBinding = $false)]
param([string]$Tag = "sea", [string]$Script = "accept", [switch]$Sea60, [int]$Rate = 30, [int]$Interp = 1,
      [string]$Pin = "", [string]$Poke = "", [switch]$NoShips, [string]$Flip = "",
      [string]$Keys = "", [int]$SailFirst = 0, [string]$Save = "", [switch]$Sound,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$d = Join-Path $root "tools\test\drive.ps1"
if (-not $env:PIRATES_TEST_EXE) { $env:PIRATES_TEST_EXE = Join-Path $root "build\Release\pirates_recomp.exe" }
$g = $(if ($env:PIRATES_TEST_GAME) { $env:PIRATES_TEST_GAME } else { [IO.Path]::GetFullPath((Join-Path $root "..\testgame")) })
$out = $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $root "..\testrun")) })
if (-not $env:SEA60_FIXTURE) { throw "set SEA60_FIXTURE to the UDATA folder of a save at sea" }
# Check the fixture before anything is cleared: it must be a folder with a
# save in it, and not the test game's UDATA (or inside it, or holding it),
# which the clean-up below would delete before the copy.
$fix = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($env:SEA60_FIXTURE).TrimEnd('\', '/')
$ud = [IO.Path]::GetFullPath((Join-Path $g "UDATA")).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $fix -PathType Container)) { throw "SEA60_FIXTURE is not a folder: $fix" }
# A junction or symlink could make the two the same folder under other names:
# refuse one on either path, or on any folder up to the drive root.
function Get-Reparse($path) {
  for ($q = $path; $q; $q = Split-Path -Parent $q) {
    $i = Get-Item -LiteralPath $q -Force -ErrorAction SilentlyContinue
    if ($i -and ($i.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $q }
  }
}
foreach ($q in $fix, $ud) { $r = Get-Reparse $q; if ($r) { throw "$r is a junction or symlink: give SEA60_FIXTURE and the test game as real folders" } }
if (-not (Get-ChildItem -LiteralPath $fix -Recurse -File | Select-Object -First 1)) { throw "SEA60_FIXTURE holds no files: $fix" }
$sep = [IO.Path]::DirectorySeparatorChar
if ($fix -ieq $ud -or $fix.StartsWith("$ud$sep", [StringComparison]::OrdinalIgnoreCase) -or
    $ud.StartsWith("$fix$sep", [StringComparison]::OrdinalIgnoreCase)) {
  throw "SEA60_FIXTURE ($fix) overlaps the test game's UDATA ($ud): copy the save somewhere else first"
}
$runs = $(if ($env:SEA60_RUNS) { $env:SEA60_RUNS } else { Join-Path $env:TEMP "sea60" })

# drive.ps1 sets the run's variables in this PowerShell: clear the last run's
Get-ChildItem Env: | Where-Object { $_.Name -like "RECOMP_*" } | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$log = Join-Path $runs $Tag; Remove-Item -Recurse -Force $log -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $log | Out-Null
# The copy is made beside the old UDATA first, and replaces it only once whole.
$stage = "$ud.new"; Remove-Item -Recurse -Force -LiteralPath $stage -ErrorAction SilentlyContinue
Copy-Item -Recurse -LiteralPath $fix $stage -ErrorAction Stop
foreach ($x in "UDATA", "TDATA", "CACHE") { Remove-Item -Recurse -Force (Join-Path $g $x) -ErrorAction SilentlyContinue }
if (Test-Path -LiteralPath $ud) { throw "could not clear the old save: $ud (is the game still running?)" }
Move-Item -LiteralPath $stage $ud -ErrorAction Stop

$a = @("start", "-Tag", $Tag, "-KeepData", "RECOMP_FRAME_INTERP=$Interp", "RECOMP_PIRATES_SEA60_TEST=$log")
if ($Sea60)   { $Rate = 60 }
$a += "RECOMP_PIRATES_SEA_FPS=$Rate"; $a += "RECOMP_PIRATES_SEA_60=0"
if ($Pin)     { $a += "RECOMP_PIRATES_SEA60_PIN=$Pin" }
if ($Poke)    { $a += "RECOMP_PIRATES_SEA60_POKE=$Poke" }
if ($NoShips) { $a += "RECOMP_PIRATES_SEA60_NOSHIPS=1" }
if ($Flip)    { $a += "RECOMP_PIRATES_SEA60_FLIP=$Flip"; $a = $a | Where-Object { $_ -notlike "RECOMP_PIRATES_SEA_*=*" } }
if ($Sound)   { New-Item -ItemType Directory -Force "$log\audio" | Out-Null; $a += "RECOMP_AUDIO_CAPTURE=$log\audio" }
$a += $Env
& $d @a | Out-Null

# Main menu -> LOAD GAME -> the first save -> at sea (about 100 s)
& $d wait 37 | Out-Null; & $d press start | Out-Null; & $d wait 20 | Out-Null
& $d press dpad_down:250 | Out-Null; & $d wait 2 | Out-Null; & $d press a | Out-Null; & $d wait 6 | Out-Null
& $d press a | Out-Null; & $d wait 4 | Out-Null; & $d press a | Out-Null; & $d wait 30 | Out-Null
& $d press a | Out-Null
"$([DateTime]::Now.ToString('HH:mm:ss.fff')) atsea" | Set-Content "$log\marks.txt"
function Mark($m) { "$([DateTime]::Now.ToString('HH:mm:ss.fff')) $m" | Add-Content "$log\marks.txt" }
switch ($Script) {
  "accept" {   # 30 s straight, 25 s turning left, 25 s right, 30 s with no input
    & $d wait 30 | Out-Null; & $d screen "s1" | Out-Null; Mark "left"
    & $d press lstick_left:25000 | Out-Null; & $d wait 25 | Out-Null; & $d screen "s2" | Out-Null; Mark "right"
    & $d press lstick_right:25000 | Out-Null; & $d wait 25 | Out-Null; & $d screen "s3" | Out-Null; Mark "idle"
    & $d wait 30 | Out-Null; & $d screen "s4" | Out-Null; Mark "end"
  }
  "drift" {    # 11 minutes; A every 30 s, the same in both runs, closes the crew's dialogs
    foreach ($k in 1..11) {
      & $d wait 29 | Out-Null; & $d screen ("m{0:D2}a" -f $k) | Out-Null; & $d press a | Out-Null
      & $d wait 29 | Out-Null; & $d screen ("m{0:D2}b" -f $k) | Out-Null; & $d press a | Out-Null; Mark "m$k"
    }
  }
  "burst" {    # 3 x 32 in-game frames ~125 ms apart (bob.py)
    $st = @{}; Get-Content (Join-Path $out "drive_state.txt") | ForEach-Object { $k, $v = $_ -split '=', 2; $st[$k] = $v }
    foreach ($k in 1..3) {
      & $d wait 12 | Out-Null
      foreach ($i in 1..32) {
        $bmp = "$log\b$k-$('{0:D2}' -f $i).bmp"
        [IO.File]::WriteAllText("$($st.req).tmp", "$bmp`n"); Move-Item -Force "$($st.req).tmp" $st.req
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while (-not (Test-Path $bmp) -and $sw.ElapsedMilliseconds -lt 3000) { Start-Sleep -Milliseconds 5 }
        "$k $i $([DateTime]::Now.ToString('HH:mm:ss.fff'))" | Add-Content "$log\burst.txt"
        Start-Sleep -Milliseconds 60
      }
    }
  }
  "keys" {     # -Keys "start,wait:2,lt,a,shot": after -SailFirst s, a screen after each press
    & $d wait $SailFirst | Out-Null; $n = 0
    foreach ($k in ($Keys -split ',')) {
      $n++
      if ($k -like "wait:*") { & $d wait ([double]$k.Substring(5)) | Out-Null; continue }
      if ($k -eq "shot") { & $d screen ("k{0:D2}" -f $n) | Out-Null; continue }
      & $d press $k | Out-Null; & $d wait 2.5 | Out-Null; & $d screen ("k{0:D2}" -f $n) | Out-Null
    }
  }
  "idle" { foreach ($k in 1..6) { & $d wait 15 | Out-Null; & $d screen "s$k" | Out-Null } }
}
& $d stop | Out-Null
if ($Save) { Remove-Item -Recurse -Force $Save -ErrorAction SilentlyContinue; Copy-Item -Recurse (Join-Path $g "UDATA") $Save }
Copy-Item (Join-Path $out "runs\$Tag.*") $log -ErrorAction SilentlyContinue
Copy-Item (Join-Path $out "runs\$($Tag)_*") $log -ErrorAction SilentlyContinue
"done $Tag"
