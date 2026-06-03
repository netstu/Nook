# Nook Memory Scan Callback Implementation Plan

**Goal:** Add a Frida-shaped `Memory.scan(address, size, pattern, callbacks)` API while reusing the already working `Memory.scanSync(...)` scanner core.

**Architecture:** Keep execution synchronous in the current runtime. Reuse pattern parsing and the scan matcher, then invoke `onMatch`, `onError`, and `onComplete` JS callbacks directly from `js_runtime.cpp`.

---

### Task 1: Add failing runtime tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

1. exact pattern invokes `onMatch` twice and `onComplete` once
2. returning `'stop'` from `onMatch` halts scanning
3. unreadable range invokes `onError` then `onComplete`
4. invalid callbacks object throws

Compile and run the targeted test binary first to confirm RED.

### Task 2: Implement shared scan helper reuse

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Add:

1. a helper that collects match offsets from an address range and parsed pattern
2. `JsMemoryScan(...)`
3. `Memory.scan` binding

Rules:

- invalid arguments throw
- unreadable range uses `onError`/`onComplete` if available instead of throwing
- callback exceptions propagate
- `onComplete` is called exactly once on non-throwing paths

### Task 3: Verify locally

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task17.exe
build\test_js_runtime_native_attach_task17.exe
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

Add one simple callback-based scan example that reports first and second match, then completion.

### Task 5: Rebuild and push Android artifacts

Rebuild `build/android`, push the three main artifacts, then hand the user one exact validation command.
