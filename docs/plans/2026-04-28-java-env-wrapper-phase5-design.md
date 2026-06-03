# Nook Java Env Wrapper Phase 5 Design

## Goal

Extend the `Env` wrapper with the next smallest Frida-aligned string helper:

- `env.newStringUtf(str)`

Phase 5 should not also add:

- `env.getStringUtfChars(...)`
- `env.releaseStringUtfChars(...)`
- a broader `Env` string read surface

## Why This Next

Phase 4 already proved that:

- `env.isInstanceOf(obj, klass)` works
- object/class relationship helpers can stay loader-aware and Frida-aligned
- Android JNI calls remain safe when the actual JNI operation owns a live local `JavaEnv jenv`

The next useful step is to create a real JNI `jstring` from JavaScript.

This matters because:

- string creation is a foundational JNI primitive
- later helpers around exception text, class names, and broader JNI interop will need a reliable write-side string primitive
- it is a narrower and safer next step than introducing `GetStringUTFChars` / release-pair semantics immediately

## Public Target

Phase 5 should support:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();
  var jstr = env.newStringUtf("hello");
  send({
    type: "send",
    payload: jstr.toString()
  });
});
```

## Proposed Public Shape

### `env.newStringUtf(str)`

- accepts one JS string
- returns a `NativePointer` for the resulting local `jstring`
- rejects non-string input
- does not attempt to auto-wrap the result as a Java object wrapper

Phase 5 should not reinterpret this return value as a class wrapper or object wrapper.

## Existing `Nook.Jni.readJStringUtf8(...)` Boundary

Phase 5 should explicitly keep the current `Nook.Jni.readJStringUtf8(...)` boundary unchanged:

- it still exists at the JS API layer
- it is still intentionally guarded in the current async native-hook runtime
- phase 5 does not broaden it into a newly safe general-purpose read bridge

This means phase 5 is a write-side string primitive only.

## Implementation Approaches Considered

### Option 1: Add only `env.newStringUtf(str)`

Pros:

- smallest safe Frida-aligned next step
- avoids introducing a half-finished `GetStringUTFChars` ownership model
- keeps phase 5 focused on one JNI primitive

Cons:

- string creation is validated more narrowly on device than a full write+read roundtrip

### Option 2: Add `env.newStringUtf(str)` and `env.getStringUtfChars(...)`

Pros:

- gives a more symmetrical string bridge immediately

Cons:

- `GetStringUTFChars` without its release pair is incomplete
- ownership/lifetime mistakes become much easier
- it broadens the phase beyond the agreed minimum

### Option 3: Rework `Nook.Jni.readJStringUtf8(...)` into a new fully safe synchronous read bridge first

Pros:

- stronger roundtrip validation story

Cons:

- changes a previously documented guarded boundary
- risks conflating `Env` expansion with async native-hook runtime constraints
- much larger regression surface

## Recommendation

Choose Option 1.

Reasoning:

- it is the narrowest Frida-aligned next step
- it keeps the string bridge incremental
- it avoids dragging async hook/runtime safety concerns into the same phase

## Android Lifetime Model

Phase 5 should keep the same corrected Android rule:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the actual call site
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to `newStringUtf(...)` too.

## `env.newStringUtf(str)`

This is the right next helper because it:

- creates a real JNI string handle from JS text
- stays read-only from the JS caller's ownership perspective
- provides a foundation for later, more complete string helpers

Phase 5 should:

- require a JS string argument
- call JNI `NewStringUTF(...)`
- return the resulting `jstring` as a pointer-like wrapped value
- surface a clear error if JNI string creation fails

Phase 5 should not yet add:

- `env.getStringUtfChars(...)`
- `env.releaseStringUtfChars(...)`
- a new `Env` string roundtrip convenience helper

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.newStringUtf("hello")` returns a pointer-like value
- the runtime forwards the exact source string into the JNI helper
- non-string input is rejected

The host runtime should use one narrow test-only callback for:

- UTF-8 string creation

Avoid building broader fake JNI behavior in this phase.

### Device smoke

Add a focused smoke proving:

- `env.newStringUtf("hello")` returns a non-null pointer-like value
- `env.exceptionCheck()` remains `false` after creation

Device smoke should not depend on changing the current guarded semantics of `Nook.Jni.readJStringUtf8(...)`.

## Boundaries

Phase 5 does not attempt:

- `env.getStringUtfChars(...)`
- `env.releaseStringUtfChars(...)`
- changing `Nook.Jni.readJStringUtf8(...)` from its documented guarded behavior
- string-to-wrapper conversion helpers

## Success Criteria

- `env.newStringUtf(str)` works
- non-string input is rejected
- Android attach lifetime remains correct at the actual JNI call site
- the current `Nook.Jni.readJStringUtf8(...)` boundary remains explicit and unchanged
