param([string]$Capture, [switch]$SkipLifecycle,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\LayoutDock.exe'))
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DockMonitorTest {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c, string t);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
 [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr w);
 [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr w, uint flags);
 [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr w, uint m, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
 public static IntPtr Secondary() { return FindWindow("Shell_SecondaryTrayWnd",null); }
 public static IntPtr Widget() {
   IntPtr view = FindWindowEx(FindWindow("Shell_TrayWnd",null),IntPtr.Zero,"LayoutDock.Widget.v1",null);
   IntPtr bar = IntPtr.Zero;
   while(view == IntPtr.Zero && (bar = FindWindowEx(IntPtr.Zero,bar,"Shell_SecondaryTrayWnd",null)) != IntPtr.Zero)
     view = FindWindowEx(bar,IntPtr.Zero,"LayoutDock.Widget.v1",null);
   return view != IntPtr.Zero ? view : FindWindow("LayoutDock.Widget.v1",null);
 }
 public static long Call(uint msg, long wp, long lp) {
   IntPtr control = FindWindow("LayoutDock.Controller.v1",null), result;
   if(control == IntPtr.Zero || SendMessageTimeout(control,msg,new IntPtr(wp),new IntPtr(lp),2,3000,out result) == IntPtr.Zero)
     throw new Exception("Controller unavailable");
   return result.ToInt64();
 }
}
'@
function Wait-MonitorCondition([scriptblock]$Condition) {
    $end = [DateTime]::UtcNow.AddSeconds(8)
    do { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $end)
    throw 'Monitor check timed out'
}
$bar = [DockMonitorTest]::Secondary()
if ($bar -eq [IntPtr]::Zero) { throw 'Secondary taskbar not available' }
$second = [DockMonitorTest]::MonitorFromWindow($bar, 0).ToInt64()
$original = [DockMonitorTest]::Call(32813,4,0)
$docked = [DockMonitorTest]::Call(32813,3,0)
$originalX = [DockMonitorTest]::Call(32813,5,0)
$originalY = [DockMonitorTest]::Call(32813,6,0)
try {
    [void][DockMonitorTest]::Call(273,201,0)
    [void][DockMonitorTest]::Call(32815,$second,0)
    Wait-MonitorCondition { [DockMonitorTest]::GetParent([DockMonitorTest]::Widget()) -eq $bar }
    'PASS: widget attached to secondary taskbar'
    if (-not $SkipLifecycle) {
        & (Join-Path $PSScriptRoot 'verify-lifecycle.ps1') -Executable $Executable
        Wait-MonitorCondition { [DockMonitorTest]::GetParent([DockMonitorTest]::Widget()) -eq $bar }
        if ([DockMonitorTest]::Call(32813,4,0) -ne $second) { throw 'Monitor preference not restored' }
        'PASS: secondary monitor restored after restart'
    }
    if ($Capture) {
        Start-Sleep -Milliseconds 600 # Allow the newly attached composition surface to present.
        & (Join-Path $PSScriptRoot 'inspect-taskbar.ps1') -Secondary -Capture $Capture
    }
} finally {
    [void][DockMonitorTest]::Call(32815,$original,0)
    Wait-MonitorCondition { [DockMonitorTest]::MonitorFromWindow([DockMonitorTest]::Widget(),0).ToInt64() -eq $original }
    [void][DockMonitorTest]::Call(32811,$originalX,$originalY)
    [void][DockMonitorTest]::Call(273,$(if ($docked) {201} else {202}),0)
}
