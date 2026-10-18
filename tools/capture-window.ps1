<#
.SYNOPSIS
    Captures one window to a PNG.

.DESCRIPTION
    Screen-capture based, so it works for OpenGL windows where PrintWindow
    returns an empty surface. Brings the window to the foreground first, waits
    for the frame to settle, then reads the region from the desktop.

    Captures the WHOLE window by default, not just the client area. That is
    deliberate: capturing only the client area hides the non-client frame, and
    a defect lived there for the whole of M6 without ever showing up in a
    capture — two title bars, one system and one drawn by the application. The
    default now shows what the user sees. Use -ClientOnly when a pixel-exact
    comparison of the client area is what is wanted.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 `
        -ProcessName Bolt -OutputPath analysis/tmp/shot.png
#>
[CmdletBinding()]
param(
    [string]$ProcessName = 'Bolt',
    [string]$OutputPath  = 'analysis\tmp\shot.png',
    [int]$SettleMs = 1400,
    [switch]$ClientOnly
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
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
'@
}

$process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw "process '$ProcessName' is not running" }

$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) { throw "process '$ProcessName' has no main window yet" }

if ([Win32Capture]::IsIconic($handle)) { [void][Win32Capture]::ShowWindow($handle, 9) }  # SW_RESTORE

# `SetForegroundWindow` **non basta**: Windows lo rifiuta quando il primo piano ce l'ha
# un'altra applicazione, e la chiamata riesce lo stesso. Serve `AttachThreadInput` al
# thread che possiede il primo piano (e' quello che fa `focus-window.ps1`).
if ([Win32Capture]::GetForegroundWindow() -ne $handle) {
    $foregroundThread = [Win32Capture]::GetWindowThreadProcessId([Win32Capture]::GetForegroundWindow(), [IntPtr]::Zero)
    $mine = [Win32Capture]::GetCurrentThreadId()
    [void][Win32Capture]::AttachThreadInput($mine, $foregroundThread, $true)
    [void][Win32Capture]::SetForegroundWindow($handle)
    [void][Win32Capture]::AttachThreadInput($mine, $foregroundThread, $false)
}
Start-Sleep -Milliseconds $SettleMs

# E se il primo piano e' ancora di qualcun altro, **non si cattura**: quello che sta
# sopra finirebbe dentro il rettangolo della finestra, e una cattura cosi' e' la
# schermata di chi lavora, non una prova. E' successo il 2026-10-18, ed e' la ragione
# di queste righe.
if ([Win32Capture]::GetForegroundWindow() -ne $handle) {
    throw "la finestra di '$ProcessName' non e' in primo piano: non catturo (sopra c'e' un'altra finestra)"
}

if ($ClientOnly) {
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
