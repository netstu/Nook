# Nook Java.retain Minimal Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal Frida-style `Java.retain(objectWrapper)` API that promotes a Java object wrapper to an Android global reference and returns a stable wrapper for later use.

**Architecture:** Reuse the existing Java wrapper metadata and wrapper factory. `Java.retain()` will parse the input wrapper, delegate the retain operation to a narrow Android-side dependency that returns a new retained handle, and then rebuild a wrapper with the same class name and the retained handle. This keeps object lifetime work isolated from `Java.cast()` and existing invoke/read/write paths.

**Tech Stack:** C++, QuickJS runtime glue, JNI global refs on Android, dependency injection for desktop tests, Python host smoke script

---

### Task 1: Add failing desktop tests for `Java.retain`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Reference: `src/agent_runtime/js_runtime.cpp`
- Reference: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write the failing test**

Add tests near the other Java wrapper tests:

- `TestJavaRetainBindingExists()`
- `TestJavaRetainReturnsRewrappedObjectWithRetainedHandle()`
- `TestJavaRetainRejectsNonJavaObject()`
- `TestJavaRetainRejectsNullHandleObject()`

Use script snippets shaped like:

```cpp
const char* source =
    "send({ type: 'send', payload: typeof Java.retain });";
```

and:

```cpp
const char* source =
    "var TextFragment = Java.use('com.demo.target.TextFragment');"
    "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
    "  var kept = Java.retain(this);"
    "  send({"
    "    type: 'send',"
    "    payload: kept.$className + ':' +"
    "             String(kept !== this) + ':' +"
    "             String(kept.__nookJavaReceiverHandle !== this.__nookJavaReceiverHandle)"
    "  });"
    "};";
```

Also add a fake retain helper and capture structure to assert:

- original handle passed in
- retained handle returned

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.retain` does not exist yet.

**Step 3: Write minimal implementation**

Do not implement runtime code in this task.

**Step 4: Run test to verify it still fails for the intended reason**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL on `Java.retain`-specific assertions, not due to unrelated compile issues.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add Java.retain regression coverage"
```

### Task 2: Add retain dependency plumbing

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Use the Task 1 tests as the active red state. They should need a retain dependency path to pass.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because the runtime still has no retain entrypoint or no dependency wiring.

**Step 3: Write minimal implementation**

In `src/agent_runtime/nook_java_js_bridge.h`:

- add a new dependency type:

```cpp
using RetainJavaObjectFn = bool (*)(uint64_t object_handle,
                                    uint64_t* retained_handle,
                                    std::string* error_message);
```

- extend `JavaJsHookInstallerDependencies` with:

```cpp
RetainJavaObjectFn retain_object = nullptr;
```

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- add a narrow helper that dispatches through `dependencies.retain_object`
- return a clear error if the dependency is missing

Do not add broader object-lifetime state in this task.

**Step 4: Run test to verify it still fails narrowly**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: still FAIL until `Java.retain()` is exposed from JS, but compile succeeds and dependency path exists.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "refactor: add Java object retain dependency hook"
```

### Task 3: Implement `Java.retain()` in the JS runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Reference: `src/agent_runtime/nook_java_js_bridge.h`
- Reference: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Use the existing Task 1 retain tests as the active failing coverage.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.retain` is missing or returns the wrong wrapper.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

1. Add:

```cpp
JSValue JsJavaRetain(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
```

2. Parse the first arg with `ParseJavaJsValue(...)` and require:

- `kind == JavaJsValueKind::kObject`
- `object_handle != 0u`

3. On non-Android builds:

- return `InternalError: Java.retain is only available on Android`

4. On Android builds:

- call the new retain dependency
- rebuild a wrapper using:

```cpp
CreateJavaUseWrapper(ctx, source_value.object_class_name.c_str(), retained_handle)
```

5. Register `Java.retain` beside `Java.use`, `Java.cast`, and `Java.deopt`

Use exact errors:

- `Java.retain requires a Java object wrapper`
- `Java.retain object handle is invalid`
- `Java.retain is only available on Android`

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: PASS.

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add minimal Java.retain support"
```

### Task 4: Implement Android-side retain helper

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `src/java_hook/deferred/java_hook_loader_resolver.cpp`

**Step 1: Write the failing test**

No new desktop test is required here beyond the existing retain tests and later device validation.

**Step 2: Run verification to confirm missing Android behavior**

Desktop tests should already pass through the fake dependency path. This task exists to provide the real Android implementation for device smoke.

**Step 3: Write minimal implementation**

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- add a real retain helper that:
  - acquires `JavaEnv`
  - treats the input handle as `jobject`
  - calls `NewGlobalRef`
  - returns the retained `jobject` as `uint64_t`
  - emits a useful error when JNI env or `NewGlobalRef` is unavailable

- ensure the runtime's default `JavaJsHookInstallerDependencies` gets this function wired in

Do not add `DeleteGlobalRef` handling in this task.

**Step 4: Run verification**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: PASS.

Then rebuild Android artifacts:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected: `libnook-agent.so`, `libnook.so`, and `nook-server` install to `libs/arm64-v8a/`.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "feat: wire Android Java object retain helper"
```

### Task 5: Add device smoke and docs

**Files:**
- Create: `host/nook-py/java_retain_smoke.js`
- Modify: `docs/code_review.md`

**Step 1: Write the smoke script**

Create:

```javascript
Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload: "java-retain-bindings:" + typeof Java.retain
  });

  initView.implementation = function (view) {
    const kept = Java.retain(this);
    const casted = Java.cast(kept, TextFragment);
    send({
      type: "send",
      payload:
        "java-retain-result:" +
        kept.$className + ":" +
        String(kept !== this) + ":" +
        String(casted.formatBalance(10.0))
    });
    return this.initView.callOriginal(view);
  };
});
```

**Step 2: Push rebuilt Android artifacts**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Expected: all three files push successfully.

**Step 3: Run device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_retain_smoke.js --wait --usb
```

Expected key output:

- `java-retain-bindings:function`
- `java-retain-result:com.demo.target.TextFragment:true:BAL 10.00`

**Step 4: Update docs**

Append a short section to `docs/code_review.md` covering:

- supported scope
- missing lifecycle/release support
- desktop test pass
- device smoke result

**Step 5: Commit**

```bash
git add host/nook-py/java_retain_smoke.js docs/code_review.md
git commit -m "docs: record Java.retain minimal support"
```
