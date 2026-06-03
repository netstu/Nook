# Nook Script.bindWeak Design

## Goal

Align Nook's script/runtime lifecycle model with Frida's `Script.bindWeak()` family before implementing Java GC-driven wrapper cleanup.

The immediate target is not "QuickJS finalizer support" by itself. The target is a Frida-style weak binding facility that:

- invokes a callback when a JS value becomes unreachable
- also integrates with script unload
- can be reused by Java wrapper lifetime management and future language bindings

## Why This Comes Before Java GC Cleanup

Frida's public design is centered on script-level weak lifecycle hooks, not on exposing raw JS engine finalizers directly as the API surface.

For Nook, this matters because:

- Java wrappers are currently pure JS `Proxy` objects
- other future bindings may need weak cleanup too
- a generic runtime primitive is a better compatibility anchor than a Java-only finalizer trick

So the Frida-aligned design priority is:

1. `Script.bindWeak(...)`
2. `Script.unbindWeak(...)`
3. `Script.pin()` / `Script.unpin()` if needed for callback safety
4. Java wrappers consuming that facility

## Current State

Nook already has:

- explicit Java wrapper disposal through `$dispose()`
- deterministic cleanup on unload/shutdown

What it does not have:

- a runtime-level weak binding API
- a generic way to register "run this callback when this JS value dies"

## Frida Reference Direction

Frida's documented script lifecycle API includes:

- `Script.bindWeak(value, fn)`
- `Script.unbindWeak(valueOrId)` depending on surface/version
- `Script.pin()`
- `Script.unpin()`

For our purposes the important semantic points are:

- weak callbacks are owned by the script runtime, not by one feature
- unload and weak cleanup are part of the same lifecycle story
- language bindings can rely on this primitive instead of inventing ad-hoc finalizer behavior

## Recommended Design

Implement a generic `Script.bindWeak(...)` runtime primitive first.

Java GC-style wrapper cleanup should then be built on top of it.

## Proposed API Surface

Minimum phase:

- `Script.bindWeak(value, callback)`
- `Script.unbindWeak(token)`

Optional follow-up for stronger Frida parity:

- `Script.pin()`
- `Script.unpin()`

Suggested `bindWeak` behavior:

- accepts any JS object/function as `value`
- returns an opaque token id
- registers `callback` to run once when:
  - the bound value becomes unreachable
  - or the script is unloaded before that happens

Suggested `unbindWeak` behavior:

- removes a previously registered weak binding
- returns whether an active binding was removed

## Internal Architecture

### 1. Runtime-managed weak binding records

Add a per-script weak binding registry, for example:

- `WeakBindingRecord`

Suggested fields:

- `binding_id`
- `script_id`
- `target`
- `callback`
- `fired`
- `enqueued`

The target and callback may need to be held in a way compatible with QuickJS weak/finalizer behavior.

### 2. Finalizer is an implementation detail, not the API

Under the hood, Nook may still need:

- a native host object
- a QuickJS class
- a finalizer

But that should stay internal to `Script.bindWeak(...)`.

This keeps the user-facing model Frida-like and keeps future consumers decoupled from QuickJS-specific details.

### 3. Deferred callback dispatch

Weak callbacks should not do arbitrary runtime work directly inside a JS engine finalizer.

Instead:

- finalizer marks the binding as pending
- runtime enqueues the weak callback
- callback is dispatched later at safe runtime drain points

This is the same safety principle as deferred Java handle release.

### 4. Pin/unpin semantics

If we also implement `Script.pin()` / `Script.unpin()`, weak callbacks can safely prolong the script during deferred processing, which is consistent with Frida's lifecycle design.

Even if pin/unpin is deferred to a later pass, the weak binding implementation should avoid making that impossible.

## How Java Should Use It

Once `Script.bindWeak(...)` exists:

- owned Java wrappers register a weak binding when created
- the weak callback does not call JNI directly
- it enqueues the owned handle for safe deferred release
- existing `$dispose()` and unload/shutdown cleanup remain intact

This means Java GC cleanup becomes:

- a consumer of `Script.bindWeak(...)`
- not the owner of the lifetime abstraction

## Why This Is Better Than A Java-only Finalizer First

Pros:

- closer to Frida's public design
- reusable for non-Java bindings
- avoids baking QuickJS engine details into the Java API contract
- cleaner layering

Cons:

- bigger first step than adding one Java-specific finalizer

That tradeoff is worth it here because the goal is Frida alignment, not just "some GC cleanup exists".

## Testing Strategy

Phase 1 tests should focus on the generic primitive:

- bind weak callback to object
- drop references and trigger collection/drain
- callback fires once
- unload fires pending weak callbacks once
- `unbindWeak(...)` prevents callback firing

Phase 2 tests should focus on Java as a consumer:

- owned Java wrapper weak binding enqueues release
- weak cleanup plus `$dispose()` does not double release
- weak cleanup plus unload does not double release

## Rollout

### Phase 1: Generic weak binding runtime

- add `Script.bindWeak(...)`
- add `Script.unbindWeak(...)`
- add pending weak-callback queue + safe drain

### Phase 2: Optional pin/unpin parity

- add `Script.pin()` / `Script.unpin()` if needed for callback safety and closer Frida parity

### Phase 3: Java integration

- migrate Java GC cleanup design to consume `Script.bindWeak(...)`

## Boundary

This design deliberately changes the priority order:

- primary design target: Frida-style weak binding
- secondary implementation detail: QuickJS finalizers or equivalent engine hooks

If both are possible, the API should always be designed around the former.
