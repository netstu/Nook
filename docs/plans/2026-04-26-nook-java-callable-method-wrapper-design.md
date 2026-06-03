# Nook Java Callable Method Wrapper Design

**Date:** 2026-04-26

**Status:** Approved

## Goal

Make Nook's Java method wrappers behave more like Frida:

- `typeof SomeClass.someMethod === "function"`
- `this.someMethod(arg1, ...)` directly invokes the original Java method
- `.overload(...)`, `.implementation`, and `.callOriginal(...)` keep working

This step is intentionally narrower than full Frida parity. The target is callable method wrappers plus a minimal native invoke bridge.

## Current Gap

Today Nook already supports:

- `Java.perform(...)`
- `Java.use(...)`
- `method.implementation = fn`
- hook callback `callOriginal(...)`
- `Java.ready(...)`

But method wrappers are still plain JS objects. That means:

- device smoke prints `typeof LoginFragment.verifyPasswordNative === "object"`
- users cannot write Frida-style direct calls like `this.verifyPasswordNative(password)`

## Recommended Approach

Use a real callable wrapper, backed by a new minimal native method-invoke bridge.

### JS-side shape

Inside `CreateJavaUseWrapper(...)`:

- method wrappers become function objects
- metadata remains on the function object:
  - `$className`
  - `$methodName`
  - `$signature`
  - `$isStatic`
  - `__nookJavaReceiverHandle`
- existing APIs stay attached:
  - `.overload(...)`
  - `.implementation`
  - `.callOriginal(...)`

### Native-side shape

Add a new bridge path that can invoke a Java method outside hook callback context:

- static methods: invoke through class + signature
- instance methods: invoke through receiver handle + signature
- reuse the existing Java value conversion code
- reuse signature parsing already present in `nook_java_js_bridge.cpp`

## Boundaries

This step will support:

- direct original-method invocation from JS wrappers
- both static and instance dispatch where receiver metadata exists
- continued hook install and callback behavior

This step will not yet try to support:

- constructors / `$new`
- arbitrary object lifetime management beyond current wrapper model
- `Java.choose`
- full Frida class/model parity

## Test Strategy

Use TDD with runtime behavior tests:

- add a failing JS runtime test that proves `typeof wrapper === "function"`
- add a failing JS runtime test that directly calls a static Java wrapper and gets the fake original result
- keep existing implementation-install and `callOriginal(...)` tests green
