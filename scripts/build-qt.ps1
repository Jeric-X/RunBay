param(
    [Alias("C")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$QtPrefix = "",
    [string]$BuildDir = "",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

function Find-CMake {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) {
        return $cmake.Source
    }

    $candidates = @(
        "C:\Qt\Tools\CMake_64\bin\cmake.exe",
        "C:\Qt\Tools\CMake_32\bin\cmake.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "CMake was not found. Add cmake to PATH or install Qt's CMake tool."
}

function Find-Ninja {
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninja) {
        return $ninja.Source
    }

    $candidates = @(
        "C:\Qt\Tools\Ninja\ninja.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return ""
}

function Find-VsDevCmd {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    return ""
}

function Import-VsDevEnv {
    $cl = Get-Command cl -ErrorAction SilentlyContinue
    if ($cl) {
        return
    }

    $vsDevCmd = Find-VsDevCmd
    if (-not $vsDevCmd) {
        throw "MSVC compiler environment was not found. Install Visual Studio C++ tools or run this script from a Developer PowerShell."
    }

    Write-Host "Loading MSVC environment: $vsDevCmd"
    $envLines = cmd /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to load MSVC environment from $vsDevCmd"
    }

    foreach ($line in $envLines) {
        $index = $line.IndexOf("=")
        if ($index -le 0) {
            continue
        }
        $name = $line.Substring(0, $index)
        $value = $line.Substring($index + 1)
        Set-Item -Path "Env:$name" -Value $value
    }

    $cl = Get-Command cl -ErrorAction SilentlyContinue
    if (-not $cl) {
        throw "Loaded Visual Studio environment, but cl.exe is still unavailable. Check that the Desktop development with C++ workload is installed."
    }
}

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
                    Where-Object { Test-Path (Join-Path $_.FullName "lib\cmake\Qt6\Qt6Config.cmake") }
            } |
            Sort-Object FullName -Descending

        if ($matches) {
            return $matches[0].FullName
        }
    }

    return ""
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
$sourceDir = Join-Path $repoRoot "qt-client"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Join-Path $sourceDir "build") $Configuration
}

$cmake = Find-CMake
$ninja = Find-Ninja
$resolvedQtPrefix = Find-QtPrefix -RequestedPrefix $QtPrefix

if ($resolvedQtPrefix -match "msvc") {
    Import-VsDevEnv
}

$configureArgs = @("-S", $sourceDir, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$Configuration")
if ($resolvedQtPrefix) {
    $configureArgs += "-DCMAKE_PREFIX_PATH=$resolvedQtPrefix"
}
if ($Generator) {
    $configureArgs += @("-G", $Generator)
} elseif ($ninja) {
    $configureArgs += @("-G", "Ninja")
    $configureArgs += "-DCMAKE_MAKE_PROGRAM=$ninja"
}

Invoke-Checked -FilePath $cmake -Arguments $configureArgs
Invoke-Checked -FilePath $cmake -Arguments @("--build", $BuildDir, "--config", $Configuration)

$exe = Join-Path $BuildDir "bin\runbay-client.exe"
if (Test-Path $exe) {
    Write-Host "Built Qt client: $exe"
} else {
    Write-Host "Build finished. Expected executable path: $exe"
}
