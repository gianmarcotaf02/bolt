# ---------------------------------------------------------------------------
# Costruisce il pacchetto di Bolt. Due artefatti, in `out/` (esclusa dal
# controllo di versione):
#
#   Bolt-<versione>-win64-setup.exe     l'installer (Inno Setup 6)
#   Bolt-<versione>-win64-portable.zip  la stessa cosa da scompattare dove si
#                                       vuole, senza installare niente
#
# La versione non si scrive a mano: si legge da `CMakeLists.txt`, che e' dove il
# progetto la dichiara, e si passa a Inno con `/DAppVersion`. Lo script
# dell'installer (`installer/bolt.iss`) non la contiene.
#
# Dipendenza **di build**, non di runtime: Inno Setup 6 (`ISCC.exe`). Non serve a
# Bolt per funzionare, serve a produrre il setup. Il percorso si cerca nel
# registro dell'installazione e poi nei posti soliti; se non c'e', lo script lo
# dice e si ferma, invece di produrre un pacchetto a meta'.
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/make-installer.ps1
# ---------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $Configuration = 'RelWithDebInfo',
    [string] $OutputDir = 'out'
)

$ErrorActionPreference = 'Stop'
$radice = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $radice

function Passo($testo) { Write-Host "== $testo" }

# --- versione dal progetto ---------------------------------------------------
$cmake = Get-Content -LiteralPath 'CMakeLists.txt' -Raw
$match = [regex]::Match($cmake, 'project\(\s*Bolt\s+VERSION\s+([0-9.]+)', 'IgnoreCase')
if (-not $match.Success) { throw 'versione non trovata in CMakeLists.txt (project(Bolt VERSION ...))' }
$versione = $match.Groups[1].Value
Passo "Bolt $versione"

# --- cosa impacchettare -----------------------------------------------------
$build = Join-Path $radice "build/$Configuration"
$exe   = Join-Path $build 'Bolt.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "manca $exe : costruisci prima (cmake --build build)" }
foreach ($shader in @('ui.vert', 'ui.frag')) {
    if (-not (Test-Path -LiteralPath (Join-Path $build "shaders/$shader"))) {
        throw "manca build/$Configuration/shaders/$shader"
    }
}

$out = Join-Path $radice $OutputDir
if (-not (Test-Path -LiteralPath $out)) { New-Item -ItemType Directory -Force -Path $out | Out-Null }
$base = "Bolt-$versione-win64"

# Gli artefatti di un giro vecchio non restano in giro: un setup prodotto da un
# altro tentativo sembra una cosa da provare, e non lo e'.
Get-ChildItem -LiteralPath $out -Filter "Bolt-$versione-*" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @("$base-setup.exe", "$base-portable.zip") } |
    Remove-Item -Force -ErrorAction SilentlyContinue

# --- 1. lo zip portatile ----------------------------------------------------
# Layout **installato** (con `shaders` accanto all'exe): e' quello che Bolt cerca
# all'avvio, e cosi' lo zip si scompatta e si lancia, senza passaggi.
$staging = Join-Path $out "staging-$base"
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $staging 'shaders') | Out-Null
Copy-Item -LiteralPath $exe -Destination $staging
foreach ($shader in @('ui.vert', 'ui.frag')) {
    Copy-Item -LiteralPath (Join-Path $build "shaders/$shader") -Destination (Join-Path $staging 'shaders')
}
Copy-Item -LiteralPath (Join-Path $radice 'installer/README.txt') -Destination $staging

$zip = Join-Path $out "$base-portable.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
Passo ("zip:   {0}  ({1} MB)" -f $zip, [math]::Round((Get-Item $zip).Length / 1MB, 2))

# --- 2. l'installer (Inno Setup) -------------------------------------------
# Il percorso di ISCC: prima dal registro dell'installazione di Inno (dove dice
# lui di essere), poi dai posti in cui si installa di solito. `/CURRENTUSER`
# mette Inno sotto il profilo; la macchina di sviluppo qui lo ha li'.
$iscc = $null
foreach ($chiave in @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')) {
    if (Test-Path -LiteralPath $chiave) {
        $dove = (Get-ItemProperty -LiteralPath $chiave).InstallLocation
        if ($dove) {
            $candidato = Join-Path $dove 'ISCC.exe'
            if (Test-Path -LiteralPath $candidato) { $iscc = $candidato; break }
        }
    }
}
if (-not $iscc) {
    foreach ($cartella in @(
            (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6'))) {
        $candidato = Join-Path $cartella 'ISCC.exe'
        if (Test-Path -LiteralPath $candidato) { $iscc = $candidato; break }
    }
}
if (-not $iscc) {
    throw ('manca Inno Setup 6 (ISCC.exe). Non serve a Bolt per funzionare, serve a ' +
           'produrre il setup: https://jrsoftware.org/isdl.php - poi rilancia questo script.')
}

Passo "installer con $iscc"
# Il registro di ISCC contiene i percorsi assoluti di questa macchina: serve solo se
# qualcosa va storto, quindi si tiene in caso di errore e si toglie quando il setup e'
# buono. In `out/` restano i due file da dare a qualcuno, non i pezzi di lavorazione.
$log = Join-Path $out "iscc-$base.log"
& $iscc "/DAppVersion=$versione" "/O$out" (Join-Path $radice 'installer/bolt.iss') | Tee-Object -FilePath $log | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "ISCC ha fallito (exit $LASTEXITCODE): il registro sta in $log"
}
Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
$setup = Join-Path $out "$base-setup.exe"
if (-not (Test-Path -LiteralPath $setup)) { throw "ISCC non ha prodotto $setup" }
Passo ("setup: {0}  ({1} MB)" -f $setup, [math]::Round((Get-Item $setup).Length / 1MB, 2))

Write-Host ''
Write-Host 'Fatto.'
Write-Host "  $base-setup.exe      si installa (per utente, senza amministratore)"
Write-Host "  $base-portable.zip   si scompatta dove vuoi e si lancia Bolt.exe"
