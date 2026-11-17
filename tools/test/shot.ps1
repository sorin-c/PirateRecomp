# Capture the game's picture to PNG.
# usage: shot.ps1 -ProcessId <pid> -Out <file.png> [-Request <file>] [-Capture]
#
# With -Request (the RECOMP_SHOT_REQUEST file the run was started with, as
# drive.ps1 and run.ps1 do) the game writes its own next frame: that works for
# a headless run, whose window is off the desktop, and for a background one
# under other windows. -Capture also asks for a replayable .d3dcap, as F11.
# Without it, or when the game does not answer (an old build, a suspended
# process), the window is captured with PrintWindow (PW_RENDERFULLCONTENT).
param([int]$ProcessId, [string]$Out, [string]$Request = "", [switch]$Capture, [switch]$Screen, [int]$TimeoutMs = 5000)
Add-Type -AssemblyName System.Drawing
$Out = [IO.Path]::GetFullPath($Out)
if ($Request) {
  $bmp = [IO.Path]::ChangeExtension($Out, ".bmp")
  Remove-Item $bmp -ErrorAction SilentlyContinue
  # Written under another name and renamed, so the game never reads half a line.
  $tmp = "$Request.tmp"
  # -Screen: the window as presented, the overlay and settings menu included.
  $line = $(if ($Capture) { "capture $bmp" } elseif ($Screen) { "screen $bmp" } else { $bmp })
  [IO.File]::WriteAllText($tmp, "$line`n")
  Move-Item -Force $tmp $Request
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while (-not (Test-Path $bmp) -and $sw.ElapsedMilliseconds -lt $TimeoutMs) {
    if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 50
  }
  if (Test-Path $bmp) {
    $img = [System.Drawing.Image]::FromFile($bmp)
    $img.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    "saved $Out ($($img.Width) x $($img.Height)) from the game"
    $img.Dispose(); Remove-Item $bmp
    return
  }
  Remove-Item $Request -ErrorAction SilentlyContinue   # not taken: do not leave it for later
  "the game did not answer the picture request in $TimeoutMs ms; trying PrintWindow"
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<IntPtr> For(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }
}
"@
$i = 0
foreach ($h in [W]::For([uint32]$ProcessId)) {
  $r = New-Object W+RECT; [void][W]::GetClientRect($h, [ref]$r)
  $wd = $r.R - $r.L; $ht = $r.B - $r.T
  if ($wd -lt 50 -or $ht -lt 50) { continue }
  $sb = New-Object System.Text.StringBuilder 256; [void][W]::GetWindowText($h, $sb, 256)
  $bmp = New-Object System.Drawing.Bitmap $wd, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
  [void][W]::PrintWindow($h, $hdc, 3); $g.ReleaseHdc($hdc); $g.Dispose()
  $f = if ($i -eq 0) { $Out } else { $Out -replace '\.png$', "_$i.png" }
  $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  "saved $f ($wd x $ht) '$($sb.ToString())'"
  $i++
}
if ($i -eq 0) { "no window found for pid $ProcessId" }
