# Native Hook Ignore Level Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the temporary high-frequency sampling workaround with a Frida-style per-thread ignore mechanism so high-frequency native hooks like `strcmp` avoid re-entrant JS churn without dropping real target hits.

**Architecture:** Move the hot-path decision in the native inline hook dispatcher from probabilistic sampling to deterministic thread-local bypass. The dispatcher will skip JS callback delivery whenever the current thread is already inside a native hook callback or has been explicitly marked ignored for the duration of callback execution. This keeps hook installation semantics unchanged while making runtime behavior closer to Frida’s `guard_key + ignore_level` design.

**Tech Stack:** C++17, Android NDK, QuickJS bridge, existing Nook native inline hook bridge tests, lightweight source regression tests.

---

### Task 1: Replace sampling with ignore-level test coverage

**Files:**
- Modify: `tests/communication/test_native_js_bridge.cpp`
- Modify: `tests/headers/test_java_hook_runtime_regressions.cpp`

**Step 1: Write the failing test**

Add a new native hook bridge unit test that:
- installs a blocking hook through the existing fake inline invoker
- enters the callback path while the current thread is marked ignored
- verifies the dispatcher directly returns the original function result instead of trying to run the callback body

Also update the source regression test to assert:
- the sampling counter constant no longer exists
- ignore-level state exists
- `DispatchInlineHookSlot()` bypasses when ignore level is greater than zero

**Step 2: Run test to verify it fails**

Run:
```powershell
g++ -std=c++17 -I . -I include tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions_ignorelevel.exe
build\test_java_hook_runtime_regressions_ignorelevel.exe
```

Expected: FAIL because the source still contains sampling and does not yet contain ignore-level bypass.

**Step 3: Commit**

Do not commit yet. Continue to implementation after observing the red state.

### Task 2: Implement per-thread ignore level in the native hook bridge

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_native_js_bridge.h`

**Step 1: Write minimal implementation**

Implement:
- thread-local `ignore_level`
- helper functions for increment/decrement and scoped guard
- hot-path bypass in `DispatchInlineHookSlot()` when `ignore_level > 0`
- remove sampling counter, time-window throttling, and related constants

Keep the existing recursion-depth guard, but treat ignore-level as the Frida-style fast-path decision used during callback execution.

**Step 2: Ensure callback execution scopes the ignore level**

Wrap synchronous `JsRuntime::InvokeNativeHookCallbackSync(...)` calls with a scoped ignore guard so:
- callback entry marks the current thread ignored
- callback exit restores the previous ignore state

This must apply to both enter and leave callbacks.

**Step 3: Run focused regression test**

Run:
```powershell
g++ -std=c++17 -I . -I include tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions_ignorelevel.exe
build\test_java_hook_runtime_regressions_ignorelevel.exe
```

Expected: PASS

### Task 3: Rebuild Android agent and verify artifact freshness

**Files:**
- Build output: `obj/local/arm64-v8a/libnook-agent.so`

**Step 1: Rebuild**

Run:
```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build\android\Android.mk NDK_APPLICATION_MK=build\android\Application_static.mk APP_MODULES="nook_agent" -j4
```

Expected: successful `libnook-agent.so` build

**Step 2: Push fresh artifact**

Run:
```powershell
adb push obj/local/arm64-v8a/libnook-agent.so /data/local/tmp/nook-test/libnook-agent.so
adb shell su -c 'sha1sum /data/local/tmp/nook-test/libnook-agent.so'
```

Expected: device hash changes from the previous deployed artifact

**Step 3: Restart server**

Run:
```powershell
adb shell su -c 'pkill -f /data/local/tmp/nook-test/nook-server'
adb shell su -c 'cd /data/local/tmp/nook-test; nohup ./nook-server >/dev/null 2>/dev/null &'
adb shell su -c 'pidof nook-server'
```

Expected: new server PID

### Task 4: Verify against the `frida-0x8` repro

**Files:**
- Runtime verification only

**Step 1: Clear logs**

Run:
```powershell
adb logcat -c
```

**Step 2: Reproduce**

Run:
```powershell
nook-cli -U -f com.ad2001.frida0x8 -l E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\Test_Lab\nook-frida-labs\frida-0x8\script2.js
```

Expected:
- no long white-screen stall
- hook remains active after app enters
- `Hookin the strcmp function`
- `Input Hello`
- `The flag is FRIDA{NATIVE_LAND}`

**Step 3: Gather logs if mismatch remains**

Run:
```powershell
adb logcat -d -s NookCommApi NookServer
```

Expected:
- no sampling/throttle path references
- hook install succeeds
- callback execution continues after startup

