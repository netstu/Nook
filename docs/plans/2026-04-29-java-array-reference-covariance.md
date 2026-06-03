# Java.array Reference-Array Covariance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend `Java.array(...)` conversion so reference arrays can widen through real Java assignability, not just the current `Object[]` special case.

**Architecture:** Keep `Java.array(...)` unchanged on the JS side. Fix the bridge in `nook_java_js_bridge.cpp` so reference-array materialization reuses reflective descriptor assignability when choosing the element descriptor used for conversion. This keeps the change local to the invoke/conversion path and avoids fake `registerClass` semantics.

**Tech Stack:** C++, QuickJS runtime, Android JNI bridge, existing host-side test binaries.

---

### Task 1: Add failing bridge-level tests for generic reference-array covariance

**Files:**
- Modify: `tests/communication/test_java_js_bridge.cpp`

**Step 1: Write the failing test**

Add tests covering:

- `CharSequence <- String`
- `CharSequence[] <- String[]`
- `CharSequence[][] <- String[][]`

using a testing callback that models Java assignability.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe
.\build\test_java_js_bridge.exe
```

Expected:

- FAIL because the bridge still only exposes exact/reference-`Object` covariance behavior

**Step 3: Write minimal implementation**

Add a small helper in `src/agent_runtime/nook_java_js_bridge.cpp` plus test exposure in
`src/agent_runtime/nook_java_js_bridge.h` if needed, so the bridge can choose the
source component descriptor when Java assignability allows it.

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe
.\build\test_java_js_bridge.exe
```

Expected:

- PASS

### Task 2: Use the new covariance helper in reference-array JNI materialization

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Write the failing test**

If Task 1 only proves the pure helper, add one more narrow test around the final
element-descriptor choice used by array conversion.

**Step 2: Run test to verify it fails**

Run the same bridge test binary and confirm the new case fails for the expected reason.

**Step 3: Write minimal implementation**

Update the reference-array branch in `ConvertJavaJsArrayToJniArray(...)` to:

- derive the source array descriptor from `value.array_type_name`
- derive the source component descriptor
- choose the source component descriptor when:
  - target component and source component are reference-like
  - reflective assignability says target can accept source

Keep primitive branches unchanged.

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe
.\build\test_java_js_bridge.exe
```

Expected:

- PASS

### Task 3: Add or update smoke coverage for the widened reference-array case

**Files:**
- Modify: `host/nook-py/java_array_smoke.js` or add a focused new smoke under `host/nook-py/`
- Optional Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing smoke/test**

Add a focused scenario that builds a more specific source array and sends it to a widened
reference-array target.

**Step 2: Run test to verify it fails**

Run the smallest relevant host test if added, otherwise rely on device smoke as the first
full integration check.

**Step 3: Write minimal implementation**

Only if Task 2 was insufficient. Otherwise just keep the smoke.

**Step 4: Run verification**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- PASS

### Task 4: Device validation

**Files:**
- Runtime artifacts only

**Step 1: Rebuild Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

**Step 2: Push standard build outputs**

Run:

```powershell
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so
adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so
```

**Step 3: Run device smoke**

Run the new or updated array smoke through `attach` or `spawn` as appropriate.

**Step 4: Verify**

Expected:

- widened reference-array invocation succeeds on device
- no regression in existing primitive/object array smokes

### Task 5: Document the result

**Files:**
- Modify: `docs/code_review.md`
- Optional Modify: `docs/step6.md`

**Step 1: Record the exact supported covariance boundary**

Document:

- what widened reference-array cases now work
- that primitive covariance is still unchanged
- that this was a `Java.array(...)` bridge improvement, not a `registerClass` change

**Step 2: Re-run verification**

Keep the same host and device evidence with the final note.
