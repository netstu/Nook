# Nook

Nook is a dynamic instrumentation toolkit for rooted Android devices.

This public source tree currently focuses on:

- Android
- root access
- arm64-v8a
- `spawn` / `attach`
- Java / native / memory instrumentation
- single-file deployment through `nook-server`
- Python CLI: `nook-cli`

## Quick Start

### 1. Build `nook-server`

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1 -ForceRebuild
```

Expected output:

```text
build/single-server-package/arm64-v8a/nook-server
```

If `ndk-build` is not in `PATH`, set:

```powershell
$env:NOOK_NDK_BUILD="E:\SDK\ndk\25.2.9519653\ndk-build.cmd"
```

### 2. Install `nook-cli`

From `host/nook-py`:

```powershell
python -m pip install .
```

Then verify:

```powershell
nook-cli --help
```

### 3. Deploy and start the server

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

If your device requires an explicit linker launch:

```powershell
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook:$LD_LIBRARY_PATH /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

## Basic Usage

### Attach

```powershell
nook-cli -U com.demo.target -l .\hook.js
```

### Spawn

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
```

### List apps / processes

```powershell
nook-cli apps
nook-cli ps
```

## Repository Layout

```text
build/android/      Android NDK build files
docs/               core architecture and design docs
examples/           basic examples
host/nook-py/       Python SDK and CLI
include/            public headers
server/             nook-server sources
src/                framework and runtime sources
tests/              unit and regression tests
third_party/        third-party dependencies
tools/              build and helper scripts
```

## Notes

- This repository is intended to be cloneable and buildable by contributors.
- `nook-cli` does not bundle the Android server binary.
- The current public target is rooted Android arm64.
