# Nook Java.ClassFactory.get(loader) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Frida-aligned `Java.ClassFactory.get(loader)` API on Android and make the returned factory support loader-scoped `use(className)`.

**Architecture:** Keep `Java.use(...)` unchanged for the default app loader. Add a small `Java.ClassFactory` surface in the JS runtime and propagate an explicit loader handle through the Java bridge records and JNI/JavaHook resolution paths. Loader-scoped wrappers should reuse the existing wrapper model instead of introducing a parallel object system.

**Tech Stack:** C++17, QuickJS, JNI, existing Nook Java bridge/runtime tests, Android NDK

---

### Task 1: Add failing desktop tests for `Java.ClassFactory.get(loader)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing tests**

Add host tests for:

- `TestJavaClassFactoryBindingExists()`
- `TestJavaClassFactoryGetRejectsNonLoaderObject()`
- `TestJavaClassFactoryGetReturnsFactoryWithUse()`
- `TestJavaClassFactoryUseForwardsLoaderHandleToMethodResolveAndInvoke()`
- `TestJavaClassFactoryUseForwardsLoaderHandleToImplementationInstall()`

Use fake dependencies that capture:

- `loader_handle`
- `class_name`
- method / hook metadata

Expected:

- `Java.ClassFactory.get(loaderWrapper).use("...")` returns a wrapper
- direct method invocation forwards the explicit loader handle
- `.implementation = fn` installation also forwards the explicit loader handle

**Step 2: Run the test to verify it fails**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
build\test_js_runtime_native_attach.exe
```

Expected:

- failure because `Java.ClassFactory` is not implemented yet

### Task 2: Add loader-handle plumbing to bridge records and dependencies

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Extend the bridge record types**

Add `loader_handle` to:

- `JavaJsHookRequest`
- `JavaJsHookRecord`
- `JavaJsFieldRecord`
- `JavaJsMethodRecord`

**Step 2: Update bridge entrypoints**

Propagate `loader_handle` through:

- hook install
- overload resolution
- field resolution
- invoke method
- read/write field

**Step 3: Re-run the desktop test**

Expected:

- still failing because JS runtime surface is not exposed yet

### Task 3: Implement JS `Java.ClassFactory.get(loader)` and loader-scoped wrappers

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add `Java.ClassFactory` surface**

Expose:

- `Java.ClassFactory.get(loader)`

Validation:

- `loader` must be a Java object wrapper
- its class name should be `java.lang.ClassLoader` or a subclass

**Step 2: Extend wrapper factory**

Update `CreateJavaUseWrapper(...)` to accept:

- `class_name`
- `receiver_handle`
- `loader_handle`

Embed:

- `__nookJavaLoaderHandle`

Ensure loader handle flows into:

- `__nookJavaResolveOverloadSignature(...)`
- `__nookJavaResolveField(...)`
- `__nookJavaInstallImplementation(...)`
- direct invocation metadata

**Step 3: Re-run the desktop test**

Expected:

- tests now reach bridge code and fail only where native loader propagation is still incomplete

### Task 4: Implement loader-aware JNI / JavaHook resolution

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Reference: `src/java_hook/JavaHook.cpp`
- Reference: `src/java_hook/deferred/java_hook_loader_resolver.cpp`

**Step 1: Add helper for optional loader-aware class resolution**

Behavior:

- `loader_handle == 0`:
  - use current `JavaHook::FindClass(...)`
- `loader_handle != 0`:
  - use `JavaHookLoaderResolver::LoadClassWithLoader(...)`

**Step 2: Apply helper everywhere relevant**

Use it in:

- loader-scoped hook installation
- loader-scoped method signature reflection
- loader-scoped field reflection
- loader-scoped invoke/read/write paths

**Step 3: Re-run the desktop test**

Expected:

- desktop tests pass via fake dependencies

### Task 5: Add smoke script and rebuild Android

**Files:**
- Create: `host/nook-py/java_class_factory_smoke.js`

**Step 1: Add smoke script**

Suggested shape:

```javascript
Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch(loader) {
      if (chosen === null && loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
      }
    },
    onComplete() {
      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      send({
        type: "send",
        payload: "java-class-factory-use:" + TextFragment.$className
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

### Task 6: Update review notes

**Files:**
- Modify: `docs/code_review.md`

Add:

- why `Java.ClassFactory.get(loader)` was chosen over global loader mutation
- what part of Frida's public design is now mirrored
- what remains deferred
