# Java.registerClass Method Spec Alignment Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Frida-style `registerClass.methods` declaration-object support while preserving the current proxy-based callback architecture.

**Architecture:** Normalize each `spec.methods[methodName]` entry inside the runtime parser. Continue dispatching callbacks by method name only. Reject unsupported multiple-declaration cases instead of pretending overload-aware callback dispatch exists.

**Tech Stack:** C++, QuickJS bootstrap/runtime, host-side C++ tests, Android smoke JS

---

### Task 1: Add failing host tests for declaration-object support

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add host tests for:

- `methods.onClick = { implementation: function (...) {} }`
- `methods.onClick = [{ implementation: function (...) {} }]`
- `methods.onClick = [{...}, {...}]` rejects
- invalid `implementation` rejects

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: tests fail because `registerClass` still only accepts plain functions.

### Task 2: Implement minimal runtime normalization

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write minimal implementation**

Update `CollectJavaRegisterClassMethods(...)` so each method value may be:

- a function
- a declaration object with `implementation`
- a one-entry declaration array containing such an object

Validate optional `returnType` and `argumentTypes`, but do not use them for dispatch yet.

**Step 2: Run tests to verify they pass**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: all host tests pass.

### Task 3: Add device smoke using declaration-object form

**Files:**
- Create: `host/nook-py/java_register_class_method_spec_smoke.js`

**Step 1: Write smoke script**

Reuse the current listener callback flow, but define `methods.onClick` using the declaration object form.

**Step 2: Build Android if runtime changed**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected: arm64 runtime artifacts rebuild successfully.

### Task 4: Push and validate on device

**Files:**
- Use: `libs/arm64-v8a/libnook-agent.so`
- Use: `libs/arm64-v8a/libnook.so`
- Use: `libs/arm64-v8a/nook-server`
- Use: `libs/arm64-v8a/libc++_shared.so`

**Step 1: Push fresh artifacts**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
adb shell su -c 'chmod 755 /data/local/tmp/nook/nook-server'
```

Expected: all pushes succeed.

**Step 2: Run device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_register_class_method_spec_smoke.js --wait --usb
```

Expected: binding exists, install succeeds, callback fires through declaration-object form.

### Task 5: Document the behavior boundary

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Record implementation/result**

Document:

- supported declaration forms
- explicit rejection of multiple declarations
- why this is closer to Frida but still not overload-aware callback dispatch

**Step 2: Re-run verification if needed**

If code changed during cleanup, rerun host verification.
