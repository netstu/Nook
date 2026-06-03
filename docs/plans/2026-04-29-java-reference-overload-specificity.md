# Java Reference Overload Specificity Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Improve Nook's default Java direct-invoke overload resolution so reference-type overloads use real assignability and pick the more specific match when Java semantics clearly justify it.

**Architecture:** Keep JS candidate generation narrow and safe, and move reference-type compatibility plus specificity decisions into the native reflected overload resolver. Use real Java assignability checks for both argument acceptance and winner selection, while preserving ambiguity when no strict best match can be proven.

**Tech Stack:** C++, QuickJS runtime/bootstrap, existing Java bridge, host-side C++ tests, optional Android validation

---

### Task 1: Add failing host tests for the next reference-overload outcomes

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Write the failing tests**

Add focused tests for the observable next-step behavior:

- `null` prefers a more specific reference overload over `Object`
- wrapper/direct invoke still reaches reference overload paths cleanly
- unresolved primitive `null` behavior stays failing

Keep the fake resolver surface minimal and purpose-built.

**Step 2: Run host test binary to verify red**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe
.\build\test_js_runtime_native_attach.exe
```

Expected:

- new tests fail before native specificity logic exists

### Task 2: Add native reference-compatibility helpers

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Implement minimal helper layer**

Add helper functions for:

- resolving a reference descriptor to a Java `Class`
- checking whether one reference descriptor is assignable from another
- handling the internal `null` candidate cleanly

Keep helper scope as small as possible and avoid broad refactors.

**Step 2: Re-run host test binary**

Expected:

- no green yet on the new specificity cases, but compile remains stable

### Task 3: Upgrade native overload matching to use real assignability

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Replace narrow descriptor-only reference checks**

In `ResolveJavaMethodSignatureByTypeNames(...)`:

- keep primitive matching behavior stable
- for reference candidates:
  - accept exact matches
  - accept `null` against references
  - otherwise use real assignability

**Step 2: Re-run host test binary**

Expected:

- some new reference cases pass
- ambiguity may still remain until specificity comparison lands

### Task 4: Add conservative "more specific overload wins" comparison

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Implement pairwise specificity comparison**

When multiple reflected methods match:

- compare their parameter types pairwise
- prefer the one that is strictly more specific by assignability
- keep ambiguity if neither strictly dominates

This logic should also improve `null` behavior where one reference overload is clearly narrower than another.

**Step 2: Re-run host test binary**

Expected:

- new reference-specificity tests pass
- old overload/array/ClassFactory/registerClass tests remain green

### Task 5: Document the new reference-overload boundary

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Record the behavior**

Document:

- real reference assignability matching
- conservative specificity resolution
- improved `null` behavior
- the explicit remaining ambiguity boundary

**Step 2: Re-run verification if cleanup changed code**

Run the host test binary again before closing if needed.
