# Nook Java Env Wrapper Phase 3 Design

## Goal

Extend the new `Env` wrapper with the smallest comparison helper that moves Nook closer to Frida:

- `env.isSameObject(a, b)`

Phase 3 should not also add `isInstanceOf(...)`, class-name helpers, or broader reference utilities.

## Why This Next

Phase 2 already proved that:

- `env.exceptionOccurred()` works
- `env.exceptionClear()` works
- `env.getObjectClass(obj)` works
- Android JNI calls are now safe when the actual JNI operation owns a live local `JavaEnv jenv`

The next useful step is to compare Java object identity correctly.

This matters because:

- raw handle equality is not enough once wrappers come from different JNI references
- Frida-style behavior should compare Java object identity, not pointer text
- later helpers like `isInstanceOf(...)` are easier to trust once object identity semantics are proven first

## Public Target

Phase 3 should support:

```javascript
Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new();
  var kept = Java.retain(instance);

  send({
    type: "send",
    payload: String(env.isSameObject(instance, kept))
  });
});
```

And:

```javascript
Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var left = TextFragment.$new();
  var right = TextFragment.$new();

  send({
    type: "send",
    payload: String(env.isSameObject(left, right))
  });
});
```

## Proposed Public Shape

### `env.isSameObject(a, b)`

- accepts two Nook Java object wrappers
- returns `true` when the two wrappers refer to the same Java object
- returns `false` when they refer to different Java objects
- rejects malformed non-Java-object input

Phase 3 should not broaden this into a generic `NativePointer` comparison API.

## Implementation Approaches Considered

### Option 1: Add `env.isSameObject(a, b)` directly to `Env`

Pros:

- matches the current incremental `Env` direction
- keeps comparison semantics where users already expect JNI helpers
- lets Nook compare real Java identity instead of wrapper-handle text

Cons:

- requires one more Java-wrapper argument parser path on the runtime side

### Option 2: Expose only a low-level `Nook.Jni.isSameObject(...)`

Pros:

- smaller public surface on `Env`
- simpler native routing in the short term

Cons:

- pushes callers toward lower-level APIs again
- moves away from the Frida-style `Env` direction already established in phases 1 and 2

### Option 3: Skip this and implement `isInstanceOf(...)` first

Pros:

- surfaces a more recognizable high-level helper sooner

Cons:

- depends on object/class argument validation at the same time
- makes debugging harder if object identity semantics are still not validated

## Recommendation

Choose Option 1.

Reasoning:

- it is the narrowest next step
- it directly addresses a real JNI identity semantic that raw wrapper handles cannot represent
- it is a better foundation for a later `isInstanceOf(...)` phase

## Android Lifetime Model

Phase 3 should keep the same corrected Android rule:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the actual JNI call site
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to `isSameObject(...)` too.

## `env.isSameObject(a, b)`

This is the right comparison helper to add first because it:

- validates Java identity semantics instead of pointer equality
- naturally fits the current wrapper-based API direction
- gives a strong real-device test by comparing an object wrapper to a retained wrapper of the same object

Phase 3 should:

- parse both arguments as Java object wrappers
- reject malformed input with a clear type error
- call JNI `IsSameObject(...)`
- return a JS boolean

Phase 3 should not yet add:

- null-handling expansion beyond the currently supported wrapper model
- class-wrapper comparison helpers
- object-to-class relationship helpers

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.isSameObject(obj, obj2)` returns the callback-provided boolean
- the runtime forwards both Java object handles into the JNI helper
- non-Java-object input is rejected

The host runtime should use one narrow test-only callback for:

- object identity comparison

Avoid building broader fake JNI behavior.

### Device smoke

Add a focused smoke proving:

- `env.isSameObject(instance, kept)` returns `true` for a retained wrapper of the same object
- `env.isSameObject(instance, other)` returns `false` for a different object

## Boundaries

Phase 3 does not attempt:

- `isInstanceOf(...)`
- class-name helpers
- reference lifetime helpers on `Env`
- generic pointer comparison helpers

## Success Criteria

- `env.isSameObject(a, b)` works
- same-object comparison does not collapse into raw handle-string equality
- Android attach lifetime remains correct at the actual JNI call site
- the `Env` wrapper remains small and ready for a later `isInstanceOf(...)` phase
