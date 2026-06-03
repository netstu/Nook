# Nook Module GetExportByName Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Module.getExportByName(moduleName, exportName)` as the strict variant of `Module.findExportByName(...)`.

**Architecture:** Reuse the existing native export lookup helper and add one JS binding that throws instead of returning `null` on miss. Keep the current nullable API unchanged.

**Tech Stack:** C++17, QuickJS runtime bindings, existing native-js export resolver, Python smoke/docs.

---

### Task 1: Write the failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:

- `typeof Module.getExportByName`
- hit path returns `NativePointer`
- miss path throws

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task23.exe
build\test_js_runtime_native_attach_task23.exe
```

Expected: assertions fail because `Module.getExportByName` is not bound yet.

### Task 2: Implement the minimal binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Reuse export lookup**

- keep `FindNativeJsExportByName(...)` as the only native resolver path

**Step 2: Add strict binding**

- add `JsModuleGetExportByName(...)`
- return the resolved `NativePointer` on success
- throw on miss

**Step 3: Register binding**

- add `getExportByName` on the `Module` object

### Task 3: Verify local GREEN

Run:

```powershell
build\test_js_runtime_native_attach_task23.exe
python host\nook-py\tests\test_cli.py
```

Expected: runtime binary passes and CLI regression remains green.

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke message proving:

- `Module.findExportByName('libnook-agent.so', 'NookInlineHookAddress')`
- `Module.getExportByName('libnook-agent.so', 'NookInlineHookAddress')`
- both resolve to the same pointer string

### Task 5: Rebuild Android artifacts and push

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user one exact validation command.
