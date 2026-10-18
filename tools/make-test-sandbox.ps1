<#
.SYNOPSIS
    Crea la cartella di prova usa e getta per le verifiche.

.DESCRIPTION
    Le prove si fanno QUI, mai nelle cartelle reali di chi lavora — nemmeno solo
    per una cattura, perche' uno screenshot di una cartella vera ne contiene i
    nomi dei file. Questa e' la regola 0 di AGENTS.md, e questo script esiste per
    renderla comoda oltre che obbligatoria.

    Il contenuto e' finto, deterministico e sempre uguale: due cartelle, qualche
    file di testo, un CSV e qualche PNG di colori distinti (sono le anteprime da
    guardare). Cosi' una prova ripetuta due volte
    parte dalle stesse condizioni, e il risultato e' confrontabile.

    La cartella e' sotto analysis/, che e' esclusa dal controllo di versione:
    niente di tutto questo finisce nel repository.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/make-test-sandbox.ps1
    # -> analysis/tmp/sandbox
#>
[CmdletBinding()]
param(
    [string]$Path = 'analysis\tmp\sandbox',
    [switch]$Keep   # non svuota la cartella prima di ricrearla
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$sandbox = Join-Path $root $Path

if ((Test-Path $sandbox) -and -not $Keep) {
    Remove-Item -Recurse -Force $sandbox
}

New-Item -ItemType Directory -Force -Path $sandbox          | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'documenti') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'immagini')  | Out-Null

# Contenuto fisso: nomi e dimensioni sempre uguali, cosi' i confronti fra due
# prove parlano delle modifiche al programma e non dei dati.
Set-Content -Path (Join-Path $sandbox 'nota.txt')      -Value "nota di prova`nriga due`n"           -NoNewline
Set-Content -Path (Join-Path $sandbox 'relazione.txt') -Value ("riga di prova`n" * 40)                -NoNewline
Set-Content -Path (Join-Path $sandbox 'dati.csv')      -Value "colonna,due,tre`n1,2,3`n4,5,6`n"      -NoNewline
Set-Content -Path (Join-Path $sandbox 'documenti\contratto.txt') -Value "documento di prova`n"       -NoNewline
Set-Content -Path (Join-Path $sandbox 'documenti\appunti.txt')   -Value "appunti di prova`n"         -NoNewline

# Una PNG piccola, per provare le anteprime senza usare immagini vere.
Add-Type -AssemblyName System.Drawing
$bitmap = New-Object System.Drawing.Bitmap 64, 64
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(76, 141, 255))   # l'accento di Bolt
$graphics.Dispose()
$bitmap.Save((Join-Path $sandbox 'immagini\riquadro.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

# Qualche anteprima in piu', di **colori distinti**: servono a guardare le miniature
# — nella griglia dell'ispettore e nella lista — senza usare immagini di qualcuno. Un
# colore diverso per file e' quello che rende visibile a colpo d'occhio una cella
# vuota, una miniatura sbagliata o una al posto di un'altra.
foreach ($item in @(
    @('caldo.png',  230, 108,  90),
    @('verde.png',   76, 200, 141),
    @('scuro.png',   35,  36,  40))) {
    $bmp = New-Object System.Drawing.Bitmap 64, 64
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb($item[1], $item[2], $item[3]))
    $g.Dispose()
    $bmp.Save((Join-Path $sandbox "immagini\$($item[0])"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# Una sottocartella e un testo dentro `immagini`, perche' una griglia di sole
# miniature non mostra i due casi che si vedono piu' spesso: una cartella e un file
# senza niente da mostrare.
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'immagini\vecchie') | Out-Null
Set-Content -Path (Join-Path $sandbox 'immagini\didascalia.txt') -Value "didascalia di prova`n" -NoNewline
$bmp = New-Object System.Drawing.Bitmap 64, 64
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(145, 149, 160))
$g.Dispose()
$bmp.Save((Join-Path $sandbox 'immagini\vecchie\sbiadita.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# Una cartella **vuota** e una con piu' voci di quante ne stiano in griglia: sono i
# due casi che l'anteprima dell'ispettore deve saper dire («(vuota)» e «e altre
# voci»), e senza di loro si provano solo i casi comodi.
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'vuota') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'molte') | Out-Null
1..15 | ForEach-Object {
    Set-Content -Path (Join-Path $sandbox ("molte\voce-{0:d2}.txt" -f $_)) -Value "x" -NoNewline
}

Write-Output "cartella di prova pronta: $sandbox"
Get-ChildItem -Recurse $sandbox | ForEach-Object {
    $relative = $_.FullName.Substring($sandbox.Length + 1)
    if ($_.PSIsContainer) { Write-Output "  [dir]  $relative" }
    else                  { Write-Output ("  {0,7} {1}" -f $_.Length, $relative) }
}
