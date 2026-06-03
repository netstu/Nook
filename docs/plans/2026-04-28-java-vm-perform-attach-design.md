# Nook Java.vm.perform Attach Semantics Design

## Goal

Refine `Java.vm.perform(fn)` so it matches Frida's intended role more closely:

- ensure the current thread is attached to the JVM before executing `fn`

This pass is not about adding new public APIs. It is about fixing the execution semantics of the existing primitive.

## Why This Next

Nook now has:

- `Java.vm.perform(fn)`
- `Java.vm.getEnv()`
- `Java.vm.tryGetEnv()`

But the latest device smoke showed a semantic mismatch:

- direct `Java.vm.tryGetEnv()` returned `null`
- inside `Java.vm.perform(...)`, `Java.vm.tryGetEnv()` also returned `null`

That means `Java.vm.perform(fn)` is still only a synchronous callback wrapper, not a true attach-and-execute primitive.

Frida's layering depends on:

- `perform()` making an env available for the callback
- `tryGetEnv()` reporting existing attachment state without attaching
- `getEnv()` being strict

Without correct `perform()` semantics, the rest of `Java.vm` cannot line up cleanly.

## Public Target

This phase should support:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.tryGetEnv();
  send({
    type: "send",
    payload: env === null ? "null" : env.toString()
  });
});
```

Expected behavior:

- callback still runs synchronously
- callback still preserves visible execution order
- inside the callback, `Java.vm.tryGetEnv()` must return a non-null pointer
- `Java.vm.getEnv()` must keep working

## Proposed Semantics

### `Java.vm.perform(fn)`

- `fn` must still be a function
- on Android:
  - ensure the current thread is attached to the JVM before invoking `fn`
  - if attach/env acquisition fails, throw
- execute `fn` synchronously after that

This preserves:

- immediate execution order
- existing `Java.performNow(fn)` delegation
- existing `Java.perform(fn)` delegation chain

## Implementation Approaches Considered

### Option 1: Keep `perform()` as-is and special-case `tryGetEnv()`

Pros:

- tiny patch

Cons:

- wrong layering
- `perform()` remains semantically misleading
- does not match Frida's VM contract

### Option 2: Make `perform()` explicitly acquire a JVM env before callback execution

Pros:

- correct layering
- smallest fix that addresses the real bug
- improves all callers transitively:
  - `Java.performNow(fn)`
  - `Java.perform(fn)`

Cons:

- requires a small runtime change

### Option 3: Move Java callback execution to a dedicated attached worker thread

Pros:

- could support a larger future runtime model

Cons:

- much too large for this fix
- changes threading semantics far beyond current scope

## Recommendation

Choose Option 2.

Reasoning:

- it fixes the actual semantic gap
- it keeps scope narrow
- it aligns directly with Frida without redesigning the runtime

## Architecture Direction

This phase should:

- keep `InvokeJavaCallbackImmediately(...)` as the synchronous JS-call helper
- add a small Android-only env-ensure step in `JsJavaVmPerform(...)`
- continue to let `Java.vm.getEnv()` and `Java.vm.tryGetEnv()` sit on top of the runtime's env state

This phase should not:

- add new `Java.vm` APIs
- change ready/lifecycle policy
- introduce worker-thread dispatch

## Testing Strategy

### Host / desktop regression

Add coverage proving:

- `Java.vm.perform(...)` still executes synchronously
- inside `Java.vm.perform(...)`, `Java.vm.tryGetEnv()` is non-null
- existing `getEnv()` and `tryGetEnv()` tests remain green

Because host tests do not have a real JVM, the test-only env callback should model the "perform has attached" state explicitly.

### Device smoke

Re-run:

- `java_vm_perform_smoke.js`
- `java_vm_trygetenv_smoke.js`

Expected device result:

- direct `tryGetEnv()` may still be `null`
- `perform`-scoped `tryGetEnv()` must be non-null

## Boundaries

This pass does not attempt:

- async Java callback scheduling
- main-thread dispatch policy
- env wrapper objects
- full Frida VM parity

## Success Criteria

- `Java.vm.perform(fn)` ensures callback-time env availability
- `Java.vm.tryGetEnv()` returns non-null inside `Java.vm.perform(...)`
- existing immediate execution behavior stays unchanged
- `Java.performNow(fn)` and `Java.perform(fn)` benefit transitively without API changes
