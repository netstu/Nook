param(
    [string]$InputApk = "",
    [string]$OutputApk = ".\build\nook-gadget\patched-target.apk",
    [string]$GadgetLib = ".\libs\arm64-v8a\libnook-gadget.so",
    [ValidateSet("minimal", "proxy-loader")]
    [string]$BootstrapMode = "minimal",
    [string]$StartupScript = "",
    [switch]$StartupScriptRequired,
    [ValidateSet("auto-start", "manual")]
    [string]$StartupMode = "auto-start",
    [ValidateSet("default")]
    [string]$TransportMode = "default",
    [ValidateSet("listen", "connect")]
    [string]$InteractionType = "listen",
    [string]$ConnectHost = "",
    [int]$ConnectPort = 0,
    [string]$ListenAddress = "",
    [int]$ListenPort = 0,
    [switch]$DebugLogging,
    [string]$Python = "python",
    [string]$Serial = "",
    [ValidateSet("internal-zip", "apktool")]
    [string]$DecodeBackend = "internal-zip",
    [string]$Apktool = "apktool",
    [string]$Jarsigner = "jarsigner",
    [string]$Apksigner = "E:\SDK\build-tools\34.0.0\apksigner.bat",
    [string]$Zipalign = "E:\SDK\build-tools\34.0.0\zipalign.exe",
    [string]$Keystore = "",
    [string]$Storepass = "",
    [string]$KeyAlias = "",
    [switch]$Sign
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($InputApk)) {
    throw "InputApk is required. Pass -InputApk <path-to-target.apk>."
}

if (-not (Test-Path $GadgetLib)) {
    throw "gadget library not found: $GadgetLib`nBuild it first with: powershell -ExecutionPolicy Bypass -File .\tools\build_nook_gadget.ps1"
}

$patchTool = Join-Path $repoRoot "tools\nook_patchapk.py"
if (-not (Test-Path $patchTool)) {
    throw "patch tool not found: $patchTool"
}

$outputDir = Split-Path -Parent $OutputApk
if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
    New-Item -ItemType Directory -Force $outputDir | Out-Null
}

Write-Host "[nook-gadget] repo=$repoRoot"
Write-Host "[nook-gadget] input_apk=$InputApk"
Write-Host "[nook-gadget] output_apk=$OutputApk"
Write-Host "[nook-gadget] gadget_lib=$GadgetLib"
Write-Host "[nook-gadget] bootstrap_mode=$BootstrapMode"
Write-Host "[nook-gadget] decode_backend=$DecodeBackend"
Write-Host "[nook-gadget] startup_mode=$StartupMode"
Write-Host "[nook-gadget] transport_mode=$TransportMode"
Write-Host "[nook-gadget] interaction_type=$InteractionType"
if ($InteractionType -eq "connect") {
    Write-Host "[nook-gadget] connect_host=$ConnectHost"
    Write-Host "[nook-gadget] connect_port=$ConnectPort"
} else {
    Write-Host "[nook-gadget] listen_address=$ListenAddress"
    Write-Host "[nook-gadget] listen_port=$ListenPort"
}
Write-Host "[nook-gadget] debug_logging=$($DebugLogging.IsPresent)"
if (-not [string]::IsNullOrWhiteSpace($StartupScript)) {
    Write-Host "[nook-gadget] startup_script=$StartupScript"
    Write-Host "[nook-gadget] startup_script_required=$($StartupScriptRequired.IsPresent)"
}

$adbPrefix = "adb "
if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $adbPrefix = "adb -s $Serial "
}

$patchArgs = @(
    $patchTool,
    "--input-apk", $InputApk,
    "--output-apk", $OutputApk,
    "--abi", "arm64-v8a",
    "--gadget-lib", $GadgetLib,
    "--bootstrap-mode", $BootstrapMode,
    "--decode-backend", $DecodeBackend,
    "--startup-mode", $StartupMode,
    "--transport-mode", $TransportMode,
    "--interaction-type", $InteractionType
)

if (-not [string]::IsNullOrWhiteSpace($ConnectHost)) {
    $patchArgs += @("--connect-host", $ConnectHost)
}
if ($ConnectPort -gt 0) {
    $patchArgs += @("--connect-port", $ConnectPort)
}
if (-not [string]::IsNullOrWhiteSpace($ListenAddress)) {
    $patchArgs += @("--listen-address", $ListenAddress)
}
if ($ListenPort -gt 0) {
    $patchArgs += @("--listen-port", $ListenPort)
}

if ($DecodeBackend -eq "apktool") {
    $patchArgs += @("--apktool", $Apktool)
}

if (-not [string]::IsNullOrWhiteSpace($StartupScript)) {
    if (-not (Test-Path $StartupScript)) {
        throw "startup script not found: $StartupScript"
    }
    $patchArgs += @("--startup-script", $StartupScript)
}

if ($StartupScriptRequired) {
    $patchArgs += "--startup-script-required"
}

if ($DebugLogging) {
    $patchArgs += "--debug-logging"
}

if ($Sign) {
    if ([string]::IsNullOrWhiteSpace($Keystore) -or [string]::IsNullOrWhiteSpace($Storepass) -or [string]::IsNullOrWhiteSpace($KeyAlias)) {
        throw "Sign requested, but Keystore, Storepass, and KeyAlias are required."
    }
    if ([string]::IsNullOrWhiteSpace($Apksigner) -or -not (Test-Path $Apksigner)) {
        throw "Sign requested, but Apksigner is missing or not found: $Apksigner"
    }
    if ([string]::IsNullOrWhiteSpace($Zipalign) -or -not (Test-Path $Zipalign)) {
        throw "Sign requested, but Zipalign is missing or not found: $Zipalign"
    }

    $patchArgs += @(
        "--jarsigner", $Jarsigner,
        "--apksigner", $Apksigner,
        "--zipalign", $Zipalign,
        "--keystore", $Keystore,
        "--storepass", $Storepass,
        "--key-alias", $KeyAlias
    )
    Write-Host "[nook-gadget] signing=enabled"
} else {
    $patchArgs += "--no-sign"
    Write-Host "[nook-gadget] signing=disabled"
}

& $Python @patchArgs
if ($LASTEXITCODE -ne 0) {
    throw "patch step failed"
}

Write-Host ""
Write-Host "[nook-gadget] patch step completed"
Write-Host "[nook-gadget] next manual steps:"
if (-not [string]::IsNullOrWhiteSpace($StartupScript)) {
    Write-Host "  - startup script is packaged into assets/nook-gadget/startup.js"
    if ($StartupMode -eq "auto-start") {
        Write-Host "  - launch the patched app once before attaching; startup hook should already be active"
    } else {
        Write-Host "  - startup_mode=manual disables packaged startup-script auto-load on cold start"
    }
}
if (-not $Sign) {
    Write-Host "  1. Sign the patched APK with your preferred Android signing flow."
    Write-Host "  2. Install it: ${adbPrefix}install -r $OutputApk"
    if ($InteractionType -eq "connect") {
        Write-Host "  3. Ensure the device-side Nook server is already running before launch."
        Write-Host "     interaction_type=connect requires a live device-side Nook server before launch"
        Write-Host "     Example: powershell -ExecutionPolicy Bypass -File .\\tools\\device_start_nook_server.ps1"
        Write-Host "  4. Launch the target app on device."
        if ($StartupMode -eq "auto-start") {
            Write-Host "  5. Attach from host: nook-cli attach <package-or-pid> --usb"
            Write-Host "  6. Load a script after attach only if you need live inspection."
        } else {
            Write-Host "  5. Trigger the packaged startup script later through gadget RPC when you want hooks to start."
            Write-Host "     RPC method: nook.gadget.load-configured-startup"
            Write-Host "  6. Example: powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target <package-or-pid>"
        }
    } else {
        Write-Host "  3. Launch the target app on device."
        if ($StartupMode -eq "auto-start") {
            Write-Host "  4. Attach from host: nook-cli attach <package-or-pid> --usb"
            Write-Host "  5. Load a script after attach only if you need live inspection."
        } else {
            Write-Host "  4. Trigger the packaged startup script later through gadget RPC when you want hooks to start."
            Write-Host "     RPC method: nook.gadget.load-configured-startup"
            Write-Host "  5. Example: powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target <package-or-pid>"
        }
    }
} else {
    Write-Host "  1. Install it: ${adbPrefix}install -r $OutputApk"
    if ($InteractionType -eq "connect") {
        Write-Host "  2. Ensure the device-side Nook server is already running before launch."
        Write-Host "     interaction_type=connect requires a live device-side Nook server before launch"
        Write-Host "     Example: powershell -ExecutionPolicy Bypass -File .\\tools\\device_start_nook_server.ps1"
        Write-Host "  3. Launch the target app on device."
        if ($StartupMode -eq "auto-start") {
            Write-Host "  4. Attach from host: nook-cli attach <package-or-pid> --usb"
            Write-Host "  5. Load a script after attach only if you need live inspection."
        } else {
            Write-Host "  4. Trigger the packaged startup script later through gadget RPC when you want hooks to start."
            Write-Host "     RPC method: nook.gadget.load-configured-startup"
            Write-Host "  5. Example: powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target <package-or-pid>"
        }
    } else {
        Write-Host "  2. Launch the target app on device."
        if ($StartupMode -eq "auto-start") {
            Write-Host "  3. Attach from host: nook-cli attach <package-or-pid> --usb"
            Write-Host "  4. Load a script after attach only if you need live inspection."
        } else {
            Write-Host "  3. Trigger the packaged startup script later through gadget RPC when you want hooks to start."
            Write-Host "     RPC method: nook.gadget.load-configured-startup"
            Write-Host "  4. Example: powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target <package-or-pid>"
        }
    }
}
