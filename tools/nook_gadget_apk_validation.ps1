param(
    [string]$SampleName = "sample",
    [string]$InputApk = "",
    [string]$OutputApk = ".\build\nook-gadget\patched-target.apk",
    [string]$GadgetLib = ".\libs\arm64-v8a\libnook-gadget.so",
    [ValidateSet("minimal", "proxy-loader")]
    [string]$BootstrapMode = "minimal",
    [string]$PackageName = "",
    [string]$LaunchActivity = "",
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
    [ValidateSet("internal-zip", "apktool")]
    [string]$DecodeBackend = "apktool",
    [string]$Apktool = "apktool",
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
    [string]$KeyAlias = "",
    [string]$StartupLogPattern = "script create ok|script load ok|Hooked successfully|dropping script message without control channel",
    [string]$PrimaryTargetName = "",
    [string]$PrimaryAttachScript = "",
    [string]$PrimaryTriggerDescription = "",
    [string]$SecondaryTargetName = "",
    [string]$SecondaryAttachScript = "",
    [string]$SecondaryTriggerDescription = "",
    [string]$SecondaryNote = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$logPrefix = "[$SampleName]"
$startCommand = "adb shell am start -n $LaunchActivity"

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $adbArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($Serial)) {
        $adbArgs += @("-s", $Serial)
    }
    $adbArgs += $Arguments

    & adb @adbArgs
    if ($LASTEXITCODE -ne 0) {
        throw "adb command failed: adb $($adbArgs -join ' ')"
    }
}

function Invoke-AdbCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $adbArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($Serial)) {
        $adbArgs += @("-s", $Serial)
    }
    $adbArgs += $Arguments

    $output = & adb @adbArgs
    if ($LASTEXITCODE -ne 0) {
        throw "adb command failed: adb $($adbArgs -join ' ')"
    }

    return $output
}

function Get-TargetPid {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPackage
    )

    $pidOutput = Invoke-AdbCapture -Arguments @("shell", "pidof", $TargetPackage)
    $pidText = (($pidOutput | Out-String).Trim())
    if ([string]::IsNullOrWhiteSpace($pidText)) {
        throw "Failed to resolve target pid for package: $TargetPackage"
    }

    $targetPid = ($pidText -split "\s+")[0]
    if ($targetPid -notmatch '^\d+$') {
        throw "Unexpected pidof output for package ${TargetPackage}: $pidText"
    }

    return $targetPid
}

function Get-TargetLogLines {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPid
    )

    $logcatArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($Serial)) {
        $logcatArgs += @("-s", $Serial)
    }
    $logcatArgs += @("logcat", "-d", "-v", "time", "--pid", $TargetPid)
    $logLines = & adb @logcatArgs
    if ($LASTEXITCODE -ne 0) {
        throw "adb command failed: adb $($logcatArgs -join ' ')"
    }

    return $logLines
}

if ([string]::IsNullOrWhiteSpace($InputApk)) {
    throw "InputApk is required. Pass -InputApk <path-to-target.apk>."
}
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    throw "PackageName is required. Pass -PackageName <package>."
}
if ([string]::IsNullOrWhiteSpace($LaunchActivity)) {
    throw "LaunchActivity is required. Pass -LaunchActivity <package/.Activity>."
}
if ($TriggerPackagedStartup -and -not $InstallAndLaunch) {
    throw "TriggerPackagedStartup requires -InstallAndLaunch."
}
if ($TriggerPackagedStartup -and $StartupMode -ne "manual") {
    throw "TriggerPackagedStartup requires -StartupMode manual."
}
if ($InteractionType -eq "connect" -and [string]::IsNullOrWhiteSpace($ConnectHost)) {
    throw "ConnectHost is required when -InteractionType connect."
}
if ($InteractionType -eq "connect" -and $ConnectPort -le 0) {
    throw "ConnectPort must be greater than zero when -InteractionType connect."
}

Write-Host "$logPrefix input_apk=$InputApk"
Write-Host "$logPrefix output_apk=$OutputApk"
Write-Host "$logPrefix gadget_lib=$GadgetLib"
Write-Host "$logPrefix bootstrap_mode=$BootstrapMode"
Write-Host "$logPrefix package=$PackageName"
Write-Host "$logPrefix launch_activity=$LaunchActivity"
Write-Host "$logPrefix decode_backend=$DecodeBackend"
Write-Host "$logPrefix startup_mode=$StartupMode"
Write-Host "$logPrefix transport_mode=$TransportMode"
Write-Host "$logPrefix interaction_type=$InteractionType"
if ($InteractionType -eq "connect") {
    Write-Host "$logPrefix connect_host=$ConnectHost"
    Write-Host "$logPrefix connect_port=$ConnectPort"
} else {
    Write-Host "$logPrefix listen_address=$ListenAddress"
    Write-Host "$logPrefix listen_port=$ListenPort"
}
Write-Host "$logPrefix debug_logging=$($DebugLogging.IsPresent)"
if (-not [string]::IsNullOrWhiteSpace($StartupScript)) {
    Write-Host "$logPrefix startup_script=$StartupScript"
    Write-Host "$logPrefix startup_script_required=$($StartupScriptRequired.IsPresent)"
}

$args = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "tools\nook_gadget_patch_smoke.ps1"),
    "-InputApk", $InputApk,
    "-OutputApk", $OutputApk,
    "-GadgetLib", $GadgetLib,
    "-BootstrapMode", $BootstrapMode,
    "-StartupScript", $StartupScript,
    "-StartupMode", $StartupMode,
    "-TransportMode", $TransportMode,
    "-InteractionType", $InteractionType,
    "-Python", $Python,
    "-DecodeBackend", $DecodeBackend,
    "-Apktool", $Apktool,
    "-Apksigner", $Apksigner,
    "-Zipalign", $Zipalign
)

if (-not [string]::IsNullOrWhiteSpace($ConnectHost)) {
    $args += @("-ConnectHost", $ConnectHost)
}
if ($ConnectPort -gt 0) {
    $args += @("-ConnectPort", $ConnectPort)
}
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
    throw "$SampleName patch validation failed"
}

if ($InstallAndLaunch) {
    if (-not $Sign) {
        throw "InstallAndLaunch requires -Sign so the rebuilt APK is installable."
    }

    Write-Host ""
    Write-Host "$logPrefix optional automated cold-start validation"
    Write-Host "  install: adb install -r -t $OutputApk"
    Write-Host "  stop: adb shell am force-stop $PackageName"
    Write-Host "  clear logs: adb logcat -c"
    Write-Host "  start: $startCommand"
    Write-Host "  startup_mode: $StartupMode"
    Write-Host "  interaction_type: $InteractionType"
    if ($InteractionType -eq "connect") {
        Write-Host "  note: connect mode requires a live Nook server before launch"
        Write-Host "  helper: powershell -ExecutionPolicy Bypass -File .\\tools\\device_start_nook_server.ps1"
    }
    if ($StartupMode -eq "auto-start") {
        Write-Host "  pid lookup: adb shell pidof $PackageName"
        Write-Host "  dump: adb logcat -d -v time --pid <resolved-pid>"
        Write-Host "  pattern: $StartupLogPattern"
    } else {
        Write-Host "  note: startup_mode=manual does not expect packaged startup-script cold-start logs"
        if ($TriggerPackagedStartup) {
            Write-Host "  trigger: powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target $PackageName"
        }
    }

    Invoke-Adb -Arguments @("install", "-r", "-t", $OutputApk)
    Invoke-Adb -Arguments @("shell", "am", "force-stop", $PackageName)
    Invoke-Adb -Arguments @("logcat", "-c")
    Invoke-Adb -Arguments @("shell", "am", "start", "-n", $LaunchActivity)
    Start-Sleep -Seconds $LaunchWaitSeconds
    $targetPid = Get-TargetPid -TargetPackage $PackageName
    Write-Host "$logPrefix resolved target pid=$targetPid"

    if ($StartupMode -eq "auto-start") {
        $logLines = Get-TargetLogLines -TargetPid $targetPid

        Write-Host "$logPrefix automated cold-start matches:"
        $matches = $logLines | Select-String -Pattern $StartupLogPattern
        if ($null -eq $matches -or $matches.Count -eq 0) {
            throw "No startup validation logs matched pattern: $StartupLogPattern"
        }
        $matches | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host "$logPrefix automated cold-start validation skipped startup-log matching because startup_mode=manual"
        if ($TriggerPackagedStartup) {
            $triggerArgs = @(
                "-ExecutionPolicy", "Bypass",
                "-File", (Join-Path $repoRoot "tools\nook_gadget_trigger_packaged_startup.ps1"),
                "-Target", $PackageName,
                "-NookCli", $NookCli
            )
            if (-not [string]::IsNullOrWhiteSpace($Serial)) {
                $triggerArgs += @("-Serial", $Serial)
            }
            & powershell @triggerArgs
            if ($LASTEXITCODE -ne 0) {
                throw "manual packaged startup trigger failed"
            }
            Write-Host "$logPrefix manual packaged startup trigger completed"

            Start-Sleep -Seconds 1
            $targetPid = Get-TargetPid -TargetPackage $PackageName
            Write-Host "$logPrefix resolved target pid after manual trigger=$targetPid"
            $manualLogLines = Get-TargetLogLines -TargetPid $targetPid

            Write-Host "$logPrefix manual packaged startup matches:"
            $manualMatches = $manualLogLines | Select-String -Pattern $StartupLogPattern
            if ($null -eq $manualMatches -or $manualMatches.Count -eq 0) {
                throw "No manual startup validation logs matched pattern: $StartupLogPattern"
            }
            $manualMatches | ForEach-Object { Write-Host $_ }
        }
    }
}

Write-Host ""
Write-Host "$logPrefix recommended hook validation targets:"
if ($InteractionType -eq "connect") {
    Write-Host "  connect mode requires a live Nook server before launch"
    Write-Host "     Helper: powershell -ExecutionPolicy Bypass -File .\\tools\\device_start_nook_server.ps1"
    Write-Host "     Expected gadget endpoint: ${ConnectHost}:$ConnectPort"
}
if (-not [string]::IsNullOrWhiteSpace($StartupScript)) {
    if ($StartupMode -eq "auto-start") {
        Write-Host "  0. authoritative gadget validation path"
        Write-Host "     Install and launch the patched app without attach -l."
        Write-Host "     Expected behavior: the packaged startup script is already active before any host attach."
        Write-Host "     If you attach later, use nook-cli attach $PackageName --usb only for observation."
    } else {
        Write-Host "  0. manual startup mode path"
        Write-Host "     Install and launch the patched app normally; the packaged startup script will not auto-load."
        Write-Host "     Start hooks later with the helper script, for example powershell -ExecutionPolicy Bypass -File .\\tools\\nook_gadget_trigger_packaged_startup.ps1 -Target $PackageName"
    }
}
if (-not [string]::IsNullOrWhiteSpace($PrimaryTargetName)) {
    Write-Host "  1. Primary target - ${PrimaryTargetName}: $PrimaryAttachScript"
    if (-not [string]::IsNullOrWhiteSpace($PrimaryTriggerDescription)) {
        Write-Host "     Expected trigger: $PrimaryTriggerDescription"
    }
    if (-not [string]::IsNullOrWhiteSpace($PrimaryAttachScript)) {
        Write-Host "     Attach command: nook-cli attach $PackageName -l $PrimaryAttachScript --wait --usb"
    }
}
if (-not [string]::IsNullOrWhiteSpace($SecondaryTargetName)) {
    Write-Host "  2. Secondary target - ${SecondaryTargetName}: $SecondaryAttachScript"
    if (-not [string]::IsNullOrWhiteSpace($SecondaryTriggerDescription)) {
        Write-Host "     Expected trigger: $SecondaryTriggerDescription"
    }
    if (-not [string]::IsNullOrWhiteSpace($SecondaryAttachScript)) {
        Write-Host "     Attach command: nook-cli attach $PackageName -l $SecondaryAttachScript --wait --usb"
    }
    if (-not [string]::IsNullOrWhiteSpace($SecondaryNote)) {
        Write-Host "     Note: $SecondaryNote"
    }
}
Write-Host "     Important: without --wait, the attach command unloads the script immediately after load."
Write-Host "     Observation-only attach: nook-cli attach <package-or-pid> --usb"
