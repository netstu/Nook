# Nook Hexdump Style Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend the existing `hexdump(...)` helper with `header` and `ansi` options and upgrade the output to a Frida-like layout with address, hex, and ASCII columns.

**Architecture:** Keep one `JsHexdump(...)` entrypoint in `src/agent_runtime/js_runtime.cpp`. Reuse the current target parsing and validation, then feed the resolved byte span into one shared formatting helper that can emit header lines, aligned columns, and optional ANSI color sequences.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, current runtime tests under `tests/communication/`, Python smoke script, Android NDK build.

---

### Task 1: Add failing tests for header output

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add one `ArrayBuffer` test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "var blob = Memory.dup(src, 5);"
    "send({ type: 'send', payload: hexdump(blob, { header: true }) });";
```

Assert that:

- the output contains a leading header line
- the output contains the `68 65 6c 6c 6f` byte sequence
- the output contains the ASCII text `hello`

Add one `NativePointer` test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "send({ type: 'send', payload: hexdump(src, { length: 5, header: true }) });";
```

Assert that:

- the output contains the byte sequence
- the output contains `hello`
- the output contains at least one address-like prefix

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task15.exe
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because the current formatter does not emit header or ASCII/address columns.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- extend option parsing with `header`
- upgrade the internal formatter to emit:
  - optional header line
  - address column
  - hex byte column
  - ASCII column

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: header tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add hexdump header and ascii layout"
```

### Task 2: Add failing tests for ANSI output

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test:

```cpp
const char* source =
    "var src = Memory.allocUtf8String('hello');"
    "var blob = Memory.dup(src, 5);"
    "send({ type: 'send', payload: hexdump(blob, { ansi: true }) });";
```

Assert that the output contains:

- the byte sequence `68 65 6c 6c 6f`
- the ASCII text `hello`
- ANSI escape introducer `\u001b[` or `\x1b[`

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because ANSI styling is not present yet.

**Step 3: Write minimal implementation**

Extend the formatter:

- parse `ansi`
- wrap address and ASCII columns with lightweight ANSI color codes
- preserve the same visible layout when ANSI is disabled

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: ANSI tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add ansi styling to hexdump"
```

### Task 3: Add failing tests for printable and non-printable ASCII rendering

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one mixed-content test:

```cpp
const char* source =
    "var p = Memory.alloc(6);"
    "p.add(0).writeU8(0x41);"
    "p.add(1).writeU8(0x42);"
    "p.add(2).writeU8(0x00);"
    "p.add(3).writeU8(0x7f);"
    "p.add(4).writeU8(0x43);"
    "p.add(5).writeU8(0x44);"
    "var blob = Memory.dup(p, 6);"
    "send({ type: 'send', payload: hexdump(blob, { header: true }) });";
```

Assert that:

- the hex side contains `41 42 00 7f 43 44`
- the ASCII side contains `AB..CD`

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure if ASCII rendering is incomplete or misaligned.

**Step 3: Write minimal implementation**

Refine the formatter:

- printable bytes `0x20..0x7e` render as characters
- all other bytes render as `.`
- keep ASCII column aligned on short lines

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: styled hexdump tests all pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: refine hexdump ascii rendering"
```

### Task 4: Extend smoke coverage, docs, and build verification

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Write smoke and doc updates**

In `host/nook-py/memory_api_smoke.js`, add one short styled dump message such as:

```javascript
send({
  type: 'send',
  payload: `hex-styled:${hexdump(dupBlob, { header: true })}`
});
```

Keep the dumped length short enough to avoid noisy logs.

Update `docs/architecture.md`:

- document `header` and `ansi` support
- note that the output now includes address, hex, and ASCII columns

Update `host/nook-py/README.md`:

- add one short example line showing styled hexdump output

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
git commit -m "feat: add styled hexdump output"
```
