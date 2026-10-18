# Porta una finestra in primo piano, e la ripristina se e' ridotta.
#
# Serve alle prove che hanno bisogno di **due** finestre — un trascinamento fra Bolt
# ed Esplora risorse, per esempio — e serve perche' `SetForegroundWindow` da solo
# **non basta**: Windows lo rifiuta (o lo esegue a meta') quando il primo piano ce
# l'ha un'altra applicazione e il "blocco del primo piano" e' attivo, che e' lo stato
# normale dopo pochi secondi che il puntatore non tocca niente. Il sintomo e' il
# peggiore possibile: la chiamata **riesce**, la finestra non viene in primo piano, e
# i tasti e i clic successivi vanno da un'altra parte. Una prova cosi' sembra
# un'applicazione che non risponde.
#
# AttachThreadInput al thread che possiede il primo piano e' il modo documentato di
# vincere il blocco: finche' i due thread sono attaccati, il primo piano si puo'
# cambiare. Va staccato subito dopo, o il primo piano del sistema resta legato al
# thread di questo script.
#
# **Da non usare durante un trascinamento**: portare in primo piano qualcosa a gesto
# in corso lo annulla (`tools/drag-mouse.ps1` non tocca l'ordine delle finestre per
# questo motivo). Si usa *prima* del gesto, non durante.
#
# Nota per chi lavora con una persona davanti: questa e' l'unica cosa di `tools/` che
# **cambia quello che si vede sullo schermo**. Chi sta usando il computer per altro se
# lo ritrova davanti, e su Windows 11 il cambio di primo piano puo' perfino riportare
# la scrivania virtuale su quella della finestra attivata.
#
# Per questo lo strumento **si rifiuta** di agire se la finestra non e' sulla scrivania
# virtuale corrente (`IVirtualDesktopManager`, `IsWindowOnCurrentVirtualDesktop`): li'
# vuol dire che chi lavora e' su un'altra, e portarla davanti gliela strapperebbe. Il
# caso "non e' sulla scrivania corrente" non e' stato provato — si prova solo cambiando
# scrivania, e chi lavora e' li' — mentre il caso positivo si'.
param(
    [string]$ProcessName = 'Bolt',
    # Riquadro dove rimettere la finestra (cliente = schermo per una finestra senza
    # bordo come quella di Bolt). Vuoto: non spostarla.
    [int]$X = -1,
    [int]$Y = -1,
    [int]$Width = 0,
    [int]$Height = 0
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoltFocus {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after,
                                                                     int x, int y, int cx, int cy,
                                                                     uint flags);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}

// La scrivania virtuale su cui sta una finestra. L'interfaccia non e' documentata per
// l'uso diretto, ma e' stabile da Windows 10, e qui serve solo la prima domanda.
[ComImport, Guid("AA509086-5CA9-4C25-8F95-589D3C07B48A")]
public class VirtualDesktopManager { }

[ComImport, Guid("a5cd92ff-29be-454c-8d04-d82879fb3f1b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IVirtualDesktopManager
{
    [PreserveSig] int IsWindowOnCurrentVirtualDesktop(IntPtr window, out int onCurrentDesktop);
    [PreserveSig] int GetWindowDesktopId(IntPtr window, out Guid desktopId);
    [PreserveSig] int MoveWindowToDesktop(IntPtr window, ref Guid desktopId);
}
'@

$process = Get-Process $ProcessName -ErrorAction Stop | Select-Object -First 1
$handle = $process.MainWindowHandle

# Prima di toccare il primo piano: la finestra e' sulla scrivania che si sta guardando?
# Se non lo e', chi lavora e' altrove, e portarla davanti lo riporterebbe qui.
$onCurrent = 1
$checked = $false
try {
    $desktops = New-Object VirtualDesktopManager
    $value = 0
    if ($desktops.IsWindowOnCurrentVirtualDesktop($handle, [ref]$value) -eq 0) {
        $onCurrent = $value
        $checked = $true
    }
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($desktops)
} catch {
    # Se la domanda non si puo' fare si procede: meglio una prova che rischia di
    # spostare la vista che una prova che non parte mai.
}
if ($checked -and $onCurrent -eq 0) {
    Write-Output 'la finestra non e'' sulla scrivania corrente: non la porto davanti'
    exit 2
}

[void][BoltFocus]::ShowWindow($handle, 9)   # SW_RESTORE: se era ridotta, non e' in primo piano
if ($Width -gt 0 -and $Height -gt 0) {
    # Senza SWP_NOZORDER|SWP_NOACTIVATE: qui il punto e' proprio di portarla davanti.
    [void][BoltFocus]::SetWindowPos($handle, [IntPtr]::Zero, $X, $Y, $Width, $Height, 0x0040)
}

$foreground = [BoltFocus]::GetForegroundWindow()
$foregroundThread = [BoltFocus]::GetWindowThreadProcessId($foreground, [IntPtr]::Zero)
$mine = [BoltFocus]::GetCurrentThreadId()

[void][BoltFocus]::AttachThreadInput($mine, $foregroundThread, $true)
[void][BoltFocus]::BringWindowToTop($handle)
[void][BoltFocus]::SetForegroundWindow($handle)
[void][BoltFocus]::AttachThreadInput($mine, $foregroundThread, $false)

Start-Sleep -Milliseconds 500
$ok = ([BoltFocus]::GetForegroundWindow() -eq $handle)
Write-Output "in primo piano: $ok"
if (-not $ok) {
    Write-Output 'ATTENZIONE: il primo piano non e'' cambiato; i tasti successivi andranno altrove'
    exit 1
}
