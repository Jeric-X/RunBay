param(
    [Alias("C")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$QtPrefix = ""
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "build-daemon.ps1") -C $Configuration
& (Join-Path $PSScriptRoot "build-qt.ps1") -C $Configuration -QtPrefix $QtPrefix
