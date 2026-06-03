# Java Env Wrapper Phase 6 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `Env.getStringUtfChars(jstr)` and `Env.releaseStringUtfChars(jstr, cstr)` in a strict Frida-aligned way while preserving the corrected Android attach lifetime model.

**Architecture:** Keep `Env` runtime-backed and narrow. Add only the paired JNI UTF-8 access primitives, validate both pointer-like arguments explicitly, and perform the actual JNI calls while a local `JavaEnv jenv` is alive at the call site on Android.

**Tech Stack:** QuickJS runtime/bootstrap in `js_runtime.cpp`, JVM helper scope management, desktop runtime tests, Android smoke script

---

### Task 1: Add failing desktop tests for `Env.getStringUtfChars(...)` and `Env.releaseStringUtfChars(...)`

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/js_runtime_test_api.h`

**Step 1: Write the failing tests**

Add focused coverage for:

- `env.getStringUtfChars(jstr)` returns a pointer-like value
- the JNI helper receives the original `jstring` handle
- `env.releaseStringUtfChars(jstr, cstr)` returns `true`
- the release helper receives both pointers
- invalid input is rejected for both methods

**Step 2: Run the desktop test binary and verify failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because the methods and test hooks do not exist yet

**Step 3: Keep scope narrow**

Do not add auto-release or convenience helpers in this phase.

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Implement the narrow host test hooks for UTF-8 char acquire/release

**Files:**
- Modify: `src/agent_runtime/js_runtime_test_api.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the test-only callback contracts**

Add minimal callback types and setter/resetter APIs for:

- JNI `GetStringUTFChars`
- JNI `ReleaseStringUTFChars`

Keep the callback signatures narrow and explicit.

**Step 2: Wire them into runtime state**

Extend `JniBridgeState` with the callbacks and expose matching testing setters/resets.

**Step 3: Run the desktop binary and verify failure shifts**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- build succeeds
- tests still fail because the `Env` methods themselves are not implemented yet

**Step 4: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Implement the phase-6 `Env` methods in the runtime

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add the wrapper methods**

Implement:

- `Env.getStringUtfChars(jstr)`
- `Env.releaseStringUtfChars(jstr, cstr)`

Expose them on the existing wrapper factory only.

**Step 2: Preserve Android attach lifetime**

For Android, ensure each real JNI call executes while a local `JavaEnv jenv` is alive.

Do not rely on previously captured `JNIEnv*` handles for the actual JNI operations.

**Step 3: Validate pointer inputs**

Accept only non-null pointer-like arguments and reject malformed input with clear type errors.

**Step 4: Run the desktop binary**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

Expected:

- all new phase-6 tests pass
- phase-1 through phase-5 Env tests remain green
- existing `Java.vm.perform/getEnv/tryGetEnv` semantics remain green

**Step 5: Refactor only if needed**

Keep cleanup local to `Env` readability and JNI lifetime correctness.

**Step 6: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add an Android smoke for phase 6

**Files:**
- Create: `host/nook-py/java_env_wrapper_phase6_smoke.js`

**Step 1: Write the smoke script**

Print:

- wrapper binding shape
- `env.newStringUtf("hello")` result
- `env.getStringUtfChars(jstr)` pointer result
- `env.releaseStringUtfChars(jstr, cstr)` result
- `env.exceptionCheck()` result after release

Keep it focused and do not add any auto-release layer.

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
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_env_wrapper_phase6_smoke.js --wait --usb
```

Expected:

- `jstring` result is non-null
- char pointer result is non-null
- release result is `true`
- `exceptionCheck()` remains `false`

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

## Implementation Notes

- `Env.getStringUtfChars(jstr)` and `Env.releaseStringUtfChars(jstr, cstr)` were added only on the existing `Env` wrapper surface.
- Desktop host tests use narrow test callbacks for acquire/release, matching the existing phase 2 to phase 5 pattern.
- `Env.newStringUtf(...)` now promotes the created `jstring` local reference to a `GlobalRef` on Android before returning it.
- The returned `jstring` handle is registered with existing script-owned Java handle cleanup so later `getStringUtfChars(...)` calls do not depend on a dead local reference.

## Verification

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c .\\build\\test_js_runtime_native_attach.exe`

## Device Validation

- Android rebuild/push completed in a later verification pass.
- Device smoke script:
  - `host/nook-py/java_env_wrapper_phase6_smoke.js`
- Observed device output:
  - `java-env-wrapper-phase6-bindings:object:object:function:function`
  - `java-env-wrapper-phase6-direct:object:function:function:function`
  - `java-env-wrapper-phase6-new:0x2e36:false`
  - `java-env-wrapper-phase6-chars:0xb400007408201298:false`
  - `java-env-wrapper-phase6-release:true`
  - `java-env-wrapper-phase6-exception:false`

### Task 5: Update docs and regression notes

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Document the phase-6 surface**

Record:

- why the strict acquire/release pair was selected
- why auto-release remains out of scope
- how Android attach lifetime is preserved at call time

**Step 2: Record verification evidence**

Capture:

- desktop test command and result
- Android build command if used
- device smoke command and result

**Step 3: Keep boundaries explicit**

Document what remains out of scope:

- auto-release helpers
- convenience wrappers
- changed `Nook.Jni.readJStringUtf8(...)` semantics

**Step 4: Run fresh final verification**

Run:

```powershell
cmd /c build\test_js_runtime_native_attach.exe
```

If Android validation was part of the pass, ensure the smoke above was run against freshly pushed binaries.

**Step 5: Commit**

Skip commit in-session unless explicitly requested.
