param(
    [string]$InputApk = "E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk",
    [string]$OutputApk = ".\build\nook-gadget\TargetDemo_1.0-patched.apk",
    [string]$GadgetLib = ".\libs\arm64-v8a\libnook-gadget.so",
    [ValidateSet("minimal", "proxy-loader")]
    [string]$BootstrapMode = "minimal",
    [string]$StartupScript = "",
    [switch]$StartupScriptRequired,
    [ValidateSet("auto-start", "manual")]
    [string]$StartupMode = "auto-start",
    [ValidateSet("default")]
    [string]$TransportMode = "default",
    [string]$ListenAddress = "",
    [int]$ListenPort = 0,
    [switch]$DebugLogging,
    [string]$Python = "python",
    [string]$Apktool = "E:\Re_tools\APKTool\apktool.bat",
    [string]$Apksigner = "E:\SDK\build-tools\34.0.0\apksigner.bat",
    [string]$Zipalign = "E:\SDK\build-tools\34.0.0\zipalign.exe",
    [string]$Serial = "",
    [string]$NookCli = ".\tools\nook_cli_local.ps1",
    [switch]$InstallAndLaunch,
    [switch]$TriggerPackagedStartup,
    [int]$LaunchWaitSeconds = 4,
    [switch]$Sign,
    [string]$Jarsigner = "jarsigner",
    [string]$Keystore = "",
    [string]$Storepass = "",
    [string]$KeyAlias = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$args = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "tools\nook_gadget_apk_validation.ps1"),
    "-SampleName", "targetdemo",
    "-InputApk", $InputApk,
    "-OutputApk", $OutputApk,
    "-GadgetLib", $GadgetLib,
    "-BootstrapMode", $BootstrapMode,
    "-PackageName", "com.demo.target",
    "-LaunchActivity", "com.demo.target/.MainActivity",
    "-StartupScript", $StartupScript,
    "-StartupMode", $StartupMode,
    "-TransportMode", $TransportMode,
    "-InteractionType", "listen",
    "-Python", $Python,
    "-DecodeBackend", "apktool",
    "-Apktool", $Apktool,
    "-Apksigner", $Apksigner,
    "-Zipalign", $Zipalign,
    "-NookCli", $NookCli,
    "-LaunchWaitSeconds", $LaunchWaitSeconds,
    "-PrimaryTargetName", "Login page",
    "-PrimaryAttachScript", ".\host\nook-py\java_perform_smoke.js",
    "-PrimaryTriggerDescription", "open Login tab and submit any 6-char password.",
    "-SecondaryTargetName", "Ad page",
    "-SecondaryAttachScript", ".\host\nook-py\adwall_loadad.js",
    "-SecondaryTriggerDescription", "open the Ad Wall tab.",
    "-SecondaryNote", "AdWallFragment.loadAd() runs during app startup and is not a reliable attach-time validation target."
)

if (-not [string]::IsNullOrWhiteSpace($ListenAddress)) {
    $args += @("-ListenAddress", $ListenAddress)
}
if ($ListenPort -gt 0) {
    $args += @("-ListenPort", $ListenPort)
}

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
        "-Jarsigner", $Jarsigner,
        "-Keystore", $Keystore,
        "-Storepass", $Storepass,
        "-KeyAlias", $KeyAlias
    )
}

& powershell @args
if ($LASTEXITCODE -ne 0) {
    throw "TargetDemo patch validation failed"
}
