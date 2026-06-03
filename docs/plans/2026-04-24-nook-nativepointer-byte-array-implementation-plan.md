# Nook NativePointer Byte Array Implementation Plan

**Goal:** Add `readByteArray(length)` and `writeByteArray(value)` to `NativePointer`.

**Architecture:** Extend `js_runtime.cpp` with two new NativePointer methods, reusing existing pointer parsing and memory readability / writability checks.

---

### Task 1: Add failing runtime tests

Add tests for:

1. reading bytes from a UTF-8 allocation
2. writing from `ArrayBuffer`
3. writing from number array
4. unreadable read rejection
5. unwritable write rejection

### Task 2: Implement methods

Add:

- `JsNativePointerReadByteArray(...)`
- `JsNativePointerWriteByteArray(...)`
- bindings in `MakeNativePointer(...)`

### Task 3: Verify locally

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task21.exe
build\test_js_runtime_native_attach_task21.exe
python host\nook-py\tests\test_cli.py
```

### Task 4: Extend smoke and docs

Update:

- `host/nook-py/memory_api_smoke.js`
- `host/nook-py/README.md`
- `docs/architecture.md`

### Task 5: Rebuild and push Android artifacts
