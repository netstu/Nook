# Nook Minimal Java.perform Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add the first minimal Android Java scripting path to `Nook`: `Java.perform(fn)`, `Java.use(className)`, and `Class.method.implementation = fn`, backed by the existing deferred Java hook core.

**Architecture:** Keep the JS API in `src/agent_runtime/js_runtime.cpp`, but put Java-hook install state and original-call plumbing in a new `nook_java_js_bridge` layer. The first version stays intentionally narrow: method-name-only resolution, explicit `callOriginal(...)`, and limited value conversion.

**Tech Stack:** C++17, QuickJS, JNI, existing `NookJavaHook*` framework API, Android NDK, current communication/runtime test binaries.

---

### Task 1: Add a failing JS runtime test for the Java API surface

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add tests that expect:

```cpp
"typeof Java + ':' + typeof Java.perform + ':' + typeof Java.use"
```

to evaluate to:

```text
object:function:function
```

Also add a test that:

```javascript
var called = false;
Java.perform(function () { called = true; });
send({ type: 'send', payload: String(called) });
```

must produce:

```text
true
```

**Step 2: Run test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task70.exe
build\test_js_runtime_native_attach_task70.exe
```

Expected:

- FAIL because `Java` is not defined

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- add a global `Java` object
- add stub `Java.perform`
- add stub `Java.use`

For this step:

- `Java.perform(fn)` only validates function and calls it immediately
- `Java.use(className)` returns a plain object placeholder

**Step 4: Run test to verify it passes**

Run the same command.

Expected:

- PASS for the new API-surface tests

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp
git commit -m "feat: add minimal Java global api surface"
```

### Task 2: Add a failing bridge test for Java JS hook registration

**Files:**
- Create: `tests/communication/test_java_js_bridge.cpp`
- Create: `src/agent_runtime/nook_java_js_bridge.h`
- Create: `src/agent_runtime/nook_java_js_bridge.cpp`
- Test: `build/test_java_js_bridge_task70.exe`

**Step 1: Write the failing test**

Create a bridge test that expects:

- incrementing Java JS hook ids
- storage of class name and method name
- injectable install callback dependency
- cleanup/uninstall path

Use an injected fake dependency shaped like:

```cpp
using InstallJavaHookFn = bool (*)(const JavaJsHookRequest&, JavaJsHookRecord*, std::string*);
```

Expected behavior:

- first install returns hook id `1`
- second install returns hook id `2`

**Step 2: Run test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp -o build/test_java_js_bridge_task70.exe
```

Expected:

- compile failure because the bridge files do not exist

**Step 3: Write minimal implementation**

Add `src/agent_runtime/nook_java_js_bridge.h/.cpp` with:

- request struct
- record struct
- registry state
- install function with injectable dependency
- uninstall/reset test helpers

Keep it agent-runtime-only and avoid direct ART dependencies in the bridge itself.

**Step 4: Run test to verify it passes**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge_task70.exe
build\test_java_js_bridge_task70.exe
```

Expected:

- PASS

**Step 5: Commit**

```bash
git add tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "feat: add java js bridge registry"
```

### Task 3: Connect `Java.use(className)` to method-wrapper placeholders

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `build/android/Android.mk`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a test that expects:

```javascript
var LoginFragment = Java.use("com.demo.target.LoginFragment");
send({
  type: "send",
  payload:
    typeof LoginFragment + ":" +
    typeof LoginFragment.verifyPasswordNative + ":" +
    typeof LoginFragment.verifyPasswordNative.callOriginal
});
```

to produce:

```text
object:object:function
```

**Step 2: Run test to verify it fails**

Run the Task 1 compile command, but include the new bridge source:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task70.exe
build\test_js_runtime_native_attach_task70.exe
```

Expected:

- FAIL because method wrappers do not exist yet

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- make `Java.use(className)` return a class wrapper with lazy property access
- unknown property access returns a method wrapper object
- each method wrapper stores:
  - class name
  - method name
- add `callOriginal(...)` as a function on the method wrapper

Do not install any hook yet in this step.

Update `build/android/Android.mk` to compile `src/agent_runtime/nook_java_js_bridge.cpp`.

**Step 4: Run test to verify it passes**

Run the same command.

Expected:

- PASS for wrapper-shape tests

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp build/android/Android.mk tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add Java.use class and method wrappers"
```

### Task 4: Route `.implementation = fn` through the Java JS bridge

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a test that injects a fake Java install dependency and expects:

```javascript
var LoginFragment = Java.use("com.demo.target.LoginFragment");
LoginFragment.verifyPasswordNative.implementation = function (password) {
  return password;
};
send({
  type: "send",
  payload: "implementation-installed"
});
```

Native-side assertions should verify the bridge received:

- class name `com.demo.target.LoginFragment`
- method name `verifyPasswordNative`
- a JS callback registration

**Step 2: Run test to verify it fails**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- FAIL because `implementation` assignment has no install semantics yet

**Step 3: Write minimal implementation**

In `src/agent_runtime/js_runtime.cpp`:

- define a setter for `methodWrapper.implementation`
- validate assigned value is a function
- call into `nook_java_js_bridge`
- store JS callback in runtime state keyed by Java JS hook id

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- accept install requests
- invoke an injectable Java-hook installer adapter
- return a bridge record containing installed/deferred ids

Do not implement original-call or full invocation dispatch yet.

**Step 4: Run test to verify it passes**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- PASS

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: install Java hooks from implementation assignment"
```

### Task 5: Add callback dispatch and minimal `callOriginal(...)`

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add tests that simulate a Java hook invocation and expect:

1. JS replacement callback receives converted arguments
2. `this.verifyPasswordNative.callOriginal(password)` routes to the bridge
3. returned value is propagated back

Start with the narrow supported set:

- `java.lang.String`
- `boolean`
- `int`

Expected test payload:

```javascript
var LoginFragment = Java.use("com.demo.target.LoginFragment");
LoginFragment.verifyPasswordNative.implementation = function (password) {
  return this.verifyPasswordNative.callOriginal(password);
};
```

**Step 2: Run test to verify it fails**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- FAIL because `callOriginal(...)` is only a wrapper stub

**Step 3: Write minimal implementation**

In the bridge:

- add an invocation context record
- add injectable original-call dependency
- expose a dispatch helper used by tests first

In `js_runtime.cpp`:

- build a `this` object that includes the active method wrapper
- make `callOriginal(...)` require an active invocation context
- implement minimal value conversion:
  - string
  - bool
  - int
  - null object

If conversion is unsupported, throw explicit errors.

**Step 4: Run test to verify it passes**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- PASS

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add minimal Java callback dispatch and callOriginal"
```

### Task 6: Fail clearly on overload ambiguity

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach_task70.exe`

**Step 1: Write the failing test**

Add a fake installer dependency that reports:

- method name exists
- multiple overloads found

The JS-side expectation:

```javascript
var Demo = Java.use("com.demo.target.SomeClass");
Demo.foo.implementation = function (x) { return x; };
```

must fail with text containing:

```text
overload resolution is not implemented yet
```

**Step 2: Run test to verify it fails**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- FAIL because ambiguity is not surfaced yet

**Step 3: Write minimal implementation**

In the Java JS bridge:

- map ambiguity to a clear runtime error
- do not silently pick one method

Keep the first-version rule strict.

**Step 4: Run test to verify it passes**

Run `build\test_js_runtime_native_attach_task70.exe`.

Expected:

- PASS

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: fail clearly on Java overload ambiguity"
```

### Task 7: Add smoke script, docs, and Android verification flow

**Files:**
- Create: `host/nook-py/java_perform_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/step6.md`
- Test: Android device push + `nook-cli`

**Step 1: Write the failing smoke script**

Create:

```javascript
Java.perform(function () {
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  LoginFragment.verifyPasswordNative.implementation = function (password) {
    send({ type: "send", payload: "java-hook:" + password });
    return this.verifyPasswordNative.callOriginal(password);
  };
});
```

Before implementation, this script should fail during load because the Java API is incomplete.

**Step 2: Run device verification to confirm it fails**

Run:

```bash
nook-cli attach com.demo.target -l host\nook-py\java_perform_smoke.js --wait --usb
```

Expected:

- script load error or missing Java API behavior

**Step 3: Document and verify the completed flow**

Update:

- `host/nook-py/README.md`
- `docs/architecture.md`
- `docs/step6.md`

Document:

- first-version API
- `callOriginal(...)` rule
- overload limitation
- on-device test command

Then run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge_task70.exe
build\test_java_js_bridge_task70.exe

g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task70.exe
build\test_js_runtime_native_attach_task70.exe

E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
```

Expected:

- both desktop test binaries pass
- Android build passes
- device artifacts push successfully

**Step 4: Run device verification to verify it passes**

Run:

```bash
adb shell am force-stop com.demo.target
nook-cli attach com.demo.target -l host\nook-py\java_perform_smoke.js --wait --usb
```

Expected:

- script loads successfully
- invoking `verifyPasswordNative` produces `java-hook:...`

**Step 5: Commit**

```bash
git add host/nook-py/java_perform_smoke.js host/nook-py/README.md docs/architecture.md docs/step6.md tests/communication/test_java_js_bridge.cpp tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp src/agent_runtime/js_runtime.cpp build/android/Android.mk
git commit -m "feat: add minimal Java.perform runtime path"
```
