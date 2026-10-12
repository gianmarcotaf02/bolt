# Trascina col tasto sinistro e fotografa a meta' gesto.
#
# Un trascinamento non e' un clic: e' un gesto continuo, e per guardare quello che
# si vede DURANTE il gesto — la destinazione evidenziata — bisogna fotografare
# mentre il tasto e' ancora giu'. Tenerlo premuto fra due script non si puo':
# qui il gesto e la cattura stanno nello stesso processo.
#
# Coordinate SCHERMO, non cliente: un trascinamento attraversa due finestre, e le
# coordinate cliente di una non vogliono dire niente per l'altra.
#
# Non tocca l'ordine delle finestre ne' il fuoco: portare in primo piano qualcosa
# mentre un trascinamento e' in corso lo annulla, e la prova misurerebbe un'altra
# cosa.
param(
    [Parameter(Mandatory = $true)][int]$FromX,
    [Parameter(Mandatory = $true)][int]$FromY,
    [Parameter(Mandatory = $true)][int]$ToX,
    [Parameter(Mandatory = $true)][int]$ToY,
    # Riquadro da salvare, in coordinate schermo. Vuoto = nessuna cattura.
    [string]$OutputPath = '',
    [int]$CropX = 0,
    [int]$CropY = 0,
    [int]$CropW = 0,
    [int]$CropH = 0,
    [int]$Steps = 20,
    [int]$HoldMs = 1200,
    [switch]$Cancel
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltDrag {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, IntPtr extra);

    public const uint MOVE      = 0x0001;
    public const uint LEFTDOWN  = 0x0002;
    public const uint LEFTUP    = 0x0004;

    public static void MoveTo(int x, int y) {
        SetCursorPos(x, y);
        // Anche un movimento di zero pixel: e' l'evento che la finestra riceve, e
        // senza evento il trascinamento non parte.
        mouse_event(MOVE, 0, 0, 0, IntPtr.Zero);
    }
}
'@

function Move-Steps([int]$fromX, [int]$fromY, [int]$toX, [int]$toY, [int]$steps) {
    for ($i = 1; $i -le $steps; $i++) {
        $x = [int]($fromX + ($toX - $fromX) * $i / $steps)
        $y = [int]($fromY + ($toY - $fromY) * $i / $steps)
        [BoltDrag]::MoveTo($x, $y)
        Start-Sleep -Milliseconds 25
    }
}

Write-Output "giu' in ($FromX,$FromY)"
[BoltDrag]::MoveTo($FromX, $FromY)
Start-Sleep -Milliseconds 200
[BoltDrag]::mouse_event([BoltDrag]::LEFTDOWN, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 200

Move-Steps $FromX $FromY $ToX $ToY $Steps
Start-Sleep -Milliseconds $HoldMs

if ($OutputPath -ne '' -and $CropW -gt 0 -and $CropH -gt 0) {
    $image = New-Object System.Drawing.Bitmap($CropW, $CropH)
    $graphics = [System.Drawing.Graphics]::FromImage($image)
    # Solo il riquadro chiesto: lo schermo intero contiene anche quello che non
    # riguarda la prova, e non si salva.
    $graphics.CopyFromScreen($CropX, $CropY, 0, 0, (New-Object System.Drawing.Size($CropW, $CropH)))
    $graphics.Dispose()
    $image.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $image.Dispose()
    Write-Output "cattura: $OutputPath ($CropX,$CropY ${CropW}x${CropH})"
}

if ($Cancel) {
    # Esc: il trascinamento si annulla e non succede niente. Il tasto va mandato
    # qui, altrimenti l'evento di rilascio concluderebbe il gesto.
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    Write-Output 'annullato'
}

[BoltDrag]::mouse_event([BoltDrag]::LEFTUP, 0, 0, 0, [IntPtr]::Zero)
Write-Output "rilasciato in ($ToX,$ToY)"
