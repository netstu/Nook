# Static Java Replacement Routing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make real Android `static` Java hook callbacks hit on device by introducing a minimal replacement-ArtMethod routing path instead of relying only on mutating the original ArtMethod.

**Architecture:** Keep the existing `JavaHook` path for currently working instance hooks, and add a static-only replacement path inside `src/java_hook/JavaHook.cpp`. The minimal version will allocate a replacement ArtMethod clone, redirect compiled entry to a stable native thunk or per-method router, and preserve current `InvokeOriginalMethod()` semantics so JS/host layers stay unchanged.

**Tech Stack:** C++17, Android ART internals, existing Nook inline hook framework, existing JavaHook C/JS bridge, NDK build, device logcat verification.

---

### Task 1: Lock the new design into regression tests

**Files:**
- Modify: `tests/headers/test_java_hook_runtime_regressions.cpp`
- Test: `build/test_java_hook_runtime_regressions.exe`

**Step 1: Write the failing test**

Add a regression that checks `src/java_hook/JavaHook.cpp` for the new static replacement path markers:
- `StaticReplacementHookHandle`
- `InstallStaticReplacementHook`
- `UninstallStaticReplacementHook`
- a `if (isStatic)` branch inside `JavaHook::HookMethod`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions.exe
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `1`

**Step 3: Write minimal implementation**

Add the new helper declarations and the static branch in `src/java_hook/JavaHook.cpp`.

**Step 4: Run test to verify it passes**

Run the same command again.

Expected: exit code `0`

### Task 2: Add static-only replacement hook state and install path

**Files:**
- Modify: `src/java_hook/JavaHook.h`
- Modify: `src/java_hook/JavaHook.cpp`

**Step 1: Write the failing test**

Extend the same regression to assert:
- `HookInfo` stores replacement/static hook state
- static unhook path restores original entrypoint and frees hook handle

**Step 2: Run test to verify it fails**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `1`

**Step 3: Write minimal implementation**

Implement:
- a static replacement handle structure
- install helper using existing inline hook allocator/patcher
- storage in `HookInfo`

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `0`

### Task 3: Route static calls through replacement/native callback path

**Files:**
- Modify: `src/java_hook/JavaHook.cpp`

**Step 1: Write the failing test**

Add a regression asserting:
- `JavaHook::HookMethod` branches to static replacement install for `isStatic`
- `InvokeOriginalMethod` still uses backup/original method data instead of the replacement

**Step 2: Run test to verify it fails**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `1`

**Step 3: Write minimal implementation**

Implement the static routing path with minimal behavior:
- compiled static target patched with inline hook
- replacement callback reuses current argument/result marshalling
- original invocation remains on backup/original ArtMethod

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `0`

### Task 4: Make unhook and cleanup symmetric

**Files:**
- Modify: `src/java_hook/JavaHook.cpp`

**Step 1: Write the failing test**

Add a regression asserting:
- `JavaHook::Unhook` calls static replacement uninstall helper
- `JavaHook::UnhookAll` releases static replacement resources

**Step 2: Run test to verify it fails**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `1`

**Step 3: Write minimal implementation**

Implement symmetric cleanup for single unhook and unhook-all.

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_java_hook_runtime_regressions.exe
```

Expected: exit code `0`

### Task 5: Verify Android build and device smoke

**Files:**
- Modify: `docs/code_review.md`
- Verify: `host/nook-py/java_static_log_smoke.js`
- Verify: `host/nook-py/java_static_app_smoke.js`

**Step 1: Build Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4
```

Expected: exit code `0`

**Step 2: Push device binaries**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb push libs/arm64-v8a/libc++_shared.so /data/local/tmp/nook/libc++_shared.so
```

**Step 3: Restart server**

Run:

```powershell
adb shell "su -c 'pkill -f /data/local/tmp/nook/nook-server 2>/dev/null'"
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

**Step 4: Run device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_static_log_smoke.js --wait --usb
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_static_app_smoke.js --wait --usb
adb logcat -d -s JavaHook
```

Expected:
- install success
- real static enter/leave logs
- no longer only “installed but never hit”

### Task 6: Record findings and residual risk

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document**

Record:
- why the old mutated-original-ArtMethod path still missed static callbacks
- what minimal replacement routing was added
- what still remains before full Frida-like Java hook parity

**Step 2: Verify docs**

Run:

```powershell
Get-Content docs\code_review.md | Select-Object -Last 80
```

Expected: latest milestone and residual risk are present.
