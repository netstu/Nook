# Nook NativeCallback Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal `NativeCallback` API that exposes a JS function as a callable native function pointer for the same small type subset as `NativeFunction`.

**Architecture:** Reuse the current QuickJS runtime and per-script ownership model. Store callback metadata in `src/agent_runtime/js_runtime.cpp`, generate a minimal trampoline/dispatch path, return a pointer-like value from `NativeCallback`, and verify the path by invoking that pointer through `NativeFunction`.

**Tech Stack:** C++17, QuickJS, current `JsRuntime`, existing runtime tests under `tests/communication/`, Python smoke/docs, Android NDK build.

---

### Task 1: Write the failing runtime tests for `NativeCallback`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Add a binding test**

Add:

```javascript
send({ type: 'send', payload: typeof NativeCallback });
```

Expected after implementation: `function`

**Step 2: Add constructor validation tests**

Cover:

- non-function first argument
- unsupported return type
- unsupported argument type

**Step 3: Add callback roundtrip tests**

Drive these from JS:

- `uint32(uint32, uint32)` callback called through `NativeFunction`
- `pointer(pointer)` callback roundtrip
- `void(uint32)` callback mutating native test state

**Step 4: Add unload cleanup test**

Create a callback, unload the script, and assert callback records owned by that script are released.

**Step 5: Run test binary to verify RED**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task29.exe
build\test_js_runtime_native_attach_task29.exe
```

Expected: the new assertions fail because `NativeCallback` is not exposed yet.

**Step 6: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add failing native callback runtime tests"
```

### Task 2: Add constructor validation and per-script callback registry

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Extend runtime state with callback registry**

Track per-script callback records containing:

- callback id
- JS function
- return type
- argument types
- trampoline address or trampoline slot identifier

**Step 2: Add `NativeCallback(...)` constructor validation**

Validate:

- first argument is a function
- return type string is supported
- `argTypes` is an array of supported argument types

**Step 3: Return a pointer-like value**

For the first version, return a `NativePointer` wrapping the generated trampoline address.

**Step 4: Run test binary**

Run:

```powershell
build\test_js_runtime_native_attach_task29.exe
```

Expected: binding and validation tests move green while roundtrip tests still fail.

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add native callback constructor validation"
```

### Task 3: Implement minimal callback trampoline dispatch

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add argument marshalling from native into JS**

Support:

- `int`
- `uint32`
- `pointer`

**Step 2: Invoke JS under the owning script context**

Use the same script ownership discipline already present in the runtime so callbacks execute under the correct script id.

**Step 3: Marshal JS return values back to native**

Support:

- `void`
- `int`
- `uint32`
- `pointer`

**Step 4: Run test binary to verify GREEN**

Run:

```powershell
build\test_js_runtime_native_attach_task29.exe
```

Expected: roundtrip callback tests pass.

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add minimal native callback dispatch"
```

### Task 4: Add unload cleanup and regression verification

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Release callback records on script unload**

Ensure:

- JS function references are freed
- trampoline slots are released
- per-script callback entries are erased

**Step 2: Re-run runtime test binary**

Run:

```powershell
build\test_js_runtime_native_attach_task29.exe
```

Expected: cleanup tests pass and no QuickJS shutdown leak assertions occur.

**Step 3: Run CLI regression**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 4: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "fix: clean up native callbacks on script unload"
```

### Task 5: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Add a self-contained device smoke**

Add one smoke block that:

- builds a `NativeCallback`
- wraps it in `NativeFunction`
- invokes it
- sends the result

**Step 2: Document current limits**

Be explicit that the first version:

- returns a pointer-like value
- supports only `void | int | uint32 | pointer`
- is intended as the basis for later `Interceptor.replace`

**Step 3: Run CLI regression again**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 4: Commit**

```bash
git add host/nook-py/memory_api_smoke.js host/nook-py/README.md docs/architecture.md
git commit -m "docs: document minimal native callback support"
```

### Task 6: Rebuild Android artifacts and push to device

**Files:**
- Build output only

**Step 1: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

**Step 2: Push updated artifacts**

Run:

```powershell
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

**Step 3: Hand over one exact validation command**

Expected handoff command shape:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\memory_api_smoke.js --wait --usb
```

**Step 4: Commit**

```bash
git add docs/plans/2026-04-24-nook-native-callback-design.md docs/plans/2026-04-24-nook-native-callback-implementation-plan.md
git commit -m "docs: add native callback plan"
```
