param(
    [string]$Target = "",
    [string]$NookCli = ".\tools\nook_cli_local.ps1",
    [string]$Serial = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($Target)) {
    throw "Target is required. Pass -Target <package-or-pid>."
}

$args = @(
    "call",
    $Target,
    "nook.gadget.load-configured-startup",
    "--attach",
    "--call-args",
    "[]",
    "--usb"
)

if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $args += @("--serial", $Serial)
}

Write-Host "[nook-gadget] triggering packaged startup script target=$Target"
& $NookCli @args
if ($LASTEXITCODE -ne 0) {
    throw "packaged startup trigger failed"
}

Write-Host "[nook-gadget] packaged startup trigger completed"
