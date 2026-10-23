# Confronto al pixel di due catture della stessa finestra.
#
# Serve alla regola di `AGENTS.md` sez. 2: uno spostamento di codice non si verifica
# "guardando che funziona", si verifica mostrando che **non e' cambiato niente**. Due
# catture allo stesso stato, stesso rettangolo, e il conto dei pixel diversi.
#
# Dice anche **dove** stanno i pixel diversi, perche' il numero da solo non basta:
# "486 pixel diversi" su una finestra di 1240x760 puo' essere un testo diverso o il
# bordo arrotondato che mostra il desktop dietro (`docs/05` sez. 5), e le due cose vogliono
# dire cose opposte.
#
#   powershell -ExecutionPolicy Bypass -File tools/diff-png.ps1 -A a.png -B b.png
#
# Esce con 0 se le due catture sono identiche, 1 se differiscono: cosi' si puo' usare
# dentro un copione senza leggere l'uscita.
param(
    [Parameter(Mandatory = $true)][string]$A,
    [Parameter(Mandatory = $true)][string]$B,
    # Che differenza e' accettabile: **zero** per impostazione predefinita, perche' uno
    # spostamento di codice non deve cambiare niente. Le due soglie esistono per il caso
    # che il confronto incontra sempre su questa macchina: il bordo arrotondato e le
    # dissolvenze lasciano vedere il desktop dietro, e un pixel di bordo puo' scostarsi
    # di **un** livello fra due avvii (`docs/05` sez. 5). Alzandole, si dice perche'.
    [int]$MaxPixels = 0,
    [int]$MaxDelta = 0
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$ia = [System.Drawing.Bitmap]::FromFile((Resolve-Path $A).Path)
$ib = [System.Drawing.Bitmap]::FromFile((Resolve-Path $B).Path)
try {
    if ($ia.Width -ne $ib.Width -or $ia.Height -ne $ib.Height) {
        "DIVERSE PER DIMENSIONE: {0}x{1} contro {2}x{3}" -f $ia.Width, $ia.Height, $ib.Width, $ib.Height
        exit 1
    }
    $w = $ia.Width
    $h = $ia.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $da = $ia.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $db = $ib.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $len = $w * $h * 4
    $bufA = New-Object byte[] $len
    $bufB = New-Object byte[] $len
    [System.Runtime.InteropServices.Marshal]::Copy($da.Scan0, $bufA, 0, $len)
    [System.Runtime.InteropServices.Marshal]::Copy($db.Scan0, $bufB, 0, $len)
    $ia.UnlockBits($da)
    $ib.UnlockBits($db)

    $diff = 0; $worst = 0
    $minX = $w; $maxX = -1; $minY = $h; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $w * 4
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + $x * 4
            $dB = [Math]::Abs($bufA[$i] - $bufB[$i])
            $dG = [Math]::Abs($bufA[$i + 1] - $bufB[$i + 1])
            $dR = [Math]::Abs($bufA[$i + 2] - $bufB[$i + 2])
            if ($dB -ne 0 -or $dG -ne 0 -or $dR -ne 0) {
                $diff++
                $m = [Math]::Max($dB, [Math]::Max($dG, $dR))
                if ($m -gt $worst) { $worst = $m }
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
}
finally {
    $ia.Dispose()
    $ib.Dispose()
}

if ($diff -eq 0) {
    "0 pixel diversi su {0}x{1}" -f $w, $h
    exit 0
}
$line = "{0} pixel diversi su {1}x{2}, scarto massimo {3} livelli, dentro x {4}..{5} y {6}..{7}" -f `
    $diff, $w, $h, $worst, $minX, $maxX, $minY, $maxY
if ($diff -le $MaxPixels -and $worst -le $MaxDelta) {
    "{0} (dentro la tolleranza: {1} pixel, {2} livelli)" -f $line, $MaxPixels, $MaxDelta
    exit 0
}
$line
exit 1
