param(
    [string]$NdkBuild = $env:NOOK_NDK_BUILD,
    [string]$Abi = "arm64-v8a",
    [switch]$ForceRebuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($NdkBuild)) {
    $defaultNdkBuild = "E:\SDK\ndk\25.2.9519653\ndk-build.cmd"
    if (Test-Path $defaultNdkBuild) {
        $NdkBuild = $defaultNdkBuild
    } else {
        $NdkBuild = "ndk-build.cmd"
    }
}

if (-not (Get-Command $NdkBuild -ErrorAction SilentlyContinue) -and -not (Test-Path $NdkBuild)) {
    throw "ndk-build not found at '$NdkBuild'. Set -NdkBuild or NOOK_NDK_BUILD first."
}

$canonicalOutputDir = Join-Path $repoRoot ("libs\" + $Abi)
$packageDir = Join-Path $repoRoot ("build\single-server-package\" + $Abi)
$stagingDir = Join-Path $repoRoot ("build\single-server-staging\" + $Abi)
New-Item -ItemType Directory -Force $packageDir | Out-Null
New-Item -ItemType Directory -Force $stagingDir | Out-Null

function Invoke-NookNdkBuild {
    param(
        [string[]]$Modules
    )

    $moduleList = $Modules -join " "
    $arguments = @(
        "NDK_PROJECT_PATH=.",
        "APP_BUILD_SCRIPT=build/android/Android.mk",
        "NDK_APPLICATION_MK=build/android/Application_static.mk",
        "APP_ABI=$Abi",
        "APP_MODULES=$moduleList",
        "-j4"
    )

    if ($ForceRebuild) {
        $arguments = @("-B") + $arguments
    }

    Write-Host ("[build] {0}" -f $moduleList)
    & $NdkBuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ndk-build failed for modules: $moduleList"
    }
}

function Copy-CanonicalArtifact {
    param(
        [string]$FileName,
        [string]$DestinationDirectory
    )

    $sourcePath = Join-Path $canonicalOutputDir $FileName
    if (-not (Test-Path $sourcePath)) {
        throw "expected artifact not found: $sourcePath"
    }

    New-Item -ItemType Directory -Force $DestinationDirectory | Out-Null
    $targetPath = Join-Path $DestinationDirectory $FileName
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
    Write-Host ("[copy] {0} -> {1}" -f $sourcePath, $targetPath)
    return $targetPath
}

Invoke-NookNdkBuild -Modules @("nook_agent", "nook_ncore", "nook_zygote_helper")

$embeddedAgentSource = Copy-CanonicalArtifact -FileName "libnook-agent.so" -DestinationDirectory $stagingDir
$embeddedNcoreSource = Copy-CanonicalArtifact -FileName "libncore.so" -DestinationDirectory $stagingDir
$embeddedZygoteHelperSource = Copy-CanonicalArtifact -FileName "libnook-zygote-helper.so" -DestinationDirectory $stagingDir

$env:NOOK_EMBEDDED_AGENT_SOURCE = $embeddedAgentSource
& powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\build_embedded_agent_blob.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "embedded agent blob generation failed"
}

$env:NOOK_EMBEDDED_AGENT_SOURCE = $null

$env:NOOK_EMBEDDED_NCORE_SOURCE = $embeddedNcoreSource
& powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\build_embedded_ncore_blob.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "embedded ncore blob generation failed"
}
$env:NOOK_EMBEDDED_NCORE_SOURCE = $null

$env:NOOK_EMBEDDED_ZYGOTE_HELPER_SOURCE = $embeddedZygoteHelperSource
& powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\build_embedded_zygote_helper_blob.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "embedded zygote helper blob generation failed"
}
$env:NOOK_EMBEDDED_ZYGOTE_HELPER_SOURCE = $null

Invoke-NookNdkBuild -Modules @("nook_server")

$serverPath = Join-Path $canonicalOutputDir "nook-server"
if (-not (Test-Path $serverPath)) {
    throw "expected server artifact not found: $serverPath"
}

$packagedServerPath = Join-Path $packageDir "nook-server"
Copy-Item -LiteralPath $serverPath -Destination $packagedServerPath -Force
Write-Host ("[copy] {0} -> {1}" -f $serverPath, $packagedServerPath)

@("libnook-agent.so", "libncore.so") | ForEach-Object {
    $stalePackagePath = Join-Path $packageDir $_
    if (Test-Path $stalePackagePath) {
        Remove-Item -LiteralPath $stalePackagePath -Force -ErrorAction Stop
        Write-Host ("[clean] removed stale packaged sidecar {0}" -f $stalePackagePath)
    }
}

Get-Item $packagedServerPath, `
         (Join-Path $repoRoot "server\generated\nook_embedded_agent_blob.h"), `
         (Join-Path $repoRoot "server\generated\nook_embedded_ncore_blob.h"), `
         (Join-Path $repoRoot "server\generated\nook_embedded_zygote_helper_blob.h") |
    Select-Object FullName, Length, LastWriteTime

Remove-Item -LiteralPath $embeddedAgentSource -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $embeddedNcoreSource -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $embeddedZygoteHelperSource -Force -ErrorAction SilentlyContinue
