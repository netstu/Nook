# Nook Gadget Validation Status

## Supported v1 scope

- Distinct `libnook-gadget.so` build target exists.
- Gadget runtime startup uses the gadget entrypoint and the control-only host path.
- Patch tool can:
  - parse the first-class patch CLI surface
  - place `libnook-gadget.so` into `lib/arm64-v8a/`
  - create `lib/arm64-v8a/` when the source APK has no native-lib directories at all
  - emit `assets/nook-gadget/config.json`
  - optionally package a startup script into `assets/nook-gadget/startup.js`
  - inject `System.loadLibrary("nook-gadget")` into a decoded `Application.onCreate()` smali target
  - fall back to launcher `Activity.onCreate(Bundle)` when the APK has no custom `Application`
  - resolve startup target smali across `smali*` trees produced by `apktool`
  - unpack and rebuild a synthetic ZIP/APK with the `internal-zip` backend
  - decode and rebuild through `apktool` when `--decode-backend apktool` is selected
  - preserve installable native-lib packaging for `apktool` rebuilds by syncing injected `libnook-gadget.so` into `apktool.yml -> doNotCompress` when needed
  - align the rebuilt APK through `zipalign` before signing
  - sign the rebuilt APK through `apksigner` when keystore credentials are provided
  - retain the legacy `jarsigner` signing path as a non-primary fallback
- Host attach can backfill missing attach `process_name` from cached runtime-ready identity.
- Real sample validation confirmed the `apktool` + `--no-sign` patch path against:
  - `E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk`
  - current authoritative runtime validation target is the login page hook path, not the cold-start ad page hook path
- Real device startup validation has now confirmed:
  - the packaged startup script loads on cold start without manual `attach -l`
  - the login-page hook is the authoritative success signal for the current sample
  - later host attach is optional and only needed for observation/control
  - three independent launcher-fallback APK samples now pass signed install + cold-start gadget validation
  - one synthesized custom-`Application` sample now passes signed install + cold-start gadget validation on the explicit `Application.onCreate()` branch
  - one non-synthesized custom-`Application` sample now passes signed install + cold-start gadget validation through the `proxy-loader` bootstrap path

## Smoke harness

- Use `tools/nook_gadget_apk_validation.ps1` as the generic real-APK validation entrypoint.
  - it centralizes patch, optional signed install, cold launch, and startup-log observation
  - it now resolves the launched target PID and scopes startup-log matching to `adb logcat --pid <resolved-pid>` so unrelated instrumented apps cannot satisfy the validator by accident
  - it now accepts explicit `listen` / `connect` interaction validation inputs so v2.1 config metadata can be exercised without inventing a separate patch flow
- sample-specific wrappers should only provide default package/activity/script hints
- Use `tools/nook_gadget_patch_smoke.ps1` to:
  - validate `libnook-gadget.so` exists
  - run `tools/nook_patchapk.py` with either `internal-zip` or `apktool`
  - optionally forward `--startup-script` for packaged gadget startup hooks
  - optionally forward `--startup-script-required`, `--startup-mode`, `--transport-mode`, `--interaction-type`, endpoint fields, and `--debug-logging`
  - optionally forward `zipalign` + `apksigner` signing credentials
  - print follow-up install/launch/attach guidance that now distinguishes `auto-start` vs `manual` startup mode and calls out the device-side server precondition for `connect`
- Use `tools/nook_patchapk_local_smoke.py` as the no-device preflight check before real APK or device validation.
  - it synthesizes a minimal APK fixture with no original native-lib directory
  - it validates startup-script packaging, `config.json` emission, synthetic `lib/arm64-v8a/` creation, and launcher-activity `loadLibrary("nook-gadget")` injection
  - it now covers both `bootstrap_mode=minimal` and `bootstrap_mode=proxy-loader`
- Use `tools/build_nook_gadget.ps1` to build `libnook-gadget.so` before patch or validation work.
  - it targets the current `build/android/Android.mk` + `Application_static.mk` path
  - it can optionally build `nook_gadget_smoke` together with the main gadget artifact
- Use `tools/nook_gadget_local_validation.ps1` as the one-command host-side validation wrapper before device work.
  - it compiles and runs the gadget-focused host regression binaries
  - it runs both local patch smoke modes after the host regression suite
  - it now auto-builds `libnook-gadget.so` through `tools/build_nook_gadget.ps1` when the artifact is missing
- Current harness is intentionally explicit; it does not automate device interaction or signing credential discovery.
- Use `tools/nook_gadget_targetdemo_validation.ps1` for the current representative explicit listen-mode sample workflow, including optional automated install/cold-launch/log validation.
- Use `tools/nook_gadget_connect_validation.ps1` for the matching TargetDemo connect-mode patch/validation entrypoint.
  - it defaults to `ConnectHost=127.0.0.1` and `ConnectPort=27042`
  - it can optionally start the device-side server first through `tools/device_start_nook_server.ps1`
- Track broader sample coverage in `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`.

## Known gaps

- Manifest rewrite is a marker-only text rewrite, not a full Android binary manifest editor.
- No tool auto-discovery or environment validation is built into `nook_patchapk.py`; callers must provide working external tool paths when using `apktool` or signing.
- Real-device end-to-end validation is now script-assisted for the current sample, but it is still not generalized into a reusable multi-target device orchestration layer.
- `proxy-loader` currently covers custom-`Application` takeover only; broader takeover paths such as provider-first bootstrap remain future compatibility breadth, not part of the frozen v2 contract.
- `startup_mode=manual` now suppresses packaged startup-script auto-load until you explicitly trigger `nook.gadget.load-configured-startup` through the host `call --attach` flow.
- `tools/nook_gadget_trigger_packaged_startup.ps1` now wraps that manual packaged-startup RPC into a one-command helper for package-or-pid targets.
- Manual packaged-startup validation now checks post-trigger runtime evidence instead of stopping at RPC success:
  - the helper must return success
  - the validator must then observe startup-script create/load or hook-install logs after the trigger
- The manual packaged startup trigger is now part of the authoritative validation path for `startup_mode=manual`.
- PID-scoped startup-log validation is now part of the authoritative validation path for both `auto-start` and `manual` modes.
- Real-device `connect` mode evidence is now available for the TargetDemo baseline through the dedicated wrapper and device-side server bring-up path.
- The local `tools/nook_patchapk_local_smoke.py` runner is now part of the recommended pre-device validation path for patch-tool changes.
- The local `tools/nook_gadget_local_validation.ps1` wrapper is now the recommended single-command host-side validation entrypoint before real-device patch/install checks.

## Startup script status

- Packaging support for a default gadget startup script is implemented in the patch tooling surface.
- `startup_mode=auto-start` now drives the authoritative cold-start gadget path.
- `startup_mode=manual` now suppresses packaged startup-script auto-load, the validator skips cold-start startup-log matching in that mode, and the packaged asset can be loaded later through `nook.gadget.load-configured-startup`.
- `startup_mode=manual` validation now performs post-trigger log matching after `nook.gadget.load-configured-startup` so a passing run proves the packaged script actually loaded.
- `startup_script.required=true` now propagates through `InitializeRuntime()` instead of being swallowed after helper-level failure.
- `debug_logging=true` now enables additional startup-path gadget debug logs without changing the existing error-log path.
- unsupported `transport_mode` values now fail gadget control initialization instead of being silently ignored.
- The validated v1 flow is:
  - patch APK with `--startup-script`
  - launch the app without `attach -l`
  - use the login page as the authoritative gadget validation path
- Real-device validation on `TargetDemo` found and fixed three runtime blockers before cold-start success:
  - control-channel setup failure aborted gadget init before the bridge/startup path ran
  - global `ScriptRegistry` initialization caused a startup crash (`std::overflow_error: __next_prime overflow`)
  - detached startup-script `send(...)` failed when no host control channel was attached
- Signed install validation on Android 11+ found and fixed one packaging blocker:
  - `zipalign + jarsigner` still produced an APK that failed install-time alignment checks
  - the current installable signing path is `zipalign + apksigner`
- Current detached behavior is intentional for v1:
  - if no host control channel is active, script `send(...)` messages are dropped
  - startup script load still succeeds so the hook can remain active
- Current detached behavior is now also intentional for v2 `proxy-loader` listen-mode fallback:
  - if control-channel bring-up fails and runtime-ready notify cannot reach a server, gadget runtime still completes startup-script initialization
  - this preserves Frida-Gadget-style detached cold-start behavior on repackaged apps that launch without a live `nook-server`

## Latest evidence

- Real-device logs from the final validation included:
  - `script create ok name=startup.js script_id=1`
  - `script load ok script_id=1`
  - `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
  - `dropping script message without control channel`
- PID-scoped `TargetDemo` auto-start validation on `2026-05-19` also confirmed:
  - validator resolved `com.demo.target` to pid `22327`
  - pid-filtered logs included `script create ok name=startup.js script_id=1`
  - pid-filtered logs included `script load ok script_id=1`
  - pid-filtered logs included `startup script load ok path=assets/nook-gadget/startup.js script_id=1`
  - pid-filtered logs included `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
- Manual packaged-startup validation on `2026-05-19` also confirmed:
  - `nook.gadget.load-configured-startup` returned `loaded=true`
  - the post-trigger log dump included `script create ok name=startup.js script_id=1`
  - the post-trigger log dump included `script load ok script_id=1`
  - the post-trigger log dump included `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
- PID-scoped `TargetDemo` manual packaged-startup validation on `2026-05-19` also confirmed:
  - validator resolved `com.demo.target` to pid `22130` before and after the manual trigger
  - `nook.gadget.load-configured-startup` returned `loaded=true`
  - pid-filtered post-trigger logs included `script create ok name=startup.js script_id=1`
  - pid-filtered post-trigger logs included `script load ok script_id=1`
  - pid-filtered post-trigger logs included `startup script load ok path=assets/nook-gadget/startup.js script_id=1`
  - pid-filtered post-trigger logs included `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
- Automated signed install/cold-launch validation on `2026-05-19` also confirmed:
  - `adb install -r -t build\nook-gadget\TargetDemo_1.0-patched.apk` returned `Success`
  - the packaged startup script auto-loaded on first cold launch without manual `attach -l`
- Explicit listen-mode wrapper validation on `2026-05-19` also confirmed:
  - command used: `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1 -StartupScript .\host\nook-py\java_perform_startup_login.js -Sign -Apksigner E:\SDK\build-tools\34.0.0\apksigner.bat -Zipalign E:\SDK\build-tools\34.0.0\zipalign.exe -Keystore .\build\keystore\nook-debug.keystore -Storepass android -KeyAlias androiddebugkey -InstallAndLaunch -Serial 21ce24db`
  - validator resolved `com.demo.target` to pid `30225`
  - pid-filtered logs included `script create ok name=startup.js script_id=1`
  - pid-filtered logs included `script load ok script_id=1`
  - pid-filtered logs included `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
- Explicit connect-mode wrapper validation on `2026-05-19` also confirmed:
  - command used: `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_connect_validation.ps1 -StartupScript .\host\nook-py\java_perform_startup_login.js -Sign -Apksigner E:\SDK\build-tools\34.0.0\apksigner.bat -Zipalign E:\SDK\build-tools\34.0.0\zipalign.exe -Keystore .\build\keystore\nook-debug.keystore -Storepass android -KeyAlias androiddebugkey -InstallAndLaunch -StartNookServer -Serial 21ce24db`
  - `tools\device_start_nook_server.ps1` pushed `nook-server`, verified SHA-256 `255c62b5e089269b50b5ff1d977710b822a612ff039d3bccde99edfe0162a99b`, and observed `server started tcp=27042`
  - validator resolved `com.demo.target` to pid `30987`
  - pid-filtered logs included `script create ok name=startup.js script_id=1`
  - pid-filtered logs included `script load ok script_id=1`
  - pid-filtered logs included `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
  - follow-up host RPC succeeded with:
    - command: `powershell -ExecutionPolicy Bypass -File .\tools\nook_cli_local.ps1 call com.demo.target nook.gadget.load-configured-startup --attach --call-args [] --usb --serial 21ce24db --json`
    - result: `{"ok": true, "mode": "attach", "session_id": 2, "pid": 30987, "process_name": "com.demo.target", "rpc": {"method": "nook.gadget.load-configured-startup", "result": {"loaded": true, "method": "nook.gadget.load-configured-startup", "startup_mode": "auto-start"}}}`
- PID-scoped `ConfigApp-V1.3.0.apk` cold-start recheck on `2026-05-19` also confirmed:
  - validator resolved `com.jiqiu.configapp` to pid `21939`
  - pid-filtered logs included `script create ok name=startup.js script_id=1`
  - pid-filtered logs included `script load ok script_id=1`
  - unrelated background logs from `com.ad2001.frida0x1` no longer contaminated the pass signal
- Additional automated signed install/cold-launch validation on `2026-05-19` confirmed:
  - `adb install -r -t build\nook-gadget\HookEveryThing-debug-patched.apk` returned `Success` after the `apktool.yml doNotCompress` fix
  - `adb install -r -t build\nook-gadget\ConfigApp-V1.3.0-patched.apk` returned `Success` after synthetic `lib/arm64-v8a/` creation for APKs with no original native libs
  - `adb install -r -t build\nook-gadget\Challenge-0x1-patched.apk` returned `Success` through the same no-native-lib compatibility path on an independent sample
  - `adb install -r -t build\nook-gadget\TargetDemo_1.0-customapp-patched.apk` returned `Success`, and the decoded patched output showed `nook-gadget` injected into `GadgetSampleApplication.onCreate()` rather than the launcher `MainActivity`
- Real non-synthesized custom-`Application` proxy-loader validation on `2026-05-19` also confirmed:
  - chosen sample: `E:\tmp\Nook-Gadget\build\device-apk-scan\icu.nullptr.applistdetector.apk`
  - package/activity: `icu.nullptr.applistdetector` / `icu.nullptr.applistdetector/.MainActivity`
  - manifest `Application`: `icu.nullptr.applistdetector.MyApplication`
  - failing minimal evidence:
    - command: `python .\tools\nook_patchapk.py --input-apk E:\tmp\Nook-Gadget\build\device-apk-scan\icu.nullptr.applistdetector.apk --output-apk .\build\nook-gadget\icu-nullptr-applistdetector-minimal.apk --gadget-lib .\libs\arm64-v8a\libnook-gadget.so --startup-script .\host\nook-py\hook.js --decode-backend apktool --apktool E:\Re_tools\APKTool\apktool.bat --bootstrap-mode minimal --no-sign`
    - result: `no supported startup onCreate smali target found`
  - succeeding proxy-loader evidence:
    - command: `powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_apk_validation.ps1 -SampleName applistdetector-proxy -InputApk E:\tmp\Nook-Gadget\build\device-apk-scan\icu.nullptr.applistdetector.apk -OutputApk .\build\nook-gadget\icu-nullptr-applistdetector-proxy-signed.apk -GadgetLib .\libs\arm64-v8a\libnook-gadget.so -BootstrapMode proxy-loader -PackageName icu.nullptr.applistdetector -LaunchActivity icu.nullptr.applistdetector/.MainActivity -StartupScript .\host\nook-py\java_callable_smoke.js -DecodeBackend apktool -Apktool E:\Re_tools\APKTool\apktool.bat -Apksigner E:\SDK\build-tools\34.0.0\apksigner.bat -Zipalign E:\SDK\build-tools\34.0.0\zipalign.exe -Keystore .\build\keystore\nook-debug.keystore -Storepass android -KeyAlias androiddebugkey -Sign -InstallAndLaunch -Serial 21ce24db -DebugLogging -StartupLogPattern 'NookCallable|hello-from-nook|script create ok|script load ok|dropping script message without control channel'`
    - install returned `Success`
    - validator resolved `icu.nullptr.applistdetector` to pid `3243`
    - pid-filtered logs included `script create ok name=startup.js script_id=1`
    - pid-filtered logs included `script load ok script_id=1`
    - pid-filtered logs included `startup script load ok path=assets/nook-gadget/startup.js script_id=1`
    - pid-filtered logs included `D/NookCallable( 3243): hello-from-nook`
    - pid-filtered logs included `dropping script message without control channel`
- User-side manual verification confirmed the intended outcome:
  - the app could cold start normally
  - the login-page hook was already effective before any manual host-side script load

## Recommended next step

- Keep the current matrix as the baseline compatibility bar and only add more samples when you want broader confidence or when a new target class exposes a new packaging/runtime constraint.
