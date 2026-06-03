[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$KeystorePath = ".\build\keystore\nook-debug.keystore",
    [string]$Storepass = "android",
    [string]$Keypass = "android",
    [string]$KeyAlias = "androiddebugkey",
    [string]$Dname = "CN=Android Debug,O=Android,C=US",
    [int]$ValidityDays = 10000,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$keytool = Get-Command keytool -ErrorAction SilentlyContinue
if ($null -eq $keytool) {
    throw "keytool not found in PATH"
}

if ((Test-Path $KeystorePath) -and -not $Force) {
    Write-Host "[ensure-nook-debug-keystore] existing=$KeystorePath"
    exit 0
}

$keystoreDir = Split-Path -Parent $KeystorePath
if (-not [string]::IsNullOrWhiteSpace($keystoreDir)) {
    New-Item -ItemType Directory -Force $keystoreDir | Out-Null
}

if ((Test-Path $KeystorePath) -and $Force) {
    Remove-Item -LiteralPath $KeystorePath -Force
}

& $keytool.Source `
    -genkeypair `
    -v `
    -keystore $KeystorePath `
    -storepass $Storepass `
    -keypass $Keypass `
    -alias $KeyAlias `
    -dname $Dname `
    -keyalg RSA `
    -keysize 2048 `
    -validity $ValidityDays

if ($LASTEXITCODE -ne 0) {
    throw "keytool failed to generate debug keystore"
}

Write-Host "[ensure-nook-debug-keystore] created=$KeystorePath alias=$KeyAlias"
