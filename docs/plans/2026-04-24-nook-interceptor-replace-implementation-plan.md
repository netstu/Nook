# Nook Interceptor.replace Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal `Interceptor.replace(target, replacement)` and `Interceptor.revert(target)` API with Frida-like external shape, backed by the existing inline-hook implementation.

**Architecture:** Keep the public API target-centric while internally reusing the current native hook install/unhook path. Add a small per-script replace registry in `src/agent_runtime/js_runtime.cpp`, validate `replacement` as a `NativeCallback` trampoline, install through the existing inline hook backend, and unhook through the existing script cleanup path.

**Tech Stack:** C++17, QuickJS, current `JsRuntime`, existing native hook bridge, runtime tests under `tests/communication/`, Python smoke/docs, Android NDK build.

---

### Task 1: Write the failing runtime tests for `Interceptor.replace` / `revert`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Add binding tests**

Add:

```javascript
send({
  type: 'send',
  payload: typeof Interceptor.replace + ':' + typeof Interceptor.revert
});
```

Expected after implementation: `function:function`

**Step 2: Add validation tests**

Cover:

- non-pointer target
- non-`NativeCallback` replacement
- duplicate replace on same target
- revert on missing target

**Step 3: Add behavioral tests**

Use a small exported test function already reachable in-process and verify:

- baseline result
- replaced result after `Interceptor.replace(...)`
- restored result after `Interceptor.revert(...)`

**Step 4: Add unload cleanup test**

Install one replacement, unload the script, and assert the function behavior is restored.

**Step 5: Run test binary to verify RED**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task30.exe
build\test_js_runtime_native_attach_task30.exe
```

Expected: new replace/revert assertions fail because the bindings do not exist yet.

**Step 6: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add failing interceptor replace runtime tests"
```

### Task 2: Add replace/revert bindings and replace registry

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add a per-script replace registry**

Track:

- `target_address`
- `replacement_address`
- `hook_id`

Key the registry by script id and `target_address`.

**Step 2: Add `Interceptor.replace(...)` validation**

Validate:

- `target` is a non-zero pointer-like value
- `replacement` is a `NativeCallback` trampoline pointer created by the runtime
- duplicate replace of the same target is rejected

**Step 3: Add `Interceptor.revert(...)` lookup validation**

Validate:

- target is pointer-like
- target exists in the current script replace registry

**Step 4: Expose the new bindings**

Register:

- `Interceptor.replace`
- `Interceptor.revert`

**Step 5: Run the runtime test binary**

Run:

```powershell
build\test_js_runtime_native_attach_task30.exe
```

Expected: binding and validation tests move green while behavioral tests still fail.

**Step 6: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add interceptor replace binding and validation"
```

### Task 3: Reuse the inline hook backend for replace/revert behavior

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Install replacement through the existing inline hook path**

Use the already available hook installer infrastructure to install:

- target address
- replacement trampoline address

Capture the internal `hook_id` for later revert and unload cleanup.

**Step 2: Implement `revert(target)`**

Look up the replace record by `target_address`, unhook through the existing detach path, and erase the record.

**Step 3: Re-run runtime test binary to verify GREEN**

Run:

```powershell
build\test_js_runtime_native_attach_task30.exe
```

Expected: replace changes behavior and revert restores it.

**Step 4: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add interceptor replace and revert behavior"
```

### Task 4: Add unload cleanup and regression verification

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Release replace records on script unload**

Ensure:

- the replacement hook is uninstalled
- replace registry entries are erased
- no QuickJS or hook state leaks remain

**Step 2: Re-run runtime test binary**

Run:

```powershell
build\test_js_runtime_native_attach_task30.exe
```

Expected: unload cleanup tests pass.

**Step 3: Run CLI regression**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 4: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "fix: clean up interceptor replace records on unload"
```

### Task 5: Extend smoke and docs

**Files:**
- Modify: `host/nook-py/memory_api_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Add a minimal replace/revert smoke**

Add one smoke block that:

- resolves a stable export
- replaces it with `NativeCallback`
- calls it through `NativeFunction`
- verifies replaced result
- reverts it
- verifies original result

**Step 2: Document current limits**

Be explicit that the first version:

- requires `replacement` to be `NativeCallback`
- supports pointer-like targets only
- does not yet expose `original`

**Step 3: Run CLI regression again**

Run:

```powershell
python host\nook-py\tests\test_cli.py
```

Expected: `OK`

**Step 4: Commit**

```bash
git add host/nook-py/memory_api_smoke.js host/nook-py/README.md docs/architecture.md
git commit -m "docs: document interceptor replace support"
```

### Task 6: Rebuild Android artifacts and push to device

**Files:**
- Build output only

**Step 1: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

**Step 2: Push updated artifacts**

Run:

```powershell
adb push build\android\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push build\android\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push build\android\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

**Step 3: Hand over one exact validation command**

Expected handoff command shape:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\memory_api_smoke.js --wait --usb
```

**Step 4: Commit**

```bash
git add docs/plans/2026-04-24-nook-interceptor-replace-design.md docs/plans/2026-04-24-nook-interceptor-replace-implementation-plan.md
git commit -m "docs: add interceptor replace plan"
```
