param(
    [string]$ServiceName = "RunBay",
    [string]$DaemonPath = "",
    [string]$Addr = "127.0.0.1:8732",
    [string]$DataPath = ""
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DaemonPath) {
    $localDaemon = Join-Path $PSScriptRoot "runbayd.exe"
    if (Test-Path $localDaemon) {
        $DaemonPath = $localDaemon
    } else {
        $DaemonPath = Join-Path $repoRoot "dist\Release\runbayd.exe"
    }
}
if (-not (Test-Path $DaemonPath)) {
    throw "Daemon executable was not found: $DaemonPath"
}

$binPath = "`"$DaemonPath`" -addr $Addr"
if ($DataPath) {
    $binPath += " -data `"$DataPath`""
}

$existing = & sc.exe query $ServiceName 2>$null
if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $ServiceName | Out-Null
    Invoke-Checked -FilePath "sc.exe" -Arguments @("delete", $ServiceName)
    Start-Sleep -Seconds 1
}

Invoke-Checked -FilePath "sc.exe" -Arguments @(
    "create",
    $ServiceName,
    "binPath= $binPath",
    "start= auto",
    "DisplayName= RunBay"
)
Invoke-Checked -FilePath "sc.exe" -Arguments @("description", $ServiceName, "RunBay local task daemon")
Invoke-Checked -FilePath "sc.exe" -Arguments @("failure", $ServiceName, "reset= 86400", "actions= restart/5000/restart/15000/none/0")
Invoke-Checked -FilePath "sc.exe" -Arguments @("start", $ServiceName)

Write-Host "Installed and started service: $ServiceName"
