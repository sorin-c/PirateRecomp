# Run pirates_recomp.exe from build\Release for a bounded time, stderr to run.log.
# Usage: run.ps1 [-Seconds 60] [-Tag name] [-Sound] [-Env @{K='V'}]
param([int]$Seconds = 60, [string]$Tag = "run", [switch]$Sound, [hashtable]$Env = @{}, [int[]]$ShotAt = @(), [switch]$KeepData, [string]$SaveDir = "")
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sp = $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) })
$runs = Join-Path $sp "runs"; New-Item -ItemType Directory -Force $runs | Out-Null
$saves = if ($SaveDir) { $SaveDir } else { Join-Path $sp "saves_$Tag" }; New-Item -ItemType Directory -Force $saves | Out-Null
$env:RECOMP_SAVE_DIR = $saves
# The game's E:/T:/Z: data (UDATA, TDATA, CACHE) lives in the game folder, so
# runs use a hard-linked copy whose data folders start empty unless -KeepData.
$env:RECOMP_GAME_DIR = $(if ($env:PIRATES_TEST_GAME) { $env:PIRATES_TEST_GAME } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testgame")) })
if (-not $KeepData) {
    foreach ($d in "UDATA","TDATA","CACHE") { Remove-Item -Recurse -Force (Join-Path $env:RECOMP_GAME_DIR $d) -ErrorAction SilentlyContinue }
}
if (-not $Sound) { $env:RECOMP_MUTE = "1" } else { Remove-Item Env:RECOMP_MUTE -ErrorAction SilentlyContinue }
foreach ($k in $Env.Keys) { Set-Item "Env:$k" $Env[$k] }
$log = Join-Path $root "run.log"
$exe = Join-Path $root "build\Release\pirates_recomp.exe"
$p = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $root "build\Release") `
    -RedirectStandardError $log -RedirectStandardOutput (Join-Path $runs "$Tag.stdout") -PassThru
$elapsed = 0; $exited = $false
foreach ($t in ($ShotAt | Sort-Object)) {
    if ($t -ge $Seconds) { break }
    if ($p.WaitForExit(($t - $elapsed) * 1000)) { $exited = $true; break }
    $elapsed = $t
    & (Join-Path $PSScriptRoot "shot.ps1") -ProcessId $p.Id -Out (Join-Path $runs "$Tag`_t$t.png")
}
if (-not $exited) { $exited = $p.WaitForExit(($Seconds - $elapsed) * 1000) }
if (-not $exited) { Stop-Process -Id $p.Id -Force; "killed after $Seconds s" } else { "exited code=$($p.ExitCode)" }
Copy-Item $log (Join-Path $runs "$Tag.log") -Force
$l = Get-Content $log
"lines=$($l.Count)"
