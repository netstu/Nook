# Nook Gadget V2 Status

## Scope

This note freezes the current `nook-gadget` v2 contract after the completed
v2.1 runtime work and the first real v2.2 proxy-loader validation.

## Completed v2.1 deliverables

- gadget config now supports:
  - `interaction.type = listen | connect`
  - endpoint fields for listen/connect metadata
  - startup-script policy separate from `startup_mode`
- patch tooling now emits authoritative v2 config metadata through
  `tools/nook_patchapk.py`
- gadget runtime now supports:
  - explicit `listen` mode
  - explicit `connect` mode
  - config-driven control initialization
  - internal RPC handling for manual packaged startup loading
- host CLI/session surface now treats gadget sessions as first-class across
  listen/connect flows
- validation wrappers now cover:
  - explicit listen-mode validation
  - explicit connect-mode validation
  - PID-scoped startup-log matching

## Completed v2.2 deliverables

- patch tooling now exposes `--bootstrap-mode minimal|proxy-loader`
- proxy-loader patching now performs real bootstrap work instead of metadata-only
  declaration:
  - rewrites manifest `android:name` to a generated proxy application class
  - emits proxy smali that subclasses the original `Application`
  - preserves original app identity in patch metadata
  - strips class-level `final` when proxy subclassing requires it
  - strips method-level `final` from original `onCreate()V` when needed
  - selects `onCreate()` bootstrap when present, otherwise falls back to
    `attachBaseContext()`
- detached gadget runtime now tolerates runtime-ready notify failure after
  listen-mode control fallback so packaged startup scripts can still auto-load
  without a live `nook-server`
- real non-synthesized custom-`Application` validation is complete on:
  - APK: `E:\tmp\Nook-Gadget\build\device-apk-scan\icu.nullptr.applistdetector.apk`
  - package: `icu.nullptr.applistdetector`
  - manifest app class: `icu.nullptr.applistdetector.MyApplication`

## Authoritative evidence

- minimal bootstrap failure on the real sample:
  - `python .\tools\nook_patchapk.py ... --bootstrap-mode minimal --no-sign`
  - result: `no supported startup onCreate smali target found`
- proxy-loader success on the same sample:
  - signed install succeeded
  - detached cold launch succeeded
  - PID-scoped logs for pid `3243` included:
    - `script create ok name=startup.js script_id=1`
    - `script load ok script_id=1`
    - `startup script load ok path=assets/nook-gadget/startup.js script_id=1`
    - `dropping script message without control channel`
    - `D/NookCallable( 3243): hello-from-nook`

## Frozen supported contract

- supported:
  - `listen` gadget sessions with detached packaged startup
  - `connect` gadget sessions with a live device-side `nook-server`
  - `startup_mode=auto-start`
  - `startup_mode=manual` via `nook.gadget.load-configured-startup`
  - `bootstrap-mode=minimal` for simple startup targets
  - `bootstrap-mode=proxy-loader` for custom-`Application` takeover
  - `apktool` rebuild/sign/install workflow with `zipalign + apksigner`
- intentionally not frozen as supported:
  - provider-first or provider-only bootstrap takeover
  - generalized binary AndroidManifest editing
  - automatic tool-path discovery
  - generalized multi-target orchestration beyond the current validators

## Authoritative commands

- listen-mode baseline:
  - `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1 ...`
- connect-mode baseline:
  - `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_connect_validation.ps1 -StartNookServer ...`
- real proxy-loader custom-`Application` baseline:
  - `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_apk_validation.ps1 -SampleName applistdetector-proxy ... -BootstrapMode proxy-loader ...`

## Host CLI productization progress

The temporary gadget workspace now also includes the first usable
`nook-cli patchapk` productization slice on top of the frozen gadget runtime.

Current state on `2026-05-26`:

- CLI surface implemented:
  - `nook-cli patchapk <input.apk>`
  - `-o, --output`
  - `-s, --startup-script`
  - `--install`
  - `--launch`
  - `--usb`
  - `--serial`
- default command behavior implemented:
  - `bootstrap=proxy-loader`
  - `startup-mode=auto-start`
  - `interaction=listen`
  - `decode-backend=apktool`
  - sign enabled with repo debug keystore defaults
- new host helper module added:
  - `host/nook-py/nook/patchapk.py`
- current host helper responsibilities:
  - normalize `patchapk` command options
  - discover `apktool`, `apksigner`, and `zipalign`
  - inject repo-local gadget/signing defaults
  - invoke `tools/nook_patchapk.py`
  - optionally install and launch the rebuilt APK
  - emit connect-mode pre-launch guidance

Focused host verification completed for the current `patchapk` slice:

- `patchapk` parser surface tests
- patch-engine wrapper mapping tests
- tool discovery tests
- default signing tests
- install / launch orchestration tests
- help / user-facing error tests
- `host/nook-py/tests/test_nook_cli_local_surface.py`

Not yet recorded in this note:

- a new real-device `nook-cli patchapk ...` smoke command run
- full migration of the `patchapk` productization slice back into the main repo
