[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$DestinationRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot ".."
}

if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    $DestinationRoot = Join-Path $PSScriptRoot "..\\github-ready"
}

function Resolve-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path does not exist: $Path"
    }
    return [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
}

function Remove-IfExists {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    if (Test-Path -LiteralPath $LiteralPath) {
        Remove-Item -LiteralPath $LiteralPath -Recurse -Force
    }
}

function Remove-ChildrenByPattern {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [switch]$Directory,
        [switch]$File
    )

    $items = Get-ChildItem -LiteralPath $Root -Force -ErrorAction SilentlyContinue
    foreach ($item in $items) {
        if ($item.Name -notlike $Pattern) {
            continue
        }
        if ($Directory -and -not $item.PSIsContainer) {
            continue
        }
        if ($File -and $item.PSIsContainer) {
            continue
        }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

$sourceRoot = Resolve-NormalizedPath $SourceRoot
$destinationParent = Split-Path -Parent $DestinationRoot
if (![string]::IsNullOrWhiteSpace($destinationParent)) {
    New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
}
if (!(Test-Path -LiteralPath $DestinationRoot)) {
    New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
}
$destinationRoot = Resolve-NormalizedPath $DestinationRoot

Write-Host "[refresh] source      = $sourceRoot"
Write-Host "[refresh] destination = $destinationRoot"

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
    "/XD", "build\\android\\obj",
    "/XD", "build\\single-server-package",
    "/XD", "build\\single-server-staging",
    "/XD", "docs",
    "/XD", "github-publish",
    "/XD", "github-ready",
    "/XD", "libs",
    "/XD", "obj",
    "/XD", "out",
    "/XD", "publish-clean",
    "/XD", "publish-final",
    "/XD", "publish-repo",
    "/XD", "release-prep",
    "/XD", "tmp",
    "/XD", "tests\\Test_Lab",
    "/XD", "host\\nook-py\\dist",
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

$cleanupDirs = @(
    ".claude",
    ".hermes",
    ".tmp",
    ".tmp_reverse_skills",
    ".tmp_verify",
    ".tmp_verify_android",
    ".worktrees",
    "build\\android\\obj",
    "build\\single-server-package",
    "build\\single-server-staging",
    "docs",
    "github-publish",
    "libs",
    "obj",
    "out",
    "publish-clean",
    "publish-final",
    "publish-repo",
    "release-prep",
    "tmp",
    "tests\\Test_Lab",
    "host\\nook-py\\dist",
    "host\\nook-py\\debug_probes"
)

foreach ($path in $cleanupDirs) {
    Remove-IfExists -LiteralPath (Join-Path $destinationRoot $path)
}

Get-ChildItem -LiteralPath (Join-Path $destinationRoot "host\\nook-py") -Force -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*.egg-info" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Get-ChildItem -LiteralPath (Join-Path $destinationRoot "host\\nook-py") -Force -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*-dexdump" -or $_.Name -like "*-sodump" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Get-ChildItem -LiteralPath (Join-Path $destinationRoot "build") -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "android" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

Remove-ChildrenByPattern -Root $destinationRoot -Pattern ".tmp_*" -Directory
Remove-ChildrenByPattern -Root $destinationRoot -Pattern "tmp_*" -File
Remove-ChildrenByPattern -Root $destinationRoot -Pattern "*-dexdump" -Directory
Remove-ChildrenByPattern -Root $destinationRoot -Pattern "*-sodump" -Directory

Get-ChildItem -LiteralPath $destinationRoot -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @(".gitignore", "README.md", "README.en.md") } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

Get-ChildItem -LiteralPath $destinationRoot -File -Filter "*.md" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @("README.md", "README.en.md") } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

Write-Host "[refresh] completed"
