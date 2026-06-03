# Java.registerClass Signature-Aware Dispatch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Frida-style multi-declaration support for `Java.registerClass(spec).methods` by dispatching callbacks using the actual runtime Java method signature.

**Architecture:** Extend `registerClass` method parsing to normalize declarations into signature-keyed callback records. Extend the native `InvocationHandler` bridge to derive a JNI signature from the reflected `Method`, then dispatch callbacks by `method_name + signature`, with backward-compatible fallback to the existing single-callback-by-name behavior.

**Tech Stack:** C++, QuickJS runtime/bootstrap, existing Nook Java bridge, host-side C++ tests, optional Android smoke

---

### Task 1: Add failing host tests for multi-declaration signature dispatch

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- success case:
  - `methods.marker = [{ int signature }, { string signature }]`
  - invoke callback dispatch twice with the same method name but different signatures
  - expect the correct JS implementation each time
- failure case:
  - duplicate declaration signatures for the same method name
  - expect registration failure
- compatibility case:
  - old single-callback-by-name behavior still works

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: tests fail because runtime storage and callback dispatch are still keyed only by method name.

### Task 2: Extend registerClass method normalization with signatures

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write minimal normalization support**

Update method parsing so multi-declaration arrays can produce:

- method name
- normalized JNI signature
- callback

Keep backward compatibility:

- plain single-function form still allowed
- single declaration object form still allowed

Reject:

- duplicate signatures for one method name
- multi-declaration entries missing `returnType`
- multi-declaration entries missing `argumentTypes`

**Step 2: Run tests to verify partial progress**

Run the host test binary again.

Expected: parsing-related tests improve, but dispatch tests may still fail until native callback path is updated.

### Task 3: Extend runtime callback storage and lookup

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Change callback map structure**

Store callbacks so lookup can be:

- exact `method_name + signature`
- fallback legacy `method_name`

Update register and invoke paths accordingly.

**Step 2: Run tests**

Run the host test binary again.

Expected: registration tests pass, dispatch tests may still fail until native bridge supplies signature.

### Task 4: Derive JNI signature from reflected `Method` in native callback bridge

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add native reflected-signature extraction**

In the Java `InvocationHandler` callback path:

- read parameter types from `Method`
- read return type from `Method`
- convert to JNI signature string
- pass signature into runtime callback dispatch

**Step 2: Run host tests to verify they pass**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected: all registerClass multi-declaration dispatch tests pass, and previous single-declaration tests remain green.

### Task 5: Add smoke coverage and document the boundary

**Files:**
- Create: `host/nook-py/java_register_class_signature_dispatch_smoke.js`
- Modify: `docs/code_review.md`

**Step 1: Add smoke script**

If the demo app exposes a safe real callback site with two signatures, validate on device.

If not, keep the smoke script scoped to the closest available path and document that multi-signature device validation is limited by the demo target.

**Step 2: Record results**

Document:

- exact supported declaration shapes
- exact dispatch rule
- backward-compatible fallback behavior
- remaining unsupported edges

**Step 3: Re-run verification if needed**

If code changed during cleanup, rerun host verification before closing.
