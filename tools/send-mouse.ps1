# Invia clic e rotella a una finestra di Bolt, in coordinate CLIENTE.
#
# Serve perche' il menu contestuale si apre col tasto destro e nessun tasto lo
# sostituisce davvero: senza un iniettore di mouse la via del puntatore — che e'
# quella che usa la gente — resta non verificata. Le coordinate sono relative
# all'area cliente, cosi' una prova si ripete anche se la finestra si sposta.
#
# Le coordinate vengono convertite in coordinate schermo con ClientToScreen: se
# la finestra non e' in primo piano il clic va a finire sopra qualcun altro.
param(
    [Parameter(Mandatory = $true)][ValidateSet('right', 'left', 'move', 'wheel')][string]$Action,
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Clicks = 1
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltMouse {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, IntPtr extra);

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }

    public const uint MOVE      = 0x0001;
    public const uint LEFTDOWN  = 0x0002;
    public const uint LEFTUP    = 0x0004;
    public const uint RIGHTDOWN = 0x0008;
    public const uint RIGHTUP   = 0x0010;
    public const uint WHEEL     = 0x0800;

    public static POINT Screen(IntPtr h, int x, int y) {
        POINT p = new POINT();
        p.X = x; p.Y = y;
        ClientToScreen(h, ref p);
        return p;
    }

    public static void Click(uint down, uint up) {
        mouse_event(down, 0, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(40);
        mouse_event(up, 0, 0, 0, IntPtr.Zero);
    }
}
'@

$process = Get-Process Bolt -ErrorAction Stop | Select-Object -First 1
$handle = $process.MainWindowHandle

[void][BoltMouse]::SetWindowPos($handle, [IntPtr](-1), 0, 0, 0, 0, 3)
[void][BoltMouse]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 700

if ([BoltMouse]::GetForegroundWindow() -ne $handle) {
    Write-Output 'ATTENZIONE: la finestra non e'' in primo piano'
}

$point = [BoltMouse]::Screen($handle, $X, $Y)
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($point.X, $point.Y)
Start-Sleep -Milliseconds 150

switch ($Action) {
    'move'  { [BoltMouse]::mouse_event([BoltMouse]::MOVE, 0, 0, 0, [IntPtr]::Zero) }
    'right' { for ($i = 0; $i -lt $Clicks; $i++) { [BoltMouse]::Click([BoltMouse]::RIGHTDOWN, [BoltMouse]::RIGHTUP) } }
    'left'  { for ($i = 0; $i -lt $Clicks; $i++) { [BoltMouse]::Click([BoltMouse]::LEFTDOWN,  [BoltMouse]::LEFTUP) } }
    # Clicks e' il numero di tacche, il segno e' la direzione: positivo in su.
    'wheel' { [BoltMouse]::mouse_event([BoltMouse]::WHEEL, 0, 0, $Clicks * 120, [IntPtr]::Zero) }
}

Write-Output "ok: $Action ($X,$Y) -> schermo ($($point.X),$($point.Y))"
