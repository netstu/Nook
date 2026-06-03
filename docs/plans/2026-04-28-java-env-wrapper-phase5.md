# Java Env Wrapper Phase 5 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.newStringUtf(str)` while preserving the corrected Android attach lifetime model and without expanding the current guarded boundary of `Nook.Jni.readJStringUtf8(...)`.

**Architecture:** Keep `Env` runtime-backed and narrow. Add only the JNI UTF-8 string creation primitive, validate the JS argument as a string, and perform the actual JNI `NewStringUTF(...)` call while a local `JavaEnv jenv` is alive at the call site on Android.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for `Env.newStringUtf(str)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.newStringUtf("hello")` returns a pointer-like value
- the JNI helper receives the exact source string
- non-string input is rejected

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the method and test hook do not exist yet

**Step 3: Keep scope narrow**

Do not add `getStringUtfChars(...)`, `releaseStringUtfChars(...)`, or any new `Nook.Jni.readJStringUtf8(...)` behavior in this phase.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement the narrow host test hook for UTF-8 string creation

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contract**

Add one minimal callback type and setter/resetter API for:

- JNI `NewStringUTF`

Keep the callback signature narrow and explicit:

- env pointer
- source string
- result handle output

**Step 2: Wire it into runtime state**

Extend `JniBridgeState` with the callback and expose matching testing setters/resets.

**Step 3: Run the desktop binary and verify failure shifts**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the `Env` method itself is not implemented yet

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement `Env.newStringUtf(str)` in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper method**

Implement:

- `Env.newStringUtf(str)`

Expose it on the existing wrapper factory only.

**Step 2: Preserve Android attach lifetime**

For Android, ensure the actual JNI `NewStringUTF(...)` call executes while a local `JavaEnv jenv` is alive.

Do not rely on previously captured `JNIEnv*` handles for the actual JNI operation.

**Step 3: Validate the argument**

Accept only a JS string and reject malformed input with a clear type error.

**Step 4: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new phase-5 tests pass
- phase-1 through phase-4 Env tests remain green
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 5: Refactor only if needed**

Keep cleanup local to `Env` readability and JNI lifetime correctness.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add an Android smoke for phase 5

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase5_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- `env.newStringUtf("hello")` pointer result
- `env.exceptionCheck()` result after creation

Keep it focused and do not change the documented guarded semantics of `Nook.Jni.readJStringUtf8(...)` in this phase.

**Step 2: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 3: Push fresh binaries**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
```

**Step 4: Run device smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase5_smoke.js --wait --usb
```

Expected:

- returned string handle is non-null
- `exceptionCheck()` remains `false`

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the phase-5 surface**

Record:

- why `newStringUtf(...)` was selected before broader string read helpers
- why `Nook.Jni.readJStringUtf8(...)` remains intentionally guarded in this phase
- how Android attach lifetime is preserved at call time

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- `getStringUtfChars(...)`
- `releaseStringUtfChars(...)`
- changing `Nook.Jni.readJStringUtf8(...)` semantics

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
