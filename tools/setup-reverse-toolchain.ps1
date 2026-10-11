<#
.SYNOPSIS
    Installs and verifies the reverse-engineering toolchain for this project.

.DESCRIPTION
    Reproduces, from a clean machine to a verified state:
      1. a 64-bit JDK 21+ (uses an existing JAVA_HOME when it already qualifies)
      2. the latest Ghidra 12.1.x public release
      3. the rea-agents CLI from npm
      4. the REA MCP registration in Pi's own mcp.json
      5. end-to-end verification of the provider and the MCP inventory

    Idempotent: re-running it reuses what is already in place and only reports.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/setup-reverse-toolchain.ps1
#>
[CmdletBinding()]
param(
    [string]$ToolsRoot = 'C:\tools'
)

$ErrorActionPreference = 'Stop'
$script:Problems = [System.Collections.Generic.List[string]]::new()

function Write-Step { param([string]$Text) Write-Host ""; Write-Host "== $Text" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Text) Write-Host "   OK   $Text" -ForegroundColor Green }
function Write-Warn { param([string]$Text) Write-Host "   WARN $Text" -ForegroundColor Yellow }
function Write-Bad  { param([string]$Text) Write-Host "   FAIL $Text" -ForegroundColor Red; $script:Problems.Add($Text) }

# ---------------------------------------------------------------------------
# 1. Java
# ---------------------------------------------------------------------------
Write-Step 'Java runtime'

$javaHome = [Environment]::GetEnvironmentVariable('JAVA_HOME', 'Machine')
if (-not $javaHome) { $javaHome = [Environment]::GetEnvironmentVariable('JAVA_HOME', 'User') }

function Test-Jdk {
    param([string]$Home)
    if (-not $Home) { return $false }
    $javaExe  = Join-Path $Home 'bin\java.exe'
    $javacExe = Join-Path $Home 'bin\javac.exe'
    if (-not (Test-Path $javaExe))  { return $false }
    # Ghidra needs a full JDK, not a JRE: javac must be present.
    if (-not (Test-Path $javacExe)) { return $false }

    $version = (& $javaExe -XshowSettings:properties -version 2>&1 |
                Select-String -Pattern '^\s*java\.version\s*=\s*(.+)$' |
                ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }) | Select-Object -First 1
    $bits = (& $javaExe -XshowSettings:properties -version 2>&1 |
             Select-String -Pattern '^\s*sun\.arch\.data\.model\s*=\s*(.+)$' |
             ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }) | Select-Object -First 1

    if ($bits -ne '64') { return $false }

    $major = [int]($version -split '\.')[0]
    return $major -ge 21
}

if (Test-Jdk -Home $javaHome) {
    Write-Ok "JAVA_HOME qualifies: $javaHome"
} else {
    Write-Warn "JAVA_HOME is unset or does not qualify (needs a 64-bit full JDK 21+): '$javaHome'"

    $candidates = @()
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, "$env:LOCALAPPDATA\Programs", "$env:USERPROFILE\.jdks")) {
        if ($root -and (Test-Path $root)) {
            $candidates += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match 'jdk|jbr|java|adoptium|zulu|corretto|temurin' } |
                Select-Object -ExpandProperty FullName
        }
    }

    $found = $candidates | Where-Object { Test-Jdk -Home $_ } | Select-Object -First 1
    if ($found) {
        $javaHome = $found
        Write-Ok "found a qualifying JDK elsewhere: $javaHome"
    } else {
        Write-Warn 'downloading Eclipse Temurin JDK 21'
        $jdkUrl = 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse'
        $jdkZip = Join-Path $ToolsRoot 'temurin-jdk21.zip'
        New-Item -ItemType Directory -Force -Path $ToolsRoot | Out-Null
        Invoke-WebRequest -Uri $jdkUrl -OutFile $jdkZip -UseBasicParsing
        & "$env:SystemRoot\System32\tar.exe" -xf $jdkZip -C $ToolsRoot
        $javaHome = (Get-ChildItem -Path $ToolsRoot -Directory |
                     Where-Object { $_.Name -match '^jdk-21' } |
                     Select-Object -First 1 -ExpandProperty FullName)
        if (-not (Test-Jdk -Home $javaHome)) {
            Write-Bad 'automatic JDK installation did not produce a usable JDK 21'
            return
        }
        Write-Ok "installed $javaHome"
    }

    [Environment]::SetEnvironmentVariable('JAVA_HOME', $javaHome, 'User')
    $env:JAVA_HOME = $javaHome
    Write-Ok 'JAVA_HOME set for the current user'
}

# ---------------------------------------------------------------------------
# 2. Ghidra
# ---------------------------------------------------------------------------
Write-Step 'Ghidra'

$ghidraDir = [Environment]::GetEnvironmentVariable('GHIDRA_INSTALL_DIR', 'User')
if (-not $ghidraDir) { $ghidraDir = [Environment]::GetEnvironmentVariable('GHIDRA_INSTALL_DIR', 'Machine') }

function Test-Ghidra {
    param([string]$Dir)
    if (-not $Dir) { return $false }
    return (Test-Path (Join-Path $Dir 'support\analyzeHeadless.bat')) -and
           (Test-Path (Join-Path $Dir 'Ghidra\application.properties'))
}

if (Test-Ghidra -Dir $ghidraDir) {
    $version = (Select-String -Path (Join-Path $ghidraDir 'Ghidra\application.properties') -Pattern '^application\.version=(.+)$').Matches[0].Groups[1].Value
    Write-Ok "GHIDRA_INSTALL_DIR qualifies: $ghidraDir (version $version)"
} else {
    Write-Warn "no usable Ghidra at '$ghidraDir'"
    New-Item -ItemType Directory -Force -Path $ToolsRoot | Out-Null

    # Pick the newest 12.1.x release, which is the line rea-agents verifies.
    Write-Host '   querying GitHub for the latest Ghidra 12.1.x release'
    $releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/NationalSecurityAgency/ghidra/releases?per_page=20' -UseBasicParsing
    $release = $releases |
        Where-Object { $_.tag_name -match 'Ghidra_12\.1' } |
        Select-Object -First 1
    if (-not $release) { $release = $releases | Select-Object -First 1 }

    $asset = $release.assets | Where-Object { $_.name -match '_PUBLIC_.*\.zip$' } | Select-Object -First 1
    if (-not $asset) {
        Write-Bad "no PUBLIC zip asset in release $($release.tag_name)"
        return
    }

    Write-Host "   downloading $($asset.name) ($([int]($asset.size / 1MB)) MB)"
    $zipPath = Join-Path $ToolsRoot $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing

    Write-Host '   extracting'
    # Windows bsdtar handles zip archives and is much faster than Expand-Archive.
    & "$env:SystemRoot\System32\tar.exe" -xf $zipPath -C $ToolsRoot
    if ($LASTEXITCODE -ne 0) { Write-Bad 'extraction failed'; return }

    $ghidraDir = (Get-ChildItem -Path $ToolsRoot -Directory |
                  Where-Object { $_.Name -match '^ghidra_12\.1' } |
                  Sort-Object Name -Descending |
                  Select-Object -First 1 -ExpandProperty FullName)
    if (-not (Test-Ghidra -Dir $ghidraDir)) { Write-Bad 'extracted Ghidra is not usable'; return }

    [Environment]::SetEnvironmentVariable('GHIDRA_INSTALL_DIR', $ghidraDir, 'User')
    $env:GHIDRA_INSTALL_DIR = $ghidraDir
    Remove-Item $zipPath -ErrorAction SilentlyContinue
    Write-Ok "installed $ghidraDir"
}

# ---------------------------------------------------------------------------
# 3. rea-agents
# ---------------------------------------------------------------------------
Write-Step 'rea-agents CLI'

$reaVersion = $null
try { $reaVersion = (rea --version 2>$null) } catch { }

if ($reaVersion) {
    Write-Ok "rea $reaVersion already on PATH"
} else {
    Write-Host '   installing from npm'
    & npm install --global rea-agents
    if ($LASTEXITCODE -ne 0) { Write-Bad 'npm install failed'; return }
    Write-Ok 'installed'
}

# ---------------------------------------------------------------------------
# 4. Pi registration
# ---------------------------------------------------------------------------
Write-Step 'Pi MCP registration'

$agentDir = if ($env:PI_CODING_AGENT_DIR) { $env:PI_CODING_AGENT_DIR } else { Join-Path $env:USERPROFILE '.pi\agent' }
$mcpJson = Join-Path $agentDir 'mcp.json'

$nodeExe = (Get-Command node -ErrorAction Stop).Source
$npmRoot = (& npm root -g).Trim()
$launcher = Join-Path $npmRoot 'rea-agents\scripts\rea.mjs'

if (-not (Test-Path $launcher)) {
    Write-Bad "cannot find the REA launcher at $launcher"
} else {
    # Read, patch only our own entry, and write back as strict JSON: Pi rejects
    # comments, trailing commas and a byte-order mark.
    $config = @{ mcpServers = @{} }
    if (Test-Path $mcpJson) {
        $raw = Get-Content -Raw -Path $mcpJson
        if ($raw.Trim()) {
            try { $config = $raw | ConvertFrom-Json -AsHashtable } catch {
                Write-Bad "$mcpJson is not valid JSON, refusing to overwrite: $($_.Exception.Message)"
                return
            }
        }
    }
    if (-not $config.ContainsKey('mcpServers')) { $config['mcpServers'] = @{} }

    $config['mcpServers']['rea'] = @{
        command     = $nodeExe
        args        = @($launcher, 'mcp')
        env         = @{
            REA_ANALYSIS_PROVIDER          = 'ghidra'
            GHIDRA_INSTALL_DIR             = $ghidraDir
            JAVA_HOME                      = $javaHome
            REA_GHIDRA_STARTUP_TIMEOUT_MS  = '900000'
        }
        # codemode keeps 139 tools out of the system prompt; they stay reachable
        # from codemode scripts and tool_search. pi-mcp-adapter translates
        # codemode to "proxy only".
        exposure    = 'codemode'
        # Cold Ghidra import of a multi-megabyte binary can exceed a minute.
        timeout     = 900
        description = 'Reverse engineering: native binaries (Ghidra), JavaScript/Electron, .NET, Android APK, browser, firmware, network captures'
    }

    New-Item -ItemType Directory -Force -Path $agentDir | Out-Null
    ($config | ConvertTo-Json -Depth 10) | Set-Content -Path $mcpJson -Encoding utf8NoBOM
    Write-Ok "wrote mcpServers.rea in $mcpJson"
}

# ---------------------------------------------------------------------------
# 5. Verification
# ---------------------------------------------------------------------------
Write-Step 'Verification'

$env:REA_ANALYSIS_PROVIDER = 'ghidra'
$env:GHIDRA_INSTALL_DIR = $ghidraDir
$env:JAVA_HOME = $javaHome

$providerReady = $false
try {
    $doctor = rea mcp doctor --json 2>$null | ConvertFrom-Json
    if ($doctor.healthy) {
        $tools = $doctor.inventory.tools
        Write-Ok "MCP server healthy: $($tools.observed) tools, $($doctor.inventory.prompts.observed) prompts"
        $providerReady = $true
    } else {
        Write-Bad 'MCP server reported unhealthy'
    }
} catch {
    Write-Bad "could not validate the MCP server: $($_.Exception.Message)"
}

if ($providerReady) {
    Write-Host ''
    Write-Host 'Ready. In an already-running Pi session run /reload or restart Pi to pick up the server.' -ForegroundColor Green
    Write-Host 'Then verify with:  pi mcp list'
}

# ---------------------------------------------------------------------------
Write-Step 'Summary'
if ($script:Problems.Count -eq 0) {
    Write-Host 'Everything is in place.' -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:Problems.Count) problem(s):" -ForegroundColor Red
    $script:Problems | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
