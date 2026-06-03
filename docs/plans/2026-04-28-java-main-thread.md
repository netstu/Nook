# Java Main Thread Helpers Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal `Java.isMainThread()` and `Java.scheduleOnMainThread(fn)` helpers for Android Java scripting.

**Architecture:** Prefer a JS-bootstrap implementation built on top of existing `Java.use`, `Java.registerClass`, direct Java invocation, and `$new` constructor support. Only add native bridge code if the existing Java surface cannot express the required main-thread flow.

**Tech Stack:** C++17, QuickJS, existing Java bootstrap in `js_runtime.cpp`, Python host smoke scripts

---

### Task 1: Add failing desktop tests for the new Java helpers

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Test: `build/test_js_runtime_native_attach.exe`

**Step 1: Write the failing tests**

Add coverage for:

- `typeof Java.isMainThread`
- `typeof Java.scheduleOnMainThread`
- `Java.scheduleOnMainThread(1)` rejects non-function input
- `Java.scheduleOnMainThread(fn)` drives the expected Java call chain

**Step 2: Run the test binary and verify failure**

Run:

```bash
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the Java bootstrap does not yet expose these helpers

### Task 2: Implement the helpers in Java bootstrap

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add `Java.isMainThread()`**

Implement in the Java bootstrap using:

- `Java.use("android.os.Looper")`
- `Looper.myLooper()`
- `Looper.getMainLooper()`

Compare wrapper handles and return `boolean`.

**Step 2: Add `Java.scheduleOnMainThread(fn)`**

Implement in the Java bootstrap using:

- `Java.use("android.os.Looper")`
- `Java.use("android.os.Handler")`
- `Java.use("java.lang.Runnable")`
- `Java.registerClass(...)`
- `Handler.$new("(Landroid/os/Looper;)V", Looper.getMainLooper())`
- `handler.post(runnable)`

Add a small unique-name generator for the synthetic Runnable class.

### Task 3: Extend the desktop fake Java method bridge as needed

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Teach the fake method invoker about Looper/Handler**

Add minimal fake responses for:

- `android.os.Looper.myLooper`
- `android.os.Looper.getMainLooper`
- `android.os.Handler.<init>(Looper)`
- `android.os.Handler.post(Runnable)`

**Step 2: Keep scope minimal**

Only add the signatures needed by the new tests.

### Task 4: Add device smoke coverage

**Files:**
- Create: `host/nook-py/java_main_thread_smoke.js`

**Step 1: Add a focused smoke script**

The script should:

- print binding existence
- call `Java.isMainThread()` once from `Java.performNow(...)`
- schedule a callback onto the main thread
- print `Java.isMainThread()` again inside the scheduled callback

**Step 2: Keep output diagnostic and minimal**

Expected tags should clearly distinguish:

- immediate-thread observation
- scheduled callback observation

### Task 5: Verify and document

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Run desktop verification**

Run:

```bash
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all tests pass

**Step 2: If Android runtime changed, rebuild and push**

Run:

```bash
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
```

**Step 3: Document the feature**

Record:

- chosen JS-only architecture
- current Android-only boundary
- any remaining gap relative to Frida
