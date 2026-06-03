# Nook Memory Copy And Dup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal block-level `Memory.copy(...)` and `Memory.dup(...)` APIs so QuickJS scripts can copy memory between pointers and snapshot native memory into a JS `ArrayBuffer`.

**Architecture:** Implement both APIs inside `src/agent_runtime/js_runtime.cpp` next to the existing `Memory.alloc(...)` bindings. Reuse the current pointer parsing and readable/writable range checks. Use `memmove` for `copy` and a QuickJS-managed `ArrayBuffer` for `dup`.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, current runtime tests under `tests/communication/`, Python CLI smoke script, Android NDK build.

---

### Task 1: Add failing tests for `Memory.copy(...)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add one test that validates a simple copy:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello-copy');"
    "var dst = Memory.alloc(32);"
    "Memory.copy(dst, src, 11);"
    "send({ type: 'send', payload: dst.readUtf8String() });";
```

Expected payload:

```text
hello-copy
```

Add a second test for overlap:

```cpp
const char* source =
    "var p = Memory.allocUtf8String('abcdef');"
    "Memory.copy(p.add(1), p, 5);"
    "send({ type: 'send', payload: p.readUtf8String() });";
```

Expected payload:

```text
aabcde
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task15.exe
build\test_js_runtime_native_attach_task15.exe
```

Expected: runtime failure because `Memory.copy` is not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add `JsMemoryCopy(...)`
- bind `Memory.copy`
- parse `dst`, `src`, `size`
- validate source readability and destination writability
- use `std::memmove(...)`

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: both `Memory.copy(...)` tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add memory copy api"
```

### Task 2: Add failing tests for `Memory.copy(...)` error handling

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test for unreadable source:

```cpp
const char* source =
    "try {"
    "  var dst = Memory.alloc(8);"
    "  Memory.copy(dst, ptr('0x1'), 4);"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('source unreadable') >= 0 ? 'source-unreadable' : String(e) });"
    "}";
```

Add one test for unwritable destination:

```cpp
const char* source =
    "try {"
    "  var src = Memory.alloc(8);"
    "  Memory.copy(ptr('0x1'), src, 4);"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('destination unwritable') >= 0 ? 'destination-unwritable' : String(e) });"
    "}";
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failures because the new error paths do not exist yet.

**Step 3: Write minimal implementation**

Extend `JsMemoryCopy(...)` to:

- reject unreadable source ranges with `TypeError`
- reject unwritable destination ranges with `TypeError`
- keep zero-length copies valid

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: copy success and copy error tests all pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: validate memory copy ranges"
```

### Task 3: Add failing tests for `Memory.dup(...)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test that validates `dup` returns a JS `ArrayBuffer` with the right length:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello-memory');"
    "var blob = Memory.dup(src, 5);"
    "send({ type: 'send', payload: String(blob.byteLength) });";
```

Expected payload:

```text
5
```

Add a second test that validates content using a typed array:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('ABC');"
    "var blob = Memory.dup(src, 3);"
    "var bytes = new Uint8Array(blob);"
    "send({ type: 'send', payload: String(bytes[0]) + ':' + String(bytes[1]) + ':' + String(bytes[2]) });";
```

Expected payload:

```text
65:66:67
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: runtime failure because `Memory.dup` is not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add `JsMemoryDup(...)`
- bind `Memory.dup`
- validate source readability
- allocate and return a QuickJS `ArrayBuffer`

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: `Memory.dup(...)` tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add memory dup api"
```

### Task 4: Add failing tests for `Memory.dup(...)` error handling and smoke coverage

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Write the failing test**

Add one test:

```cpp
const char* source =
    "try {"
    "  Memory.dup(ptr('0x1'), 4);"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('unreadable source') >= 0 ? 'dup-unreadable' : String(e) });"
    "}";
```

Expected payload:

```text
dup-unreadable
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: error string mismatch or missing validation path.

**Step 3: Write minimal implementation and docs**

In `src/agent_runtime/js_runtime.cpp`:

- finalize `Memory.dup(...)` unreadable-range errors

In `host/nook-py/memory_api_smoke.js`:

- add one copy smoke
- add one dup smoke using `Uint8Array`

In `docs/architecture.md`:

- add `Memory.copy(...)`
- add `Memory.dup(...)`

In `host/nook-py/README.md`:

- mention the new block memory helpers in the smoke section

**Step 4: Run regression and build**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
python host\nook-py\tests\test_cli.py
```

Expected:

- runtime test binary passes
- Python CLI tests print `OK`

Run Android build from:

```text
E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android
```

Command:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

Expected: build succeeds.

**Step 5: Push rebuilt artifacts and commit**

Run:

```powershell
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Commit:

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp host/nook-py/memory_api_smoke.js host/nook-py/README.md docs/architecture.md
git commit -m "feat: add memory copy and dup apis"
```
