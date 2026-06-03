# Nook Java Wrapper GC/Finalizer Design

## Goal

Design the next Java wrapper lifetime step after:

- explicit `wrapper.$dispose()`
- deterministic cleanup on script unload / runtime shutdown

The target is Frida-like best-effort GC cleanup for owned Java wrappers without regressing the existing explicit and unload cleanup paths.

This document is design-only. It does not propose immediate implementation in the current pass.

## Current State

Nook already has:

- owned-wrapper tracking through `__nookJavaOwnedHandle`
- explicit release through `$dispose()`
- per-script unload/shutdown cleanup through `owned_java_handles`

That is enough for deterministic cleanup, but not enough for GC-driven cleanup when a script drops all JS references and keeps running.

## Problem

Two constraints make this non-trivial:

### Constraint 1: Current Java wrappers are pure JS objects

`CreateJavaUseWrapper(...)` currently builds wrappers from a JS factory string and returns a `Proxy` over a plain JS object. There is no native QuickJS class or opaque payload attached to the wrapper.

That means there is nowhere to attach a native QuickJS finalizer today.

### Constraint 2: Finalizer-time JNI release is risky

Even after adding a native wrapper class, directly calling `DeleteGlobalRef()` from a QuickJS finalizer is dangerous because:

- the finalizer may run during runtime teardown
- the current thread / JNI attachment state may not be what the Java bridge expects
- bridge dependencies may already be partially torn down
- finalizer ordering relative to script unload and runtime shutdown is not deterministic

So "finalizer calls JNI release immediately" is not a safe default design.

## What Frida Alignment Means Here

The useful Frida-aligned behavior is:

- explicit disposal still exists and stays deterministic
- unload/shutdown cleanup still exists and stays deterministic
- if a script drops the last JS reference to an owned wrapper while the runtime is still alive, Nook should eventually release the retained/global ref without requiring explicit `$dispose()`

The design does not need exact byte-for-byte Frida internals. It needs the same user-visible lifetime behavior with acceptable runtime safety.

## Approaches Considered

### Approach A: Keep current design and do not add GC finalizers

Only keep explicit `$dispose()` and unload/shutdown cleanup.

Pros:

- no runtime restructuring
- lowest risk

Cons:

- permanent Frida-compatibility gap for long-running scripts
- retained refs can survive much longer than necessary

### Approach B: Replace Java wrappers with a native QuickJS class and release immediately in the finalizer

Introduce a custom QuickJS class for Java wrapper state and call `ReleaseJavaObject(...)` straight from the finalizer when the wrapper is GC'd.

Pros:

- conceptually simple
- matches the intuition of "object dies, ref dies"

Cons:

- unsafe JNI / teardown timing
- hard to reason about interaction with existing unload cleanup
- larger regression surface

### Approach C: Hybrid record + deferred release queue

Introduce a native wrapper record that can participate in finalization, but make the finalizer only mark or enqueue release work. Actual JNI release happens later at a known-safe runtime point.

Pros:

- keeps finalizer small and side-effect-light
- preserves deterministic explicit/unload cleanup
- gives a path to GC cleanup without calling JNI from the finalizer

Cons:

- requires a wrapper architecture change
- adds queue / record lifecycle bookkeeping

## Recommended Design

Use Approach C.

## Architecture

### 1. Add a native Java wrapper record

Introduce an internal record owned by the JS runtime, for example:

- `JavaWrapperRecord`

Suggested fields:

- `class_name`
- `receiver_handle`
- `loader_handle`
- `owns_handle`
- `released`
- `script_id`
- `release_enqueued`

This record becomes the single native lifetime authority for owned Java wrapper state.

### 2. Add a QuickJS class for Java wrapper host objects

Introduce a native host object with:

- a QuickJS class id
- an opaque pointer to `JavaWrapperRecord`
- a finalizer

Important boundary:

- the public JS API can still expose a `Proxy` if needed for dynamic method/field lookup
- but that proxy must hold a strong reference to the native host object
- the finalizable ownership state must live in the host object, not only in plain JS properties

### 3. Make finalizers enqueue release instead of releasing directly

Finalizer behavior should be minimal:

- if the record is already released, no-op
- if the record is non-owning, no-op
- if release is already enqueued, no-op
- otherwise:
  - mark `release_enqueued = true`
  - put the record or handle into a pending-release queue in `RuntimeState`

The finalizer should not call JNI directly.

### 4. Drain pending Java wrapper releases at safe points

Add a runtime helper like:

- `DrainPendingOwnedJavaReleasesLocked(...)`

Drain points should be explicit and small:

- before or after script evaluation returns
- before or after dispatching inbound script messages
- before returning from RPC calls
- during `RemoveMessageHandler(...)`
- during `Shutdown()`

This keeps release work on the normal runtime control path, not inside QuickJS finalizer callbacks.

### 5. Preserve existing deterministic cleanup

Existing behavior should remain first-class:

- `$dispose()` still releases immediately
- unload/shutdown cleanup still releases everything left

Finalizer-driven cleanup is additive, not a replacement.

### 6. Unify state transitions around one record

Allowed transitions should be:

- `owned + live`
- `owned + release_enqueued`
- `released`

Once `released` is set:

- `$dispose()` becomes a no-op
- unload/shutdown cleanup skips the record
- finalizer skips the record

This is the key to preventing double release.

## Why Not Direct Finalizer JNI Release

Direct JNI release in the finalizer looks smaller, but it couples GC timing to Java bridge execution. That is the wrong direction in this runtime because:

- `JsRuntime::Shutdown()` already has explicit teardown order
- Java bridge dependencies can be reset during shutdown
- GC can run at inconvenient times

Deferring release preserves control over ordering.

## Data Structure Impact

The current `owned_java_handles` set is enough for unload cleanup but not enough for finalizers, because finalizers need per-wrapper state and de-duplication beyond just a raw handle value.

The likely direction is:

- keep `owned_java_handles` or replace it with a richer per-script map
- add:
  - wrapper record storage
  - pending-release queue

One reasonable shape is:

- `owned_java_records[script_id][record_id] -> JavaWrapperRecord`
- `pending_java_release_record_ids`

The exact container choice is less important than the state model:

- one owner record
- one released bit
- one enqueue bit

## Testing Strategy

Implementation should require both desktop and device validation.

Desktop coverage should verify:

- finalizer path enqueues release work for owned wrappers
- draining the queue releases a handle once
- `$dispose()` before finalization prevents queue-driven double release
- unload after queued release does not double release
- shutdown after queued release does not double release

Device validation should verify:

- explicit `$dispose()` still works
- `attach --wait` unload cleanup still works
- `spawn --wait` unload cleanup still works
- a long-running script that drops wrapper references can trigger eventual cleanup without unload

## Phased Rollout

### Phase 1: Wrapper host refactor only

- add native host object + wrapper record
- preserve current behavior
- do not add finalizer release yet

### Phase 2: Finalizer enqueue only

- add finalizer
- enqueue pending release work
- add queue drain helper

### Phase 3: Device validation and hardening

- verify long-running script behavior
- add diagnostics
- remove temporary diagnostics once stable

## Boundary

This design intentionally does not recommend:

- releasing JNI refs directly inside QuickJS finalizers
- broad cross-wrapper refcounting across all casted views
- changing non-owning class wrappers into owning wrappers

The minimal credible next step is:

- native wrapper host object
- deferred finalizer cleanup
- keep explicit and unload cleanup exactly as they already work
