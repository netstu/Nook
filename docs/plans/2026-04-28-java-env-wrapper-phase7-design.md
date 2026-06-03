# Nook Java Env Wrapper Phase 7 Design

## Goal

Extend the `Env` wrapper with the next smallest stable Frida-aligned reference management primitives:

- `env.newGlobalRef(obj)`
- `env.deleteGlobalRef(ref)`

Phase 7 should not also add:

- `env.newLocalRef(obj)`
- `env.deleteLocalRef(ref)`
- `env.newWeakGlobalRef(...)`
- `env.deleteWeakGlobalRef(...)`
- automatic ownership tracking for user-created global refs

## Why This Next

Phase 6 already proved that:

- `env.newStringUtf(str)` can return a stable reference for later use
- `env.getStringUtfChars(...)` and `env.releaseStringUtfChars(...)` work on-device
- Android JNI calls remain safe when the actual JNI operation owns a live local `JavaEnv jenv`

The next useful step is to expose the smallest stable explicit reference-lifetime primitive.

This matters because:

- global reference promotion is foundational JNI behavior
- Frida-style Java work often needs a stable reference that survives later calls
- this is practical immediately, unlike local refs in Nook's current `Env` call model

## Root-Cause Constraint

During the first phase 7 attempt, `newLocalRef(...)` / `deleteLocalRef(...)` caused crashes on device.

Root cause:

- each Nook `Env` method is a separate native/JNI entry
- `NewLocalRef(...)` created a local ref in one JNI call
- `DeleteLocalRef(...)` tried to consume it in a later independent JNI call
- JNI local refs are not a safe cross-call handle in this architecture

So phase 7 must be narrowed to global refs only.

## Public Target

Phase 7 should support:

```javascript
Java.perform(function () {
  var env = Java.vm.getEnv();
  var obj = Java.use("com.demo.target.TextFragment").$new();
  var globalRef = env.newGlobalRef(obj);
  send({
    type: "send",
    payload: globalRef.toString()
  });
  env.deleteGlobalRef(globalRef);
});
```

## Proposed Public Shape

### `env.newGlobalRef(obj)`

- accepts one non-null Java object/reference
- returns a `NativePointer` for the resulting global ref
- rejects non-object input
- does not auto-register the ref for script cleanup in this phase

### `env.deleteGlobalRef(ref)`

- accepts one non-null pointer-like reference
- deletes that global reference through JNI
- returns `true`
- rejects malformed or null input

## Ownership Boundary

Phase 7 should keep ownership explicit:

- Nook should not automatically track user-created global refs from `env.newGlobalRef(...)`
- the caller that creates a global ref is responsible for deleting it
- this matches the low-level Frida direction better than folding these refs into Nook's script-owned cleanup machinery immediately

The existing script auto-cleanup for internally-owned refs, such as the current `newStringUtf(...)` globalized result, stays unchanged.

## Android Lifetime Model

Phase 7 should keep the same corrected Android rule:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the actual call site
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to both phase 7 methods.

## Implementation Approaches Considered

### Option 1: Add only global-reference primitives

Pros:

- matches what is actually stable in Nook's current architecture
- maximizes practical usefulness
- avoids exposing a misleading local-ref surface

Cons:

- leaves the `Env` surface asymmetrical until the architecture changes or a different local-ref model is introduced

### Option 2: Keep local and global refs together

Pros:

- looks closer to Frida at the API checklist level

Cons:

- local refs are not safe cross-call handles in the current design
- already reproduced as a device crash path
- would knowingly expose a broken primitive

### Option 3: Add global and weak global refs together

Pros:

- reduces a future phase

Cons:

- weak refs add more subtle lifetime semantics
- larger regression surface
- unnecessary before the stable global-ref path is closed out cleanly

## Recommendation

Choose Option 1.

Reasoning:

- it is the smallest stable Frida-aligned step
- it matches Nook's current JNI execution model
- it avoids shipping an API that is correct only on paper

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.newGlobalRef(obj)` returns a pointer-like value
- the runtime forwards the original object handle into the global-ref helper
- `env.deleteGlobalRef(ref)` returns `true`
- invalid input is rejected for both methods

Use narrow test-only callbacks for:

- global-ref creation
- global-ref deletion

### Device smoke

Add a focused smoke proving:

- `env.newGlobalRef(obj)` returns a non-null pointer-like value
- `env.deleteGlobalRef(ref)` returns `true`
- `env.exceptionCheck()` remains `false`

## Boundaries

Phase 7 does not attempt:

- `env.newLocalRef(obj)`
- `env.deleteLocalRef(ref)`
- `env.newWeakGlobalRef(...)`
- `env.deleteWeakGlobalRef(...)`
- auto-cleanup for user-created global refs
- new Java object wrapper semantics for raw refs

## Success Criteria

- `env.newGlobalRef(obj)` works
- `env.deleteGlobalRef(ref)` works
- invalid inputs are rejected clearly
- Android attach lifetime remains correct at the actual JNI call site
- ownership of user-created global refs remains explicit
