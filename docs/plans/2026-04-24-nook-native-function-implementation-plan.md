# Nook NativeFunction Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal `NativeFunction` API that can synchronously call native functions with basic integer and pointer types.

**Architecture:** Keep the implementation inside the current QuickJS runtime layer. Parse constructor metadata in `src/agent_runtime/js_runtime.cpp`, attach it to a callable JS function object, and invoke a narrow arm64-oriented native call bridge for `void`, `int`, `uint32`, and `pointer`.

**Tech Stack:** C++17, QuickJS, current `JsRuntime`, existing runtime tests under `tests/communication/`, Python smoke/docs, Android NDK build.

---

### Task 1: Write the failing runtime tests for `NativeFunction`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Add a binding test**

Add a test that loads:

```javascript
send({ type: 'send', payload: typeof NativeFunction });
```

Expected payload after implementation: `function`

**Step 2: Add validation tests**

Add tests covering:

- unsupported return type
- unsupported argument type
- non-array `argTypes`
- wrong argument count on invocation

**Step 3: Add native call tests**

Add tiny in-process test helpers in the same test file:

```cpp
extern "C" int TestNativeFunctionAdd(int left, int right);
extern "C" uintptr_t TestNativeFunctionEchoPointer(uintptr_t value);
extern "C" void TestNativeFunctionSinkU32(uint32_t value);
```

Drive them from JS with `new NativeFunction(...)`.

**Step 4: Run the test binary to verify RED**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task28.exe
build\test_js_runtime_native_attach_task28.exe
```

Expected: the new `NativeFunction` assertions fail because the binding does not exist yet.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add failing native function runtime tests"
```

### Task 2: Add `NativeFunction` metadata parsing and callable object creation

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add a minimal native function type enum and parsers**

Parse:

- `void`
- `int`
- `uint32`
- `pointer`

Reject everything else with `TypeError`.

**Step 2: Add constructor validation**

Implement:

```javascript
new NativeFunction(address, returnType, argTypes)
```

Validation requirements:

- exactly 3 arguments
- pointer-like target
- string return type
- array `argTypes`

**Step 3: Build a callable JS object**

Attach hidden runtime properties for:

- target address
- return type enum
- argument type enum array

Return a callable function that dispatches to a dedicated invoke helper.

**Step 4: Run the runtime test binary**

Run:

```powershell
build\test_js_runtime_native_attach_task28.exe
```

Expected: validation tests move green while call-path tests still fail.

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add native function constructor validation"
```

### Task 3: Implement the minimal native call bridge

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add argument marshalling**

Marshal JS arguments into a small fixed native slot array:

- `int` -> signed 32-bit narrowed from JS integer
- `uint32` -> unsigned 32-bit narrowed from JS integer
- `pointer` -> existing pointer parser

**Step 2: Add a narrow call dispatcher**

Implement a minimal dispatcher for:

- 0 to 4 arguments
- return kinds: `void`, `int`, `uint32`, `pointer`

Keep the implementation intentionally explicit instead of generic metaprogramming.

**Step 3: Convert native return values back to JS**

- `void` -> `undefined`
- `int` -> JS `int32`
- `uint32` -> JS `uint32`
- `pointer` -> `NativePointer`

**Step 4: Run the runtime test binary to verify GREEN**

Run:

```powershell
build\test_js_runtime_native_attach_task28.exe
```

Expected: all `NativeFunction` runtime tests pass.

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add minimal native function invocation"
```

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Add a device smoke**

Add one smoke block that:

- resolves a known exported test function
- builds a `NativeFunction`
- calls it
- sends the result

**Step 2: Document the current supported type subset**

Be explicit that the first version only supports:

- return: `void`, `int`, `uint32`, `pointer`
- args: `int`, `uint32`, `pointer`

**Step 3: Run Python CLI regression**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 4: Commit**

```bash
git add host/nook-py/memory_api_smoke.js host/nook-py/README.md docs/architecture.md
git commit -m "docs: document minimal native function support"
```

### Task 5: Rebuild Android artifacts and push to device

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
git add docs/plans/2026-04-24-nook-native-function-design.md docs/plans/2026-04-24-nook-native-function-implementation-plan.md
git commit -m "docs: add native function plan"
```
