# Nook Hexdump Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal global `hexdump(...)` helper that formats bytes from `NativePointer` or `ArrayBuffer` into a plain hexadecimal string.

**Architecture:** Implement `hexdump(...)` entirely inside `src/agent_runtime/js_runtime.cpp`. Reuse the current pointer parsing and readable-range validation for pointer input, and use QuickJS `ArrayBuffer` access APIs for buffer input. Keep formatting intentionally simple: 16 bytes per line, lowercase hex, no header or ANSI formatting.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, current runtime tests under `tests/communication/`, Python smoke script, Android NDK build.

---

### Task 1: Add failing tests for `hexdump(ArrayBuffer)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add one test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "var blob = Memory.dup(src, 5);"
    "send({ type: 'send', payload: hexdump(blob) });";
```

Expected payload:

```text
68 65 6c 6c 6f
```

Add one multiline test:

```cpp
const char* source =
    "var p = Memory.alloc(20);"
    "for (var i = 0; i < 20; i++) p.add(i).writeU8(i);"
    "var blob = Memory.dup(p, 20);"
    "send({ type: 'send', payload: hexdump(blob) });";
```

Expected payload:

```text
00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n10 11 12 13
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task15.exe
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because `hexdump` is not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add `JsHexdump(...)`
- add one helper to format a byte span into the target text layout
- handle `ArrayBuffer` input first
- bind global `hexdump`

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: `ArrayBuffer` hexdump tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add minimal hexdump for array buffers"
```

### Task 2: Add failing tests for `hexdump(NativePointer, options)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "send({ type: 'send', payload: hexdump(src, { length: 5 }) });";
```

Expected payload:

```text
68 65 6c 6c 6f
```

Add one offset test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "send({ type: 'send', payload: hexdump(src, { offset: 1, length: 4 }) });";
```

Expected payload:

```text
65 6c 6c 6f
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because pointer-specific parsing is not implemented yet.

**Step 3: Write minimal implementation**

Extend `JsHexdump(...)`:

- detect `NativePointer` / pointer-like input
- require `length`
- apply `offset`
- validate readable range
- format bytes directly from the pointer

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: both `ArrayBuffer` and `NativePointer` hexdump tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add minimal hexdump for native pointers"
```

### Task 3: Add failing tests for `hexdump(...)` errors

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one missing-length pointer test:

```cpp
const char* source =
    "try {"
    "  var src = Memory.allocUtf8String('hello');"
    "  hexdump(src);"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('requires length') >= 0 ? 'length-required' : String(e) });"
    "}";
```

Add one unreadable-pointer test:

```cpp
const char* source =
    "try {"
    "  hexdump(ptr('0x1'), { length: 4 });"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('unreadable pointer') >= 0 ? 'unreadable' : String(e) });"
    "}";
```

Add one out-of-bounds buffer test:

```cpp
const char* source =
    "try {"
    "  var src = Memory.allocUtf8String('hello');"
    "  var blob = Memory.dup(src, 5);"
    "  hexdump(blob, { offset: 4, length: 2 });"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('out of bounds') >= 0 ? 'bounds' : String(e) });"
    "}";
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: one or more tests fail because these validation paths are not complete yet.

**Step 3: Write minimal implementation**

Extend `JsHexdump(...)` validation to:

- require `length` for pointer input
- reject unreadable pointer ranges
- reject invalid `ArrayBuffer` slices

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: all hexdump tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: validate minimal hexdump inputs"
```

### Task 4: Extend smoke coverage, docs, and build verification

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Write smoke and doc updates**

In `host/nook-py/memory_api_smoke.js` add:

```javascript
send({
  type: 'send',
  payload: `hex:${hexdump(Memory.dup(copySrc, 5))}`
});
```

Expected payload:

```text
hex:68 65 6c 6c 6f
```

Update `docs/architecture.md`:

- document the new global `hexdump(target, options?)`
- explicitly note that this first version only supports `offset` and `length`

Update `host/nook-py/README.md`:

- add the expected `hex:` smoke output

**Step 2: Run regression checks**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
python host\nook-py\tests\test_cli.py
```

Expected:

- runtime test binary passes
- Python CLI tests print `OK`

**Step 3: Run Android build**

Run from:

```text
E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android
```

Command:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

Expected: build succeeds.

**Step 4: Push rebuilt artifacts and commit**

Run:

```powershell
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Commit:

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp host/nook-py/memory_api_smoke.js host/nook-py/README.md docs/architecture.md
git commit -m "feat: add minimal hexdump helper"
```
