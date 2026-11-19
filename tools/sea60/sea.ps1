# SEA60 spike: load the sea fixture headless, then a fixed input script at sea.
#   sea.ps1 -Tag t [-Sea60] [-Wait 31|1] [-Script idle|turns|long|burst|probe] [-Sound] [VAR=value ...]
#   -Sea60 sails at 60 with sea_60; -Wait N is the spike's raw frame wait (no fixes).
[CmdletBinding(PositionalBinding = $false)]
param([string]$Tag = "sea", [int]$Wait = 31, [string]$Script = "turns", [switch]$Sound, [switch]$Sea60, [int]$Interp = 1, [string]$Tp = "", [string]$Keys = "", [int]$SailFirst = 0, [string]$Save = "",
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Env)
$env:TEMP = "K:\Claude\sea60"; $env:TMP = "K:\Claude\sea60"
# drive.ps1 sets the run's variables in this PowerShell: clear the last run's
Get-ChildItem Env: | Where-Object { $_.Name -like "RECOMP_*" } | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$env:PIRATES_TEST_EXE = "K:\Code\pirategame\sea60\build\Release\pirates_recomp.exe"
$env:PIRATES_TEST_GAME = "K:\Code\pirategame\sea60game"; $env:PIRATES_TEST_OUT = "K:\Code\pirategame\sea60run"
$d = "K:\Code\pirategame\sea60\tools\test\drive.ps1"; $g = "K:\Code\pirategame\sea60game"
$log = "K:\Claude\sea60\runs\$Tag"; Remove-Item -Recurse -Force $log -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $log | Out-Null
foreach ($x in "UDATA", "TDATA", "CACHE") { Remove-Item -Recurse -Force (Join-Path $g $x) -ErrorAction SilentlyContinue }
$Fixture = $(if ($env:SEA60_FIXTURE) { $env:SEA60_FIXTURE } else { "K:\Code\pirategame\fixtures\sea-near-margarita\UDATA" })
Copy-Item -Recurse $Fixture (Join-Path $g "UDATA")
$a = @("start", "-Tag", $Tag, "-KeepData", "RECOMP_FRAME_INTERP=$Interp", "RECOMP_PIRATES_SEA_WAIT_MS=$Wait",
       "RECOMP_PIRATES_SEA_LOG=$log", "RECOMP_PIRATES_TEST_PROBE=$log", "RECOMP_PIRATES_SEA_WATCH=$((0x4564A0..0x45651F | ? { $_ % 4 -eq 0 } | % { $_.ToString('X') }) -join ','),49C284,49C28C,49C298,4D65E8,49C288,4C72FC,4D65B8,4D6600,40D4C4,4D652C")
if ($Sea60) { $a += @("RECOMP_PIRATES_SEA_60=1") }
if ($Sound) { New-Item -ItemType Directory -Force "$log\audio" | Out-Null; $a += @("RECOMP_AUDIO_CAPTURE=$log\audio") }   # muted run, the mix is still captured
$a += $Env
& $d @a | Out-Null
& $d wait 37 | Out-Null; & $d press start | Out-Null; & $d wait 20 | Out-Null
& $d press dpad_down:250 | Out-Null; & $d wait 2 | Out-Null; & $d press a | Out-Null; & $d wait 6 | Out-Null
& $d press a | Out-Null; & $d wait 4 | Out-Null; & $d press a | Out-Null; & $d wait 30 | Out-Null
& $d press a | Out-Null
if ($Tp) { & $d wait 1 | Out-Null; Set-Content "$log\tp.tmp" $Tp; Move-Item "$log\tp.tmp" "$log\teleport.txt" -Force }
"$([DateTime]::Now.ToString('HH:mm:ss.fff')) atsea" | Set-Content "$log\marks.txt"
function Mark($m) { "$([DateTime]::Now.ToString('HH:mm:ss.fff')) $m" | Add-Content "$log\marks.txt" }
switch ($Script) {
  "probe" { foreach ($k in 1..4) { & $d wait 10 | Out-Null; & $d screen "s$k" | Out-Null } }
  "accept" {
    & $d wait 30 | Out-Null; & $d screen "s1" | Out-Null; Mark "left"
    & $d press lstick_left:25000 | Out-Null; & $d wait 25 | Out-Null; & $d screen "s2" | Out-Null; Mark "right"
    & $d press lstick_right:25000 | Out-Null; & $d wait 25 | Out-Null; & $d screen "s3" | Out-Null; Mark "idle"
    & $d wait 30 | Out-Null; & $d screen "s4" | Out-Null; Mark "end"
  }
  "keys" {
    # -Keys "start,wait:2,rt,a": presses (and waits) after -SailFirst seconds
    # at sea, a screen after each; -Save <dir> copies UDATA there at the end.
    & $d wait $SailFirst | Out-Null; $n = 0
    foreach ($k in ($Keys -split ',')) {
      $n++
      if ($k -like "wait:*") { & $d wait ([double]$k.Substring(5)) | Out-Null; continue }
      & $d press $k | Out-Null; & $d wait 2.5 | Out-Null; & $d screen ("k{0:D2}" -f $n) | Out-Null
    }
  }
  "idle" { foreach ($k in 1..6) { & $d wait 15 | Out-Null; & $d screen "s$k" | Out-Null } }
  "turns" {
    & $d wait 20 | Out-Null; & $d screen "s1" | Out-Null; Mark "left"
    & $d press lstick_left:10000 | Out-Null; & $d wait 20 | Out-Null; & $d screen "s2" | Out-Null; Mark "right"
    & $d press lstick_right:6000 | Out-Null; & $d wait 20 | Out-Null; & $d screen "s3" | Out-Null; Mark "idle"
    & $d wait 30 | Out-Null; & $d screen "s4" | Out-Null; Mark "end"
  }
  "burst" {
    $st = @{}; Get-Content "K:\Code\pirategame\sea60run\drive_state.txt" | % { $k, $v = $_ -split '=', 2; $st[$k] = $v }
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
  "long" { foreach ($k in 1..12) { & $d wait 20 | Out-Null; & $d screen "s$k" | Out-Null } }
}
& $d stop | Out-Null
if ($Save) { Remove-Item -Recurse -Force $Save -ErrorAction SilentlyContinue; Copy-Item -Recurse (Join-Path $g "UDATA") $Save }
Copy-Item "K:\Code\pirategame\sea60run\runs\$Tag*" $log -ErrorAction SilentlyContinue
"done $Tag"
