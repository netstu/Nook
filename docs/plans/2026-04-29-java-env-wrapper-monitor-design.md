# Nook Java Env Wrapper Monitor Design

## Goal

Extend the `Env` wrapper with the next smallest stable Frida-aligned monitor primitives:

- `env.monitorEnter(obj)`
- `env.monitorExit(obj)`

This phase should not also add:

- `env.getSuperclass(...)`
- `env.isAssignableFrom(...)`
- any `withMonitor(...)` or `synchronized(...)` JS helper
- any local-ref or local-frame API

## Why This Next

Nook's current Java surface already covers:

- `Java.ready(...)`
- `Java.perform(...)`
- `Java.performNow(...)`
- `Java.vm.perform(...)`
- `Java.vm.getEnv()` / `Java.vm.tryGetEnv()`
- core `Env` helpers through strings, global refs, and weak global refs

The next practical low-level Frida-aligned step should stay inside the current `Env` architecture and avoid reopening the known local-ref boundary.

`MonitorEnter` / `MonitorExit` fit that requirement because:

- they are real JNI monitor primitives
- they operate on persistent Java object wrappers, not frame-local references
- they are useful in real Java synchronization workflows
- they keep the roadmap focused on low-level Frida parity instead of broadening into Nook-specific helpers

## Public Target

This phase should support:

```javascript
Java.perform(function () {
  var env = Java.vm.getEnv();
  var obj = Java.use("com.demo.target.TextFragment").$new();
  env.monitorEnter(obj);
  try {
    send({ type: "send", payload: "inside-monitor" });
  } finally {
    env.monitorExit(obj);
  }
});
```

## Proposed Public Shape

### `env.monitorEnter(obj)`

- accepts one non-null Java object wrapper
- enters the JNI monitor for that object
- returns `true`
- rejects non-object input

### `env.monitorExit(obj)`

- accepts one non-null Java object wrapper
- exits the JNI monitor for that object
- returns `true`
- rejects non-object input

## Why Not Add a JS Helper

The tempting next layer would be something like:

- `Java.withMonitor(obj, fn)`
- `Java.synchronized(obj, fn)`

That would be more convenient, but it is not the right phase.

Reasons:

- it is a Nook-side convenience helper, not a direct Frida-aligned low-level primitive
- it broadens scope into exception-wrapping and helper semantics
- it can still be added later on top of the same public monitor pair if real scripts need it

## Android Lifetime Model

This phase keeps the same corrected Android rule used by the existing `Env` helpers:

- `env.handle` remains diagnostic only
- each real JNI operation must execute while a local `JavaEnv jenv` is alive at the actual call site
- the implementation must not assume a previously captured `JNIEnv*` remains valid

That means `MonitorEnter` and `MonitorExit` should follow the same structure as:

- `newGlobalRef(...)`
- `deleteGlobalRef(...)`
- `newWeakGlobalRef(...)`
- `deleteWeakGlobalRef(...)`

## Input Model

Keep this phase wrapper-first:

- accept Java object wrappers only
- do not also accept raw `NativePointer` handles

Reasons:

- this matches the already established shape of `env.newGlobalRef(obj)`
- it avoids introducing another mixed calling convention inside `Env`
- the object wrapper already carries the right receiver handle

## Implementation Approaches Considered

### Option 1: Strict low-level `Env` pair only

Pros:

- smallest Frida-aligned step
- fits the current architecture cleanly
- low regression surface

Cons:

- slightly less convenient than a higher-level helper

### Option 2: Add monitor pair plus JS convenience helper

Pros:

- easier to use in scripts immediately

Cons:

- expands scope
- mixes low-level parity work with Nook-only helper design
- increases testing surface unnecessarily

### Option 3: Skip monitors and do `getSuperclass()` / `isAssignableFrom()` first

Pros:

- also safe in the current architecture
- simpler relationship helpers

Cons:

- lower practical value
- less useful in real synchronization scenarios
- weaker next step after refs and strings

## Recommendation

Choose Option 1.

Reasoning:

- it is the best value-for-scope next step
- it stays strictly within the stable `Env` execution model
- it keeps the roadmap focused on Frida-aligned JNI primitives

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.monitorEnter(obj)` returns `true`
- `env.monitorExit(obj)` returns `true`
- the runtime forwards the correct `env_ptr` and `object_handle`
- invalid input is rejected for both methods

Use narrow test-only callbacks for:

- `MonitorEnter`
- `MonitorExit`

### Device smoke

Add a focused smoke proving:

- bindings exist
- `monitorEnter(...)` succeeds on a real Java wrapper
- `monitorExit(...)` succeeds on the same wrapper
- `env.exceptionCheck()` remains `false`

## Boundaries

This phase does not attempt:

- `getSuperclass(...)`
- `isAssignableFrom(...)`
- JS `withMonitor(...)` helper
- local refs
- local frames
- monitor diagnostics or recursion counters

## Success Criteria

- `env.monitorEnter(obj)` works
- `env.monitorExit(obj)` works
- invalid inputs are rejected clearly
- Android attach lifetime remains correct at the real JNI call site
- the API shape stays strictly low-level and Frida-aligned

## Status Update

Superseded on 2026-04-29 after device validation.

Reason:

- desktop tests passed
- Android device diagnostics showed `monitorEnter(...)` succeeds but `monitorExit(...)` fails on the same wrapper
- root cause is the current `Env` execution model, where each `env.xxx()` is a separate JNI re-entry rather than one stable live `JNIEnv*` session

Outcome:

- do not ship public `monitorEnter/monitorExit` in the current architecture
- see [2026-04-29-java-env-wrapper-monitor-postmortem.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-29-java-env-wrapper-monitor-postmortem.md)
