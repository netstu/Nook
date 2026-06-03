# Nook Java.vm.getEnv Design

## Goal

Add the next minimal Frida-aligned `Java.vm` primitive:

- `Java.vm.getEnv()`

This pass should expose the current thread's `JNIEnv*` as a `NativePointer` without broadening into a full `Env` wrapper or adding `tryGetEnv()`.

## Why This Next

Nook now has the first useful `Java.vm` execution layer:

- `Java.vm.perform(fn)`
- `Java.performNow(fn)` delegating through `Java.vm.perform(fn)`
- `Java.perform(fn)` composing `Java.ready(...)` and `Java.vm.perform(fn)`

The next missing low-level primitive is direct access to the current thread's JNI environment pointer.

Frida's `Java.vm` surface is useful not only because it can execute callbacks, but because it clearly separates:

- VM/thread attachment state
- lifecycle readiness
- higher-level Java wrappers

Adding `Java.vm.getEnv()` is the smallest next step in that direction.

## Public Target

This phase should support:

```javascript
var env = Java.vm.getEnv();
send({
  type: 'send',
  payload: typeof env + ':' + env.isNull() + ':' + env.toString()
});
```

Expected behavior in this phase:

- `Java.vm.getEnv` exists as a function
- it returns a `NativePointer`
- the pointer is non-null when the runtime can provide a JNI environment on the current thread
- failures are explicit instead of returning a fake value

## Proposed Semantics

### `Java.vm.getEnv()`

- takes no arguments
- returns a `NativePointer` representing the current thread's `JNIEnv*`
- if no usable JNI environment is available, throw a clear runtime error

For this phase, it is acceptable that the implementation may attach the current thread if that is how the existing `JavaEnv` helper obtains `JNIEnv*`.

This means:

- `Java.vm.perform(fn)` remains the execution primitive
- `Java.vm.getEnv()` becomes the first direct JNI environment primitive
- lifecycle and class-loader readiness remain unrelated concerns

## Implementation Approaches Considered

### Option 1: JS-only placeholder

Expose `Java.vm.getEnv = function () { ... }` in bootstrap only.

Pros:

- tiny patch

Cons:

- cannot produce a real `JNIEnv*`
- would be a fake API that must be replaced immediately

### Option 2: Minimal runtime-backed native pointer return

Expose `Java.vm.getEnv()` through the runtime and return a `NativePointer`.

Pros:

- real low-level primitive
- matches the current `Java.vm.perform(fn)` layering
- easy to compose with existing `NativePointer` helpers

Cons:

- needs one more runtime-backed entrypoint
- host tests need a controlled test-only env source

### Option 3: Full `getEnv() + tryGetEnv() + Env wrapper`

Pros:

- closer to larger Frida parity

Cons:

- too much scope for this pass
- larger regression surface

## Recommendation

Choose Option 2.

Reasoning:

- it keeps the public API honest
- it aligns with the user's "follow Frida incrementally" requirement
- it stays small enough to validate with host tests and one device smoke

## Architecture Direction

This phase should add:

- a runtime-backed `Java.vm.getEnv()` entrypoint
- a small internal helper that resolves the current thread's `JNIEnv*`
- host-test injection for the env pointer source when not running on Android

This phase should not add:

- `Java.vm.tryGetEnv()`
- any JS `Env` wrapper object
- direct JNI method wrappers on top of the returned pointer
- lifecycle policy changes

## Testing Strategy

### Host / desktop regression

Add coverage for:

- `typeof Java.vm.getEnv === 'function'`
- `Java.vm.getEnv()` returns a `NativePointer`
- repeated calls return the same pointer in the same test setup
- `Java.vm.perform(function () { return Java.vm.getEnv(); })` can use it immediately

Because host tests do not have a real Android JVM, provide a focused test-only env pointer source instead of broadening the fake Java bridge.

### Device smoke

Add a dedicated smoke script proving:

- binding existence
- non-null env pointer
- `Java.vm.getEnv()` works both directly and inside `Java.vm.perform(...)`

The smoke does not need to dereference `JNIEnv*`; it only needs to prove that the pointer is exposed consistently.

## Boundaries

This pass does not attempt:

- full Frida `Java.vm` parity
- env lifetime modeling
- cross-thread env caching semantics
- `Java.vm.tryGetEnv()`
- JNI method helpers layered on the env pointer

## Success Criteria

- `Java.vm.getEnv()` exists on the public `Java.vm` surface
- host tests prove it returns a `NativePointer` and is usable inside `Java.vm.perform(...)`
- device smoke proves non-null pointer exposure on a real target
- no regression to existing `Java.vm.perform(fn)`, `Java.performNow(fn)`, or `Java.perform(fn)` behavior
