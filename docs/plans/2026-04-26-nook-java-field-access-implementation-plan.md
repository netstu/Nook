# Nook Java Field Access Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal Frida-style Java field access through `Java.use(...).fieldName.value` for common scalar and string fields.

**Architecture:** Extend the current lazy `Java.use(...)` wrapper to support field wrappers in parallel with method wrappers, and add a matching native bridge for field resolution and field read/write. Keep the type matrix narrow and validate one static and one instance field path.

**Tech Stack:** C++, QuickJS, JNI reflection, existing Java method bridge, C++ communication tests, Android smoke scripts.

---

### Task 1: Add failing host-side tests for field wrappers and field IO

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.h`

**Step 1: Write the failing test**

Add tests for:

- static field wrapper shape
- static field read
- static field write
- instance field wrapper shape inside callback receiver
- instance field read/write through callback receiver

Use fake bridge dependencies first, consistent with the current Java method tests.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 tests/communication/test_js_runtime_native_attach.cpp -o build/test_js_runtime_native_attach_task_fields.exe
.\build\test_js_runtime_native_attach_task_fields.exe
```

Expected: FAIL because field bridge types/functions do not exist yet.

**Step 3: Write minimal implementation**

Add only the declarations needed for the tests to compile against the new field bridge surface.

**Step 4: Run test to verify it still fails for the right reason**

Run the same command and confirm failure is now behavioral, not missing declarations.

### Task 2: Add native field metadata and bridge operations

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Add field metadata structures**

Introduce minimal field record/dependency types:

- field request/record
- resolve field
- read field
- write field

Reuse `JavaJsValue` instead of creating a separate type system.

**Step 2: Implement default Android field resolution**

Use reflection to resolve:

- field name
- JNI signature
- static flag

Support private demo-app fields by calling `setAccessible(true)` if needed.

**Step 3: Implement scalar/string read-write**

Add Android default read/write helpers for:

- `Z`
- `I`
- `J`
- `F`
- `D`
- `Ljava/lang/String;`

### Task 3: Extend `Java.use(...)` wrappers with field `.value`

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing wrapper behavior tests**

Use the tests from Task 1 to require:

- field wrapper metadata
- `.value` getter
- `.value` setter

**Step 2: Implement minimal JS factory changes**

Extend the Java wrapper factory with:

- `makeField(...)`
- cached field wrappers
- `.value` property backed by native bridge helpers

Keep existing method behavior unchanged.

**Step 3: Preserve callback receiver compatibility**

Ensure instance callback receivers expose fields so `this.someField.value` works during Java hook callbacks.

### Task 4: Verify locally and add device smoke coverage

**Files:**
- Create: `host/nook-py/java_field_smoke.js`
- Modify: `host/nook-py/README.md`
- Modify: `docs/code_review.md`

**Step 1: Add a smoke script**

Use:

- static field: `MainActivity.interceptCount`
- instance field inside method hook: `AdWallFragment.adCount`

Expected output should show:

- static before/after values
- instance field before/after inside the hook callback

**Step 2: Run local verification**

Run:

```powershell
g++ -std=c++17 tests/communication/test_js_runtime_native_attach.cpp -o build/test_js_runtime_native_attach_task_fields.exe
.\build\test_js_runtime_native_attach_task_fields.exe
```

**Step 3: Document the feature**

Update docs with:

- supported field types
- current instance-field boundary
- smoke command
