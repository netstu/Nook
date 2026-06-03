# Nook Java Env Wrapper Phase 6 Design

## Goal

Extend the `Env` wrapper with the next strict Frida-aligned string read pair:

- `env.getStringUtfChars(jstr)`
- `env.releaseStringUtfChars(jstr, cstr)`

Phase 6 should not also add auto-release helpers, callback wrappers, or higher-level convenience APIs.

## Why This Next

Phase 5 already proved that:

- `env.newStringUtf(str)` works
- Nook can create real JNI `jstring` values from JS text
- Android JNI calls remain safe when the actual JNI operation owns a live local `JavaEnv jenv`

The next useful step is to complete the minimal write/read string bridge with the matching UTF-8 access pair.

This matters because:

- Frida exposes these as explicit paired primitives
- later JNI text workflows depend on deterministic acquire/release semantics
- doing the raw pair first avoids baking Nook-specific convenience semantics into the core API

## Public Target

Phase 6 should support:

```javascript
Java.vm.perform(function () {
  var env = Java.vm.getEnv();
  var jstr = env.newStringUtf("hello");
  var cstr = env.getStringUtfChars(jstr);
  send({
    type: "send",
    payload: cstr.toString()
  });
  env.releaseStringUtfChars(jstr, cstr);
});
```

## Proposed Public Shape

### `env.getStringUtfChars(jstr)`

- accepts a `jstring` as a `NativePointer`
- returns a `NativePointer` to UTF-8 chars
- rejects null or non-pointer input

### `env.releaseStringUtfChars(jstr, cstr)`

- accepts the original `jstring` pointer and the returned UTF-8 pointer
- returns `true`
- rejects null or non-pointer input

Phase 6 should not auto-release on GC or script unload.

## Implementation Approaches Considered

### Option 1: Add only the strict pair

Pros:

- closest to Frida semantics
- ownership is explicit
- easiest to reason about and test

Cons:

- script authors must remember to release manually

### Option 2: Add the pair plus an auto-release helper

Pros:

- friendlier for casual scripts

Cons:

- introduces Nook-specific behavior too early
- hides lifetime semantics
- increases double-release and leak risk if mixed with manual release

### Option 3: Add only a high-level read helper and skip the raw pair

Pros:

- simpler script surface

Cons:

- not Frida-aligned
- removes explicit control over JNI ownership
- makes lower-level debugging harder

## Recommendation

Choose Option 1.

Reasoning:

- it keeps Nook aligned with Frida
- it establishes the primitive correctly before any convenience layer exists
- it makes correctness and ownership visible

## Android Lifetime Model

Phase 6 should keep the same corrected Android rule:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the actual call site
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to both `getStringUtfChars(...)` and `releaseStringUtfChars(...)`.

## `env.getStringUtfChars(jstr)`

Phase 6 should:

- require a non-null pointer-like `jstring`
- call JNI `GetStringUTFChars(...)`
- return the resulting char pointer as a `NativePointer`
- surface a clear error on failure

It should not:

- automatically copy the string into JS
- automatically release the returned chars

## `env.releaseStringUtfChars(jstr, cstr)`

Phase 6 should:

- require the original non-null `jstring`
- require the non-null char pointer returned earlier
- call JNI `ReleaseStringUTFChars(...)`
- return `true`

It should not:

- accept arbitrary pointers unrelated to a prior acquire path as a supported use-case
- silently ignore invalid arguments

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.getStringUtfChars(jstr)` returns a pointer-like value
- the runtime forwards the original `jstring` handle into the JNI helper
- `env.releaseStringUtfChars(jstr, cstr)` returns `true`
- the runtime forwards both pointers into the release helper
- invalid input is rejected for both methods

Use narrow test-only callbacks for:

- UTF-8 char acquisition
- UTF-8 char release

### Device smoke

Add a focused smoke proving:

- `env.newStringUtf("hello")` produces a `jstring`
- `env.getStringUtfChars(jstr)` returns a non-null char pointer
- the pointer can be rendered through `toString()`
- `env.releaseStringUtfChars(jstr, cstr)` returns `true`
- `env.exceptionCheck()` remains `false`

## Boundaries

Phase 6 does not attempt:

- auto-release helpers
- callback-style wrappers
- changing `Nook.Jni.readJStringUtf8(...)`
- JS string auto-conversion on `getStringUtfChars(...)`

## Success Criteria

- `env.getStringUtfChars(jstr)` works
- `env.releaseStringUtfChars(jstr, cstr)` works
- ownership remains explicit and Frida-aligned
- Android attach lifetime remains correct at the actual JNI call site
