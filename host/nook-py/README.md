# Nook Python Host Package

`host/nook-py` provides the Python host SDK and CLI for Nook.

It exposes:

- `nook-cli`
- `nook-gadget`

## Install

Install from source:

```powershell
pip install .
```

Verify:

```powershell
nook-cli --help
nook-gadget --help
```

## Prerequisite

`nook-cli` has two runtime paths:

- default server mode: requires a running rooted `nook-server`
- explicit gadget mode: use `--gadget` to attach to a gadgetized APK in `listen` mode

The Python package does not build or launch `nook-server` automatically. Download the matching `nook-server` release, then push and start it manually:

```powershell
adb push .\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

## Common Usage

Server attach:

```powershell
nook-cli -U com.demo.target -l .\hook.js
```

Server spawn:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
```

Gadget attach over the gadget listen socket:

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
```

List processes:

```powershell
nook-cli ps
```

List applications:

```powershell
nook-cli apps
```

Patch an APK with the gadget:

```powershell
nook-gadget patchapk --source .\target.apk
nook-cli patchapk .\target.apk
```

Patch an APK and package a startup script that runs inside the app on launch:

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\hook.js
nook-cli patchapk .\target.apk -s .\hook.js
```

Patch an APK in gadget `listen` mode and pause app startup until the host attaches:

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

Dump in-memory dex artifacts from a running target:

```powershell
nook-cli dexdump com.demo.target -U
```

Spawn a target, wait, then scan for dex artifacts:

```powershell
nook-cli dexdump --spawn com.demo.target -U --deep --sleep 3000
```

Install a patched APK:

```powershell
nook-gadget install --apk .\target-nook.apk
```

Launch the target app:

```powershell
nook-gadget launch --package com.demo.target --activity .MainActivity --stop-first --clear-logcat --wait 3
```

## Notes

- Rooted Android device required
- Current primary target is `arm64-v8a`
- `--strict-zygote-control` remains experimental
- `nook-gadget patchapk` and `nook-cli patchapk` currently expect a full Nook repo checkout because they reuse repo build/signing helpers
- `--gadget` is explicit by design; without it, `nook-cli -U ...` still targets `nook-server`
- gadget packaged startup-script mode and host-attached `listen` mode are both supported
- See [`docs/nook-gadget-usage.md`](../../docs/nook-gadget-usage.md) for the practical gadget workflow
- See [`docs/nook-dexdump-usage.md`](../../docs/nook-dexdump-usage.md) for the practical dexdump workflow
