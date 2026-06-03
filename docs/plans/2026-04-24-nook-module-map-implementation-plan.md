# Nook ModuleMap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal snapshot-based `ModuleMap` with `has/find/get/values`.

**Architecture:** Build `ModuleMap` as a lightweight JS object whose methods capture a module snapshot array created at construction time from `Module.enumerateModules()`. Reuse the same module object shape and address normalization logic already present in the runtime.

**Tech Stack:** C++17, QuickJS closures via `JS_NewCFunctionData`, existing module enumeration helpers, Python smoke/docs.

---

### Task 1: Write the failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- `typeof ModuleMap`
- `new ModuleMap().has/find/get/values`
- miss behavior for `ptr('0x1')`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task26.exe
build\test_js_runtime_native_attach_task26.exe
```

Expected: binding assertion fails because `ModuleMap` is not exposed yet.

### Task 2: Implement the minimal snapshot object

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Create a module snapshot array**

- reuse the existing module enumeration path

**Step 2: Add per-instance methods**

- `has(address)`
- `find(address)`
- `get(address)`
- `values()`

**Step 3: Expose the constructor**

- add global `ModuleMap`
- mark it as a constructor in QuickJS

### Task 3: Verify local GREEN

Run:

```powershell
build\test_js_runtime_native_attach_task26.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime binary passes and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving that a fresh `ModuleMap` resolves `libnook-agent.so`.

### Task 5: Rebuild Android artifacts and push

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command.
