[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$DestinationRoot = "E:\\Learn\\my_program\\all_my_hook\\kanxue\\github\\Nook\\Nook-repo"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot "..\\github-ready"
}

function Resolve-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
}

function Ensure-RepositoryRoot {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path does not exist: $Path"
    }
    $gitDir = Join-Path $Path ".git"
    if (!(Test-Path -LiteralPath $gitDir)) {
        throw "Destination is not a git repository root: $Path"
    }
}

function Remove-IfExists {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    if (Test-Path -LiteralPath $LiteralPath) {
        Remove-Item -LiteralPath $LiteralPath -Recurse -Force
    }
}

$sourceRoot = Resolve-NormalizedPath $SourceRoot
Ensure-RepositoryRoot -Path $DestinationRoot
$destinationRoot = Resolve-NormalizedPath $DestinationRoot

Write-Host "[sync] source      = $sourceRoot"
Write-Host "[sync] destination = $destinationRoot"

$robocopyArgs = @(
    $sourceRoot,
    $destinationRoot,
    "/MIR",
    "/R:2",
    "/W:1",
    "/NFL",
    "/NDL",
    "/NJH",
    "/NJS",
    "/NP",
    "/XD", ".git",
    "/XD", "build\\single-server-package",
    "/XD", "build\\single-server-staging",
    "/XD", "build\\android\\obj",
    "/XD", "obj",
    "/XD", "libs",
    "/XD", "out"
)

& robocopy @robocopyArgs | Out-Host
$robocopyExitCode = $LASTEXITCODE
if ($robocopyExitCode -ge 8) {
    throw "robocopy failed with exit code $robocopyExitCode"
}

Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\single-server-package")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\single-server-staging")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\android\\obj")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "obj")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "libs")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "out")

Write-Host "[sync] completed"
Write-Host "[next] cd `"$destinationRoot`""
Write-Host "[next] git status --short"
Write-Host "[next] git add ."
Write-Host "[next] git commit -m `"sync github-ready`""
Write-Host "[next] git push"
