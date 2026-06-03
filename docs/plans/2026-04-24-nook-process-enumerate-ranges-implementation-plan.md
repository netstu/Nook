# Nook Process EnumerateRanges Implementation Plan

**Goal:** Add a minimal `Process.enumerateRanges(protection)` API returning `{ base, size, protection }[]`.

**Architecture:** Extend `src/agent_runtime/js_runtime.cpp` with platform-specific range enumeration helpers, then bind a small global `Process` object exposing `enumerateRanges`.

---

### Task 1: Add failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

1. `typeof Process.enumerateRanges`
2. invalid filter rejection
3. finding a dedicated RW mapping in `rw-`
4. finding the same mapping in `r--` after `Memory.protect`

Compile and run a targeted test binary first to confirm RED.

### Task 2: Implement range enumeration helpers

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Add:

1. a native range record struct
2. Linux/Android `/proc/self/maps` parsing
3. Windows `VirtualQuery` enumeration
4. `JsProcessEnumerateRanges(...)`
5. global `Process` binding

### Task 3: Verify locally

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task18.exe
build\test_js_runtime_native_attach_task18.exe
python host\nook-py\tests\test_cli.py
```

Expected:

- runtime tests pass
- CLI regression remains green

### Task 4: Extend smoke and docs

Update:

- `host/nook-py/memory_api_smoke.js`
- `host/nook-py/README.md`
- `docs/architecture.md`

Add one simple smoke message with the count from `Process.enumerateRanges('rw-')`.

### Task 5: Rebuild and push Android artifacts

Rebuild Android artifacts, push the three main binaries, and then provide one exact device validation command.
