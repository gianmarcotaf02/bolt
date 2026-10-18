# Invia tasti a una finestra di Bolt.
#
# Serve perche' SendKeys non da' il focus a una finestra OpenGL e la
# backtick-escape di PowerShell dentro bash mangia i caratteri: uno script su
# file e' l'unico modo affidabile di ripetere queste prove.
param(
    [Parameter(Mandatory = $true)][string]$Action,
    [string]$Text = ''
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltKeys {
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();

    public const uint WM_CHAR = 0x0102;
    public const uint WM_KEYDOWN = 0x0100;

    public static void Key(byte vk) {
        keybd_event(vk, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        keybd_event(vk, 0, 2, IntPtr.Zero);
        System.Threading.Thread.Sleep(120);
    }

    public static void Chord(byte mod, byte mod2, byte key) {
        keybd_event(mod, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(70);
        if (mod2 != 0) { keybd_event(mod2, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(70); }
        keybd_event(key, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(70);
        keybd_event(key, 0, 2, IntPtr.Zero); System.Threading.Thread.Sleep(70);
        if (mod2 != 0) { keybd_event(mod2, 0, 2, IntPtr.Zero); System.Threading.Thread.Sleep(70); }
        keybd_event(mod, 0, 2, IntPtr.Zero); System.Threading.Thread.Sleep(150);
    }
}
'@

$process = Get-Process Bolt -ErrorAction Stop | Select-Object -First 1
$handle = $process.MainWindowHandle

[void][BoltKeys]::SetWindowPos($handle, [IntPtr](-1), 0, 0, 0, 0, 3)
[void][BoltKeys]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 700

if ([BoltKeys]::GetForegroundWindow() -ne $handle) {
    Write-Output 'ATTENZIONE: la finestra non e'' in primo piano'
}

switch ($Action) {
    'text' {
        foreach ($ch in $Text.ToCharArray()) {
            [void][BoltKeys]::SendMessageW($handle, [BoltKeys]::WM_CHAR, [IntPtr][int]$ch, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 40
        }
    }
    'backspace' {
        $count = [int]$Text
        for ($i = 0; $i -lt $count; $i++) { [BoltKeys]::Key(0x08) }
    }
    'enter'  { [BoltKeys]::Key(0x0D) }
    'escape' { [BoltKeys]::Key(0x1B) }
    'delete' { [BoltKeys]::Key(0x2E) }
    'f2'     { [BoltKeys]::Key(0x71) }
    'f4'     { [BoltKeys]::Key(0x73) }
    'f5'     { [BoltKeys]::Key(0x74) }
    'down'   { [BoltKeys]::Key(0x28) }
    'up'     { [BoltKeys]::Key(0x26) }
    'left'   { [BoltKeys]::Key(0x25) }
    'right'  { [BoltKeys]::Key(0x27) }
    # Tasto Menu: apre il menu contestuale sulla riga del cursore.
    'apps'   { [BoltKeys]::Key(0x5D) }
    'home'   { [BoltKeys]::Key(0x24) }
    'end'    { [BoltKeys]::Key(0x23) }
    # Alt+sinistra arriva all'applicazione come WM_SYSKEYDOWN, non WM_KEYDOWN:
    # la finestra ha un menu di sistema e Alt lo dirotta. keybd_event genera il
    # messaggio giusto da solo, perche' Alt e' premuto davvero.
    'alt+left'  { [BoltKeys]::Chord(0x12, 0, 0x25) }
    'alt+right' { [BoltKeys]::Chord(0x12, 0, 0x27) }
    'alt+up'    { [BoltKeys]::Chord(0x12, 0, 0x26) }
    default {
        if ($Action -match '^ctrl\+([a-z])$') {
            [BoltKeys]::Chord(0x11, 0, [byte][char]$Matches[1].ToUpper())
        }
        elseif ($Action -match '^ctrl\+([0-9])$') {
            # Le cifre sono codici `VK_*` come le lettere: `Ctrl+1..4` sceglie la
            # vista, e senza questa riga la prova non premeva niente (e sembrava che
            # la scorciatoia non funzionasse).
            [BoltKeys]::Chord(0x11, 0, [byte][char]$Matches[1])
        }
        elseif ($Action -eq 'ctrl+comma') {
            # La virgola e' VK_OEM_COMMA: non e' una lettera, quindi non entra
            # nella regola generale. E' la scorciatoia delle opzioni.
            [BoltKeys]::Chord(0x11, 0, 0xBC)
        }
        elseif ($Action -eq 'ctrl+oem1') {
            # VK_OEM_1, il punto e virgola. E' il tasto che serve a provare che anche
            # la punteggiatura si puo' assegnare in una mappa dei tasti: non e' una
            # lettera, quindi non entra nella regola generale come non ci entra la
            # virgola.
            [BoltKeys]::Chord(0x11, 0, 0xBA)
        }
        elseif ($Action -eq 'oem1') {
            # Lo stesso tasto **senza** modificatori: e' il caso che deve essere
            # rifiutato, perche' sciolto lo legge la casella di testo.
            [BoltKeys]::Key(0xBA)
        }
        elseif ($Action -match '^ctrl\+shift\+([a-z])$') {
            [BoltKeys]::Chord(0x11, 0x10, [byte][char]$Matches[1].ToUpper())
        }
        elseif ($Action -match '^shift\+(\w+)$') {
            switch ($Matches[1]) {
                'delete' { [BoltKeys]::Chord(0x10, 0, 0x2E) }
                default  { Write-Output "azione sconosciuta: $Action" }
            }
        }
        else {
            Write-Output "azione sconosciuta: $Action"
        }
    }
}

Write-Output "ok: $Action"
