# Nook Java Bridge Scalar And Static Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend the current minimal Java hook bridge to support the next practical scalar types (`long`, `float`, fuller `double` coverage) and the first `static` method hook path.

**Architecture:** Reuse the existing Java JS bridge and JS runtime wrapper pipeline. Add the missing scalar conversions in the bridge first, then thread `is_static` through overload resolution and install/original-call flow, and finally add host-side regression tests that prove the widened type and static paths work without breaking the current Java instance-method behavior.

**Tech Stack:** C++17, QuickJS, Android JNI bridge, existing `JavaHook` runtime, host-side unit tests in `tests/communication/test_js_runtime_native_attach.cpp`

---

### Task 1: Add failing host tests for scalar bridge coverage

**Files:**
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Add fake signature resolver cases**

Extend `FakeResolveJavaMethodSignature(...)` with:

- one `long` overload case
- one `float` overload case
- one `static` method case

Suggested minimal signatures:

- `com.demo.target.TextFragment.formatScaled(long)` -> `(J)Ljava/lang/String;`
- `com.demo.target.TextFragment.formatScaled(float)` -> `(F)Ljava/lang/String;`
- `com.demo.target.MainActivity.incrementIntercept(int)` static -> `(I)I`

**Step 2: Add wrapper-selection tests**

Write failing tests for:

- `TextFragment.formatScaled.overload('long')`
- `TextFragment.formatScaled.overload('float')`
- `MainActivity.incrementIntercept.overload('int')` static exact signature

**Step 3: Add callback/original-call tests**

Write failing tests showing:

- `long` callback can call `callOriginal(...)`
- `float` callback can call `callOriginal(...)`
- `static` callback can call `callOriginal(...)`

**Step 4: Run test binary and confirm failure**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task70.exe
build\test_js_runtime_native_attach_task70.exe
```

Expected: FAIL on missing type/static support.

### Task 2: Extend Java scalar value conversion

**Files:**
- Modify: `src/agent_runtime/nook_java_js_bridge.h`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Extend `JavaJsValueKind`**

Add:

- `kInt64` or a narrow equivalent for `long`
- `kFloat`

Keep `kDouble` and current fields coherent.

**Step 2: Extend signature parsing**

Update `ParseTypeDescriptor(...)` to accept:

- `J`
- `F`

**Step 3: Extend JNI -> JS conversion**

In `ConvertNookJavaHookValueToJavaJsValue(...)`, support:

- `J`
- `F`

**Step 4: Extend JS -> JNI conversion**

In `ConvertJavaJsValueToNookJavaHookValue(...)`, support:

- `J`
- `F`

Keep integer-first coercion for integer descriptors and number-based coercion for float/double.

**Step 5: Extend JS runtime value marshaling**

In `MakeJavaJsValue(...)` / `ParseJavaJsValue(...)`, make sure the new Java scalar kinds map cleanly to QuickJS numbers without regressing existing `boolean` / `string` / `int`.

**Step 6: Re-run tests**

Run:

```powershell
build\test_js_runtime_native_attach_task70.exe
```

Expected: scalar tests now pass; static tests still fail if not implemented yet.

### Task 3: Thread static-method metadata through the wrapper/install path

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `src/agent_runtime/nook_java_js_bridge.cpp`

**Step 1: Add static metadata to JS method wrapper**

Update the `Java.use(...)` wrapper factory so a method wrapper can carry:

- `$isStatic`

For the minimal path, this can be exposed through a small wrapper helper such as:

- a static-bound sibling wrapper
- or a dedicated metadata setter used by tests

Do not widen API surface more than necessary.

**Step 2: Pass static metadata into overload resolution**

Update `__nookJavaResolveOverloadSignature(...)` and `ResolveJavaMethodSignature(...)` call sites so `is_static` is part of resolution instead of always forcing `false`.

**Step 3: Pass static metadata into install**

Update `JsJavaInstallImplementation(...)` so `JavaJsHookRequest.is_static` uses wrapper metadata.

**Step 4: Preserve static metadata into callback receiver**

When building the callback receiver for `callOriginal(...)`, preserve whether the installed hook record is static.

**Step 5: Re-run tests**

Run:

```powershell
build\test_js_runtime_native_attach_task70.exe
```

Expected: static install/original-call tests now pass.

### Task 4: Document current scalar/static support and limits

**Files:**
- Modify: `host/nook-py/README.md`
- Modify: `docs/code_review.md`

**Step 1: Update Java bridge support list**

Document the new supported Java scalar types:

- `void`
- `boolean`
- `int`
- `long`
- `float`
- `double`
- `java.lang.String`

**Step 2: Document `static` support**

Add one minimal example showing `Java.use(...).someStatic.overload(...).implementation`.

**Step 3: Document `long` limitation**

Explicitly note that `long` currently uses the minimal JS number bridge and is meant for common practical values, not a full BigInt/Int64 compatibility guarantee.

### Task 5: Verification handoff

**Files:**
- None

**Step 1: Rebuild host test binary**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach_task70.exe
build\test_js_runtime_native_attach_task70.exe
```

Expected: PASS.

**Step 2: Optional Android rebuild**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4
```

Expected: `libnook.so`, `libnook-agent.so`, and `nook-server` rebuild successfully.
