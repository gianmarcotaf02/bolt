# Uno spostamento di codice si verifica cosi': la **stessa** sequenza di gesti su due
# eseguibili - quello di prima e quello nuovo - e ogni stato confrontato al pixel.
#
# Nasce dalla regola di `AGENTS.md` sez. 2 ("niente e' fatto finche' non e' visto") e dal
# taglio dell'input del 2026-10-23: un lavoro che **non deve cambiare niente** si prova
# mostrando che non ha cambiato niente, non guardando che funziona. Sei gesti che
# toccano i due moduli nuovi (un clic su una riga, `Ctrl`+clic, la rotella, `Invio` che
# entra in una cartella, il tasto indietro che ne riesce, le frecce, il menu da tastiera
# e quello dal tasto destro, il filtro).
#
#   powershell -ExecutionPolicy Bypass -File tools/pixel-sequence.ps1 `
#       -Exe build/RelWithDebInfo/Bolt.exe -Reference "C:\...\Bolt-vecchio.exe"
#
# L'eseguibile di riferimento viene **copiato** in `analysis/tmp/pixel-reference/` con un
# nome suo, perche' due processi con lo stesso nome non si distinguono e `focus-window`
# prenderebbe sempre il primo. La cartella e' usa e getta come il resto di `analysis/`.
#
# La prova apre **due finestre vere**, una dopo l'altra, sulla sandbox del progetto: si
# lancia solo quando chi lavora dice che la macchina e' libera (`AGENTS.md` sez. 0), e i due
# processi vengono chiusi alla fine, anche se qualcosa va storto.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Reference,
    # Il nome che avra' il processo dell'eseguibile nuovo: e' il nome del file, senza
    # estensione. Si dichiara solo se il file e' stato rinominato.
    [string]$ProcessName = '',
    [int]$Width = 1240,
    [int]$Height = 760,
    [int]$X = 120,
    [int]$Y = 120,
    # Le soglie del confronto: zero di default (uno spostamento di codice non deve
    # cambiare niente). Si alzano solo per il rumore noto del bordo arrotondato e delle
    # dissolvenze, e allora lo si dice qui, non nel giudizio finale.
    [int]$MaxPixels = 0,
    [int]$MaxDelta = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sandbox = Join-Path $root 'analysis\tmp\sandbox'
$outDir = Join-Path $root 'analysis\tmp'
$refDir = Join-Path $outDir 'pixel-reference'
Set-Location $root

if (-not (Test-Path $sandbox)) {
    throw "manca la sandbox: prima 'powershell -ExecutionPolicy Bypass -File tools/make-test-sandbox.ps1'"
}
if (-not (Test-Path $Exe)) { throw "eseguibile nuovo non trovato: $Exe" }
if (-not (Test-Path $Reference)) { throw "eseguibile di riferimento non trovato: $Reference" }

$exePath = (Resolve-Path $Exe).Path
$refPath = (Resolve-Path $Reference).Path
if (-not $ProcessName) { $ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($exePath) }

# Il riferimento copiato con un nome che non collide con l'altro processo.
New-Item -ItemType Directory -Force -Path $refDir | Out-Null
$refCopy = Join-Path $refDir 'Bolt-rif.exe'
Copy-Item $refPath $refCopy -Force
$refName = 'Bolt-rif'
if ($refName -eq $ProcessName) { throw "il nome del processo nuovo e' 'Bolt-rif': scegline un altro con -ProcessName" }

function Shot($tag, $name) {
    $path = Join-Path $outDir ("pixel-{0}-{1}.png" -f $tag, $name)
    if (Test-Path $path) { Remove-Item $path -Force }   # una cattura vecchia non vale come nuova
    # Il puntatore fuori dalla finestra prima di ogni cattura: l'evidenziazione sotto il
    # cursore e' uno stato come un altro, e lasciarla dov'e' fa misurare il posto dove
    # stava il puntatore invece del codice (docs/05 sez. 5: due catture si confrontano
    # solo a parita' di stato). Uscendo dalla finestra arriva `WM_MOUSELEAVE`, che e'
    # codice spostato anche lui.
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'send-mouse.ps1') -Action move -X -60 -Y -60 -ProcessName $tag | Out-Null
    # `capture-window.ps1` si rifiuta se la finestra non e' in primo piano, e dopo un
    # gesto sintetico puo' non esserlo: si riprova, invece di lasciare un buco nella
    # serie che poi il confronto legge come differenza.
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'focus-window.ps1') -ProcessName $tag | Out-Null
        Start-Sleep -Milliseconds 300
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture-window.ps1') `
            -ProcessName $tag -OutputPath $path -SettleMs 2500 -ClientOnly | Out-Null
        if (Test-Path $path) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "non sono riuscito a catturare lo stato '$name' di '$tag'"
}
function Key($action, $text = '') {
    if ($text -ne '') {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'send-keys.ps1') -Action $action -Text $text -ProcessName $script:current | Out-Null
    } else {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'send-keys.ps1') -Action $action -ProcessName $script:current | Out-Null
    }
    Start-Sleep -Milliseconds 400
}
function Click($action, $x, $y, $clicks = 1, [switch]$Ctrl) {
    $arguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'send-mouse.ps1'),
                   '-Action', $action, '-X', $x, '-Y', $y, '-Clicks', $clicks, '-ProcessName', $script:current)
    if ($Ctrl) { $arguments += '-Ctrl' }
    & powershell @arguments | Out-Null
    Start-Sleep -Milliseconds 400
}

# La sequenza: un nome per stato, e i gesti che ci portano. Gli stati si catturano
# **nello stesso ordine** nei due giri, o il confronto misura due cose diverse.
$steps = @(
    @{ Name = '01-lista';        Do = { } },
    @{ Name = '02-riga';         Do = { Click 'left' 400 160 } },
    @{ Name = '03-ctrl-clic';    Do = { Click 'left' 400 190 1 -Ctrl } },
    @{ Name = '04-rotella';      Do = { Click 'wheel' 400 200 } },
    @{ Name = '05-dentro';       Do = { Key 'enter'; Start-Sleep -Milliseconds 700 } },
    @{ Name = '06-indietro';     Do = { Key 'backspace' '1'; Start-Sleep -Milliseconds 700 } },
    @{ Name = '07-tre-giu';      Do = { Key 'down'; Key 'down'; Key 'down' } },
    @{ Name = '08-menu-tastiera'; Do = { Key 'apps' } },
    @{ Name = '09-menu-mouse';   Do = { Key 'escape'; Click 'right' 400 160 } },
    @{ Name = '10-filtro';       Do = { Key 'escape'; Key 'text' 'vo' } }
)

function Run-Sequence($exe, $tag) {
    $script:current = $tag
    Get-Process -Name $tag -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    Start-Process -FilePath $exe -ArgumentList ('"' + $sandbox + '"') -WorkingDirectory $root
    Start-Sleep -Seconds 4
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'focus-window.ps1') `
        -ProcessName $tag -X $X -Y $Y -Width $Width -Height $Height | Out-Null
    Start-Sleep -Milliseconds 800
    foreach ($step in $steps) {
        & $step.Do
        Shot $tag $step.Name
    }
    Get-Process -Name $tag -ErrorAction SilentlyContinue | Stop-Process -Force
}

$verdict = 1
try {
    "1/3 la sequenza sull'eseguibile nuovo ($ProcessName)"
    Run-Sequence $exePath $ProcessName
    "2/3 la stessa sequenza sul riferimento ($refName)"
    Run-Sequence $refCopy $refName
    "3/3 confronto al pixel"
    $failures = 0
    $failed = @()
    foreach ($step in $steps) {
        $a = Join-Path $outDir ("pixel-{0}-{1}.png" -f $ProcessName, $step.Name)
        $b = Join-Path $outDir ("pixel-{0}-{1}.png" -f $refName, $step.Name)
        $line = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'diff-png.ps1') `
            -A $a -B $b -MaxPixels $MaxPixels -MaxDelta $MaxDelta
        $same = ($LASTEXITCODE -eq 0)
        if (-not $same) { $failures++; $failed += $step.Name }
        "{0}  {1,-16} {2}" -f $(if ($same) { 'uguale ' } else { 'DIVERSO' }), $step.Name, $line
    }
    if ($failures -gt 0) {
        "stati diversi: {0} su {1} - le catture sono in analysis/tmp/pixel-*.png" -f $failures, $steps.Count
        if ($failed -contains $steps[0].Name) {
            # Il primo stato non ha gesti prima di se': se e' diverso, la differenza non
            # e' del codice ma del banco (qualcuno ha toccato il mouse o la tastiera,
            # oppure una cattura e' andata a vuoto). Dirlo, invece di lasciarlo dedurre.
            "  nota: **il primo stato**, che non ha gesti prima, e' gia' diverso: il banco"
            "  era disturbato o una cattura non e' andata a buon fine. Si rilancia a macchina ferma."
        }
    }
    else {
        "tutti e {0} gli stati sono identici al pixel" -f $steps.Count
        $verdict = 0
    }
}
finally {
    # I due processi si chiudono **sempre**, anche se un passo e' fallito: una finestra
    # di prova lasciata aperta e' quella che poi si trova davanti chi lavora.
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Name $refName -ErrorAction SilentlyContinue | Stop-Process -Force
}
# L'uscita si decide **dopo** la pulizia, o il codice d'uscita lo sceglie il `finally`.
exit $verdict
