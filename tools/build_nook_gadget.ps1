[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$NdkBuild = $env:NOOK_NDK_BUILD,
    [string]$Abi = "arm64-v8a",
    [switch]$ForceRebuild,
    [switch]$IncludeSmoke
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

$modules = @("nook_gadget")
if ($IncludeSmoke) {
    $modules += "nook_gadget_smoke"
}

$arguments = @(
    "NDK_PROJECT_PATH=.",
    "APP_BUILD_SCRIPT=build/android/Android.mk",
    "NDK_APPLICATION_MK=build/android/Application_static.mk",
    "APP_ABI=$Abi",
    ("APP_MODULES=" + ($modules -join " ")),
    "-j4"
)

if ($ForceRebuild) {
    $arguments = @("-B") + $arguments
}

Write-Host ("[build-nook-gadget] modules={0}" -f ($modules -join ","))
& $NdkBuild @arguments
if ($LASTEXITCODE -ne 0) {
    throw "ndk-build failed for modules: $($modules -join ' ')"
}

$outputDir = Join-Path $repoRoot ("libs\" + $Abi)
$artifacts = @("libnook-gadget.so")
if ($IncludeSmoke) {
    $artifacts += "libnook_gadget_smoke.so"
}

foreach ($artifact in $artifacts) {
    $path = Join-Path $outputDir $artifact
    if (-not (Test-Path $path)) {
        throw "expected gadget artifact not found: $path"
    }
    Get-Item $path | Select-Object FullName, Length, LastWriteTime
}

Write-Host ("[build-nook-gadget] ok abi={0}" -f $Abi)
