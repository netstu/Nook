# Nook Memory ScanSync Implementation Plan

> **For Claude:** Implement this as a minimal synchronous scanner first. Do not add async callbacks in the same change.

**Goal:** Add `Memory.scanSync(address, size, pattern)` with exact byte-token matching plus `??` wildcards.

**Architecture:** Extend `src/agent_runtime/js_runtime.cpp` with a small pattern parser and a simple linear scanner. Reuse existing pointer parsing and readability checks, then return a JS array of `{ address, size }` matches.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, runtime tests under `tests/communication/`, Python smoke script, Android NDK build.

---

### Task 1: Add failing runtime tests for `scanSync`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

Add tests for:

1. exact pattern match on `"hello hello"` returning two matches
2. wildcard byte match using `??`
3. unmatched pattern returning `[]`
4. invalid pattern string throwing
5. unreadable range throwing

Run the targeted test binary and confirm failure before implementation.

### Task 2: Implement pattern parsing and linear scan

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

Add:

1. a helper that parses the pattern string into byte tokens
2. `JsMemoryScanSync(...)`
3. `Memory.scanSync` binding

Rules:

- exact token: two hex digits
- wildcard token: `??`
- empty pattern is invalid
- `size < pattern_length` returns `[]`

### Task 3: Verify local runtime behavior

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task16.exe
build\test_js_runtime_native_attach_task16.exe
python host\nook-py\tests\test_cli.py
```

Expected:

- runtime test binary exits `0`
- CLI regression remains green

### Task 4: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

Add one smoke example that scans `"hello hello"` for `68 65 6c 6c 6f` and reports both match offsets or addresses.

### Task 5: Build and push Android artifacts

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then hand the user an exact `nook-cli attach ... memory_api_smoke.js` command for device validation.
