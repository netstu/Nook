# Nook Java.retain Minimal Design

## Goal

Add a minimal Frida-style `Java.retain(obj)` API to Nook so a Java object wrapper received inside a callback can be promoted to a stable Android global reference and safely reused after the original callback returns.

## Scope

This pass intentionally implements only the smallest useful `Java.retain()` behavior:

- accept an existing Java object wrapper
- create an Android-side `NewGlobalRef(...)`
- return a new Java wrapper pointing at that retained global reference
- preserve the original wrapper's `$className`

This pass does not include:

- `Java.release(...)`
- automatic cleanup or object registry GC
- non-Android fallback behavior beyond a clear failure
- any hierarchy-aware or `cast`-specific extension

## Current Runtime Shape

Relevant current behavior:

- Java object wrappers are represented in JS by:
  - `$className`
  - `__nookJavaReceiverHandle`
  - `__jptr`
- `MakeJavaJsValue(...)` already converts Java object results into wrappers by calling:
  - `CreateJavaUseWrapper(ctx, class_name, object_handle)`
- `Java.cast(...)` already proved that wrapper re-packaging can stay small and reuse the same wrapper factory
- Android code in the repo already uses `NewGlobalRef(...)` / `DeleteGlobalRef(...)` in the deferred class-loader path, so the JNI primitive is already part of the codebase

This means `Java.retain()` can be implemented as:

1. parse an existing Java object wrapper
2. call a narrow Android-side retain helper
3. rebuild a new wrapper with the retained handle and original class name

## API Shape

Target API:

```javascript
Java.ready(function () {
  var kept = Java.retain(this);
});
```

Behavior:

- `Java.retain(obj)` returns a new wrapper
- the original wrapper is not mutated
- the returned wrapper keeps the same `$className`
- the returned wrapper points to a newly created Android global reference

## Error Handling

Hard-fail rules:

- no args:
  - `TypeError: Java.retain requires a Java object wrapper`
- first arg is not a Java object wrapper:
  - `TypeError: Java.retain requires a Java object wrapper`
- first arg has `object_handle == 0`:
  - `TypeError: Java.retain object handle is invalid`
- non-Android runtime:
  - `InternalError: Java.retain is only available on Android`
- `NewGlobalRef(...)` fails:
  - `InternalError` with the Android-side error string
- wrapper rebuild fails:
  - internal error, consistent with `Java.use(...)` / `Java.cast(...)`

## Validation Strategy

### Desktop regression tests

Add tests in `tests/communication/test_js_runtime_native_attach.cpp` for:

- `Java.retain` binding exists on `Java`
- invalid argument cases
- a fake retain dependency gets called with the original handle
- the returned wrapper:
  - has the same `$className`
  - is a distinct JS wrapper
  - contains the retained handle instead of the original handle

Because desktop tests do not have JNI, this pass should use dependency injection rather than direct Android behavior.

### Device smoke

Add one simple smoke script:

- `host/nook-py/java_retain_smoke.js`

Suggested validation shape:

```javascript
Java.ready(function () {
  var TextFragment = Java.use("com.demo.target.TextFragment");
  TextFragment.initView.overload("android.view.View").implementation = function (view) {
    var kept = Java.retain(this);
    var casted = Java.cast(kept, TextFragment);
    send("java-retain-result:" +
         kept.$className + ":" +
         String(kept !== this) + ":" +
         String(casted.formatBalance(10.0)));
    return this.initView.callOriginal(view);
  };
});
```

This validates:

- retain binding exists
- a new wrapper is returned
- the retained wrapper still works after being recast and invoked through the normal Java wrapper path

## Boundary

This pass is intentionally narrower than Frida's full Java object lifetime model.

It is still high value now because it unlocks the most common next step after `Java.cast(...)`:

- receive `this` or another object in a callback
- retain it for later use
- keep operating on it through `cast(...)`, method calls, and field access

If long-term leak control becomes necessary, the next phase should add explicit release semantics or a retained-object registry. That should stay separate from this pass.
