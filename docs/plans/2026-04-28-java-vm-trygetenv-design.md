# Nook Java.vm.tryGetEnv Design

## Goal

Add the next Frida-aligned `Java.vm` primitive:

- `Java.vm.tryGetEnv()`

This pass should expose a non-intrusive JNI environment query that returns the current thread's `JNIEnv*` as a `NativePointer`, or `null` if the thread is not attached.

## Why This Next

Nook now already has:

- `Java.vm.perform(fn)` as the attach-and-execute primitive
- `Java.vm.getEnv()` as the strict JNI environment accessor

The next missing piece for Frida-style layering is the non-throwing, non-attaching query path:

- `perform()` for execution with attachment
- `getEnv()` for strict access
- `tryGetEnv()` for probe-only access

That separation is important because it lets scripts distinguish:

- "I want code to run with a VM env"
- "I require an env now"
- "I want to know whether an env already exists"

## Public Target

This phase should support:

```javascript
var env = Java.vm.tryGetEnv();
send({
  type: "send",
  payload: env === null ? "null" : env.toString()
});
```

Expected behavior:

- `Java.vm.tryGetEnv` exists as a function
- if the current thread is already attached, return a `NativePointer`
- if the current thread is not attached, return `null`
- do not attach the thread
- do not throw for the unattached case

## Proposed Semantics

### `Java.vm.tryGetEnv()`

- takes no arguments
- returns `NativePointer | null`
- does not attach the current thread
- does not throw when the thread is detached
- may still throw on unexpected internal/runtime failures

This must differ intentionally from:

- `Java.vm.getEnv()`
  - strict
  - throws if env is unavailable
- `Java.vm.perform(fn)`
  - ensures a usable env by executing through the VM execution path

## Implementation Approaches Considered

### Option 1: Extend the internal env helper with strict vs try modes

Pros:

- keeps one shared env acquisition path
- minimizes drift between `getEnv()` and `tryGetEnv()`
- easiest to maintain as more `Java.vm` APIs arrive

Cons:

- requires a small internal refactor

### Option 2: Add an entirely separate `tryGetEnv()` path

Pros:

- simpler local patch in the short term

Cons:

- duplicates env-query logic
- higher future regression risk

### Option 3: Implement `tryGetEnv()` in JS as `try { getEnv() } catch { null }`

Pros:

- smallest visible patch

Cons:

- wrong semantics
- current `getEnv()` path may attach the thread, which violates Frida-style `tryGetEnv()`

## Recommendation

Choose Option 1.

Reasoning:

- it keeps the runtime layering coherent
- it matches the user's explicit Frida-alignment goal
- it gives us the correct semantic split without bloating scope

## Architecture Direction

This phase should add:

- a runtime-backed `Java.vm.tryGetEnv()`
- a shared internal helper that supports:
  - strict env resolution
  - non-attaching env query
- a slightly richer host-test callback contract so tests can model:
  - env available
  - env unavailable
  - hard failure

This phase should not add:

- any `Env` wrapper object
- direct JNI method helpers
- changes to `Java.vm.perform(fn)` behavior

## Android Behavior

For `tryGetEnv()` specifically:

- do not use the `JavaEnv` constructor path, because it may attach
- query `JavaVM->GetEnv(...)` directly
- if the result is `JNI_OK`, return the pointer
- if the result is `JNI_EDETACHED`, return `null`
- if the result is any other unexpected failure, report it as an internal error

This keeps `tryGetEnv()` aligned with Frida's semantics instead of reusing the more invasive strict path.

## Testing Strategy

### Host / desktop regression

Add coverage for:

- `typeof Java.vm.tryGetEnv === 'function'`
- attached case returns `NativePointer`
- unattached case returns `null`
- inside `Java.vm.perform(...)`, `tryGetEnv()` returns a non-null pointer

Host tests should use a test-only callback that can model:

- success with a pointer
- unattached / unavailable
- hard failure

### Device smoke

Add a small smoke proving:

- binding existence
- direct `tryGetEnv()` result
- `Java.vm.perform(...)`-scoped `tryGetEnv()` result

The smoke does not need to prove the detached-thread case on device in this pass.

## Boundaries

This pass does not attempt:

- full Frida `Java.vm` parity
- env wrapper semantics
- JNI dereference helpers on top of the returned env
- thread-state introspection beyond env availability

## Success Criteria

- `Java.vm.tryGetEnv()` exists on the public `Java.vm` surface
- host tests prove null-vs-pointer behavior
- device smoke proves the attached-thread success case
- existing `Java.vm.getEnv()` and `Java.vm.perform(fn)` behavior remain unchanged
