param(
    [Alias("C")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$Addr = "127.0.0.1:8732"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$daemonExe = Join-Path $repoRoot "daemon\bin\$Configuration\runbayd.exe"

if (-not (Test-Path $daemonExe)) {
    & (Join-Path $PSScriptRoot "build-daemon.ps1") -C $Configuration
}

& $daemonExe -addr $Addr
