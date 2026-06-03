[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Python = "python",
    [string]$Cxx = "g++",
    [switch]$BuildArtifactIfMissing = $true
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$buildRoot = Join-Path $repoRoot "build\test-bin\gadget-local-validation"
New-Item -ItemType Directory -Force $buildRoot | Out-Null

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host "[nook-gadget-local-validation] $Name"
    & $Action
}

function Invoke-CompileAndRun {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string[]]$Sources,
        [string[]]$IncludeDirs = @("include", "src")
    )

    $output = Join-Path $buildRoot "$Name.exe"
    $args = @("-std=c++17")
    foreach ($includeDir in $IncludeDirs) {
        $args += "-I$includeDir"
    }
    $args += $Sources
    $args += @("-o", $output)

    & $Cxx @args
    if ($LASTEXITCODE -ne 0) {
        throw "compile failed: $Name"
    }

    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "run failed: $Name"
    }
}

$gadgetLib = Join-Path $repoRoot "libs\arm64-v8a\libnook-gadget.so"
if (-not (Test-Path $gadgetLib)) {
    if (-not $BuildArtifactIfMissing) {
        throw "missing gadget artifact: $gadgetLib"
    }

    Invoke-Step -Name "build nook_gadget artifact" -Action {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\build_nook_gadget.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "build_nook_gadget.ps1 failed"
        }
    }
}

Invoke-Step -Name "py_compile patch tools" -Action {
    & $Python -m py_compile `
        tools\nook_patchapk.py `
        tools\nook_patchapk_local_smoke.py `
        host\nook-py\nook\gadget_cli.py
    if ($LASTEXITCODE -ne 0) {
        throw "python compile failed"
    }
}

Invoke-Step -Name "run host python gadget tests" -Action {
    Push-Location (Join-Path $repoRoot "host\nook-py")
    try {
        & $Python -m unittest tests.test_gadget_cli tests.test_packaging_metadata
        if ($LASTEXITCODE -ne 0) {
            throw "host python gadget tests failed"
        }
    } finally {
        Pop-Location
    }
}

Invoke-Step -Name "build test_nook_patchapk_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_patchapk_surface" `
        -Sources @("tests/headers/test_nook_patchapk_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_patchapk_local_smoke_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_patchapk_local_smoke_surface" `
        -Sources @("tests/headers/test_nook_patchapk_local_smoke_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_build_nook_gadget_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_build_nook_gadget_surface" `
        -Sources @("tests/headers/test_build_nook_gadget_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_gadget_local_validation_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_local_validation_surface" `
        -Sources @("tests/headers/test_nook_gadget_local_validation_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_gadget_build_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_build_surface" `
        -Sources @("tests/headers/test_nook_gadget_build_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_gadget_smoke_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_smoke_surface" `
        -Sources @("tests/headers/test_nook_gadget_smoke_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_gadget_smoke_workflow_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_smoke_workflow_surface" `
        -Sources @("tests/headers/test_nook_gadget_smoke_workflow_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "build test_nook_gadget_config" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_config" `
        -Sources @(
            "tests/headers/test_nook_gadget_config.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_control_channel" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_control_channel" `
        -Sources @(
            "tests/headers/test_nook_gadget_control_channel.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_runtime_bridge" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_runtime_bridge" `
        -Sources @(
            "tests/headers/test_nook_gadget_runtime_bridge.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_runtime_init" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_runtime_init" `
        -Sources @(
            "tests/headers/test_nook_gadget_runtime_init.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_startup_rpc" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_startup_rpc" `
        -Sources @(
            "tests/headers/test_nook_gadget_startup_rpc.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_startup_script" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_startup_script" `
        -Sources @(
            "tests/headers/test_nook_gadget_startup_script.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_entry_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_entry_surface" `
        -Sources @(
            "tests/headers/test_nook_gadget_entry_surface.cpp",
            "tests/headers/nook_gadget_runtime_test_stubs.cpp",
            "src/gadget/nook_gadget_entry.cpp",
            "src/gadget/nook_gadget_runtime.cpp",
            "src/gadget/nook_gadget_config.cpp"
        )
}

Invoke-Step -Name "build test_nook_gadget_targetdemo_device_validation_surface" -Action {
    Invoke-CompileAndRun `
        -Name "test_nook_gadget_targetdemo_device_validation_surface" `
        -Sources @("tests/headers/test_nook_gadget_targetdemo_device_validation_surface.cpp") `
        -IncludeDirs @()
}

Invoke-Step -Name "run patch smoke minimal" -Action {
    & $Python tools\nook_patchapk_local_smoke.py --bootstrap-mode minimal
    if ($LASTEXITCODE -ne 0) {
        throw "minimal patch smoke failed"
    }
}

Invoke-Step -Name "run patch smoke proxy-loader" -Action {
    & $Python tools\nook_patchapk_local_smoke.py --bootstrap-mode proxy-loader
    if ($LASTEXITCODE -ne 0) {
        throw "proxy-loader patch smoke failed"
    }
}

Write-Host "[nook-gadget-local-validation] ok"
