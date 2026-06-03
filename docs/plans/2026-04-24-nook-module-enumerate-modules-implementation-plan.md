# Nook Module EnumerateModules Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Module.enumerateModules()` returning minimal module metadata objects.

**Architecture:** Build one cross-platform native module enumeration helper, then bind it to QuickJS and expose module records as `{ name, base, size, path }`. Keep the implementation narrow so it becomes the base for later `getModuleByAddress(...)`.

**Tech Stack:** C++17, QuickJS, `/proc/self/maps` parsing on Android/Linux, Toolhelp snapshot on Windows, Python smoke/docs.

---

### Task 1: Write the failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- `typeof Module.enumerateModules`
- result is a non-empty array
- current test executable appears with:
  - non-empty `name`
  - non-empty `path`
  - non-null `base`
  - `size > 0`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task24.exe
build\test_js_runtime_native_attach_task24.exe
```

Expected: binding assertion fails because `Module.enumerateModules` is not exposed yet.

### Task 2: Implement the minimal binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add native module record collection**

- Android/Linux: aggregate pathname-backed `/proc/self/maps` entries by path
- Windows: enumerate current-process modules through a snapshot API

**Step 2: Add JS conversion**

- convert native records into JS objects with `name`, `base`, `size`, and `path`

**Step 3: Expose the binding**

- add `JsModuleEnumerateModules(...)`
- register `Module.enumerateModules`

### Task 3: Verify local GREEN

Run:

```powershell
build\test_js_runtime_native_attach_task24.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime binary passes and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving:

- `Module.enumerateModules()` returns at least one entry
- `libnook-agent.so` is present

### Task 5: Rebuild Android artifacts and push

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command.
