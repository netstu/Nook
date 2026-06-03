# Nook Gadget Compatibility Matrix

## Purpose

This matrix tracks which APK samples should be validated through the generic
`tools/nook_gadget_apk_validation.ps1` entrypoint and which compatibility
dimensions they cover.

## Current matrix

| Sample class | APK | Package | Launcher activity | Coverage value | Status |
| --- | --- | --- | --- | --- | --- |
| validated baseline | `E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk` | `com.demo.target` | `com.demo.target.MainActivity` | proven cold-start gadget path, signed install path, authoritative login hook | validated |
| launcher-activity fallback validated | `E:\Learn\my_program\all_my_hook\ConfigApp-V1.3.0.apk` | `com.jiqiu.configapp` | `com.jiqiu.configapp.MainActivity` | real app, no original `lib/<abi>/` layout, validates cold-start gadget path after synthetic `lib/arm64-v8a/` creation | validated |
| launcher-activity fallback validated | `E:\Learn\my_program\all_my_hook\HookEveryThing\app\build\outputs\apk\debug\app-debug.apk` | `cn.n1ng.hookeverything` | `cn.n1ng.hookeverything.MainActivity` | internal debug build, native-code present, validated install-time native-lib compatibility after `apktool.yml doNotCompress` fix | validated |
| launcher-activity fallback validated | `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\Frida-Labs\Frida 0x1\Challenge 0x1.apk` | `com.ad2001.frida0x1` | `com.ad2001.frida0x1.MainActivity` | small lab APK, no original `lib/<abi>/` layout, validates synthetic native-lib directory creation on a second independent sample | validated |
| custom-Application validated | `E:\tmp\Nook-Gadget\build\nook-gadget\TargetDemo_1.0-customapp-base.apk` | `com.demo.target` | `com.demo.target.MainActivity` | synthesized from `TargetDemo_1.0.apk` by adding manifest `android:name="com.demo.target.GadgetSampleApplication"` and a minimal `Application.onCreate()` implementation; validates the non-fallback injection branch directly | validated |
| proxy-loader custom-Application validated | `E:\tmp\Nook-Gadget\build\device-apk-scan\icu.nullptr.applistdetector.apk` | `icu.nullptr.applistdetector` | `icu.nullptr.applistdetector/.MainActivity` | real app with manifest `android:name="icu.nullptr.applistdetector.MyApplication"`; minimal bootstrap fails, proxy-loader succeeds through manifest rewrite + generated proxy subclass + detached startup fallback | validated |

## Coverage summary

- launcher-activity fallback path: validated across three independent APK classes
  - native-code present from the source APK
  - no-native-lib APK that now requires synthetic `lib/arm64-v8a/`
  - small lab APK that exercises the same no-native-lib path independently
- custom `Application.onCreate()` path: validated with a synthesized `TargetDemo` variant that forces manifest-level `android:name`
- proxy-loader path: validated on one real non-synthesized custom-`Application` APK
- remaining gap is no longer a code-path gap; additional samples are now only optional breadth
- the generic validator now scopes startup-log matching to the launched target PID so matrix rows are no longer vulnerable to cross-process log pollution from other instrumented apps left running on the device

## Validation entrypoints

- listen mode:
  - `tools/nook_gadget_targetdemo_validation.ps1` is the explicit TargetDemo listen-mode wrapper over `tools/nook_gadget_apk_validation.ps1`
  - it keeps the login-page hook as the authoritative validation target
- connect mode:
  - `tools/nook_gadget_connect_validation.ps1` is the matching TargetDemo connect-mode wrapper
  - it defaults to `127.0.0.1:27042` and can optionally prepare the device-side server through `tools/device_start_nook_server.ps1`
  - real-device connect evidence is not recorded yet in this matrix; the wrapper exists so Task 7 can collect it without changing the patch surface

## Latest evidence

- `HookEveryThing` (`2026-05-19`)
  - initial install failure was `INSTALL_FAILED_INVALID_APK: Failed to extract native libraries, res=-2`
  - root cause: `apktool` rebuild compressed injected `libnook-gadget.so` while the source APK expected native libraries to remain uncompressed
  - fix verified: patched APK installed successfully after syncing `lib/arm64-v8a/libnook-gadget.so` into `apktool.yml -> doNotCompress`
  - cold-start logs included `script create ok`, `script load ok`, and `dropping script message without control channel`
- `ConfigApp-V1.3.0.apk` (`2026-05-19`)
  - previous blocker was `unsupported ABI layout for arm64-v8a`
  - root cause: original APK had no `lib/arm64-v8a/` tree at all
  - fix verified: patch tool now creates `lib/arm64-v8a/` when the APK has no native-lib directories, then signed install and cold-start validation both succeeded
  - pid-scoped recheck verified `com.jiqiu.configapp` at pid `21939`, and the pass signal now comes only from that target process (`script create ok` + `script load ok`)
- `Challenge 0x1.apk` (`2026-05-19`)
  - validated the same no-native-lib compatibility branch on an independent sample
  - signed install and cold-start validation both succeeded with the packaged startup script
- `TargetDemo_1.0.apk` explicit mode wrappers (`2026-05-19`)
  - `tools/nook_gadget_targetdemo_validation.ps1` validated the listen-mode path, resolving `com.demo.target` to pid `30225`
  - `tools/nook_gadget_connect_validation.ps1 -StartNookServer` validated the connect-mode path, resolving `com.demo.target` to pid `30987`
  - the device-side server helper verified `server started tcp=27042` before the connect-mode cold launch
  - a follow-up host RPC through `nook_cli_local.ps1 call ... --attach --json` succeeded against pid `30987`, confirming that the host could attach to the runtime-backed session after gadget connect-mode bring-up
- synthesized `TargetDemo_1.0-customapp-base.apk` (`2026-05-19`)
  - created from the validated `TargetDemo_1.0.apk` by adding a manifest-level custom `Application`
  - generic validator installed and launched the gadget-patched APK successfully
  - cold-start logs included `script create ok`, `script load ok`, `dropping script message without control channel`, and the packaged startup hook logs from `AdWallFragment`
  - decoded patched output confirmed `const-string v0, "nook-gadget"` only in `GadgetSampleApplication.onCreate()`, while `MainActivity` still only loaded the app's original `payload_hook_example`
- `icu.nullptr.applistdetector.apk` real proxy-loader sample (`2026-05-19`)
  - chosen as the authoritative non-synthesized custom-`Application` sample with manifest `android:name="icu.nullptr.applistdetector.MyApplication"`
  - minimal bootstrap failure was reproduced first with `no supported startup onCreate smali target found`
  - proxy-loader patch then succeeded on the same APK after manifest rewrite and generated `NookProxyApplication`
  - earlier runtime blocker was detached `runtime-ready` failure aborting startup before script load
  - after the runtime fallback fix and gadget rebuild, signed install and cold-start validation succeeded
  - validator resolved `icu.nullptr.applistdetector` to pid `3243`
  - pid-filtered logs included `script create ok`, `script load ok`, `startup script load ok path=assets/nook-gadget/startup.js script_id=1`, `dropping script message without control channel`, and `D/NookCallable( 3243): hello-from-nook`

## Execution rule

Use the generic validator as the common entrypoint for all future matrix rows:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_apk_validation.ps1 `
  -SampleName <sample-name> `
  -InputApk <path-to.apk> `
  -OutputApk .\build\nook-gadget\<sample-name>-patched.apk `
  -GadgetLib .\libs\arm64-v8a\libnook-gadget.so `
  -PackageName <package> `
  -LaunchActivity <package/.MainActivity> `
  -DecodeBackend apktool `
  -Apktool E:\Re_tools\APKTool\apktool.bat `
  -Apksigner E:\SDK\build-tools\34.0.0\apksigner.bat `
  -Zipalign E:\SDK\build-tools\34.0.0\zipalign.exe `
  -Keystore .\build\keystore\nook-debug.keystore `
  -Storepass android `
  -KeyAlias androiddebugkey `
  -Sign `
  -InstallAndLaunch
```

For the current representative sample you can also use the dedicated wrappers:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_connect_validation.ps1 -StartNookServer
```

## Next validation order

1. optional additional real custom-`Application` samples if you want broader confidence beyond the first validated proxy-loader row
2. optional additional Frida Labs samples if you want broader launcher-fallback confidence
