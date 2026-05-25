param(
    [string]$Serial = "",
    [string]$LocalServer = ".\build\single-server-package\arm64-v8a\nook-server",
    [string]$RuntimeDir = "/data/local/tmp/nook",
    [string]$RemotePath = "",
    [switch]$EnableZygoteControl = $true,
    [switch]$SkipPush,
    [switch]$SkipLogcatClear
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not (Test-Path $LocalServer)) {
    throw "local server not found: $LocalServer"
}

if ([string]::IsNullOrWhiteSpace($RemotePath)) {
    $RemotePath = "$RuntimeDir/nook-server"
}

$adbBaseArgs = @()
if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $adbBaseArgs += @("-s", $Serial)
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & adb @adbBaseArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed: $($Arguments -join ' ')"
    }
}

function Invoke-AdbCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = & adb @adbBaseArgs @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed: $($Arguments -join ' ')`n$output"
    }
    return ($output | Out-String).TrimEnd()
}

function Invoke-RootShellCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command
    )

    return Invoke-AdbCapture -Arguments @("shell", "su 0 -c '$Command'")
}

function Stop-NookServers {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RemoteRoot
    )

    $stopScriptTemplate = @'
ps -A -o PID,ARGS | grep 'nook-server' | grep -v grep | while read pid rest; do
  kill $pid >/dev/null 2>&1 || true
done
sleep 1
ps -A -o PID,ARGS | grep 'nook-server' | grep -v grep | while read pid rest; do
  kill -9 $pid >/dev/null 2>&1 || true
done
mkdir -p __RUNTIME_DIR__
rm -f __RUNTIME_DIR__/server.out __RUNTIME_DIR__/server.err
am force-stop com.ad2001.frida0x1 >/dev/null 2>&1 || true
'@ -replace "`r", ""
    $stopScript = $stopScriptTemplate.Replace("__RUNTIME_DIR__", $RuntimeDir)

    return Invoke-RootShellCapture -Command $stopScript
}

$localItem = Get-Item $LocalServer
$localHash = (Get-FileHash -LiteralPath $LocalServer -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host "[nook] local=$($localItem.FullName)"
Write-Host "[nook] local_sha256=$localHash"
Write-Host "[nook] local_size=$($localItem.Length)"
Write-Host "[nook] remote=$RemotePath"

Stop-NookServers -RemoteRoot $RemotePath
Start-Sleep -Seconds 1

if (-not $SkipPush) {
    Invoke-Adb -Arguments @("push", $LocalServer, $RemotePath)
}

$remoteMeta = Invoke-RootShellCapture -Command "chmod 755 $RemotePath; sha256sum $RemotePath; wc -c $RemotePath; ls -l $RemotePath"
Write-Host $remoteMeta

if (-not $SkipLogcatClear) {
    Invoke-Adb -Arguments @("logcat", "-c")
}

$launchFlags = @()
if ($EnableZygoteControl) {
    $launchFlags += "--enable-zygote-control"
}
$flagString = if ($launchFlags.Count -gt 0) { " " + ($launchFlags -join " ") } else { "" }
$launchCommand = "nohup /system/bin/linker64 $RemotePath$flagString >$RuntimeDir/server.out 2>$RuntimeDir/server.err < /dev/null &"
Invoke-RootShellCapture -Command $launchCommand | Out-Null

Start-Sleep -Seconds 2

$serverState = Invoke-RootShellCapture -Command "cat $RuntimeDir/server.err; echo ----; cat $RuntimeDir/server.out; echo ----; ps -A -o PID,PPID,USER,NAME,ARGS | grep nook-server"
Write-Host $serverState

$startupLines = Invoke-AdbCapture -Arguments @("logcat", "-d") |
    Select-String -Pattern "NookServer|NookCommApi|NookZygote" |
    Select-Object -ExpandProperty Line

if (-not $startupLines) {
    throw "no Nook startup logs captured"
}

$startupLines | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }

if (-not ($startupLines | Select-String -SimpleMatch "server started tcp=27042")) {
    throw "server start log missing"
}

Write-Host "[nook] server started and verified"
