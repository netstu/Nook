# Nook Memory Protect Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal `Memory.protect(address, size, protection)` API so QuickJS scripts can change page protections using Frida-style three-character permission strings.

**Architecture:** Implement `Memory.protect(...)` in `src/agent_runtime/js_runtime.cpp`. Reuse existing pointer parsing, add a small protection-string parser, compute page-aligned ranges, then dispatch to `mprotect(...)` on Android/Linux or `VirtualProtect(...)` on Windows. Keep return semantics simple: invalid arguments throw, syscall failure returns `false`, success returns `true`.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, existing platform memory-protection primitives already used in native hook code, runtime tests under `tests/communication/`, Python smoke script, Android NDK build.

---

### Task 1: Add failing tests for invalid `Memory.protect(...)` arguments

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests for:

1. missing arguments:

```cpp
const char* source =
    "try {"
    "  Memory.protect();"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('requires address, size, and protection') >= 0 ? 'missing' : String(e) });"
    "}";
```

2. null pointer:

```cpp
const char* source =
    "try {"
    "  Memory.protect(NULL, 4096, 'rw-');"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('non-zero pointer') >= 0 ? 'null' : String(e) });"
    "}";
```

3. zero size:

```cpp
const char* source =
    "try {"
    "  var p = Memory.alloc(16);"
    "  Memory.protect(p, 0, 'rw-');"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('positive number') >= 0 ? 'size' : String(e) });"
    "}";
```

4. invalid protection string:

```cpp
const char* source =
    "try {"
    "  var p = Memory.alloc(16);"
    "  Memory.protect(p, 16, 'abc');"
    "  send({ type: 'send', payload: 'unexpected-success' });"
    "} catch (e) {"
    "  send({ type: 'send', payload: String(e).indexOf('r, w, x, and -') >= 0 ? 'prot' : String(e) });"
    "}";
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task15.exe
build\test_js_runtime_native_attach_task15.exe
```

Expected: failures because `Memory.protect` is not defined yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add `JsMemoryProtect(...)`
- add a small helper to validate the 3-character protection string
- bind `Memory.protect`
- implement argument validation only

Do not implement the real syscall yet if validation-only scaffolding helps the tests progress cleanly.

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: invalid-argument tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add memory protect validation"
```

### Task 2: Add failing tests for successful protection changes

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add one test for switching an allocation to read-only and back:

```cpp
const char* source =
    "var page = Memory.alloc(4096);"
    "send({"
    "  type: 'send',"
    "  payload: String(Memory.protect(page, 4096, 'r--')) + ':' + String(Memory.protect(page, 4096, 'rw-'))"
    "});";
```

Expected payload:

```text
true:true
```

Add one test for an executable-style change:

```cpp
const char* source =
    "var page = Memory.alloc(4096);"
    "send({ type: 'send', payload: String(Memory.protect(page, 4096, 'r-x')) });";
```

Expected payload:

```text
true
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure because real protection changes are not wired yet.

**Step 3: Write minimal implementation**

Extend `JsMemoryProtect(...)`:

- parse the protection string into platform flags
- compute page-aligned start/end
- call:
  - `mprotect(...)` on Android/Linux
  - `VirtualProtect(...)` on Windows
- return `JS_NewBool(ctx, success ? 1 : 0)`

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: protection success tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add memory protect syscall path"
```

### Task 3: Add failing tests for full protection-string coverage

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add a test covering several permission strings:

```cpp
const char* source =
    "var page = Memory.alloc(4096);"
    "send({"
    "  type: 'send',"
    "  payload: ["
    "    Memory.protect(page, 4096, '---'),"
    "    Memory.protect(page, 4096, 'r--'),"
    "    Memory.protect(page, 4096, 'rw-'),"
    "    Memory.protect(page, 4096, 'r-x'),"
    "    Memory.protect(page, 4096, 'rwx')"
    "  ].join(':')"
    "});";
```

Expected payload:

```text
true:true:true:true:true
```

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: failure if not all valid 3-character strings are accepted/mapped correctly.

**Step 3: Write minimal implementation**

Refine the protection parser so it:

- accepts any legal `rwx` combination
- rejects malformed lengths and invalid characters
- maps all legal combinations consistently

**Step 4: Run test to verify it passes**

Run:

```powershell
build\test_js_runtime_native_attach_task15.exe
```

Expected: full protection-string coverage test passes.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: support full memory protect permission strings"
```

### Task 4: Extend smoke coverage, docs, and build verification

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Write smoke and doc updates**

In `host/nook-py/memory_api_smoke.js`, add a short protect smoke:

```javascript
const page = Memory.alloc(4096);
send({
  type: 'send',
  payload: `protect:${Memory.protect(page, 4096, 'r--')}:${Memory.protect(page, 4096, 'rw-')}`
});
```

Expected payload:

```text
protect:true:true
```

Update `docs/architecture.md`:

- document `Memory.protect(address, size, protection)`
- note that the current JS runtime supports Frida-style three-character permission strings

Update `host/nook-py/README.md`:

- add the expected `protect:true:true` smoke message

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
git commit -m "feat: add memory protect api"
```
