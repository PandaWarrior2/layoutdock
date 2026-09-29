param([string]$Executable = (Join-Path $PSScriptRoot '..\build\LayoutDock.exe'))
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DockLifecycle {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string title);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
 public static IntPtr Widget() {
   IntPtr bar = FindWindow("Shell_TrayWnd", null);
   IntPtr view = FindWindowEx(bar, IntPtr.Zero, "LayoutDock.Widget.v1", null);
   IntPtr secondary = IntPtr.Zero;
   while (view == IntPtr.Zero && (secondary = FindWindowEx(IntPtr.Zero, secondary, "Shell_SecondaryTrayWnd", null)) != IntPtr.Zero)
     view = FindWindowEx(secondary, IntPtr.Zero, "LayoutDock.Widget.v1", null);
   return view != IntPtr.Zero ? view : FindWindow("LayoutDock.Widget.v1", null);
 }
}
'@
function Get-DockWindow {
    return [DockLifecycle]::Widget()
}
function Wait-Condition([scriptblock]$Condition, [int]$Seconds = 20) {
    $end = [DateTime]::UtcNow.AddSeconds($Seconds)
    do { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $end)
    throw 'Timed out waiting for lifecycle condition'
}
function Get-DockProcess {
    @(Get-Process -Name LayoutDock -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Executable })
}
Wait-Condition { (Get-DockWindow) -ne [IntPtr]::Zero }
Start-Process -FilePath $Executable -WindowStyle Hidden -Wait
if (@(Get-DockProcess).Count -ne 1) { throw 'Duplicate controller detected' }
'PASS: duplicate launch keeps one controller'
$oldView = Get-DockWindow
[void][DockLifecycle]::PostMessage($oldView, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Wait-Condition { (Get-DockWindow) -eq [IntPtr]::Zero }
Wait-Condition { (Get-DockWindow) -ne [IntPtr]::Zero }
'PASS: missing widget is recreated by controller'
$process = Get-DockProcess
Start-Process -FilePath $Executable -ArgumentList '--stop' -WindowStyle Hidden -Wait
if (-not $process.WaitForExit(5000)) { throw 'Controller did not stop' }
Wait-Condition { (Get-DockWindow) -eq [IntPtr]::Zero } 5
$bar = [DockLifecycle]::FindWindow('Shell_TrayWnd', $null)
[uint32]$explorerProcessId = 0
[void][DockLifecycle]::GetWindowThreadProcessId($bar, [ref]$explorerProcessId)
Wait-Condition { -not ((Get-Process -Id $explorerProcessId).Modules | Where-Object { $_.ModuleName -eq 'LayoutDock.Widget.dll' }) } 5
'PASS: controller exits and DLL unloads from Explorer'
Start-Process -FilePath $Executable -WindowStyle Hidden
Wait-Condition { (Get-DockWindow) -ne [IntPtr]::Zero }
'PASS: clean restart restores widget'
