# Nook Native JS Bridge Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first minimal `Nook.Native.attach({ type: "inline", module, symbol, onEnter, onLeave })` bridge so QuickJS scripts can install inline hooks and receive native hook events in JavaScript.

**Architecture:** Keep the implementation agent-local. Add a QuickJS binding layer in the runtime, a small native hook bridge/registry, and a thread-safe hook-event queue that is drained on the runtime side before invoking JS callbacks. First version supports only `type: "inline"` and `module + symbol`.

**Tech Stack:** C++17, QuickJS, existing `JsRuntime`, existing `NookInlineHook` framework, current agent runtime tests, Android NDK toolchain.

---

### Task 1: Add failing QuickJS runtime tests for `Nook.Native.attach` validation

**Files:**
- Modify: `tests/communication/test_js_runtime_rpc.cpp`
- Create: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests that validate:

- `Nook.Native` exists
- `typeof Nook.Native.attach === "function"`
- `Nook.Native.attach()` without object throws
- missing `type` throws
- `type: "plt"` throws `not implemented yet`
- missing `module` throws
- missing `symbol` throws
- non-function `onEnter` or `onLeave` throws

Example script shape for validation:

```cpp
const char* script =
    "Nook.Native.attach({"
    "  type: 'inline',"
    "  module: 'libdemo.so',"
    "  symbol: 'target',"
    "  onEnter: function(args) {},"
    "  onLeave: function(retval) {}"
    "});";
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: compile failure or runtime failure because `Nook.Native.attach` is not defined.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- create `Nook` object if needed
- create `Nook.Native`
- bind `attach`
- implement argument validation only

Do not install real hooks yet. Return a temporary success object only in the valid path if needed for test scaffolding.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: validation tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add native attach js binding validation"
```

### Task 2: Add failing bridge/registry tests for successful attach registration

**Files:**
- Create: `src/agent_runtime/nook_native_js_bridge.h`
- Create: `src/agent_runtime/nook_native_js_bridge.cpp`
- Create: `tests/communication/test_native_js_bridge.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests for a small bridge API that:

- receives `type`, `module`, `symbol`
- allocates `hookId`
- stores hook metadata in a registry
- returns `{ ok: true, hookId }`

Use fake installer dependencies so the test does not need a real inline hook install.

Test cases:

- success path assigns `hookId == 1`
- second install assigns `hookId == 2`
- unsupported type fails
- installer failure returns error text

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_native_js_bridge.cpp src/agent_runtime/nook_native_js_bridge.cpp -o build/test_native_js_bridge.exe
build\test_native_js_bridge.exe
```

Expected: missing bridge symbols or failing assertions.

**Step 3: Write minimal implementation**

Create:

- `struct NativeJsHookRequest`
- `struct NativeJsHookRecord`
- `class NativeJsHookRegistry`
- `bool InstallNativeJsHook(...);`

For first version:

- support only `"inline"`
- use injected installer function pointer for tests
- store:
  - `hook_id`
  - `module`
  - `symbol`
  - native hook handle

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: bridge tests pass.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_native_js_bridge.h src/agent_runtime/nook_native_js_bridge.cpp tests/communication/test_native_js_bridge.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add native js hook registry"
```

### Task 3: Add failing tests for JS callback reference registration

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests that after a successful attach:

- the JS runtime stores `onEnter`
- the JS runtime stores `onLeave`
- unloading the script releases callback references

Test expectation:

- attach returns `{ ok: true, hookId: 1 }`
- registry lookup for `hookId` finds both callbacks
- script unload clears that mapping

**Step 2: Run test to verify it fails**

Run the test command from Task 1.

Expected: no callback registry exists yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add a native-hook callback table keyed by:
  - `script_id`
  - `hook_id`
- store duplicated `JSValue` handles for `onEnter` and `onLeave`
- clear them on script unload and runtime shutdown

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: callback registration tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: store native js callbacks per script"
```

### Task 4: Add failing tests for hook event queue and dispatcher

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.h`
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`
- Modify: `tests/communication/test_native_js_bridge.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests that:

- enqueue an `enter` event for `hookId=1`
- dispatcher delivers it to the matching JS `onEnter`
- enqueue a `leave` event for `hookId=1`
- dispatcher delivers it to `onLeave`

Use a JS script like:

```javascript
var events = [];
Nook.Native.attach({
  type: "inline",
  module: "libdemo.so",
  symbol: "target",
  onEnter: function(args) { events.push("enter:" + args[0]); },
  onLeave: function(retval) { events.push("leave:" + retval); }
});
```

Then assert `events` contains both entries after dispatcher drain.

**Step 2: Run test to verify it fails**

Run the bridge and runtime test commands from Tasks 1 and 2.

Expected: no event queue/dispatcher exists yet.

**Step 3: Write minimal implementation**

Add:

- `enum class HookEventPhase { kEnter, kLeave };`
- `struct HookEvent`
- thread-safe queue storage
- dispatcher function callable from runtime side

First version payload:

- `hook_id`
- phase
- `arg_values[8]` or `retval`

Convert numeric values to hex strings before invoking JS.

**Step 4: Run test to verify it passes**

Run the same commands as step 2.

Expected: event queue and dispatcher tests pass.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_native_js_bridge.h src/agent_runtime/nook_native_js_bridge.cpp tests/communication/test_native_js_bridge.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add native hook event dispatch to js"
```

### Task 5: Add failing tests for callback exception safety

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests for:

- `onEnter` throwing a JS exception
- dispatcher logs or records the error
- dispatch continues without process-level failure
- later events still dispatch correctly

Script shape:

```javascript
var events = [];
Nook.Native.attach({
  type: "inline",
  module: "libdemo.so",
  symbol: "target",
  onEnter: function(args) { throw new Error("boom"); },
  onLeave: function(retval) { events.push("leave"); }
});
```

After enter failure, enqueue leave and verify leave still runs.

**Step 2: Run test to verify it fails**

Run the runtime test command from Task 1.

Expected: dispatcher aborts or no error path exists.

**Step 3: Write minimal implementation**

Wrap JS callback invocation:

- if exception, capture error text
- clear exception
- optionally route through existing runtime error reporting helper
- continue processing future events

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: callback exception tests pass.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: harden native js callback dispatch"
```

### Task 6: Wire the first real inline hook installer

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`
- Modify: `src/framework/NookInlineHook.cpp` only if a tiny glue seam is necessary
- Modify: `include/nook/NookInlineHook.h` only if a tiny glue seam is necessary
- Modify: `tests/communication/test_native_js_bridge.cpp`

**Step 1: Write the failing test**

Add bridge tests that verify the real installer wiring path shape:

- `"inline"` attach calls the inline installer function
- module and symbol are passed through intact
- hook handle is stored in the registry record

Use dependency injection in unit tests so the test still does not require executing a real code patch.

**Step 2: Run test to verify it fails**

Run the bridge test command from Task 2.

Expected: fake installer path differs from final wiring contract.

**Step 3: Write minimal implementation**

Replace the temporary installer stub with a real bridge adapter that calls:

- `NookInlineHookSymbol(...)`

Use one shared native callback entry point that:

- looks up `hookId`
- captures register or return data
- pushes a `HookEvent`

Keep PLT support explicitly unimplemented.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: bridge tests still pass with the real adapter path.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_native_js_bridge.cpp src/framework/NookInlineHook.cpp include/nook/NookInlineHook.h tests/communication/test_native_js_bridge.cpp
git commit -m "feat: wire inline hook installer into js bridge"
```

### Task 7: Run local C++ verification

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `tests/communication/test_native_js_bridge.cpp`

**Step 1: Run focused runtime tests**

Run:

```powershell
build\test_js_runtime_native_attach.exe
build\test_native_js_bridge.exe
```

Expected: both pass.

**Step 2: Run existing runtime regression tests**

Run:

```powershell
build\test_js_runtime_rpc.exe
build\test_protocol_rpc.exe
build\test_agent_connection_rpc.exe
build\test_server_handlers_rpc.exe
```

Expected: existing script runtime/RPC tests still pass.

**Step 3: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk
```

Expected: Android build succeeds.

**Step 4: Re-run the focused runtime tests if build adjusted anything**

Run the same focused test binaries again if needed.

Expected: still green.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_native_js_bridge.h tests/communication/test_js_runtime_native_attach.cpp tests/communication/test_native_js_bridge.cpp
git commit -m "test: verify native js bridge locally"
```

### Task 8: Run device smoke validation

**Files:**
- Modify: `host/nook-py/hook.js`
- Modify: `host/nook-py/README.md`

**Step 1: Prepare a smoke script**

Create or adapt a script like:

```javascript
Nook.Native.attach({
  type: "inline",
  module: "libtarget.so",
  symbol: "target_func",
  onEnter(args) {
    send({ type: "send", payload: "enter:" + args[0] });
  },
  onLeave(retval) {
    send({ type: "send", payload: "leave:" + retval });
  }
});
```

**Step 2: Start the device server**

Run:

```powershell
adb shell "su -c 'pkill -x nook-server 2>/dev/null || true'"
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

Expected: server listens on `27042`.

**Step 3: Load the smoke script through CLI**

Run:

```powershell
nook-cli repl attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\hook.js --usb
```

Expected:

- script loads successfully
- no JS exception on `Nook.Native.attach`

**Step 4: Trigger the native target function and observe messages**

Use the existing test app/path that exercises the chosen native symbol.

Expected:

- host receives `enter:...`
- host receives `leave:...`
- target app does not crash

**Step 5: Commit**

```bash
git add host/nook-py/hook.js host/nook-py/README.md
git commit -m "docs: validate native js bridge smoke flow"
```
