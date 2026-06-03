# Nook Thread.backtrace Shape Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Thread.backtrace()` accept the Frida-style no-argument and mode-only call shapes while preserving current hook-context backtrace behavior.

**Architecture:** Extend the JS argument parser so it accepts zero arguments and explicit mode-only invocations, while keeping context-based backtraces unchanged. Do not split accurate/fuzzy internals yet; only stabilize the script-facing API contract and tests.

**Tech Stack:** C++17, QuickJS embedding layer, native runtime tests, Python CLI tests, Android NDK build

---

### Task 1: Lock the JS contract with tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests covering:
- `Thread.backtrace()`
- `Thread.backtrace(Backtracer.FUZZY)`

Expected payloads:
- no-arg path returns at least one non-null frame
- fuzzy path returns at least one non-null frame

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task62.exe
.\build\test_js_runtime_native_attach_task62.exe
```

Expected: failing assertion or script-load failure in the new backtrace tests.

**Step 3: Write minimal implementation**

Update argument parsing in `src/agent_runtime/js_runtime.cpp` so:
- zero arguments mean current-thread backtrace
- `Backtracer.ACCURATE` and `Backtracer.FUZZY` are accepted as first argument
- context + mode remains valid

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_js_runtime_native_attach_task62.exe
```

Expected: exit code `0`

**Step 5: Commit**

Git commit is not available in the current workspace state; skip commit and keep changes isolated to the files above.

### Task 2: Verify host and Android packaging

**Files:**
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`
- Optionally modify: `host/nook-py/thread_backtrace_hook.js`

**Step 1: Update docs if semantics changed**

Document that:
- no-arg and mode-only forms target the current JS runtime thread
- context form targets the hooked native thread

**Step 2: Run host tests**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 3: Build Android artifacts**

Run:

```powershell
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -Command "E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4"
```

Expected: `Install : libnook.so`, `libnook-agent.so`, `nook-server`

**Step 4: Push device artifacts**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
```

Expected: all pushes succeed

**Step 5: Hand off user verification**

Provide the user with the exact `nook-cli` command to verify the updated `Thread.backtrace()` behavior on-device.
