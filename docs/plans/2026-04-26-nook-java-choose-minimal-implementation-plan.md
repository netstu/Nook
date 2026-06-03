# Nook Java.choose Minimal Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal Frida-style `Java.choose(className, callbacks)` API that enumerates live Java instances of a target class and delivers them to JS as existing Java wrappers.

**Architecture:** Keep the JS surface tiny and Frida-like while delegating enumeration to a narrow native dependency. The runtime will validate `className` and callbacks, invoke a choose dependency that returns matching Java object handles, wrap each handle with `CreateJavaUseWrapper(...)`, call `onMatch` for each, and finally call `onComplete`.

**Tech Stack:** C++, QuickJS runtime glue, JNI / ART on Android, dependency injection for desktop tests, Python host smoke script

---

### Task 1: Add failing desktop tests for `Java.choose`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Reference: `src/agent_runtime/js_runtime.cpp`
- Reference: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write the failing test**

Add tests near the other Java wrapper tests:

- `TestJavaChooseBindingExists()`
- `TestJavaChooseRejectsNonStringClassName()`
- `TestJavaChooseRejectsNonObjectCallbacks()`
- `TestJavaChooseRejectsMissingOnMatch()`
- `TestJavaChooseRejectsMissingOnComplete()`
- `TestJavaChooseDispatchesMatchesAndComplete()`

For the success path, use a fake choose dependency and a script shaped like:

```cpp
const char* source =
    "Java.choose('com.demo.target.TextFragment', {"
    "  onMatch(instance) {"
    "    send({ type: 'send', payload: 'match:' + instance.$className + ':' + String(instance.formatBalance(10.0)) });"
    "  },"
    "  onComplete() {"
    "    send({ type: 'send', payload: 'complete' });"
    "  }"
    "});";
```

Capture:

- choose dependency call count
- requested class name
- match count delivered

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.choose` does not exist yet.

**Step 3: Write minimal implementation**

Do not implement runtime code in this task.

**Step 4: Run test to verify it still fails for the intended reason**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL at `Java.choose`-specific assertions.

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add Java.choose regression coverage"
```

### Task 2: Add choose dependency plumbing

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Write the failing test**

Use the Task 1 tests as the active red state.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because there is no choose dependency or runtime binding yet.

**Step 3: Write minimal implementation**

In `src/agent_runtime/nook_java_js_bridge.h`:

- add a narrow choose callback type, for example:

```cpp
using EnumerateJavaObjectsFn = bool (*)(const std::string& class_name,
                                        std::vector<JavaJsValue>* matches,
                                        std::string* error_message);
```

- extend `JavaJsHookInstallerDependencies` with:

```cpp
EnumerateJavaObjectsFn enumerate_objects = nullptr;
```

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- add a helper:

```cpp
bool EnumerateJavaObjects(const std::string& class_name,
                          const JavaJsHookInstallerDependencies& dependencies,
                          std::vector<JavaJsValue>* matches,
                          std::string* error_message);
```

- route through the injected dependency first
- return a clear error when the dependency is unavailable on non-Android builds

**Step 4: Run test to verify compile succeeds**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
```

Expected: compile succeeds, runtime tests still fail until `Java.choose` is exposed.

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "refactor: add Java.choose dependency hook"
```

### Task 3: Implement `Java.choose()` in the JS runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Reference: `src/agent_runtime/nook_java_js_bridge.h`
- Reference: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Use the Task 1 choose tests as the active failing coverage.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: FAIL because `Java.choose` is missing or does not dispatch callbacks correctly.

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

1. Add:

```cpp
JSValue JsJavaChoose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
```

2. Validate:

- `argc >= 2`
- `className` is a string
- `callbacks` is an object
- `callbacks.onMatch` is a function
- `callbacks.onComplete` is a function

3. On non-Android builds with no injected dependency:

- throw `InternalError: Java.choose is only available on Android`

4. Call the choose dependency to obtain matches as `std::vector<JavaJsValue>`

5. For each match:

- convert with `MakeJavaJsValue(...)`
- call `onMatch(instance)`

6. After iteration:

- call `onComplete()`
- return `undefined`

Use exact TypeErrors:

- `Java.choose requires class name and callbacks`
- `Java.choose class name must be a string`
- `Java.choose callbacks must be an object`
- `Java.choose onMatch must be a function`
- `Java.choose onComplete must be a function`

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
git commit -m "feat: add minimal Java.choose support"
```

### Task 4: Implement Android-side object enumeration

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `src/java_hook/JavaHook.cpp`
- Reference: `docs/NookFramework_Design_Document.md`

**Step 1: Write the failing test**

Desktop tests should already pass through the fake choose dependency. This task exists to provide the real Android behavior for device validation.

**Step 2: Run verification to confirm Android work remains**

Run:

```powershell
build\test_js_runtime_native_attach.exe
```

Expected: PASS before Android integration.

**Step 3: Write minimal implementation**

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- add a real Android implementation behind `EnumerateJavaObjects(...)`
- keep it narrow:
  - resolve target class
  - enumerate live matching objects
  - emit each match as `JavaJsValueKind::kObject`
  - fill `object_handle` and `object_class_name`

Do not add:

- JS-visible stop semantics
- batching
- cross-class-loader diagnostics

If the ART helper needs a small internal helper function or adapter layer, keep it private to this file in the first pass.

**Step 4: Run verification**

Run:

```powershell
build\test_js_runtime_native_attach.exe
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected:

- desktop test binary passes
- Android artifacts install to `libs/arm64-v8a/`

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "feat: wire Android Java.choose enumerator"
```

### Task 5: Add device smoke and docs

**Files:**
- Create: `host/nook-py/java_choose_smoke.js`
- Modify: `docs/code_review.md`

**Step 1: Write the smoke script**

Create:

```javascript
Java.ready(function () {
  send({
    type: "send",
    payload: "java-choose-bindings:" + typeof Java.choose + ":" + String(Java._invokeResolverVersion)
  });

  Java.choose("com.demo.target.TextFragment", {
    onMatch(instance) {
      send({
        type: "send",
        payload: "java-choose-match:" + instance.$className + ":" + String(instance.formatBalance(10.0))
      });
    },
    onComplete() {
      send({ type: "send", payload: "java-choose-complete" });
    }
  });
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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_choose_smoke.js --wait --usb
```

Expected key output:

- `java-choose-bindings:function:2026-04-26-numcand-v2`
- one or more `java-choose-match:com.demo.target.TextFragment:BAL 10.00`
- `java-choose-complete`

**Step 4: Update docs**

Append a section to `docs/code_review.md` covering:

- supported scope
- missing stop/filter semantics
- desktop test pass
- device smoke result

**Step 5: Commit**

```bash
git add host/nook-py/java_choose_smoke.js docs/code_review.md
git commit -m "docs: record Java.choose minimal support"
```
