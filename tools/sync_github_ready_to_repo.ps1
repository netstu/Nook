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
    "/XD", ".claude",
    "/XD", ".hermes",
    "/XD", ".tmp",
    "/XD", ".tmp_reverse_skills",
    "/XD", ".tmp_verify",
    "/XD", ".tmp_verify_android",
    "/XD", ".worktrees",
    "/XD", "build\\single-server-package",
    "/XD", "build\\single-server-staging",
    "/XD", "build\\android\\obj",
    "/XD", "docs",
    "/XD", "github-publish",
    "/XD", "github-ready",
    "/XD", "host\\nook-py\\dist",
    "/XD", "obj",
    "/XD", "libs",
    "/XD", "out",
    "/XD", "publish-clean",
    "/XD", "publish-final",
    "/XD", "publish-repo",
    "/XD", "release-prep",
    "/XD", "tests\\Test_Lab",
    "/XD", "tmp",
    "/XF", "task_plan.md",
    "/XF", "findings.md",
    "/XF", "progress.md",
    "/XF", "nook-cli",
    "/XF", "codex会话.txt"
)

& robocopy @robocopyArgs | Out-Host
$robocopyExitCode = $LASTEXITCODE
if ($robocopyExitCode -ge 8) {
    throw "robocopy failed with exit code $robocopyExitCode"
}

Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\single-server-package")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\single-server-staging")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "build\\android\\obj")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".claude")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".hermes")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".tmp")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".tmp_reverse_skills")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".tmp_verify")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".tmp_verify_android")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot ".worktrees")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "docs")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "github-publish")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "host\\nook-py\\dist")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "host\\nook-py\\debug_probes")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "obj")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "libs")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "out")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "publish-clean")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "publish-final")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "publish-repo")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "release-prep")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "tests\\Test_Lab")
Remove-IfExists -LiteralPath (Join-Path $destinationRoot "tmp")

Get-ChildItem -LiteralPath $destinationRoot -Force -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*-dexdump" -or $_.Name -like "*-sodump" -or $_.Name -like ".tmp_*" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Get-ChildItem -LiteralPath (Join-Path $destinationRoot "host\\nook-py") -Force -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*-dexdump" -or $_.Name -like "*-sodump" -or $_.Name -like "*.egg-info" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Get-ChildItem -LiteralPath (Join-Path $destinationRoot "build") -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "android" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Get-ChildItem -LiteralPath $destinationRoot -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @(".gitignore", "README.md", "README.en.md") } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

Get-ChildItem -LiteralPath $destinationRoot -File -Filter "*.md" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @("README.md", "README.en.md") } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

Write-Host "[sync] completed"
Write-Host "[next] cd `"$destinationRoot`""
Write-Host "[next] git status --short"
Write-Host "[next] git add ."
Write-Host "[next] git commit -m `"sync github-ready`""
Write-Host "[next] git push"
