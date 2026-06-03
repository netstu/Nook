# Nook Nonblocking Hook Observer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `blocking: false` to native hook attach APIs so observer-only callbacks can run asynchronously without stalling the target thread.

**Architecture:** Thread the `blocking` flag from JS attach options into `NativeJsHookRequest` / `NativeJsHookRecord`, then branch in the native hook bridge: blocking hooks keep waiting for callback completion, nonblocking hooks enqueue and continue immediately. The JS callback still executes, but mutation results are ignored in nonblocking mode.

**Tech Stack:** C++17, QuickJS embedding, native hook bridge, native runtime tests, Android NDK

---

### Task 1: Lock the API with failing tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `tests/communication/test_native_js_bridge.cpp`

**Step 1: Write the failing test**

Add tests for:
- `Interceptor.attach(..., { blocking: false, onEnter() {} })` parses and stores nonblocking mode
- invoking a nonblocking installed native hook returns without waiting for JS completion

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_native_js_bridge.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_native_js_bridge_task65.exe
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task65.exe
```

Expected: attach parsing or nonblocking behavior test fails.

**Step 3: Write minimal implementation**

Modify:
- `src/agent_runtime/nook_native_js_bridge.h`
- `src/agent_runtime/nook_native_js_bridge.cpp`
- `src/agent_runtime/js_runtime.cpp`

Implement:
- `blocking` on request/record, default `true`
- attach option parsing for `blocking`
- bridge fast path that skips `WaitForInvocationCompletion()` when `blocking == false`

**Step 4: Run test to verify it passes**

Run both native test binaries and expect exit code `0`.

**Step 5: Commit**

Skip commit in the current workspace state.

### Task 2: Switch backtrace smoke to observer mode

**Files:**
- Modify: `host/nook-py/thread_backtrace_hook.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Update smoke script**

Pass `blocking: false` in the hook callback options.

**Step 2: Document semantics**

Document:
- blocking hooks can mutate args / retval and stall the target thread
- nonblocking hooks are observer-only

**Step 3: Verify host tests**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

### Task 3: Build and push Android artifacts

**Files:**
- No additional files beyond runtime/docs/smoke files above

**Step 1: Build Android**

Run:

```powershell
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -Command "E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4"
```

**Step 2: Push artifacts**

Run:

```powershell
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
```

**Step 3: Hand off device verification**

Re-run `thread_backtrace_hook.js` and confirm the hook still fires while UI stalls are materially reduced.
