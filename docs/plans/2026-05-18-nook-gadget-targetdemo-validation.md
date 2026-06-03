# Nook Gadget TargetDemo Validation

## Sample target

- APK: `E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk`
- Package: `com.demo.target`
- Primary runtime entry: launcher `MainActivity`
- Useful hook pages:
  - primary validation target: login page `LoginFragment.verifyPasswordNative(String)`
  - secondary, non-authoritative target: ad wall page `AdWallFragment.loadAd(String, String)`

## Why this sample matters

- It is a real APK, not a synthetic zip fixture.
- It does not define a custom `Application`.
- Startup patching therefore must support launcher-`Activity.onCreate(...)` fallback instead of assuming `Application.onCreate()`.

## Validation performed on 2026-05-18

Build command:

```powershell
E:\SDK\ndk\25.1.8937393\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk APP_MODULES=nook_gadget -j4
```

Observed result:

- `libnook-gadget.so` built successfully
- output artifact: `libs/arm64-v8a/libnook-gadget.so`

Patch command:

```powershell
python .\tools\nook_patchapk.py `
  --input-apk E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemo_1.0.apk `
  --output-apk .\build\nook-gadget\TargetDemo_1.0-patched.apk `
  --abi arm64-v8a `
  --gadget-lib .\libs\arm64-v8a\libnook-gadget.so `
  --decode-backend apktool `
  --apktool E:\Re_tools\APKTool\apktool.bat `
  --no-sign
```

Observed result:

- patch command returned exit code `0`
- patched APK was produced at `build\nook-gadget\TargetDemo_1.0-patched.apk`
- patched APK contains:
  - `lib/arm64-v8a/libnook-gadget.so`
  - `assets/nook-gadget/config.json`

## Compatibility result

- The first real run failed because bootstrap injection only searched `Application.onCreate()`.
- The sample APK proved two extra compatibility requirements:
  - fallback to launcher `Activity.onCreate(Bundle)`
  - search target class smali across `smali*` trees, not only `smali/`
- After both fixes were added, the same real APK patched successfully through the `apktool` backend.

## Suggested device-side validation

Patch helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1
```

For gadget-style startup validation, package the dedicated startup login hook:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1 `
  -StartupScript .\host\nook-py\java_perform_startup_login.js
```

For a fully automated signed install + cold-start validation on the current device setup:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\nook_gadget_targetdemo_validation.ps1 `
  -StartupScript .\host\nook-py\java_perform_startup_login.js `
  -Sign `
  -Apksigner E:\SDK\build-tools\34.0.0\apksigner.bat `
  -Zipalign E:\SDK\build-tools\34.0.0\zipalign.exe `
  -Keystore .\build\keystore\nook-debug.keystore `
  -Storepass android `
  -KeyAlias androiddebugkey `
  -InstallAndLaunch
```

Then validate with one or both of these scripts.

Use the login page as the authoritative success target for the current gadget flow.
The ad wall page is not a stable attach-time signal because its first `loadAd(...)`
calls happen during app startup before a later manual attach.

## Authoritative gadget startup validation

After patching with `-StartupScript`, validate the gadget path this way:

1. Install the patched APK.
2. Launch the patched app without `attach -l`.
3. Open the Login tab.
4. Input any 6-character password and tap login.

Expected result:

- the packaged startup script is already active before any host-side script load
- login validation should be hooked on first use after process start

Optional observation step:

```powershell
nook-cli attach com.demo.target --usb
```

Use that attach only to inspect runtime state or logs after the startup hook has already been proven.

1. Login hook target

```powershell
nook-cli attach com.demo.target -l .\host\nook-py\java_perform_smoke.js --wait --usb
```

Expected trigger:

- open the Login tab
- input any 6-character password
- tap login

Expected script messages:

- `java-implementation-installed`
- `java-hook-enter:<password>`
- `java-hook-leave-original:true|false`

Important:

- keep `--wait` on the attach command during manual validation
- without `--wait`, `nook-cli attach -l ...` loads the script and then unloads it immediately when the command exits
- that produces a false negative where `java-implementation-installed` appears once but the later login action is no longer hooked

2. Ad wall hook target

```powershell
nook-cli attach com.demo.target -l .\host\nook-py\adwall_loadad.js --wait --usb
```

Expected trigger:

- open the Ad Wall tab

Expected console output:

- `[*] hooks installed`
- `[loadAd enter] ...`
- `[loadAd leave] ...`

Important limitation:

- `AdWallFragment.loadAd(...)` is a cold-start path in this sample.
- If attach happens after the app is already running, the earliest ad-load calls may already be gone.
- A miss on the ad page should not be treated as gadget failure while the login-page hook still works.

## Device-side finding on 2026-05-18

- `Java.ready(...)` is not the blocker in the current gadget attach flow for this sample.
- Real-device attach diagnostics showed:
  - `Java._isClassLoaderReady() == true`
  - `Java._isAppReady() == true`
  - `Java.ready(...)` took the immediate path
- `java_perform_smoke.js` also installed its Java implementation successfully.
- The observed false negative came from CLI usage without `--wait`, which unloaded the script before the later manual login action.

## Real-device gadget startup result on 2026-05-19

Final cold-start validation for the packaged gadget startup script succeeded on the same sample APK.

Validated flow:

1. Rebuild `libnook-gadget.so`.
2. Patch `TargetDemo_1.0.apk` with:
   - `--decode-backend apktool`
   - `--no-sign`
   - `--startup-script .\host\nook-py\java_perform_startup_login.js`
3. Install the rebuilt patched APK on device.
4. Cold launch the app without any manual `nook-cli attach -l ...`.
5. Open the Login tab and trigger `LoginFragment.verifyPasswordNative(String)`.

Observed success evidence:

- gadget startup created and loaded the packaged script during app startup
- runtime logs included:
  - `script create ok name=startup.js script_id=1`
  - `script load ok script_id=1`
  - `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
- the user confirmed that the login hook was effective after cold start

Important behavior confirmed by this run:

- gadget startup does not require a later `attach -l` to make the packaged hook active
- a later `nook-cli attach ... --usb` is only for observation/control
- when no host control channel is attached yet, startup-script `send(...)` output is dropped instead of failing gadget startup

## Automated signed install validation on 2026-05-19

The validation helper now also supports an optional script-assisted device flow for this sample:

1. Patch the APK.
2. `zipalign` the rebuilt output.
3. Sign it through `apksigner`.
4. Install it via `adb install -r -t`.
5. Force-stop the package, clear logcat, cold-launch the app, and print matching startup logs.

Observed result from the automated run:

- install completed successfully with `Success`
- cold-start logs still showed:
  - `script create ok name=startup.js script_id=1`
  - `script load ok script_id=1`
  - `Hooked successfully: com.demo.target.LoginFragment.verifyPasswordNative`
  - `dropping script message without control channel`

## Root causes found during validation

The real-device startup pass exposed three gadget-specific issues that had to be fixed before the final success state:

1. Control-channel fallback:
   - control-channel setup failure was aborting gadget runtime before bridge/startup-script bootstrap
   - fix: continue into bridge/startup initialization even if control setup is unavailable
2. Script registry initialization:
   - startup could crash with `std::overflow_error: __next_prime overflow`
   - fix: replace the global registry object with a function-local static accessor
3. Detached send path:
   - startup script load failed when `send(...)` had no active host control channel
   - fix: drop outbound script messages in detached mode and keep script load successful

The signed install automation pass exposed one packaging/signing issue:

4. Signed APK alignment on Android 11+:
   - `zipalign + jarsigner` still produced an APK that failed install-time alignment checks
   - `zipalign -c` showed the `jarsigner` output had many `BAD` alignments, including `resources.arsc`
   - a previously working `apksigner`-signed artifact stayed fully aligned
   - fix: make `zipalign + apksigner` the primary installable signing path for the patch workflow

## Remaining gaps

- Patch, install, launch, attach, and script-load were validated on the real device for this sample.
- Cold-start gadget startup is now validated for the login-page hook path in this sample.
- The ad wall path should still be treated as non-authoritative because its earliest calls happen during app startup.
- Manifest handling is still marker-only and should be replaced with a real manifest editing path before broader APK compatibility claims are made.
- The automated signed-output validation is still sample-specific; broader keystore/tool discovery and multi-device handling are not yet generalized.
