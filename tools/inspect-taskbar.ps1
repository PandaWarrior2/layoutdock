param([string]$Capture, [switch]$Secondary)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TaskbarInspect {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder name, int count);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$oldDpi = [TaskbarInspect]::SetThreadDpiAwarenessContext([IntPtr](-4))
$taskbar = [TaskbarInspect]::FindWindow($(if ($Secondary) { 'Shell_SecondaryTrayWnd' } else { 'Shell_TrayWnd' }), $null)
if ($taskbar -eq [IntPtr]::Zero) { throw 'Taskbar not found' }
$rect = New-Object TaskbarInspect+Rect
[void][TaskbarInspect]::GetWindowRect($taskbar, [ref]$rect)
Write-Output "Taskbar: $($rect.Left),$($rect.Top) - $($rect.Right),$($rect.Bottom)"
[TaskbarInspect+EnumProc]$callback = {
  param($hwnd, $data)
  $name = New-Object System.Text.StringBuilder 256
  [void][TaskbarInspect]::GetClassName($hwnd, $name, 256)
  $r = New-Object TaskbarInspect+Rect
  [void][TaskbarInspect]::GetWindowRect($hwnd, [ref]$r)
  Write-Host ("{0} {1} [{2},{3},{4},{5}] visible={6}" -f $hwnd,$name,$r.Left,$r.Top,$r.Right,$r.Bottom,[TaskbarInspect]::IsWindowVisible($hwnd))
  return $true
}
[void][TaskbarInspect]::EnumChildWindows($taskbar, $callback, [IntPtr]::Zero)
if ($Capture) {
  Add-Type -AssemblyName System.Drawing
  $bitmap = New-Object System.Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  try {
    $graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size)
    $bitmap.Save($Capture, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output "Captured: $Capture"
  } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
[void][TaskbarInspect]::SetThreadDpiAwarenessContext($oldDpi)
