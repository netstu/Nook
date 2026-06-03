# Nook Java.enumerateClassLoaders Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal Frida-style `Java.enumerateClassLoaders({ onMatch, onComplete })` API on Android.

**Architecture:** Keep the JS API thin and aligned with `Java.choose(...)` and `Java.enumerateLoadedClasses(...)`. The runtime validates callbacks and dispatches loader wrappers, while the native bridge exposes a narrow class-loader enumeration dependency. Android uses a stable JNI-side enumeration path based on known reachable loaders and parent-chain walking.

**Tech Stack:** C++17, QuickJS, JNI, Android NDK, existing Nook Java bridge/runtime tests

---

### Task 1: Add failing desktop tests for `Java.enumerateClassLoaders`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing tests**

Add host tests for:

- `TestJavaEnumerateClassLoadersBindingExists()`
- `TestJavaEnumerateClassLoadersRejectsNonObjectCallbacks()`
- `TestJavaEnumerateClassLoadersRejectsMissingOnMatch()`
- `TestJavaEnumerateClassLoadersRejectsMissingOnComplete()`
- `TestJavaEnumerateClassLoadersDispatchesDeduplicatedMatchesAndComplete()`

Use a fake native dependency that returns:

- loader handle `0x1111` with class name `dalvik.system.PathClassLoader`
- loader handle `0x1111` again
- loader handle `0x2222` with class name `java.lang.BootClassLoader`

Expected JS behavior:

- only two `onMatch` deliveries
- each callback receives a Java object wrapper
- `onComplete` runs after both

**Step 2: Run the test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because `Java.enumerateClassLoaders` does not exist yet

### Task 2: Add native dependency plumbing

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Add the dependency type**

Add:

- `EnumerateJavaClassLoadersFn`
- `enumerate_class_loaders` to `JavaJsHookInstallerDependencies`
- declaration:
  - `bool EnumerateJavaClassLoaders(const JavaJsHookInstallerDependencies& dependencies, std::vector<JavaJsValue>* matches, std::string* error_message);`

**Step 2: Implement the helper**

In `nook_java_js_bridge.cpp`:

- use the override when provided
- on Android call the default backend placeholder
- on non-Android return:
  - `java class-loader enumerator is not configured`

**Step 3: Re-run the desktop test**

Expected:

- still failing because the JS binding is not exposed yet

### Task 3: Implement `Java.enumerateClassLoaders()` in the JS runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Add the JS entrypoint**

Add:

- `JSValue JsJavaEnumerateClassLoaders(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);`

Behavior:

- require one callbacks object
- validate `onMatch` and `onComplete`
- on non-Android without dependency override:
  - `InternalError: Java.enumerateClassLoaders is only available on Android`
- call `EnumerateJavaClassLoaders(...)`
- de-duplicate by `object_handle`
- call `onMatch(loaderWrapper)` for each unique loader
- call `onComplete()` once

**Step 2: Register the binding**

Register `Java.enumerateClassLoaders` beside:

- `Java.choose`
- `Java.enumerateLoadedClasses`
- `Java.cast`
- `Java.retain`

**Step 3: Re-run the desktop test**

Expected:

- tests pass through the fake dependency path

### Task 4: Implement the Android default backend

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `src/java_hook/deferred/java_hook_loader_resolver.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Enumerate the initial loader set safely**

Collect, when available:

- cached application class loader
- current thread context class loader
- system class loader

**Step 2: Walk parent chains**

For each discovered loader:

- follow `getParent()`
- promote each loader to a temporary global ref
- avoid duplicates by JNI object identity or global-ref handle tracking

**Step 3: Convert loaders to `JavaJsValue`**

Each match should:

- use `JavaJsValueKind::kObject`
- store the retained handle
- store the runtime class name from `DescribeJavaObject(...)`

**Step 4: Re-run desktop regression**

Expected:

- host tests stay green

### Task 5: Add smoke coverage and rebuild Android artifacts

**Files:**
- Create: `host/nook-py/java_enumerate_class_loaders_smoke.js`

**Step 1: Add smoke script**

Smoke shape:

```javascript
Java.ready(function () {
  send({
    type: "send",
    payload: "java-enum-loaders-bindings:" + (typeof Java.enumerateClassLoaders)
  });

  Java.enumerateClassLoaders({
    onMatch(loader) {
      send({
        type: "send",
        payload: "java-enum-loaders-match:" + loader.$className + ":" + String(loader.__jptr)
      });
    },
    onComplete() {
      send({
        type: "send",
        payload: "java-enum-loaders-complete"
      });
    }
  });
});
```

**Step 2: Verify desktop regression**

Run:

```bash
build\test_js_runtime_native_attach.exe
```

Expected:

- PASS

**Step 3: Rebuild Android**

Run:

```bash
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 4: Push fresh artifacts**

Push from:

- `libs/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/libnook.so`
- `libs/arm64-v8a/nook-server`

### Task 6: Record validation and boundary notes

**Files:**
- Modify: `docs/code_review.md`

Add:

- why JNI-side loader enumeration was chosen over a new ART raw walker
- what loaders are currently included
- what is intentionally deferred to `Java.ClassFactory.get(loader)`
