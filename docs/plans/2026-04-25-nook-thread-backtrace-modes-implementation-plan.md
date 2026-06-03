# Nook Thread.backtrace Modes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Give `Backtracer.ACCURATE` and `Backtracer.FUZZY` distinct native implementations while preserving the current Frida-style JS API.

**Architecture:** Keep accurate mode on the existing unwind/context path. Add a fuzzy backtracer that scans stack memory for executable-pointer candidates, using `this.context.sp` for hook-context mode and an approximate current stack pointer for no-context mode.

**Tech Stack:** C++17, QuickJS embedding layer, native runtime tests, Python CLI tests, Android NDK build

---

### Task 1: Lock fuzzy behavior with tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:
- hook-context fuzzy mode recovering addresses from synthetic stack memory
- current-thread fuzzy mode remaining non-empty

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task63.exe
.\build\test_js_runtime_native_attach_task63.exe
```

Expected: failure in the new fuzzy-mode assertions.

**Step 3: Write minimal implementation**

Modify `src/agent_runtime/js_runtime.cpp` to:
- collect executable ranges once per call
- add fuzzy stack scanning helpers
- switch `JsThreadBacktrace()` to route by `BacktracerMode`

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_js_runtime_native_attach_task63.exe
```

Expected: exit code `0`

**Step 5: Commit**

Skip commit in the current workspace state.

### Task 2: Update smoke and docs

**Files:**
- Modify: `host/nook-py/thread_backtrace_hook.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Extend smoke output**

Add one payload that compares `ACCURATE` and `FUZZY` frame counts or shape for the current runtime thread.

**Step 2: Document semantics**

Document:
- accurate = unwind / frame-based
- fuzzy = stack-scan-based

**Step 3: Run host tests**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

### Task 3: Build and push Android artifacts

**Files:**
- No source additions beyond runtime/docs files above

**Step 1: Build Android**

Run:

```powershell
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -Command "E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4"
```

**Step 2: Push**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
```

**Step 3: Hand off user verification**

Provide the exact `nook-cli attach ... thread_backtrace_hook.js` command and expected `thread-backtrace-mode-split` / hook output.
