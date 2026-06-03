# Native Hook Hot Path Frida Alignment Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Nook's native `Interceptor.attach` path behave stably for hot functions like Frida, without function-specific special-casing.

**Architecture:** Keep synchronous `onEnter` / `onLeave` semantics, but remove avoidable overhead from the intercepted-thread hot path. The first step is to replace per-byte readability checks in string access with a mapping-aware fast path, then tighten invocation-context reuse and recursion safety around native hook dispatch.

**Tech Stack:** C++17, Android NDK, QuickJS runtime bridge, native inline hook bridge, existing Nook unit tests.

---

### Task 1: Lock down the hot-path failure with tests

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing test**

Add a regression test covering `Memory.readUtf8String()` on a readable buffer and asserting the implementation does not need byte-by-byte readability revalidation to complete successfully.

Add a second regression test for repeated reads from the same readable region to cover the mapping-cache fast path.

**Step 2: Run test to verify it fails**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_agent -j4
```

Then run the relevant host/unit test target already used by this repository for agent runtime tests.

Expected: new tests fail until the string-read fast path exists.

**Step 3: Write minimal implementation**

Implement a readable-range query that resolves the containing mapping once and validates the remaining bytes against that mapping, instead of calling `IsReadableMemoryRange(..., 1)` per byte.

**Step 4: Run test to verify it passes**

Run the same unit test target and verify the new tests pass.

**Step 5: Commit**

Do not commit unless explicitly requested.

### Task 2: Apply the fast path to native pointer and memory string reads

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Extend tests, if needed, to cover both `NativePointer.readUtf8String()` and `Memory.readUtf8String()`.

**Step 2: Run test to verify it fails**

Run the focused runtime test target.

**Step 3: Write minimal implementation**

Introduce a helper that:
- normalizes the input address once
- finds the readable mapping once
- reads until NUL or `max_length`
- only revalidates when crossing a mapping boundary

Use it from both string-reading APIs.

**Step 4: Run test to verify it passes**

Run the focused runtime tests again.

**Step 5: Commit**

Do not commit unless explicitly requested.

### Task 3: Reduce native hook callback hot-path overhead

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add or extend a native-hook regression test to exercise synchronous `onEnter` reading UTF-8 strings from arguments in a tight sequence.

**Step 2: Run test to verify it fails**

Run the focused native-attach test target.

**Step 3: Write minimal implementation**

Reduce avoidable allocations and per-invocation setup in the synchronous callback path, while preserving current blocking semantics and mutation support.

**Step 4: Run test to verify it passes**

Run the focused native-attach test target.

**Step 5: Commit**

Do not commit unless explicitly requested.

### Task 4: Device verification on the Frida 0x8 lab

**Files:**
- No source changes required if previous tasks succeed

**Step 1: Build**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_server -j4
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_agent -j4
```

**Step 2: Push**

Run:

```powershell
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook-test/nook-server
adb push obj/local/arm64-v8a/libnook-agent.so /data/local/tmp/nook-test/libnook-agent.so
adb shell chmod 755 /data/local/tmp/nook-test/nook-server
adb shell chmod 755 /data/local/tmp/nook-test/libnook-agent.so
```

**Step 3: Verify**

Run:

```powershell
nook-cli -U -f com.ad2001.frida0x8 -l .\tests\Test_Lab\nook-frida-labs\frida-0x8\script2.js
nook-cli -U com.ad2001.frida0x8 -l .\tests\Test_Lab\nook-frida-labs\frida-0x8\script2.js
```

Expected:
- no white screen caused by the hook itself
- no repeated unreadable-pointer callback spam for valid pointers
- script behavior closer to Frida on both spawn and attach

**Step 4: Collect logs if needed**

Run:

```powershell
adb logcat -d -s NookCommApi NookServer
```

**Step 5: Commit**

Do not commit unless explicitly requested.
