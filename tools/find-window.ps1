# Cerca le finestre di QUALUNQUE processo il cui titolo contenga una parola nota.
#
# Serve quando l'effetto di un'azione e' una finestra di dialogo: le proprieta' di
# Windows, un avviso, una richiesta di conferma. Catturare lo schermo mostrerebbe
# il desktop di chi lavora — vietato da §0 di AGENTS.md — e la finestra potrebbe
# stare dietro a quella dell'applicazione (succede: `capture-window.ps1` mette
# Bolt in primo piano). Il titolo invece si legge dal processo, e si legge anche
# se la finestra e' invisibile o coperta: e' la stessa tecnica che ha trovato
# `OperationStatusWindow` (AGENTS.md §5).
#
# Per non stampare titoli che possono contenere nomi di file personali si
# stampano solo la CLASSE e la visibilita', piu' il conteggio.
param(
    [Parameter(Mandatory = $true)][string]$Needle,
    [switch]$Close
)

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindWindow {
    public delegate bool Proc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Proc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);

    public static List<string> Found = new List<string>();
    public static List<IntPtr> Handles = new List<IntPtr>();

    public static void Search(string needle, bool close) {
        Found.Clear();
        Handles.Clear();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            StringBuilder title = new StringBuilder(512);
            GetWindowTextW(h, title, 512);
            if (title.ToString().IndexOf(needle, StringComparison.OrdinalIgnoreCase) >= 0) {
                StringBuilder cls = new StringBuilder(128);
                GetClassNameW(h, cls, 128);
                Found.Add((IsWindowVisible(h) ? "visibile  " : "nascosta  ") + cls.ToString());
                Handles.Add(h);
            }
            return true;
        }, IntPtr.Zero);

        if (!close) return;
        foreach (IntPtr h in Handles) {
            // La finestra dell'applicazione contiene anche lei la parola cercata:
            // chiudendo tutto si chiude Bolt insieme al dialogo. E' successo.
            StringBuilder cls = new StringBuilder(128);
            GetClassNameW(h, cls, 128);
            if (cls.ToString() == "BoltWindow") continue;
            SendMessageW(h, 0x0010 /* WM_CLOSE */, IntPtr.Zero, IntPtr.Zero);
        }
    }
}
'@

[FindWindow]::Search($Needle, [bool]$Close)
"finestre col titolo che contiene '$Needle': $([FindWindow]::Found.Count)"
[FindWindow]::Found
