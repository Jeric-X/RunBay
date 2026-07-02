param(
    [Alias("C")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$daemonDir = Join-Path $repoRoot "daemon"
$outputDir = Join-Path (Join-Path $daemonDir "bin") $Configuration
$outputName = if ($IsWindows -or $env:OS -eq "Windows_NT") { "runbayd.exe" } else { "runbayd" }
$outputPath = Join-Path $outputDir $outputName

$go = Get-Command go -ErrorAction SilentlyContinue
if (-not $go) {
    throw "Go was not found in PATH. Install Go or add it to PATH, then retry."
}

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

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Push-Location $daemonDir
try {
    $env:CGO_ENABLED = "0"
    if ($Configuration -ieq "Debug") {
        Invoke-Checked -FilePath $go.Source -Arguments @("build", "-gcflags", "all=-N -l", "-o", $outputPath, "./cmd/runbayd")
    } else {
        Invoke-Checked -FilePath $go.Source -Arguments @("build", "-trimpath", "-o", $outputPath, "./cmd/runbayd")
    }
} finally {
    Pop-Location
}

Write-Host "Built daemon: $outputPath"
