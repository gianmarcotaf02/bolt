# Dove sta la finestra di Bolt, sullo schermo e in coordinate cliente.
#
# Serve prima di ogni prova col mouse: la finestra non si apre sempre nello stesso
# posto (`CW_USEDEFAULT` la sposta a ogni avvio), e le coordinate cliente che
# `send-mouse.ps1` vuole non si indovinano — si misurano. Con un'origine sbagliata
# un clic finisce su un'altra riga e la prova misura un'altra cosa.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltRect {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, ref RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, ref RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
}
'@

$process = Get-Process Bolt -ErrorAction Stop | Select-Object -First 1
$handle = $process.MainWindowHandle
$window = New-Object BoltRect+RECT; [void][BoltRect]::GetWindowRect($handle, [ref]$window)
$client = New-Object BoltRect+RECT; [void][BoltRect]::GetClientRect($handle, [ref]$client)
$origin = New-Object BoltRect+POINT; [void][BoltRect]::ClientToScreen($handle, [ref]$origin)

"finestra: ($($window.L),$($window.T))-($($window.R),$($window.B))"
"cliente:  origine ($($origin.X),$($origin.Y))  dimensione $($client.R)x$($client.B)"
