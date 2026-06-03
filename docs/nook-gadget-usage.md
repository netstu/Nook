# Nook Gadget Usage

## What It Is

`nook-gadget` patches the Nook runtime directly into an APK so the target app can load `libnook-gadget.so` inside its own process at startup.

Current primary use cases:

- package a startup script into the APK and auto-run it
- patch the APK in `listen` mode and attach later from the host

`nook-gadget` is different from rooted `nook-server`:

- `nook-server` runs as a separate device-side process and is the default target of `nook-cli -U ...`
- `nook-gadget` runs inside the app process and must be selected explicitly with `--gadget`

## Main Modes

### 1. Packaged Startup Script

Patch the APK and package a script that runs when the app starts:

```powershell
nook-cli patchapk .\target.apk -s .\startup.js
```

Equivalent short command:

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\startup.js
```

Use this when:

- the same script should always run on app startup
- you want startup-time hook logic without attaching from the host first

### 2. Listen Mode

Patch the APK so gadget waits for a later host attach:

```powershell
nook-cli patchapk .\target.apk
```

Then attach from the host:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

Important:

- `--gadget` is required
- without `--gadget`, `nook-cli -U ...` still targets rooted `nook-server`

### 3. Listen Mode With Wait Gate

Patch the APK so startup pauses until the host loads a script and resumes the process:

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

Then attach:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

This is the preferred mode when you need earlier startup coverage.

### 4. Connect Mode

Patch the APK so gadget connects outward:

```powershell
nook-cli patchapk .\target.apk --interaction connect --connect-host 127.0.0.1 --connect-port 27042
```

This mode exists, but the current recommended daily workflow is still `listen`.

## Common Workflow

### Patch

```powershell
nook-gadget patchapk --source .\target.apk --on-load wait --output .\build\target-gadget.apk
```

### Install

```powershell
nook-gadget install --apk .\build\target-gadget.apk
```

### Launch

```powershell
nook-gadget launch --package com.demo.target --activity .MainActivity --stop-first
```

### Attach

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

## `on_load` Behavior

### `resume`

- app startup is not paused
- gadget starts and the app keeps running
- host can attach later
- very early startup logic may already have passed

### `wait`

- app startup pauses behind a gadget gate
- host attaches and loads the script first
- process resumes after the gadget session is ready
- this is better for early hook timing

## Command Surfaces

### Unified CLI

```powershell
nook-cli patchapk .\target.apk -s .\startup.js
nook-cli -U --gadget com.demo.target -l .\hook.js
```

### Gadget-Focused CLI

```powershell
nook-gadget patchapk --source .\target.apk
nook-gadget install --apk .\target-gadget.apk
nook-gadget launch --package com.demo.target --activity .MainActivity
```

Recommended split:

- use `nook-gadget` for patch, install, and launch
- use `nook-cli -U --gadget ...` for live host attach

## Dependencies

Current patch workflow expects a full Nook repository checkout.

Required for patching:

- Python
- repository-local `tools/nook_patchapk.py`
- built `libs/arm64-v8a/libnook-gadget.so`
- `apktool`
- Android build-tools `zipalign`
- Android build-tools `apksigner`
- repository-local debug keystore

Required for install and launch:

- `adb`

If `libnook-gadget.so` is missing, `nook-gadget patchapk` can try to build it from the repo build scripts.

## Current Output Behavior

`patchapk` now prints:

- mode summary
- stage progress such as `[1/5] decode apk`
- heartbeat lines for long-running stages such as `apktool build still running... 10s elapsed`
- fallback notices when `apktool` rebuild paths degrade
- final output APK path

## Troubleshooting

### `nook-cli -U com.demo.target -l hook.js` connects to the wrong runtime

Cause:

- `--gadget` was omitted

Fix:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

### App starts too early and the hook misses startup logic

Use:

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

### `patchapk` appears stuck

Current builds print stage progress and heartbeats during long `apktool` phases. If output stops entirely, check:

- `apktool`
- `apksigner`
- `zipalign`
- antivirus or filesystem interference
- broken source APK resources

## Current Scope

Current primary target:

- rooted Android workflow for `nook-server`
- in-app patched `nook-gadget` workflow for APK testing
- `arm64-v8a`

Current main gadget path:

- `listen`
- optional packaged startup script
- optional `on_load=wait`
