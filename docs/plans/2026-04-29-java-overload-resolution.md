# Java Overload Resolution Tightening Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Tighten Nook's default Java overload resolution so common direct-invoke cases behave more like Frida without introducing fake semantics.

**Architecture:** Expand JS-side overload candidate generation for `null`, boxed/object fallbacks, and safe common reference candidates. Add one internal native resolver token for `null` so reference overloads can be matched safely while keeping ambiguity conservative.

**Tech Stack:** C++, QuickJS runtime/bootstrap, existing Java bridge, host-side C++ tests

---

### Task 1: Add failing host tests for the new direct-invoke overload cases

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add tests for:

- `null` can resolve a reference overload in a controlled fake-resolver case
- `null` still fails for primitive-only overloads
- JS boolean can resolve boxed/object-facing overloads
- JS string can fall back to `java.lang.Object`
- JS number can fall back to boxed/object-facing overloads without regressing existing primitive-first behavior
- Java object wrappers preserve concrete-class-first but still allow `java.lang.Object` fallback

**Step 2: Run test binary to verify red**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail because current candidate generation is still too narrow

### Task 2: Expand JS-side overload candidate generation

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Implement minimal candidate broadening**

Update `CollectJavaInvokeArgumentTypeCandidates(...)` so it:

- emits an internal `null` candidate for `null` / `undefined`
- adds boxed/object fallback candidates for:
  - booleans
  - strings
  - numeric values
- adds `java.lang.Object` fallback for object wrappers and arrays

Preserve current primitive-first ordering where already validated.

**Step 2: Re-run host test binary**

Expected:

- some new tests pass
- `null`-specific tests may still fail until native resolver support lands

### Task 3: Add native resolver support for the internal `null` candidate

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Implement minimal `null` compatibility**

Update `ResolveJavaMethodSignatureByTypeNames(...)` so one internal token:

- matches non-primitive reference parameters
- does not match primitive parameters

Do not add broad fake ranking in this pass.

**Step 2: Re-run host test binary**

Expected:

- `null` reference-overload tests pass
- primitive-only `null` tests still fail correctly

### Task 4: Refine fake resolver coverage and keep existing behavior stable

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Extend fake resolver/invoker only where needed**

Add the minimum fake method signatures needed for the new tests.

Keep the fake surface narrow and purpose-built:

- boxed boolean/object case
- string/object case
- numeric boxed/object case
- nullable reference case

**Step 2: Re-run host test binary**

Expected:

- new overload tests pass
- old overload, array, ClassFactory, and registerClass tests remain green

### Task 5: Document the new behavior and explicit boundary

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Record the change**

Document:

- the new candidate-generation rules
- the internal `null` handling rule
- which cases are now closer to Frida
- which ambiguity cases still intentionally remain conservative

**Step 2: Re-run verification if any cleanup changed code**

Run the host test binary again if needed before closing.
