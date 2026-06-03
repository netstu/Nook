# Nook Java Env Wrapper Phase 2 Design

## Goal

Extend the new `Env` wrapper with the next small, Frida-aligned JNI surface after phase 1.

Phase 2 should add only:

- `env.exceptionOccurred()`
- `env.exceptionClear()`
- `env.getObjectClass(obj)`

It should not broaden into reference management, object comparison helpers, or string helpers yet.

## Why This Next

Phase 1 already proved that:

- `Java.vm.getEnv()` can return a stable wrapper object
- `env.handle` and `env.toString()` are usable diagnostics
- `env.exceptionCheck()` works
- `env.findClass(name)` works

The next useful step is to make the wrapper able to:

- inspect the current exception object
- clear the current exception
- resolve a Java object's runtime class

These are still narrow JNI operations, but they move `Env` from "pointer plus one query" toward a more practical Frida-style helper surface.

## Public Target

Phase 2 should support:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();
  var pending = env.exceptionOccurred();
  send({
    type: "send",
    payload: pending.toString()
  });
});
```

And:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();
  env.exceptionClear();
  send({
    type: "send",
    payload: "cleared"
  });
});
```

And:

```javascript
Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new();
  var clazz = env.getObjectClass(instance);
  send({
    type: "send",
    payload: clazz.toString()
  });
});
```

## Proposed Public Shape

### `env.exceptionOccurred()`

- returns a `NativePointer`
- returns `0x0` if no pending exception exists
- does not clear the exception

### `env.exceptionClear()`

- clears the current pending exception if one exists
- returns `true`

### `env.getObjectClass(obj)`

- accepts a Nook Java object wrapper
- returns a `NativePointer` for the resulting `jclass`
- rejects non-Java-object input

## Implementation Approaches Considered

### Option 1: Expose these as `Nook.Jni.*` helpers only

Pros:

- very small native surface

Cons:

- pushes users back toward low-level helper APIs
- works against the goal of a Frida-style `Env` model

### Option 2: Add them directly to `Env`

Pros:

- consistent with Frida direction
- keeps JNI helpers on the wrapper where users expect them
- reuses the proven phase-1 wrapper shape

Cons:

- grows `Env` incrementally, so runtime method routing must stay disciplined

### Option 3: Skip these and move directly to object comparison helpers

Pros:

- adds higher-level utility sooner

Cons:

- requires more argument-shape validation
- makes debugging harder if class/object reference semantics are still shaky

## Recommendation

Choose Option 2.

Reasoning:

- this keeps the API moving in the same direction as Frida
- it is the smallest practical next step
- it exercises both exception-state and object-reference JNI paths without expanding too fast

## Android Lifetime Model

The critical lesson from phase 1 is that JNI attach lifetime must cover the actual JNI call.

Phase 2 should continue the corrected pattern:

- `env.handle` remains diagnostic only
- real JNI work must happen while a local `JavaEnv jenv` is alive
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to:

- `exceptionOccurred()`
- `exceptionClear()`
- `getObjectClass(obj)`

## `env.exceptionOccurred()`

This is the next natural companion to `exceptionCheck()` because it:

- exposes the exception reference itself
- keeps behavior read-only from the caller's perspective
- validates returning a JNI local reference-shaped value from the wrapper

Phase 2 should:

- return `NativePointer(0)` when no exception is pending
- otherwise return the pending exception reference as a pointer-like wrapped value

It should not yet convert the exception to a Java wrapper.

## `env.exceptionClear()`

This is the smallest mutation helper worth adding now because:

- it is a standard pair with exception inspection
- it keeps exception-state workflows usable
- its result surface can stay very simple

Phase 2 should:

- call `ExceptionClear()`
- always return `true`

Phase 2 should not yet add:

- `throw()`
- `throwNew()`
- exception-to-string helpers

## `env.getObjectClass(obj)`

This is the best first object-reference helper because it:

- validates Java-wrapper argument parsing
- returns a JNI class reference
- enables later class/object helpers to build on a proven primitive

Phase 2 should:

- accept only a Java object wrapper
- reject null or malformed handles
- return a pointer-like wrapped `jclass`

It should not yet add class-name decoding.

## Testing Strategy

### Host / desktop regression

Add narrow tests proving:

- `env.exceptionOccurred()` returns a pointer-like value
- `env.exceptionClear()` returns `true`
- `env.getObjectClass(obj)` returns a pointer-like value
- `env.getObjectClass(...)` rejects non-Java-object input

The host runtime should use minimal test-only callbacks for:

- exception occurred
- exception clear
- get object class

Avoid full fake JNI emulation.

### Device smoke

Add a focused smoke proving:

- `exceptionOccurred()` initially returns `0x0`
- `exceptionClear()` can be called
- `getObjectClass(...)` returns non-null for a real Java object wrapper

## Boundaries

Phase 2 does not attempt:

- `isSameObject(...)`
- `isInstanceOf(...)`
- class name/string helpers
- local/global reference management
- turning JNI refs into full Java wrappers

## Success Criteria

- `env.exceptionOccurred()` works
- `env.exceptionClear()` works
- `env.getObjectClass(obj)` works
- Android attach lifetime remains correct at the actual JNI call sites
- the wrapper stays small and ready for a later comparison/helper phase
