# Nook ModuleMap.update Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal `ModuleMap.update()` support that refreshes the snapshot in place and returns the same object.

**Architecture:** Refactor `ModuleMap` methods to read a snapshot stored on the JS instance instead of closure-captured immutable data. `update()` replaces that snapshot with a fresh `Module.enumerateModules()` result.

**Tech Stack:** C++17, QuickJS object properties, existing module enumeration helpers, Python smoke/docs.

---

### Task 1: Write failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

- `typeof new ModuleMap().update`
- `update()` returns the same object
- `update()` keeps `values()` and lookup behavior usable

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task27.exe
build\test_js_runtime_native_attach_task27.exe
```

Expected: the new assertions fail because `update()` is not exposed yet.

### Task 2: Implement `ModuleMap.update()`

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Steps:

1. store snapshot on each `ModuleMap` instance
2. update `has/find/get/values` to read the current instance snapshot
3. add `update()` with no-arg refresh semantics
4. keep miss/error behavior unchanged

### Task 3: Verify local GREEN

Run:

```powershell
build\test_js_runtime_native_attach_task27.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime tests pass and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving `ModuleMap.update()` returns the same object and still resolves `libnook-agent.so`.

### Task 5: Rebuild Android artifacts and push

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command.
