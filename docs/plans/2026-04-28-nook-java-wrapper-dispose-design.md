# Nook Java Wrapper $dispose Design

## Goal

Add the smallest useful Frida-style explicit wrapper cleanup API:

- instance Java wrappers returned by `Java.use(...).$new(...)`, `Java.cast(...)`, `Java.retain(...)`, `Java.choose(...)`, `Java.registerClass(...)`, and normal Java method returns should expose `$dispose()`
- `$dispose()` should release the wrapper's retained global ref
- `$dispose()` should be idempotent
- after disposal, later instance-method access should fail through the existing invalid-handle checks

This pass is intentionally narrow. It does not attempt to solve all Java wrapper lifetime management in one go.

## Scope

This pass covers:

- a native release primitive paired with the existing retain primitive
- JS wrapper-level `$dispose()` on instance wrappers only
- idempotent repeated calls
- preserving existing behavior for class wrappers returned by `Java.use(className)`
- focused desktop regression coverage

This pass does not include:

- automatic GC-driven cleanup
- script-unload bulk cleanup for Java wrappers
- new lifecycle tracking for temporary local refs
- class-wrapper `$dispose()`
- broad refactoring of `Java.use(...)` wrapper generation

## Approaches Considered

### Approach A: Explicit `$dispose()` only

Add a release primitive and a JS-visible `$dispose()` method on instance wrappers.

Pros:

- closest first step to Frida's public API surface
- small blast radius
- easy to validate with deterministic tests

Cons:

- does not yet cover automatic cleanup

### Approach B: Automatic cleanup first

Add wrapper finalizers and script-unload cleanup before exposing `$dispose()`.

Pros:

- stronger lifecycle story

Cons:

- much riskier in QuickJS + JNI
- harder to debug and verify
- not the minimal Frida-alignment step

### Approach C: Do both in one pass

Expose `$dispose()` and add automatic cleanup together.

Pros:

- more complete feature set

Cons:

- larger regression surface
- harder to isolate failures

## Recommended Design

Use Approach A now.

Implementation shape:

- add `ReleaseJavaObjectFn` to the Java bridge dependency table
- implement `ReleaseJavaObject(...)` beside `RetainJavaObject(...)`
- on Android, the default backend should call `DeleteGlobalRef()` on the provided handle
- extend Java instance wrappers created by `CreateJavaUseWrapper(...)` with:
  - `__nookJavaOwnedHandle`
  - `$dispose()`

Ownership rules for this pass:

- instance wrappers built around retained/global handles should set `__nookJavaOwnedHandle = true`
- class wrappers must keep `__nookJavaOwnedHandle = false`
- `$dispose()` should:
  - no-op and return `undefined` if the wrapper is already disposed
  - no-op and return `undefined` if the wrapper does not own the handle
  - otherwise call the native release primitive, then set:
    - `__nookJavaReceiverHandle = '0x0'`
    - `__jptr = '0x0'`
    - `__nookJavaOwnedHandle = false`

This keeps the post-dispose behavior aligned with existing invalid-handle error paths instead of inventing a new wrapper state machine.

## Testing Strategy

Desktop regression should cover:

- binding exists on instance wrappers
- binding is absent or falsey for class wrappers only if needed; otherwise it may exist but remain a no-op because ownership is false
- disposing an owned wrapper calls the new release dependency exactly once
- repeated `$dispose()` stays idempotent
- calling an instance method after disposal fails with the existing invalid-handle error

Android/device validation can wait until the desktop path is green, because this pass is mostly wrapper semantics plus a single JNI `DeleteGlobalRef()` call.

## Boundary

This pass intentionally stops after explicit disposal.

If we continue the Frida-alignment track, the natural follow-up is:

- automatic cleanup on script unload and/or wrapper finalization
