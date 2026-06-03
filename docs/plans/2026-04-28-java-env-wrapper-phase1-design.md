# Nook Java Env Wrapper Phase 1 Design

## Goal

Introduce the first minimal `Env` wrapper on top of `JNIEnv*` so Nook can start moving from raw env pointers toward a Frida-style Java VM surface.

Phase 1 should keep the scope deliberately small:

- `Java.vm.getEnv()` returns an `Env` object
- `Java.vm.tryGetEnv()` returns `Env | null`
- `Env` exposes only:
  - `handle`
  - `toString()`
  - `exceptionCheck()`
  - `findClass(name)`

## Why This Next

Nook now already has the lower-level pieces:

- `Java.vm.perform(fn)` with attach semantics
- `Java.vm.getEnv()`
- `Java.vm.tryGetEnv()`

Those are useful, but raw `NativePointer` env values are not a good long-term public API. Frida's `Java.vm` becomes powerful because the env is not just a pointer; it is an object with controlled JNI operations.

The next correct step is therefore not "add many JNI methods", but "stabilize the `Env` object model first".

## Public Target

Phase 1 should support:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();

  send({
    type: "send",
    payload:
      env.toString() + ":" +
      env.handle.toString() + ":" +
      String(env.exceptionCheck())
  });
});
```

And:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();
  var clazz = env.findClass("java/lang/String");
  send({
    type: "send",
    payload: clazz.toString()
  });
});
```

## Proposed Public Shape

### `Java.vm.getEnv()`

- returns `Env`
- no longer returns a raw `NativePointer`

### `Java.vm.tryGetEnv()`

- returns `Env | null`

### `Env`

Phase 1 methods/properties:

- `env.handle`
  - underlying `JNIEnv*` as `NativePointer`
- `env.toString()`
  - readable representation that includes the underlying pointer
- `env.exceptionCheck()`
  - returns boolean
- `env.findClass(name)`
  - `name` is JNI-style slash-separated class name
  - returns a wrapped pointer value for the resulting `jclass`

## Implementation Approaches Considered

### Option 1: Keep `getEnv()` returning a raw pointer and add a separate env factory

Pros:

- minimal change to current behavior

Cons:

- awkward API
- exposes the wrong long-term surface
- makes later migration messier

### Option 2: Make `getEnv()/tryGetEnv()` return `Env` directly

Pros:

- correct public shape from day one
- easiest path toward Frida-style expansion
- hides raw env details behind a controlled wrapper

Cons:

- changes the current newly-added `getEnv()` return type

### Option 3: Return both pointer and object in parallel

Pros:

- flexible in the short term

Cons:

- duplicates concepts
- creates ambiguity in future docs and scripts

## Recommendation

Choose Option 2.

Reasoning:

- this is the cleanest Frida-aligned shape
- we are still early enough to correct the return type
- it avoids freezing a pointer-only API too early

## Wrapper Model

Phase 1 should treat `Env` as:

- a JS object backed by:
  - one `JNIEnv*`
- with explicit methods implemented in the runtime

The wrapper should not pretend to be thread-portable. It represents the env pointer for the current attached thread context.

## `Env.findClass(name)`

This is the best first real JNI operation because it exercises:

- string argument conversion
- JNI invocation
- exception handling path
- object/pointer result wrapping

For phase 1:

- accept only string input
- return a pointer-like wrapped value for `jclass`
- on failure, throw a clear error

This is enough to validate the wrapper architecture without broadening into method calls or local reference lifetime helpers.

## `Env.exceptionCheck()`

This is the best first read-only JNI state method because:

- it is simple
- it does not mutate VM state
- it validates that wrapper method dispatch into JNI works

Phase 1 should not yet add:

- `exceptionOccurred()`
- `exceptionClear()`
- `throw()`

## Architecture Direction

This phase should add:

- an `Env` wrapper constructor/factory internal to the runtime
- runtime-backed `Env` methods
- `Java.vm.getEnv()` / `tryGetEnv()` returning wrapper objects instead of plain pointers

This phase should not add:

- string decoding helpers
- local/global reference management
- method invocation via env
- array helpers
- broad object wrappers

## Testing Strategy

### Host / desktop regression

Add coverage for:

- `Java.vm.getEnv()` returns object-like wrapper
- `env.handle` is a `NativePointer`
- `env.toString()` is stable and readable
- `env.exceptionCheck()` returns boolean
- `env.findClass("java/lang/String")` succeeds through a test double
- `Java.vm.tryGetEnv()` returns `null` or `Env` correctly

Because host tests do not have a real `JNIEnv*`, add narrow test-only callbacks for the new env methods instead of trying to emulate full JNI.

### Device smoke

Add a focused smoke proving:

- wrapper binding exists
- `env.handle` is visible
- `env.exceptionCheck()` runs
- `env.findClass("java/lang/String")` returns non-null

## Boundaries

Phase 1 does not attempt:

- full Frida `Env` parity
- reference lifetime helpers
- string creation helpers
- method calls through env
- exception mutation helpers

## Success Criteria

- `Java.vm.getEnv()` returns `Env`
- `Java.vm.tryGetEnv()` returns `Env | null`
- `Env.exceptionCheck()` and `Env.findClass(...)` work
- the wrapper model is stable enough to expand in phase 2
