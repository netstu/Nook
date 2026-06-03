# Java.array Mutation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Java.array(...)` preserve simple index mutations so later Java calls observe updated contents.

**Architecture:** Keep `Java.array(...)` as a JS-facing helper, but back it with mutation-aware wrapper state that the runtime marshaler can read when converting the array back into `JavaJsValue`.

**Tech Stack:** QuickJS bootstrap/runtime, C++ runtime marshalling, host-side C++ tests, Android smoke JS

---

### Task 1: Add failing host tests for mutation-aware Java.array semantics

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- primitive array mutation:
  - create `Java.array('int', [1, 2, 3])`
  - assign `values[1] = 9`
  - pass into existing fake Java method
  - expect Java-side observed contents to include `9`
- object array mutation:
  - create `Java.array('java.lang.Object', ['a', true, 2.5])`
  - mutate one entry
  - pass into existing fake Java method
  - expect updated contents
- nested array mutation:
  - create `Java.array('int[]', [row1, row2])`
  - mutate `row1[0]`
  - pass outer array into existing fake Java method
  - expect nested updated contents

**Step 2: Run tests to verify they fail**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: tests fail because current `Java.array(...)` still behaves as a construction snapshot for later marshaling.

### Task 2: Implement mutation-aware array wrapper state

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write minimal implementation**

Implement the smallest mutation-aware model:

- `Java.array(...)` returns a JS array-like value with hidden metadata
- indexed writes update hidden metadata
- later marshalling detects this wrapper and reads the latest state

Keep explicit non-goals:

- no full `push/pop/splice/sort` semantics
- no automatic conversion of arbitrary JS arrays

**Step 2: Run tests to verify they pass**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: all host tests pass.

### Task 3: Add device smoke for mutated arrays

**Files:**
- Create: `host/nook-py/java_array_mutation_smoke.js`

**Step 1: Write smoke script**

Use an existing demo array target and:

- construct a Java array
- mutate one or more indices
- pass it into a Java method that renders or aggregates contents
- send the observed result back to the host

**Step 2: Build Android if runtime changed**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected: arm64 artifacts rebuild successfully.

### Task 4: Push fresh Android artifacts and validate

**Files:**
- Use: `libs/arm64-v8a/libnook-agent.so`
- Use: `libs/arm64-v8a/libnook.so`
- Use: `libs/arm64-v8a/nook-server`
- Use: `libs/arm64-v8a/libc++_shared.so`

**Step 1: Push**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so
adb shell su -c 'chmod 755 /data/local/tmp/nook/nook-server'
```

Expected: push succeeds.

**Step 2: Run smoke**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_array_mutation_smoke.js --wait --usb
```

Expected: Java-side observed array contents reflect JS-side index mutations.

### Task 5: Document the compatibility boundary

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Record results**

Document:

- what mutation behavior is now supported
- what remains unsupported
- why `arr[i] = ...` was chosen before full array-method semantics

**Step 2: Re-run verification if needed**

If cleanup changes code or tests, rerun host verification before closing the task.
