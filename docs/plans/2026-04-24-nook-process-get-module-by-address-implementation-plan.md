# Nook Process GetModuleByAddress Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Process.getModuleByAddress(address)` returning the containing module object or `null`.

**Architecture:** Reuse the existing loaded-module collection helper from `Module.enumerateModules()`, normalize the lookup address, and return the same `{ name, base, size, path }` object shape for the first containing module match.

**Tech Stack:** C++17, QuickJS runtime bindings, existing module enumeration helper, Python smoke/docs.

---

### Task 1: Write the failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- `typeof Process.getModuleByAddress`
- hit on the current executable base
- miss on `ptr('0x1')`
- invalid input throws
- tagged-pointer hit still resolves

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task25.exe
build\test_js_runtime_native_attach_task25.exe
```

Expected: binding assertion fails because `Process.getModuleByAddress` is not exposed yet.

### Task 2: Implement the minimal binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Reuse module records**

- use the same native module-record helper as `Module.enumerateModules()`

**Step 2: Add strict address validation**

- accept only non-zero pointer-like input
- normalize tagged pointers before containment checks

**Step 3: Expose the binding**

- add `JsProcessGetModuleByAddress(...)`
- register `Process.getModuleByAddress`

### Task 3: Verify local GREEN

Run:

```powershell
build\test_js_runtime_native_attach_task25.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime binary passes and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving:

- `Process.getModuleByAddress(Module.getBaseAddress('libnook-agent.so'))`
- result name is `libnook-agent.so`

### Task 5: Rebuild Android artifacts and push

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command.
