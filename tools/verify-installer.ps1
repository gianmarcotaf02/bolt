# ---------------------------------------------------------------------------
# Prova il pacchetto **installandolo davvero**, e poi togliendolo.
#
# Non guarda l'installer da fuori: lo esegue. Fa quello che farebbe l'utente, con
# tre accortezze per non lasciare tracce:
#
#   * installa in una cartella di prova dentro `analysis/tmp`, non nella
#     destinazione vera (`/DIR=`);
#   * mette il gruppo del menu Start in una cartella di prova (`/GROUP=`), che la
#     disinstallazione toglie;
#   * la voce in "App e funzionalita'" e' quella vera (Inno la scrive in HKCU), e
#     resta per i secondi della prova: la verifica che ci sia e la disinstallazione
#     la toglie, poi si controlla che non ci sia piu'.
#
# Controlla anche la cosa piu' facile da sbagliare: che l'installazione **non
# tocchi** `%APPDATA%\Bolt\settings.ini` — ne' nel contenuto ne' nella data.
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/verify-installer.ps1
# ---------------------------------------------------------------------------
[CmdletBinding()]
param([string] $Setup = '')

$ErrorActionPreference = 'Stop'
$radice = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $radice

$versione = [regex]::Match((Get-Content 'CMakeLists.txt' -Raw),
    'project\(\s*Bolt\s+VERSION\s+([0-9.]+)', 'IgnoreCase').Groups[1].Value
if ([string]::IsNullOrWhiteSpace($Setup)) { $Setup = "out/Bolt-$versione-win64-setup.exe" }

$prova = Join-Path $radice 'analysis/tmp/install-prova'
$esiti = New-Object System.Collections.ArrayList
function Esito($nome, $ok, $dettaglio = '') {
    $segno = if ($ok) { 'OK  ' } else { 'NO  ' }
    [void]$esiti.Add($ok)
    Write-Host ("{0}{1}{2}" -f $segno, $nome, $(if ($dettaglio) { "  -> $dettaglio" } else { '' }))
}

if (-not (Test-Path -LiteralPath $Setup)) { throw "manca $Setup : lancia prima tools/make-installer.ps1" }

# La prova crea un collegamento nel menu Start **vero** (Inno ignora /GROUP quando la
# pagina del gruppo e' spenta, e la pagina e' spenta apposta), quindi se Bolt e' gia'
# installato si ferma invece di sovrascrivere l'installazione di qualcuno: meglio un
# test che si rifiuta che un test che distrugge.
$radiciReg = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')
$giaInstallato = @($radiciReg | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -ErrorAction SilentlyContinue } |
    Where-Object { (Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue).DisplayName -eq 'Bolt' })
if ($giaInstallato.Count -gt 0) { throw 'Bolt risulta gia installato: disinstallalo prima di provare il pacchetto' }

# --- 0. lo stato delle preferenze, prima --------------------------------
$preferenze = Join-Path $env:APPDATA 'Bolt/settings.ini'
$prefPrima = $null
if (Test-Path -LiteralPath $preferenze) {
    $prefPrima = (Get-Item -LiteralPath $preferenze).LastWriteTimeUtc
}

# --- 1. installazione silenziosa ---------------------------------------------
# Tutto quello che segue lascia qualcosa sul sistema (una cartella, un collegamento,
# una chiave di registro): `finally` in fondo lo toglie anche se un controllo
# fallisce a meta'. Una prova che lascia sporco e' una prova che il giro dopo
# trova gia' installato, e si rifiuta di partire.
try {
# Un Bolt rimasto aperto da un giro interrotto tiene l exe occupato, e la cartella di
# prova non si cancella.
Get-Process Bolt -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
if (Test-Path -LiteralPath $prova) { Remove-Item -LiteralPath $prova -Recurse -Force }
New-Item -ItemType Directory -Force -Path $prova | Out-Null

$destinazione = Join-Path $prova 'Bolt installato'
$gruppo       = 'Bolt'
$log          = Join-Path $prova 'setup.log'
# `/CURRENTUSER` forza l'installazione per-utente, come il default dello script:
# nessuna richiesta di amministratore, nessuna cartella di sistema toccata.
$argomenti = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
               "/DIR=`"$destinazione`"", "/GROUP=`"$gruppo`"", "/LOG=`"$log`"")
$p = Start-Process -FilePath $Setup -ArgumentList $argomenti -Wait -PassThru
Esito 'il setup installa senza errori' ($p.ExitCode -eq 0) "exit $($p.ExitCode), log: $log"

# --- 2. cosa c'e' ------------------------------------------------------------
$exe = Join-Path $destinazione 'Bolt.exe'
Esito 'eseguibile'          (Test-Path -LiteralPath $exe)
Esito 'shaders\ui.vert'     (Test-Path -LiteralPath (Join-Path $destinazione 'shaders/ui.vert'))
Esito 'shaders\ui.frag'     (Test-Path -LiteralPath (Join-Path $destinazione 'shaders/ui.frag'))
Esito 'README accanto all eseguibile' (Test-Path -LiteralPath (Join-Path $destinazione 'README.txt'))
$disinstallatore = Get-ChildItem -LiteralPath $destinazione -Filter 'unins*.exe' -File -ErrorAction SilentlyContinue |
                   Select-Object -First 1
Esito 'disinstallatore di Inno' ($null -ne $disinstallatore)

$collegamento = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/$gruppo/Bolt.lnk"
Esito 'collegamento nel menu Start' (Test-Path -LiteralPath $collegamento)
if (Test-Path -LiteralPath $collegamento) {
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($collegamento)
    Esito '  punta all eseguibile installato' `
          ((Resolve-Path -LiteralPath $lnk.TargetPath).Path -eq (Resolve-Path -LiteralPath $exe).Path) $lnk.TargetPath
}

# --- 3. la voce in "App e funzionalita'" ------------------------------------
# Il nome della chiave lo decide Inno (AppId + suffisso); si cerca per nome
# visualizzato, che e' quello che vede l'utente.
$base = @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
          'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
          'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')
$chiave = $null
foreach ($radiceReg in $base) {
    if (-not (Test-Path -LiteralPath $radiceReg)) { continue }
    foreach ($k in @(Get-ChildItem -LiteralPath $radiceReg -ErrorAction SilentlyContinue)) {
        $v = Get-ItemProperty -LiteralPath $k.PSPath -ErrorAction SilentlyContinue
        if ($v -and $v.DisplayName -eq 'Bolt') { $chiave = $k.PSPath; break }
    }
    if ($chiave) { break }
}
Esito 'voce di disinstallazione' ($null -ne $chiave) $chiave
if ($chiave) {
    $v = Get-ItemProperty -LiteralPath $chiave
    Esito '  versione nella voce' ($v.DisplayVersion -eq $versione) $v.DisplayVersion
    Esito '  icona della voce'    ($v.DisplayIcon -like '*.exe*') $v.DisplayIcon
    Esito '  comando di disinstallazione' ($v.UninstallString -like '*unins*.exe*') $v.UninstallString
    # Inno scrive la cartella con la barra finale; si confronta senza.
    $dove = "$($v.InstallLocation)".TrimEnd([char]92, [char]47)
    Esito '  cartella di installazione'   ($dove -eq $destinazione) $dove
}

# --- 4. Bolt installato parte, e legge gli shader da li' --------------------
# Il backend OpenGL e' l'unico che legge `ui.vert`/`ui.frag` dal disco: se il
# pacchetto fosse sbagliato, il difetto si vede qui e non con Direct3D.
Get-Process Bolt -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
$diag = Join-Path $destinazione 'bolt-diag.log'
$env:BOLT_BACKEND = 'opengl'; $env:BOLT_DIAG = '1'
Start-Process -FilePath $exe -ArgumentList "`"$(Join-Path $radice 'analysis/tmp/sandbox')`"" | Out-Null
Start-Sleep -Seconds 4
Esito 'Bolt installato parte' (@(Get-Process Bolt -ErrorAction SilentlyContinue).Count -ge 1)
# La cattura vuole la finestra in primo piano, e il setup appena chiuso puo' averlo
# preso lui: si usa lo script del banco, che il primo piano lo prende davvero.
& (Join-Path $radice 'tools/focus-window.ps1') -ProcessName Bolt -X 0 -Y 0 -Width 1240 -Height 760 | Out-Null
& (Join-Path $radice 'tools/capture-window.ps1') -ProcessName Bolt `
   -OutputPath (Join-Path $radice 'analysis/tmp/setup-installato.png') -SettleMs 900 | Out-Null
Esito 'cattura della finestra installata' (Test-Path -LiteralPath (Join-Path $radice 'analysis/tmp/setup-installato.png'))
$okShader = $false
if (Test-Path -LiteralPath $diag) {
    $cartella = [regex]::Match((Get-Content -LiteralPath $diag -Raw), '--- shaders: (.+)').Groups[1].Value.Trim()
    $okShader = $cartella -and $cartella.StartsWith($destinazione)
}
Esito 'shader letti dalla cartella installata' $okShader
Esito 'nessun errore di avvio' (-not (Test-Path -LiteralPath (Join-Path $destinazione 'startup-error.log')))
Get-Process Bolt -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item Env:BOLT_BACKEND, Env:BOLT_DIAG -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# --- 5. l'installazione non ha toccato le preferenze ------------------------
if ($prefPrima) {
    $prefDopo = (Get-Item -LiteralPath $preferenze -ErrorAction SilentlyContinue).LastWriteTimeUtc
    Esito 'le preferenze non sono state riscritte' ($prefDopo -eq $prefPrima)
} else {
    Esito 'nessuna preferenza creata dall installazione' (-not (Test-Path -LiteralPath $preferenze))
}

# --- 6. disinstallazione silenziosa -----------------------------------------
$silenzioso = Join-Path $destinazione ($disinstallatore.Name)
$u = Start-Process -FilePath $silenzioso -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
Esito 'la disinstallazione non da errori' ($u.ExitCode -eq 0) "exit $($u.ExitCode)"
# Il disinstallatore di Inno si copia in %TEMP% e si cancella: qualche istante.
$atteso = 0
while ($atteso -lt 30 -and ((Test-Path -LiteralPath $destinazione) -or (Test-Path -LiteralPath $chiave))) {
    Start-Sleep -Milliseconds 500
    $atteso++
}
Esito 'la cartella e sparita'          (-not (Test-Path -LiteralPath $destinazione)) "$([math]::Round($atteso * 0.5, 1)) s"
Esito 'il collegamento e sparito'      (-not (Test-Path -LiteralPath $collegamento))
Esito 'la voce di registro e sparita'  (-not ($chiave -and (Test-Path -LiteralPath $chiave)))
if ($prefPrima) {
    Esito 'le preferenze sono ancora al loro posto' (Test-Path -LiteralPath $preferenze)
} else {
    Esito 'la disinstallazione silenziosa non crea preferenze' (-not (Test-Path -LiteralPath $preferenze))
}
# Il gruppo del menu Start: se la disinstallazione l'ha lasciato vuoto, Windows
# puo' lasciare la cartella. Si controlla, e se c'e' la toglie la prova.
$cartellaGruppo = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/$gruppo"
if (Test-Path -LiteralPath $cartellaGruppo) {
    if (@(Get-ChildItem -LiteralPath $cartellaGruppo -Force -ErrorAction SilentlyContinue).Count -eq 0) {
        Remove-Item -LiteralPath $cartellaGruppo -Force -ErrorAction SilentlyContinue
    }
}
Esito 'il gruppo nel menu Start non resta appeso' (-not (Test-Path -LiteralPath $cartellaGruppo))

}
finally {
    # Pulizia, comunque vada: si chiude Bolt, si disinstalla se c'e' ancora il
    # disinstallatore, si tolgono la cartella di prova, il gruppo del menu Start se e'
    # rimasto vuoto, e la voce di registro se punta dentro la prova.
    Get-Process Bolt -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $unins = Get-ChildItem -LiteralPath $destinazione -Filter 'unins*.exe' -File -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($unins) {
        Start-Process -FilePath $unins.FullName -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -Wait | Out-Null
        Start-Sleep -Seconds 2
    }
    # La cattura sta fuori dalla cartella di prova: e' una prova, non un residuo.
    if (Test-Path -LiteralPath $prova) { Remove-Item -LiteralPath $prova -Recurse -Force -ErrorAction SilentlyContinue }
    $cartellaGruppo = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/$gruppo"
    if (Test-Path -LiteralPath $cartellaGruppo) {
        $resto = @(Get-ChildItem -LiteralPath $cartellaGruppo -Force -ErrorAction SilentlyContinue)
        if ($resto.Count -eq 0) { Remove-Item -LiteralPath $cartellaGruppo -Recurse -Force -ErrorAction SilentlyContinue }
    }
    foreach ($r in $radiciReg) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        foreach ($k in @(Get-ChildItem -LiteralPath $r -ErrorAction SilentlyContinue)) {
            $v = Get-ItemProperty -LiteralPath $k.PSPath -ErrorAction SilentlyContinue
            if ($v -and $v.DisplayName -eq 'Bolt' -and $v.InstallLocation -like "*$prova*") {
                Remove-Item -LiteralPath $k.PSPath -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

# --- esito ------------------------------------------------------------------
$falliti = @($esiti | Where-Object { -not $_ }).Count
Write-Host ''
if ($falliti -eq 0) {
    Write-Host "Tutti i $($esiti.Count) controlli sono passati."
} else {
    Write-Host "$falliti controlli su $($esiti.Count) NON sono passati." -ForegroundColor Red
    exit 1
}
