# Nook Java Wrapper Automatic Cleanup Design

## Goal

Add the smallest safe follow-up to `wrapper.$dispose()`:

- automatically release owned Java wrapper handles when a script is unloaded
- automatically release owned Java wrapper handles when the script registry is cleared
- automatically release owned Java wrapper handles when the JS runtime shuts down

This pass is intentionally limited to deterministic runtime cleanup. It does not add GC finalizers or a full lifetime model.

## Problem

The previous `$dispose()` work added explicit release for owned Java wrappers, but cleanup still depended on scripts calling `$dispose()` manually.

That left two gaps:

- retained/global refs could survive until process exit if a script forgot to dispose them
- runtime shutdown paths already cleaned native callback allocations, but not owned Java wrapper handles

Frida-style usage expects explicit disposal to exist, but runtime teardown should still clean up owned references defensively.

## Scope

This pass covers:

- tracking owned Java wrapper handles per `script_id`
- de-duplicating repeated tracking of the same handle within one script
- unregistering handles on explicit `$dispose()`
- bulk release during:
  - `JsRuntime::RemoveMessageHandler(script_id, ...)`
  - `JsRuntime::Shutdown()`
  - `ScriptRegistry::Clear()` via existing `RemoveMessageHandler(...)`

This pass does not cover:

- QuickJS GC finalizers
- cross-script shared ownership/refcounting
- invalidating already-exported JS wrapper objects after script unload
- changing method invocation semantics after disposal beyond the existing invalid-handle path

## Approaches Considered

### Approach A: Track owned handles in `RuntimeState`

Record each owned wrapper handle under the current script, release them on unload/shutdown, and remove them from tracking on explicit `$dispose()`.

Pros:

- narrowest change
- matches existing `owned_allocations` cleanup pattern
- deterministic and easy to test

Cons:

- only cleans wrappers the runtime explicitly knows are owned

### Approach B: Add QuickJS finalizers

Attach native finalizers to wrapper objects and release handles when GC collects them.

Pros:

- more automatic

Cons:

- much riskier around JNI thread/runtime state
- harder to reason about ordering during shutdown
- larger regression surface

### Approach C: Add global refcounting across wrappers

Track every wrapper view of every Java handle and release only when all views are gone.

Pros:

- stronger lifetime model

Cons:

- too large for this step
- adds complexity before the current API surface is fully stabilized

## Recommended Design

Use Approach A.

Implementation shape:

- extend `RuntimeState` with a per-script owned-handle registry
- when `CreateJavaUseWrapper(...)` creates an owning wrapper:
  - register the handle to the current script
- when JS calls `__nookJavaRelease(...)`:
  - release the handle
  - unregister it from the current script
- during script cleanup:
  - release any remaining tracked handles exactly once
  - erase the script's tracking entry

De-duplication rules:

- the same handle tracked multiple times in one script should only be released once
- explicit `$dispose()` before unload must remove the handle from tracking so unload does not double-release it

Shutdown ordering:

- perform owned Java handle cleanup before clearing Java hook installer dependencies
- ignore per-handle cleanup failures during `Shutdown()` just like other best-effort teardown paths

## Testing Strategy

Desktop regression coverage should add four focused tests:

- unload releases an owned retained handle once
- `registry.Clear()` releases an owned retained handle once
- runtime `Shutdown()` releases an owned retained handle once
- explicit `$dispose()` before unload prevents double release

These tests should reuse the existing fake retain/release dependencies and validate exact release counts/handles.

## Boundary

This pass completes deterministic runtime cleanup for owned Java wrappers.

The next logical lifecycle step, if needed, is GC/finalizer-based cleanup. That should stay separate because it changes runtime and JNI interaction semantics substantially.
