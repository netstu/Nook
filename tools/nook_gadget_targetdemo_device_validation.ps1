[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet("", "listen-auto", "connect-manual-proxy")]
    [string]$Preset = "",
    [ValidateSet("listen", "connect")]
    [string]$InteractionType = "listen",
    [ValidateSet("auto-start", "manual")]
    [string]$StartupMode = "auto-start",
    [ValidateSet("minimal", "proxy-loader")]
    [string]$BootstrapMode = "minimal",
    [string]$InputApk = "E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk",
    [string]$OutputApk = "",
    [string]$GadgetLib = ".\libs\arm64-v8a\libnook-gadget.so",
    [string]$StartupScript = ".\host\nook-py\java_perform_startup_login.js",
    [switch]$StartupScriptRequired,
    [string]$Serial = "",
    [switch]$InstallAndLaunch,
    [switch]$TriggerPackagedStartup,
    [switch]$Sign,
    [string]$Keystore = "",
    [string]$Storepass = "",
    [string]$KeyAlias = "",
    [string]$Apktool = "E:\Re_tools\APKTool\apktool.bat",
    [string]$Apksigner = "E:\SDK\build-tools\34.0.0\apksigner.bat",
    [string]$Zipalign = "E:\SDK\build-tools\34.0.0\zipalign.exe",
    [switch]$DebugLogging,
    [int]$LaunchWaitSeconds = 4,
    [string]$ConnectHost = "127.0.0.1",
    [int]$ConnectPort = 27042,
    [string]$ListenAddress = "",
    [int]$ListenPort = 0,
    [switch]$StartNookServer,
    [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-DefaultKeystorePath {
    $candidates = @(
        (Join-Path $repoRoot "build\keystore\nook-debug.keystore"),
        $env:NOOK_GADGET_KEYSTORE
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return ""
}

if (-not [string]::IsNullOrWhiteSpace($Preset)) {
    switch ($Preset) {
        "listen-auto" {
            $InteractionType = "listen"
            $StartupMode = "auto-start"
            $BootstrapMode = "minimal"
            $InstallAndLaunch = $true
            $Sign = $true
        }
        "connect-manual-proxy" {
            $InteractionType = "connect"
            $StartupMode = "manual"
            $BootstrapMode = "proxy-loader"
            $InstallAndLaunch = $true
            $TriggerPackagedStartup = $true
            $StartNookServer = $true
            $Sign = $true
        }
    }
}

if ($Sign) {
    if ([string]::IsNullOrWhiteSpace($Keystore)) {
        $Keystore = Resolve-DefaultKeystorePath
    }
    if ([string]::IsNullOrWhiteSpace($Keystore)) {
        $Keystore = ".\build\keystore\nook-debug.keystore"
    }
    if ([string]::IsNullOrWhiteSpace($Storepass) -and -not [string]::IsNullOrWhiteSpace($env:NOOK_GADGET_STOREPASS)) {
        $Storepass = $env:NOOK_GADGET_STOREPASS
    }
    if ([string]::IsNullOrWhiteSpace($KeyAlias) -and -not [string]::IsNullOrWhiteSpace($env:NOOK_GADGET_KEYALIAS)) {
        $KeyAlias = $env:NOOK_GADGET_KEYALIAS
    }
    if ([string]::IsNullOrWhiteSpace($Storepass)) {
        $Storepass = "android"
    }
    if ([string]::IsNullOrWhiteSpace($KeyAlias)) {
        $KeyAlias = "androiddebugkey"
    }
}

if ($Sign -and -not $PrintOnly) {
    if ([string]::IsNullOrWhiteSpace($Keystore)) {
        $Keystore = Join-Path $repoRoot "build\keystore\nook-debug.keystore"
    }
    if (-not (Test-Path $Keystore)) {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\ensure_nook_debug_keystore.ps1") -KeystorePath $Keystore -Storepass $Storepass -Keypass $Storepass -KeyAlias $KeyAlias
        if ($LASTEXITCODE -ne 0) {
            throw "ensure_nook_debug_keystore.ps1 failed"
        }
    }
}

if (-not (Test-Path $GadgetLib)) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\build_nook_gadget.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "gadget library not found and build_nook_gadget.ps1 failed: $GadgetLib"
    }
}

if (-not (Test-Path $GadgetLib)) {
    throw "gadget library not found after auto-build: $GadgetLib"
}

if (-not [string]::IsNullOrWhiteSpace($StartupScript) -and -not (Test-Path $StartupScript)) {
    throw "startup script not found: $StartupScript"
}

if ([string]::IsNullOrWhiteSpace($OutputApk)) {
    if ($InteractionType -eq "connect") {
        $OutputApk = ".\build\nook-gadget\TargetDemo_1.0-connect-patched.apk"
    } else {
        $OutputApk = ".\build\nook-gadget\TargetDemo_1.0-patched.apk"
    }
}

if ($Sign -and -not $PrintOnly) {
    if ([string]::IsNullOrWhiteSpace($Keystore) -or -not (Test-Path $Keystore)) {
        throw "keystore not found for signed validation. Set -Keystore or NOOK_GADGET_KEYSTORE."
    }
    if ([string]::IsNullOrWhiteSpace($Storepass)) {
        throw "Storepass is required for signed validation. Set -Storepass or NOOK_GADGET_STOREPASS."
    }
    if ([string]::IsNullOrWhiteSpace($KeyAlias)) {
        throw "KeyAlias is required for signed validation. Set -KeyAlias or NOOK_GADGET_KEYALIAS."
    }
}

$wrapper = if ($InteractionType -eq "connect") {
    Join-Path $repoRoot "tools\nook_gadget_connect_validation.ps1"
} else {
    Join-Path $repoRoot "tools\nook_gadget_targetdemo_validation.ps1"
}

$args = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $wrapper,
    "-InputApk", $InputApk,
    "-OutputApk", $OutputApk,
    "-GadgetLib", $GadgetLib,
    "-StartupScript", $StartupScript,
    "-StartupMode", $StartupMode,
    "-BootstrapMode", $BootstrapMode,
    "-Apktool", $Apktool,
    "-Apksigner", $Apksigner,
    "-Zipalign", $Zipalign,
    "-LaunchWaitSeconds", $LaunchWaitSeconds
)

if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $args += @("-Serial", $Serial)
}

if ($StartupScriptRequired) {
    $args += "-StartupScriptRequired"
}
if ($DebugLogging) {
    $args += "-DebugLogging"
}
if ($InstallAndLaunch) {
    $args += "-InstallAndLaunch"
}
if ($TriggerPackagedStartup) {
    $args += "-TriggerPackagedStartup"
}
if ($Sign) {
    $args += @(
        "-Sign",
        "-Keystore", $Keystore,
        "-Storepass", $Storepass,
        "-KeyAlias", $KeyAlias
    )
}

if ($InteractionType -eq "connect") {
    $args += @(
        "-ConnectHost", $ConnectHost,
        "-ConnectPort", $ConnectPort
    )
    if ($StartNookServer) {
        $args += "-StartNookServer"
    }
} else {
    if (-not [string]::IsNullOrWhiteSpace($ListenAddress)) {
        $args += @("-ListenAddress", $ListenAddress)
    }
    if ($ListenPort -gt 0) {
        $args += @("-ListenPort", $ListenPort)
    }
}

Write-Host "[nook-gadget-targetdemo-device-validation] interaction_type=$InteractionType startup_mode=$StartupMode bootstrap_mode=$BootstrapMode"
if ($PrintOnly) {
    Write-Host "[nook-gadget-targetdemo-device-validation] command:"
    Write-Host ("powershell " + ($args -join " "))
    exit 0
}

& powershell @args
if ($LASTEXITCODE -ne 0) {
    throw "TargetDemo device validation failed"
}

Write-Host "[nook-gadget-targetdemo-device-validation] ok"
