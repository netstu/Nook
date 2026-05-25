param(
    [string]$NdkRoot = $env:ANDROID_NDK_ROOT,
    [string]$Api = "30",
    [switch]$KeepIntermediates
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$stubSrcDir = Join-Path $repoRoot "server\symbi\stub_src"
$stubC = Join-Path $stubSrcDir "stub.c"
$stubH = Join-Path $stubSrcDir "stub.h"
$linkerScript = Join-Path $stubSrcDir "helper.lds"
$bin2Header = Join-Path $stubSrcDir "bin2header.py"
$generatedHeader = Join-Path $stubSrcDir "generated_stub.h"

if ([string]::IsNullOrWhiteSpace($NdkRoot)) {
    $defaultNdk = "E:\SDK\ndk\25.2.9519653"
    if (Test-Path $defaultNdk) {
        $NdkRoot = $defaultNdk
    }
}

if ([string]::IsNullOrWhiteSpace($NdkRoot) -or -not (Test-Path $NdkRoot)) {
    throw "ANDROID_NDK_ROOT not found. Set -NdkRoot or ANDROID_NDK_ROOT first."
}

$toolchainBin = Join-Path $NdkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin"
$clang = Join-Path $toolchainBin ("aarch64-linux-android{0}-clang.cmd" -f $Api)
$objcopy = Join-Path $toolchainBin "llvm-objcopy.exe"

if (-not (Test-Path $clang)) {
    throw "clang not found: $clang"
}
if (-not (Test-Path $objcopy)) {
    throw "llvm-objcopy not found: $objcopy"
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "python not found in PATH."
}

$outDir = Join-Path $repoRoot "build\symbi_stub"
New-Item -ItemType Directory -Force $outDir | Out-Null

$stubSo = Join-Path $outDir "stub_local.so"
$stubBin = Join-Path $outDir "stub_local.bin"

Push-Location $repoRoot
try {
    $clangArgs = @(
        "-shared",
        "-fPIC",
        "-nostdlib",
        "-Wl,-T,$linkerScript",
        "-Wl,--build-id=none",
        "-o", $stubSo,
        $stubC,
        "-I", $stubSrcDir
    )
    & $clang @clangArgs
    if ($LASTEXITCODE -ne 0) {
        throw "symbi stub compile failed"
    }

    & $objcopy -O binary -j .payload $stubSo $stubBin
    if ($LASTEXITCODE -ne 0) {
        throw "symbi stub objcopy failed"
    }

    & python $bin2Header $stubBin $generatedHeader stub_binary
    if ($LASTEXITCODE -ne 0) {
        throw "symbi stub header generation failed"
    }

    Write-Output "generated: $generatedHeader"
    Write-Output "binary: $stubBin"
    Write-Output "shared: $stubSo"
}
finally {
    Pop-Location
    if (-not $KeepIntermediates) {
        Remove-Item -LiteralPath $stubSo -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stubBin -Force -ErrorAction SilentlyContinue
    }
}
