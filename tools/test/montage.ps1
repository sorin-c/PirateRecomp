# Tile a run's screenshots into one labelled contact sheet.
# usage: montage.ps1 -Tag run24 [-Cols 4] [-Scale 0.5]
param([string]$Tag, [int]$Cols = 4, [double]$Scale = 0.5)
Add-Type -AssemblyName System.Drawing
$runs = (Join-Path $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) }) "runs")
$files = Get-ChildItem $runs -Filter "$Tag`_t*.png" | Where-Object { $_.Name -notmatch '_\d+\.png$' -or $_.Name -match '_t\d+\.png$' } |
    Sort-Object { [int]($_.Name -replace '.*_t(\d+)\.png', '$1') }
if (-not $files) { "no shots for $Tag"; return }
$w = [int](640 * $Scale); $h = [int](480 * $Scale)
$rows = [math]::Ceiling($files.Count / $Cols)
$sheet = New-Object System.Drawing.Bitmap ($w * $Cols), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font "Arial", 12, ([System.Drawing.FontStyle]::Bold)
$i = 0
foreach ($f in $files) {
    $img = [System.Drawing.Image]::FromFile($f.FullName)
    $x = ($i % $Cols) * $w; $y = [math]::Floor($i / $Cols) * $h
    $g.DrawImage($img, $x, $y, $w, $h); $img.Dispose()
    $label = ($f.Name -replace '.*_t(\d+)\.png', 't=$1s')
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 60, 18)
    $g.DrawString($label, $font, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
    $i++
}
$out = Join-Path $runs "$Tag`_sheet.png"
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $sheet.Dispose()
$out
