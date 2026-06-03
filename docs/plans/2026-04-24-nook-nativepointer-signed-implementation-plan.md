# Nook NativePointer Signed Access Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal signed scalar `NativePointer` accessors so QuickJS scripts can use `readS8/readS16/readS32/readS64` and `writeS8/writeS16/writeS32/writeS64` with the same safety model as the existing unsigned accessors.

**Architecture:** Reuse the existing `JsNativePointerRead(...)` and `JsNativePointerWrite(...)` dispatchers. Extend the `MakeNativePointer(...)` method table with signed variants, return JS numbers for signed 8/16/32-bit reads, and return minimal `Int64` objects for signed 64-bit reads. Preserve all current readable/writable range checks.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, current runtime tests under `tests/communication/`, Python CLI smoke regression, Android NDK build.

---

### Task 1: Add failing tests for signed scalar reads

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add one test that allocates a block and validates:

```cpp
const char* source =
    "var p = Memory.alloc(16);"
    "p.writeU8(255);"
    "p.add(2).writeU16(65534);"
    "p.add(4).writeU32(4294967293);"
    "send({ type: 'send', payload: "
    "String(p.readS8()) + ':' + "
    "String(p.add(2).readS16()) + ':' + "
    "String(p.add(4).readS32()) });";
```

Expected payload:

```text
-1:-2:-3
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task15.exe
build\test_js_runtime_native_attach_task15.exe
```

Expected: runtime failure because `readS8/readS16/readS32` are not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- extend `MakeNativePointer(...)` with `readS8`, `readS16`, `readS32`
- extend `JsNativePointerRead(...)` `magic` handling for signed 8/16/32 reads

Use:

- `int8_t`
- `int16_t`
- `int32_t`

Return JS number values with correct sign extension.

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: the new signed read test passes.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add signed native pointer reads"
```

### Task 2: Add failing tests for signed scalar writes

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test that validates:

```cpp
const char* source =
    "var p = Memory.alloc(16);"
    "p.writeS8(-1);"
    "p.add(2).writeS16(-2);"
    "p.add(4).writeS32(-3);"
    "send({ type: 'send', payload: "
    "String(p.readU8()) + ':' + "
    "String(p.add(2).readU16()) + ':' + "
    "String(p.add(4).readU32()) });";
```

Expected payload:

```text
255:65534:4294967293
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because `writeS8/writeS16/writeS32` are not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- extend `MakeNativePointer(...)` with `writeS8`, `writeS16`, `writeS32`
- extend `JsNativePointerWrite(...)` for signed 8/16/32 writes
- parse JS numbers and narrow to:
  - `int8_t`
  - `int16_t`
  - `int32_t`

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: both signed read and signed write tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add signed native pointer writes"
```

### Task 3: Add failing tests for signed 64-bit read/write

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test for `readS64()`:

```cpp
const char* read_source =
    "var p = Memory.alloc(8);"
    "p.writeU64(uint64('18446744073709551615'));"
    "send({ type: 'send', payload: p.readS64().toString() });";
```

Expected payload:

```text
-1
```

Add one test for `writeS64()`:

```cpp
const char* write_source =
    "var p = Memory.alloc(8);"
    "p.writeS64(int64('-1'));"
    "send({ type: 'send', payload: p.readS64().toString() + ':' + p.readU64().toString() });";
```

Expected payload:

```text
-1:18446744073709551615
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because `readS64/writeS64` are not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- extend `MakeNativePointer(...)` with `readS64` and `writeS64`
- make `readS64()` wrap the raw bits with `MakeInteger64Object(..., true)`
- make `writeS64(...)` reuse the current 64-bit parser and preserve raw bits

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: all signed scalar tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add signed 64-bit native pointer access"
```

### Task 4: Run regression checks and update docs

**Files:**
- Modify: `docs/architecture.md`
- Modify: `host/nook-py/README.md`

**Step 1: Write the doc updates**

Update [docs/architecture.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/architecture.md):

- extend the documented `NativePointer` method list with:
  - `readS8()`, `readS16()`, `readS32()`, `readS64()`
  - `writeS8(...)`, `writeS16(...)`, `writeS32(...)`, `writeS64(...)`
- note that `readS64()` returns the minimal `Int64` object

Update [host/nook-py/README.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/README.md):

- add one short note to the `Memory` / `NativePointer` smoke section about signed scalar support

**Step 2: Run runtime regression**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
python host\nook-py\tests\test_cli.py
```

Expected:

- runtime binary passes
- Python CLI tests print `Ran 42 tests ... OK` or the current updated count with `OK`

**Step 3: Run Android build**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

Working directory:

```text
E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android
```

Expected: Android build succeeds and rebuilds `libnook-agent.so`, `libnook.so`, and dependent smoke libraries.

**Step 4: Push rebuilt device artifacts if build succeeds**

Run:

```powershell
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

**Step 5: Commit**

```bash
git add docs/architecture.md host/nook-py/README.md tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add signed native pointer scalar access"
```
