param(
    [Alias("C")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$QtPrefix = "",
    [string]$OutputDir = "",
    [string]$BuildDir = "",
    [string]$Generator = "",
    [switch]$IncludeCompilerRuntime,
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"

function Find-QtPrefix {
    param([string]$RequestedPrefix)

    if ($RequestedPrefix -and (Test-Path $RequestedPrefix)) {
        return $RequestedPrefix
    }

    $qtRoots = @("C:\Qt")
    foreach ($root in $qtRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $matches = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue |
                    Where-Object { Test-Path (Join-Path $_.FullName "bin\windeployqt.exe") }
            } |
            Sort-Object FullName -Descending

        if ($matches) {
            return $matches[0].FullName
        }
    }

    return ""
}

function Find-WinDeployQt {
    param([string]$ResolvedQtPrefix)

    if ($ResolvedQtPrefix) {
        $candidate = Join-Path $ResolvedQtPrefix "bin\windeployqt.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($windeployqt) {
        return $windeployqt.Source
    }

    throw "windeployqt was not found. Add Qt bin to PATH or pass -QtPrefix C:\Qt\<version>\<kit>."
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

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedQtPrefix = Find-QtPrefix -RequestedPrefix $QtPrefix
$windeployqt = Find-WinDeployQt -ResolvedQtPrefix $resolvedQtPrefix

if (-not $BuildDir) {
    $BuildDir = Join-Path (Join-Path $repoRoot "qt-client\build") $Configuration
}
if (-not $OutputDir) {
    $OutputDir = Join-Path (Join-Path $repoRoot "dist") $Configuration
}

& (Join-Path $PSScriptRoot "build-daemon.ps1") -C $Configuration
& (Join-Path $PSScriptRoot "build-qt.ps1") -C $Configuration -QtPrefix $resolvedQtPrefix -BuildDir $BuildDir -Generator $Generator

$daemonExe = Join-Path $repoRoot "daemon\bin\$Configuration\runbayd.exe"
$qtClientExe = Join-Path $BuildDir "bin\runbay-client.exe"

if (-not (Test-Path $daemonExe)) {
    throw "Daemon executable was not found: $daemonExe"
}
if (-not (Test-Path $qtClientExe)) {
    throw "Qt client executable was not found: $qtClientExe"
}

if ((Test-Path $OutputDir) -and -not $NoClean) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Copy-Item -LiteralPath $daemonExe -Destination (Join-Path $OutputDir "runbayd.exe") -Force
Copy-Item -LiteralPath $qtClientExe -Destination (Join-Path $OutputDir "runbay-client.exe") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "install-service.ps1") -Destination (Join-Path $OutputDir "install-service.ps1") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "uninstall-service.ps1") -Destination (Join-Path $OutputDir "uninstall-service.ps1") -Force

$deployArgs = @(
    "--no-translations",
    "--no-system-d3d-compiler",
    "--no-opengl-sw"
)
if ($IncludeCompilerRuntime) {
    $deployArgs += "--compiler-runtime"
}
if ($Configuration -ieq "Debug") {
    $deployArgs += "--debug"
} else {
    $deployArgs += "--release"
}
$deployArgs += (Join-Path $OutputDir "runbay-client.exe")

Invoke-Checked -FilePath $windeployqt -Arguments $deployArgs

Write-Host "Packaged RunBay: $OutputDir"
