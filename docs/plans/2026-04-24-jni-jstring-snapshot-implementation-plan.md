# JNI JString Snapshot Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal safe path for native hook callbacks to expose selected `jstring` arguments to JavaScript without dereferencing `JNIEnv*` or local JNI references on the async JS dispatch thread.

**Architecture:** Keep the current async native-hook dispatch model. Instead of decoding JNI state inside JS, capture UTF-8 text on the original hook thread for known Android/JNI targets, store that snapshot on the queued hook event, and expose it to JS as read-only metadata attached to the corresponding argument pointer object.

**Tech Stack:** C++, QuickJS, Android JNI, existing Nook inline-hook bridge and host smoke scripts.

---

### Task 1: Add failing coverage for hook-event JNI snapshots

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing test**

- Add a test that enqueues a native hook enter event carrying one snapped UTF-8 Java string.
- In JS, assert that `args[2].$jniUtf8 === "secret"` while `String(args[2])` still behaves like a pointer.

**Step 2: Run test to verify it fails**

Run:

```powershell
build\test_js_runtime_native_attach_task35.exe
```

Expected: FAIL because hook events do not carry JNI snapshot metadata yet.

### Task 2: Extend hook-event data model and enqueue path

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.h`
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`

**Step 1: Add minimal snapshot fields**

- Extend `HookEvent` with one small fixed-size snapshot list keyed by argument index.
- Keep it narrow: UTF-8 only, bounded count, bounded payload length.

**Step 2: Capture snapshot on hook thread**

- In the inline-hook replacement entry path, detect the demo JNI target and snapshot `jstring password` before calling the original function.

**Step 3: Keep queue ownership safe**

- Ensure queued events own their copied UTF-8 bytes so later async JS dispatch never touches JNI state.

### Task 3: Expose snapshot metadata to JS callback args

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Attach snapshot fields to `NativePointer` args**

- When building `args[]` for `onEnter`, add a read-only `$jniUtf8` property on the matching pointer object if a snapshot exists.

**Step 2: Preserve current pointer semantics**

- Do not alter pointer stringification or math helpers.
- Only add metadata.

### Task 4: Update smoke script and docs

**Files:**
- Modify: `host/nook-py/jni_jstring_hook.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/architecture.md`

**Step 1: Switch demo script to snapshot metadata**

- Read `args[2].$jniUtf8` instead of calling `Nook.Jni.readJStringUtf8(...)`.

**Step 2: Document scope**

- State that current support is a minimal native-hook snapshot path for selected JNI targets, not a general-purpose Java API.

### Task 5: Verify locally and on device

**Files:**
- Modify: none

**Step 1: Run local tests**

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task35.exe
build\test_js_runtime_native_attach_task35.exe
python host\nook-py\tests\test_cli.py
```

Expected: PASS.

**Step 2: Rebuild and push Android artifacts**

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push libs/arm64-v8a/libnook.so /data/local/tmp/nook/libnook.so
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server && chmod 644 /data/local/tmp/nook/libnook-agent.so /data/local/tmp/nook/libnook.so'"
```

**Step 3: Device validation**

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\jni_jstring_hook.js --wait --usb
```

Expected:

- script loads successfully
- clicking the login button prints `decoded=<your-password>`
- target app no longer crashes
