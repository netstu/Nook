# Nook Java Env Wrapper Phase 8 Design

## Goal

Extend the `Env` wrapper with the next smallest stable Frida-aligned JNI reference primitives:

- `env.newWeakGlobalRef(obj)`
- `env.deleteWeakGlobalRef(ref)`

Phase 8 should not also add:

- `env.newLocalRef(obj)`
- `env.deleteLocalRef(ref)`
- weak-ref liveness probing helpers
- weak-ref resurrection helpers
- auto-cleanup for user-created weak global refs

## Why This Next

Phase 7 already proved that:

- the current `Env` call model can safely expose persistent JNI references
- `env.newGlobalRef(obj)` and `env.deleteGlobalRef(ref)` work in Nook's architecture
- the Android attach lifetime rule is correct when the actual JNI call happens while a local `JavaEnv jenv` is alive

Weak global refs are the next logical step because:

- they are part of Frida's low-level JNI-oriented direction
- they are persistent references, unlike local refs
- they fit Nook's current cross-call `Env` architecture better than local refs

## Constraint From Previous Phases

Local refs remain intentionally out of scope.

Root cause already established in phase 7:

- each Nook `Env` method is a separate native/JNI entry
- JNI local refs are not safe cross-call handles in this model
- exposing them would create a misleading API surface

Weak global refs do not have that same cross-call lifetime problem because they are persistent JNI references like global refs.

## Public Target

Phase 8 should support:

```javascript
Java.perform(function () {
  var env = Java.vm.getEnv();
  var obj = Java.use("com.demo.target.TextFragment").$new();
  var ref = env.newWeakGlobalRef(obj);
  send({
    type: "send",
    payload: ref.toString()
  });
  env.deleteWeakGlobalRef(ref);
});
```

## Proposed Public Shape

### `env.newWeakGlobalRef(obj)`

- accepts one non-null Java object/reference
- returns a `NativePointer` for the resulting weak global ref
- rejects non-object input
- does not auto-register the ref for script cleanup

### `env.deleteWeakGlobalRef(ref)`

- accepts one non-null pointer-like reference
- deletes that weak global reference through JNI
- returns `true`
- rejects malformed or null input

## Ownership Boundary

Phase 8 should keep ownership explicit:

- Nook should not automatically track user-created weak global refs
- the caller that creates a weak global ref is responsible for deleting it
- this stays close to Frida's low-level JNI surface

## Android Lifetime Model

Phase 8 keeps the same Android rule as phases 6 and 7:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the real call site
- methods must not rely on a previously captured `JNIEnv*`

## Implementation Approaches Considered

### Option 1: Add only the weak-global pair

Pros:

- smallest stable Frida-aligned step
- low regression surface
- keeps the API honest

Cons:

- leaves probing/resurrection behavior for a later phase

### Option 2: Add weak-global refs plus helper utilities

Pros:

- more feature-complete on paper

Cons:

- larger surface
- weaker parity story if semantics are not fully validated
- unnecessary before the raw JNI primitive is proven

### Option 3: Re-open local refs together with weak refs

Pros:

- broader checklist progress

Cons:

- repeats a known unsafe direction
- mixes a stable feature with an unstable one

## Recommendation

Choose Option 1.

Reasoning:

- it is directly useful
- it preserves the current architectural boundary
- it continues matching Frida in the same low-level order as the prior phases

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.newWeakGlobalRef(obj)` returns a pointer-like value
- the runtime forwards the original object handle into the weak-global helper
- `env.deleteWeakGlobalRef(ref)` returns `true`
- invalid input is rejected for both methods

Use narrow test-only callbacks for:

- weak-global creation
- weak-global deletion

### Device smoke

Add a focused smoke proving:

- `env.newWeakGlobalRef(obj)` returns a non-null pointer-like value
- `env.deleteWeakGlobalRef(ref)` returns `true`
- `env.exceptionCheck()` remains `false`

Do not assert GC behavior in this phase.

## Boundaries

Phase 8 does not attempt:

- `env.newLocalRef(obj)`
- `env.deleteLocalRef(ref)`
- weak-ref liveness queries
- weak-ref resurrection
- auto-cleanup for user-created weak refs
- new wrapper semantics for raw weak refs

## Success Criteria

- `env.newWeakGlobalRef(obj)` works
- `env.deleteWeakGlobalRef(ref)` works
- invalid inputs are rejected clearly
- Android attach lifetime remains correct at the real JNI call site
- user-created weak global refs remain explicitly caller-owned
