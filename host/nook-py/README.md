# Nook Python Host Package

`host/nook-py` provides the Python host SDK and CLI for Nook.

It exposes:

- `nook-cli`

## Install

Install from source:

```powershell
pip install .
```

Verify:

```powershell
nook-cli --help
```

## Prerequisite

`nook-cli` expects a running `nook-server` on the Android device.

The Python package does not build or launch the Android server automatically. Download `nook-server` from the matching GitHub Release, then push and start it manually:

```powershell
adb push .\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

## Common Usage

Attach:

```powershell
nook-cli -U com.demo.target -l .\hook.js
```

Spawn:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
```

List processes:

```powershell
nook-cli ps
```

List applications:

```powershell
nook-cli apps
```

## Notes

- Rooted Android device required
- Current primary target is `arm64-v8a`
- `--strict-zygote-control` remains experimental
