param(
    [string]$ServiceName = "RunBay"
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

& sc.exe query $ServiceName 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Service is not installed: $ServiceName"
    return
}

& sc.exe stop $ServiceName | Out-Null
Invoke-Checked -FilePath "sc.exe" -Arguments @("delete", $ServiceName)

Write-Host "Uninstalled service: $ServiceName"
