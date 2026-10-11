<#
.SYNOPSIS
    Captures one window's client area to a PNG.

.DESCRIPTION
    Screen-capture based, so it works for OpenGL windows where PrintWindow
    returns an empty surface. Brings the window to the foreground first, waits
    for the frame to settle, then reads the client rectangle from the desktop.

    Used as the visual feedback loop for this project: build, run, capture,
    inspect the PNG.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 `
        -ProcessName Bolt -OutputPath analysis/tmp/shot.png
#>
[CmdletBinding()]
param(
    [string]$ProcessName = 'Bolt',
    [string]$OutputPath  = 'analysis\tmp\shot.png',
    [int]$SettleMs = 1400,
    [switch]$WholeWindow
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

if (-not ('Win32Capture' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class Win32Capture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
}
'@
}

$process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw "process '$ProcessName' is not running" }

$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) { throw "process '$ProcessName' has no main window yet" }

if ([Win32Capture]::IsIconic($handle)) { [void][Win32Capture]::ShowWindow($handle, 9) }  # SW_RESTORE
[void][Win32Capture]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds $SettleMs

if ($WholeWindow) {
    $rect = New-Object Win32Capture+RECT
    [void][Win32Capture]::GetWindowRect($handle, [ref]$rect)
    $x = $rect.Left; $y = $rect.Top
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
} else {
    $rect = New-Object Win32Capture+RECT
    [void][Win32Capture]::GetClientRect($handle, [ref]$rect)
    $origin = New-Object Win32Capture+POINT
    $origin.X = 0; $origin.Y = 0
    [void][Win32Capture]::ClientToScreen($handle, [ref]$origin)
    $x = $origin.X; $y = $origin.Y
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
}

if ($w -le 0 -or $h -le 0) { throw "window has an empty client area" }

$directory = Split-Path -Parent $OutputPath
if ($directory -and -not (Test-Path $directory)) { New-Item -ItemType Directory -Force -Path $directory | Out-Null }

$bitmap   = New-Object System.Drawing.Bitmap $w, $h
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h))
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

Write-Output "captured ${w}x${h} from '${ProcessName}' -> $OutputPath"
