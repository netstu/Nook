# Nook Java.enumerateLoadedClasses ART Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Frida-like `Java.enumerateLoadedClasses({ onMatch, onComplete })` API on Android, backed by an ART-side loaded-class enumeration path.

**Architecture:** Keep the JS API thin and Frida-like. The JS runtime will validate callbacks and dispatch class names, while the native bridge exposes a narrow loaded-class enumeration dependency. Desktop tests will use a fake dependency, and Android will provide the real ART-backed implementation.

**Tech Stack:** C++17, QuickJS, JNI, existing Nook ART bootstrap helpers, Android NDK host build, device smoke via `nook-cli`

---

### Task 1: Add failing desktop tests for `Java.enumerateLoadedClasses`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing test**

Add host tests for:

- `TestJavaEnumerateLoadedClassesBindingExists()`
- `TestJavaEnumerateLoadedClassesRejectsNonObjectCallbacks()`
- `TestJavaEnumerateLoadedClassesRejectsMissingOnMatch()`
- `TestJavaEnumerateLoadedClassesRejectsMissingOnComplete()`
- `TestJavaEnumerateLoadedClassesDispatchesDeduplicatedMatchesAndComplete()`

Use a fake native dependency that returns:

- `com.demo.target.LoginFragment`
- `com.demo.target.TextFragment`
- `com.demo.target.TextFragment`

Expected JS behavior:

- only two `onMatch` deliveries
- `onComplete` after both

**Step 2: Run test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- compile or runtime failure because `Java.enumerateLoadedClasses` does not exist yet

**Step 3: Write minimal implementation hook points**

Only add the minimum fake-capture scaffolding needed by the tests:

- fake capture struct
- fake function that returns the three names above
- dependency wiring inside the tests

Do not add production implementation yet.

**Step 4: Run test to verify it still fails for the right reason**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- failures are now specific to missing `Java.enumerateLoadedClasses` behavior

**Step 5: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add Java.enumerateLoadedClasses regression coverage"
```

### Task 2: Add native dependency plumbing for loaded-class enumeration

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing test expectation**

Use the Task 1 tests as active red coverage.

**Step 2: Add the dependency type and helper declaration**

In `src/agent_runtime/nook_java_js_bridge.h`, add:

- `EnumerateLoadedJavaClassesFn`
- one field in `JavaJsHookInstallerDependencies`
- declaration:
  - `bool EnumerateLoadedJavaClasses(const JavaJsHookInstallerDependencies& dependencies, std::vector<std::string>* class_names, std::string* error_message);`

**Step 3: Implement the helper**

In `src/agent_runtime/nook_java_js_bridge.cpp`:

- add `EnumerateLoadedJavaClasses(...)`
- if dependency override exists, use it
- otherwise:
  - on Android, call the default Android implementation placeholder
  - on non-Android, return:
    - `java loaded-class enumerator is not configured`

Do not implement the Android ART walker yet in this task.

**Step 4: Run test to verify it still fails**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- tests still fail because the JS binding is not exposed yet

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.h src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "refactor: add Java.enumerateLoadedClasses dependency hook"
```

### Task 3: Implement `Java.enumerateLoadedClasses()` in the JS runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing test expectation**

Use the Task 1 tests as active red coverage.

**Step 2: Add the JS entrypoint**

In `src/agent_runtime/js_runtime.cpp`, add:

- `JSValue JsJavaEnumerateLoadedClasses(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);`

Behavior:

- require exactly one callbacks object
- validate:
  - `onMatch`
  - `onComplete`
- on non-Android without dependency override:
  - `InternalError: Java.enumerateLoadedClasses is only available on Android`
- call `EnumerateLoadedJavaClasses(...)`
- de-duplicate names in JS runtime before callback dispatch
- call `onMatch(name)` for each unique name
- call `onComplete()` once
- return `undefined`

**Step 3: Register the binding**

Register `Java.enumerateLoadedClasses` beside:

- `Java.use`
- `Java.choose`
- `Java.cast`
- `Java.retain`

**Step 4: Run test to verify it passes**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- Task 1 regression tests pass through the fake dependency path

**Step 5: Commit**

```bash
git add src/agent_runtime/js_runtime.cpp tests/communication/test_js_runtime_native_attach.cpp
git commit -m "feat: add Java.enumerateLoadedClasses JS runtime binding"
```

### Task 4: Implement the Android ART-backed loaded-class enumerator

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `src/java_hook/JavaHook.cpp`
- Reference: `src/common/ArtStructDetector.cpp`
- Reference: `E:/Learn/my_program/all_my_hook/kanxue/rustFrida_upstream/quickjs-hook/src/jsapi/java/*`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Keep desktop regression green**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- still green before touching Android-only code

**Step 2: Implement the Android default backend**

Add a `DefaultEnumerateLoadedJavaClasses(...)` path in `src/agent_runtime/nook_java_js_bridge.cpp`.

Implementation requirements:

- attach to `JNIEnv` through `JavaEnv`
- ensure ART runtime bootstrap state is available
- use `ArtInternals::RuntimeInstance` and `RunTimeSpec.classLinker`
- perform ART-side loaded-class traversal
- convert each discovered class to dot-style name
- ignore null / malformed entries
- collect into `std::vector<std::string>`

Important:

- keep de-duplication cheap and deterministic
- do not expose ART details outside this file
- do not add JS-visible loader metadata in this pass

**Step 3: Normalize outputs**

Guarantee:

- slash names are normalized to dot names
- duplicates are removed
- output ordering is stable enough for smoke validation on the same device session

**Step 4: Re-run desktop regression**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- still passes, proving Android-only code did not break host behavior

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "feat: wire ART-backed Java.enumerateLoadedClasses backend"
```

### Task 5: Add device smoke coverage

**Files:**
- Create: `host/nook-py/java_enumerate_loaded_classes_smoke.js`

**Step 1: Write the smoke script**

Create a script that:

- waits on `Java.ready(...)`
- sends:
  - `java-enum-classes-bindings:function:2026-04-26-numcand-v2`
- calls `Java.enumerateLoadedClasses(...)`
- emits `java-enum-classes-match:<name>` only for names beginning with:
  - `com.demo.target.`
- emits:
  - `java-enum-classes-complete`

**Step 2: Verify script shape locally**

Read the file and confirm it matches the expected output contract.

**Step 3: Commit**

```bash
git add host/nook-py/java_enumerate_loaded_classes_smoke.js
git commit -m "test: add Java.enumerateLoadedClasses smoke script"
```

### Task 6: Build Android artifacts and push to device

**Files:**
- Build output: `libs/arm64-v8a/libnook-agent.so`
- Build output: `libs/arm64-v8a/libnook.so`
- Build output: `libs/arm64-v8a/nook-server`

**Step 1: Build Android artifacts**

Run:

```bash
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected:

- build succeeds
- fresh outputs land in:
  - `libs/arm64-v8a/`

**Step 2: Push the correct artifacts**

Run:

```bash
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

Expected:

- all three files push successfully

**Step 3: Give the user the test command**

Provide:

```bash
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_enumerate_loaded_classes_smoke.js --wait --usb
```

Expected key output:

- `java-enum-classes-bindings:function:2026-04-26-numcand-v2`
- one or more:
  - `java-enum-classes-match:com.demo.target.LoginFragment`
  - `java-enum-classes-match:com.demo.target.TextFragment`
- `java-enum-classes-complete`

**Step 4: Commit if any build-script adjustments were needed**

```bash
git add build/android/Android.mk
git commit -m "build: update Android wiring for loaded-class enumeration"
```

### Task 7: Document the implementation and boundaries

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Add a new review section**

Document:

- goal
- root cause
- why ART route was chosen over plain `ClassLoader` enumeration
- implementation files touched
- host verification command
- Android build command
- smoke command
- current limitations:
  - async only
  - no loader metadata
  - no sync API

**Step 2: Verify the doc references the correct artifact path**

Make sure it says:

- push from `libs/arm64-v8a/...`

Not:

- `build/android/libs/...`

**Step 3: Commit**

```bash
git add docs/code_review.md
git commit -m "docs: record Java.enumerateLoadedClasses ART support"
```
