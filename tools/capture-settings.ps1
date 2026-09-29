param([string]$Capture = (Join-Path $PSScriptRoot 'settings-window.png'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SettingsCapture {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name,string title);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr window,IntPtr rect,IntPtr region,uint flags);
}
'@
$settingsWindow = [SettingsCapture]::FindWindow('#32770', 'LayoutDock Settings')
if ($settingsWindow -eq [IntPtr]::Zero -or -not [SettingsCapture]::IsWindowVisible($settingsWindow)) { throw 'Open LayoutDock settings first.' }
$settingsRect = New-Object SettingsCapture+Rect
[SettingsCapture]::GetWindowRect($settingsWindow, [ref]$settingsRect) | Out-Null
$settingsBitmap = New-Object System.Drawing.Bitmap(($settingsRect.Right-$settingsRect.Left), ($settingsRect.Bottom-$settingsRect.Top))
$settingsGraphics = [System.Drawing.Graphics]::FromImage($settingsBitmap)
try {
    [SettingsCapture]::RedrawWindow($settingsWindow, [IntPtr]::Zero, [IntPtr]::Zero, 0x185) | Out-Null
    $settingsDc = $settingsGraphics.GetHdc()
    try {
        if (-not [SettingsCapture]::PrintWindow($settingsWindow, $settingsDc, 2)) { throw 'Settings capture failed.' }
    } finally { $settingsGraphics.ReleaseHdc($settingsDc) }
    $settingsBitmap.Save($Capture)
} finally {
    $settingsGraphics.Dispose()
    $settingsBitmap.Dispose()
}
