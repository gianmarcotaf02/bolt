# Apre Esplora risorse su una cartella e lo mette di fianco a Bolt.
#
# Serve alle prove di trascinamento fra due programmi, che sono quelle che contano:
# un trascinamento dentro lo stesso programma non dice se il formato dei dati e'
# quello che il resto del mondo si aspetta.
#
# La cartella deve stare DENTRO il progetto: `AGENTS.md` §0 vieta di aprire o
# toccare cartelle personali, anche solo per una cattura.
#
# Il riquadro di navigazione di Esplora risorse mostra i nomi delle cartelle
# dell'utente: nei ritagli delle prove va tagliato via.
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [int]$X = 1400,
    [int]$Y = 0,
    [int]$Width = 520,
    [int]$Height = 1040
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltExplorer {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    public const uint NOZORDER = 0x0004;
}
'@

$path = (Resolve-Path $Directory).Path
Start-Process explorer.exe -ArgumentList $path
Start-Sleep -Seconds 4

$shell = New-Object -ComObject Shell.Application
$window = $shell.Windows() | Where-Object { $_.Document.Folder.Self.Path -eq $path } | Select-Object -First 1
if ($null -eq $window) {
    Write-Output 'finestra non trovata'
    exit 1
}

$handle = [IntPtr]$window.HWND
[void][BoltExplorer]::ShowWindow($handle, 9)   # SW_RESTORE, se era ingrandita
[void][BoltExplorer]::SetWindowPos($handle, [IntPtr]::Zero, $X, $Y, $Width, $Height, [BoltExplorer]::NOZORDER)
Write-Output "esplora risorse su '$Directory' a ($X,$Y) ${Width}x${Height}"
