# Nook Module FindRangeByAddress Implementation Plan

**Goal:** Add `Module.findRangeByAddress(address): RangeDetails | null`.

**Architecture:** Reuse the existing process-side range lookup implementation and expose it through the `Module` object.

---

### Task 1: Add failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

1. `typeof Module.findRangeByAddress`
2. hit on a dedicated test page mapping
3. miss on `ptr('0x1')`

Compile and run a targeted runtime binary first to confirm RED.

### Task 2: Implement binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Add:

1. a small shared helper if needed
2. `JsModuleFindRangeByAddress(...)`
3. `Module.findRangeByAddress` binding

### Task 3: Verify locally

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task20.exe
build\test_js_runtime_native_attach_task20.exe
python host\nook-py\tests\test_cli.py
```

### Task 4: Extend smoke and docs

Update:

- `host/nook-py/memory_api_smoke.js`
- `host/nook-py/README.md`
- `docs/architecture.md`

Add one smoke message showing `Module.findRangeByAddress(copyDst)` returning `rw-`.

### Task 5: Rebuild and push Android artifacts

Rebuild Android, push the three main artifacts, then hand the user one exact validation command.
