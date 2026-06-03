# Nook Process FindRangeByAddress Implementation Plan

**Goal:** Add `Process.findRangeByAddress(address): RangeDetails | null`.

**Architecture:** Reuse the new native range enumeration code by introducing an all-ranges collector, then implement containment lookup in `js_runtime.cpp`.

---

### Task 1: Add failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

1. `typeof Process.findRangeByAddress`
2. resolving a dedicated RW mapping
3. invalid pointer input throwing
4. `ptr('0x1')` returning `null`

Compile and run a targeted test binary first to confirm RED.

### Task 2: Implement lookup on top of native range records

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Add:

1. a native all-ranges collector helper
2. `JsProcessFindRangeByAddress(...)`
3. `Process.findRangeByAddress` binding

Rules:

- invalid pointer input throws
- non-zero unmapped address returns `null`
- returned object shape matches `Process.enumerateRanges(...)`

### Task 3: Verify locally

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task19.exe
build\test_js_runtime_native_attach_task19.exe
python host\nook-py\tests\test_cli.py
```

### Task 4: Extend smoke and docs

Update:

- `host/nook-py/memory_api_smoke.js`
- `host/nook-py/README.md`
- `docs/architecture.md`

Add one smoke message showing that a fresh allocation resolves to a `rw-` range.

### Task 5: Rebuild and push Android artifacts

Rebuild Android, push the three main artifacts, then provide one exact validation command for the device.
