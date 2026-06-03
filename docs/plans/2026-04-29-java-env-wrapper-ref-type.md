# Java Env object ref type Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.getObjectRefType(obj)` as the next safe Frida-aligned JNI object-reference query helper.

**Architecture:** Extend the existing `Env` wrapper with one object-wrapper-only helper. The runtime will query JNI `GetObjectRefType` once per call and map the result into a stable script-level string.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JNI bridge test hooks in `js_runtime_test_api.h`, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.getObjectRefType(obj)` returns `"global"`
- `env.getObjectRefType(obj)` returns `"invalid"`
- invalid input is rejected clearly

**Step 2: Run the desktop binary to verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the helper and test hook do not exist yet

### Task 2: Add narrow host test hook

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add callback contract**

Add one callback type and setter/resetter API for JNI `GetObjectRefType`.

**Step 2: Wire callback into runtime state**

Extend the JNI bridge testing state with the new callback.

### Task 3: Implement the public helper

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add runtime query helper**

Implement one internal query helper for `GetObjectRefType`.

**Step 2: Add JS-facing method**

Implement:

- `Env.getObjectRefType(obj)`

Behavior:

- accepts a Java object wrapper
- returns:
  - `"invalid"`
  - `"local"`
  - `"global"`
  - `"weak-global"`
- rejects invalid input with clear `TypeError`

**Step 3: Run the desktop binary**

Expected:

- new tests pass
- previous `Env` regressions remain green

### Task 4: Add Android smoke

**Files:**
- Create: `host/nook-py/java_env_wrapper_ref_type_smoke.js`

**Step 1: Write the smoke**

Print:

- binding shape
- ref type of one real Java object wrapper

**Step 2: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

### Task 5: Update regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document why this helper is safe**

Record that it is a single JNI query and remains inside the current `Env` architecture boundary.
