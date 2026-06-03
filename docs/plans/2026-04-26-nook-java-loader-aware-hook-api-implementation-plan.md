# Nook Java Loader-Aware Hook API Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add public loader-aware Java hook APIs and make deferred `.implementation` installation truly honor `Java.ClassFactory.get(loader)`.

**Architecture:** Extend the public C API with loader-aware variants, persist loader identity through pending deferred requests, and add explicit loader-aware class resolution and hook installation in `JavaHook`. Keep existing APIs as null-loader shims.

**Tech Stack:** C++, JNI, QuickJS bridge, Android ART hook infrastructure, host regression executable, Android NDK build.

---

### Task 1: Add the red tests for loader-aware native hook install

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add tests that exercise the default install path instead of a fake install dependency:

- one test for immediate install forwarding a non-zero loader handle
- one test for deferred/pending dedup treating different loaders as different requests

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`

Expected: compile failure or test failure because loader-aware native hook path is not implemented yet.

**Step 3: Commit**

```bash
git add tests/communication/test_js_runtime_native_attach.cpp
git commit -m "test: add red tests for loader-aware java hook install"
```

### Task 2: Extend the public hook API and internal framework plumbing

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookJavaHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookJavaHookInternal.h`

**Step 1: Write minimal API additions**

Add:

- `NookJavaHookHookWithLoader(...)`
- `NookJavaHookHookDeferredWithLoader(...)`
- `NookJavaHookFindClassWithLoader(...)`

Add internal overloads of `InstallNow(...)` and `ProcessPendingRequests(...)` that accept loader.

**Step 2: Run the host build**

Run the same `g++` command as Task 1.

Expected: still failing because pending registry and `JavaHook` are not yet updated.

**Step 3: Commit**

```bash
git add include/nook/NookJavaHook.h src/framework/NookJavaHook.cpp src/framework/NookJavaHookInternal.h
git commit -m "feat: add loader-aware public java hook api"
```

### Task 3: Persist loader identity in the deferred registry

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\deferred\pending_java_hook_registry.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\deferred\pending_java_hook_registry.cpp`

**Step 1: Write minimal implementation**

Add `loader_handle` to `PendingJavaHookRegistry::Request`, thread it through `Register(...)`, and include it in deduplication.

**Step 2: Run the host build**

Run the same `g++` command as Task 1.

Expected: still failing or tests still red because `JavaHook` has not yet consumed the loader.

**Step 3: Commit**

```bash
git add src/java_hook/deferred/pending_java_hook_registry.h src/java_hook/deferred/pending_java_hook_registry.cpp
git commit -m "feat: persist loader handle in pending java hook registry"
```

### Task 4: Add loader-aware JavaHook class resolution and install

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JavaHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JavaHook.cpp`

**Step 1: Write minimal implementation**

Add:

- `FindClassWithLoader(...)`
- `HookMethodWithLoader(...)`

Make old `FindClass(...)` and `HookMethod(...)` forward to null-loader behavior.

**Step 2: Run the host build**

Run the same `g++` command as Task 1.

Expected: build succeeds, but tests may still fail until the bridge default path calls the new public API.

**Step 3: Commit**

```bash
git add src/java_hook/JavaHook.h src/java_hook/JavaHook.cpp
git commit -m "feat: add loader-aware java hook resolution"
```

### Task 5: Make the default agent/runtime install path consume loader_handle

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\agent_runtime\nook_java_js_bridge.cpp`

**Step 1: Write minimal implementation**

Update `DefaultInstallJavaJsHook(...)` so:

- `loader_handle == 0` keeps current behavior
- non-zero `loader_handle` calls the new loader-aware deferred install API

**Step 2: Run host tests to verify green**

Run:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `build\\test_js_runtime_native_attach.exe`

Expected: PASS with exit code 0.

**Step 3: Commit**

```bash
git add src/agent_runtime/nook_java_js_bridge.cpp
git commit -m "feat: honor loader handle in default java hook install"
```

### Task 6: Add Android smoke validation

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_class_factory_impl_smoke.js`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\code_review.md`

**Step 1: Write the smoke script**

Create a script that:

- enumerates class loaders
- selects a `PathClassLoader`
- installs `.implementation` through `Java.ClassFactory.get(loader)`
- triggers the hooked method
- prints enter/leave evidence

**Step 2: Build Android artifacts**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Expected: build succeeds.

**Step 3: Push device binaries**

Run: `adb push libs/arm64-v8a/. /data/local/tmp/nook/`

Expected: push succeeds.

**Step 4: Commit**

```bash
git add host/nook-py/java_class_factory_impl_smoke.js docs/code_review.md
git commit -m "test: add loader-aware java implementation smoke"
```
