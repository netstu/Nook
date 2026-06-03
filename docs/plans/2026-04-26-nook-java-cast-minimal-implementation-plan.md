# Nook Java.cast Minimal Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal Frida-style `Java.cast(objectWrapper, classWrapper)` API that re-wraps an existing Java object handle with a target class view.

**Architecture:** Reuse the existing Java wrapper factory instead of building a new object model. `Java.cast()` will parse the source wrapper and target class wrapper, preserve the original Java receiver handle, and create a new wrapper with the target class name. Validation stays local to wrapper shape and handle presence; no JNI hierarchy check is added in this pass.

**Tech Stack:** C++, QuickJS runtime glue, existing Java wrapper metadata, desktop regression test binary, Python host smoke script

---

### Task 1: Add failing desktop tests for `Java.cast`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Reference: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add tests near the other Java wrapper tests:

- `TestJavaCastBindingExists()`
- `TestJavaCastReturnsRewrappedObjectWithNewClassName()`
- `TestJavaCastWrapperCanInvokeTargetClassMethodDirectly()`
- `TestJavaCastRejectsNonJavaObject()`
- `TestJavaCastRejectsNonClassWrapper()`

Use script snippets shaped like:

```cpp
const char* source =
    "var TextFragment = Java.use('com.demo.target.TextFragment');"
    "send({ type: 'send', payload: typeof Java.cast });";
```

and:

```cpp
const char* source =
    "var TextFragment = Java.use('com.demo.target.TextFragment');"
    "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
    "  var casted = Java.cast(this, TextFragment);"
    "  return casted.formatBalance(10.0);"
    "};";
```

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.cast` does not exist yet.

**Step 3: Write minimal implementation**

Do not implement anything in this task.

**Step 4: Run test to verify it still fails for the intended reason**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL at the new `Java.cast` assertions, not due to unrelated compile errors.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add Java.cast regression coverage"
```

### Task 2: Implement the `Java.cast` runtime binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Reference: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Use the tests added in Task 1 as the active failing coverage. Do not add new production code before confirming the red state.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.cast` is missing or throws the wrong error.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

1. Add a new runtime entrypoint:

```cpp
JSValue JsJavaCast(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
```

2. Parse the first arg with `ParseJavaJsValue(...)` and require:

- `value.kind == JavaJsValueKind::kObject`
- `value.object_handle != 0u`

3. Parse the second arg as a class wrapper by reading `$className` and requiring:

- object type
- non-empty class name
- no receiver handle requirement

4. Return:

```cpp
return CreateJavaUseWrapper(ctx, target_class_name.c_str(), source_value.object_handle);
```

5. Register the binding on `Java` beside the existing `Java.perform` / `Java.use` bindings.

6. Use precise TypeErrors:

- `Java.cast requires object and class wrapper`
- `Java.cast object must be a Java object wrapper`
- `Java.cast object handle is invalid`
- `Java.cast target must be a Java class wrapper`

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
git commit -m "feat: add minimal Java.cast wrapper support"
```

### Task 3: Add host smoke for real-device validation

**Files:**
- Create: `host/nook-py/java_cast_smoke.js`
- Reference: `host/nook-py/java_numeric_overload_smoke.js`
- Reference: `host/nook-py/java_perform_smoke.js`

**Step 1: Write the failing smoke script**

Create a smoke script shaped like:

```javascript
Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload: "java-cast-bindings:" + typeof Java.cast
  });

  initView.implementation = function (view) {
    const casted = Java.cast(this, TextFragment);
    send({
      type: "send",
      payload:
        "java-cast-result:" +
        casted.$className + ":" +
        String(casted.formatBalance(10.0))
    });
    return this.initView.callOriginal(view);
  };
});
```

**Step 2: Run smoke to verify behavior before push**

No desktop execution is required here. This step is only to review the script content and confirm it matches the current demo app entrypoint.

**Step 3: Write minimal implementation**

No new runtime code in this task. Only add the smoke file.

**Step 4: Run smoke on device**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_cast_smoke.js --wait --usb
```

Expected key output:

- `java-cast-bindings:function`
- `java-cast-result:com.demo.target.TextFragment:BAL 10.00`

**Step 5: Commit**

```bash
git add host/nook-py/java_cast_smoke.js
git commit -m "test: add Java.cast device smoke"
```

### Task 4: Update review notes and usage docs

**Files:**
- Modify: `docs/code_review.md`
- Reference: `docs/step6.md`

**Step 1: Write the failing documentation delta**

Document:

- what `Java.cast()` supports in this pass
- what it does not support
- the exact smoke command
- the expected output

**Step 2: Run verification for doc accuracy**

Cross-check:

- function name is `Java.cast`
- smoke path is `host/nook-py/java_cast_smoke.js`
- expected output matches the actual device test

**Step 3: Write minimal implementation**

Append a short section to `docs/code_review.md` with:

- scope
- boundary
- verification result

**Step 4: Run final verification**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: PASS.

If runtime binaries changed and you need device validation:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Then restart `nook-server` and rerun the smoke.

**Step 5: Commit**

```bash
git add docs/code_review.md docs/step6.md
git commit -m "docs: record Java.cast minimal support"
```
