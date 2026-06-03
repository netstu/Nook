# Nook Module FindBaseAddress/GetBaseAddress Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Module.findBaseAddress(name)` and `Module.getBaseAddress(name)` with Frida-style miss semantics.

**Architecture:** Resolve loaded module base addresses in one small native helper, then bind two JS entry points on `Module`. Keep `findBaseAddress(...)` nullable and `getBaseAddress(...)` strict.

**Tech Stack:** C++17, QuickJS runtime bindings, existing native hook/module-info helpers, Python smoke docs.

---

### Task 1: Write failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:

- `typeof Module.findBaseAddress` and `typeof Module.getBaseAddress`
- resolving the current test executable module name
- `Module.findBaseAddress('missing-module-for-test.so') === null`
- `Module.getBaseAddress('missing-module-for-test.so')` throws

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task22.exe
build\test_js_runtime_native_attach_task22.exe
```

Expected: runtime assertions fail because the new `Module.*BaseAddress` bindings do not exist yet.

### Task 2: Implement the minimal binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add a native module-base lookup helper**

- Android/Linux: use `ElfHooker::get_module_info(...)`
- Windows: use `GetModuleHandleA(...)`

**Step 2: Bind the JS APIs**

- add `JsModuleFindBaseAddress(...)`
- add `JsModuleGetBaseAddress(...)`
- register both functions on `Module`

**Step 3: Keep miss semantics separate**

- `findBaseAddress(...)` returns `JS_NULL`
- `getBaseAddress(...)` throws `InternalError` on miss

### Task 3: Verify local GREEN

**Files:**
- Modify: none

Run:

```powershell
build\test_js_runtime_native_attach_task22.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime test binary passes and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving:

- `Module.findBaseAddress('libnook-agent.so')` resolves
- `Module.getBaseAddress('libnook-agent.so')` matches
- the base pointer maps back to a module range

### Task 5: Rebuild Android artifacts and push

**Files:**
- Modify: none

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command for the smoke script.
