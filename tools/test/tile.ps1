# Tile named screenshots from runs\ into runs\tile.png.  usage: tile.ps1 L1_b15 L1_b16 ...  [-Cols 3]
param([int]$Cols = 3, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
Add-Type -AssemblyName System.Drawing
$runs = (Join-Path $(if ($env:PIRATES_TEST_OUT) { $env:PIRATES_TEST_OUT } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\testrun")) }) "runs")
$w = 400; $h = 300
$files = $Names | % { Join-Path $runs "$_.png" } | Where-Object { Test-Path $_ }
$rows = [math]::Ceiling($files.Count / $Cols)
$sheet = New-Object System.Drawing.Bitmap ($w * [math]::Min($Cols, $files.Count)), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$font = New-Object System.Drawing.Font "Arial", 11, ([System.Drawing.FontStyle]::Bold)
$i = 0
foreach ($f in $files) {
    $img = [System.Drawing.Image]::FromFile($f)
    $x = ($i % $Cols) * $w; $y = [math]::Floor($i / $Cols) * $h
    $g.DrawImage($img, $x, $y, $w, $h); $img.Dispose()
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 90, 17)
    $g.DrawString([IO.Path]::GetFileNameWithoutExtension($f), $font, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
    $i++
}
$out = Join-Path $runs "tile.png"
$sheet.Save($out); $g.Dispose(); $sheet.Dispose(); $out
