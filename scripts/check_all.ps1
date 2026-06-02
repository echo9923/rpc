$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($RootDir)) {
    $RootDir = (Get-Location).Path
}

$WslCommand = Get-Command wsl.exe -ErrorAction SilentlyContinue
if ($null -eq $WslCommand) {
    Write-Error "wsl.exe is required but not found. Please install or enable WSL first."
    exit 1
}

& wsl.exe --cd "$RootDir" bash ./scripts/check_all.sh
exit $LASTEXITCODE
