# Nook Java.cast Minimal Design

## Goal

Add a minimal Frida-style `Java.cast(value, Klass)` API to Nook so an existing Java object wrapper can be re-wrapped with a target class view and used with that class's method and field wrappers.

## Scope

This pass intentionally implements only the smallest useful `Java.cast()` behavior:

- accept an existing Java object wrapper as the first argument
- accept a `Java.use("...")` class wrapper as the second argument
- return a new Java instance wrapper that:
  - preserves the original Java object handle
  - exposes the target class name
  - resolves later method and field access using the target class

This pass does not include:

- JNI `instanceof` or subclass validation
- `retain()` or object lifetime changes
- `choose()` or heap enumeration
- implicit `null` pass-through semantics

## Current Runtime Shape

Relevant current behavior in `src/agent_runtime/js_runtime.cpp`:

- `Java.use(className)` builds a wrapper through `CreateJavaUseWrapper(...)`
- `MakeJavaJsValue(...)` converts Java object results into wrappers by calling `CreateJavaUseWrapper(ctx, class_name, object_handle)`
- `ParseJavaJsValue(...)` recognizes Java wrappers by reading:
  - `$className`
  - `__nookJavaReceiverHandle` or the legacy Java object pointer property
- direct Java invocation and field access already depend on wrapper metadata rather than a separate object model

This means `Java.cast()` can stay small: it only needs to parse one object wrapper, parse one class wrapper, and call the same wrapper factory with the old handle and the new class name.

## API Shape

Target API:

```javascript
Java.perform(function () {
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var casted = Java.cast(this, TextFragment);
  return casted.formatBalance(10.0);
});
```

Behavior:

- `Java.cast(value, Klass)` returns a new wrapper
- the original wrapper is not mutated
- the returned wrapper reuses the original `jobject` handle
- the returned wrapper uses `Klass.$className` as the new wrapper class

## Error Handling

Hard-fail rules:

- fewer than two args:
  - `TypeError: Java.cast requires object and class wrapper`
- first arg is not a Java object wrapper:
  - `TypeError: Java.cast object must be a Java object wrapper`
- first arg resolves to `null` / receiver handle `0`:
  - `TypeError: Java.cast object handle is invalid`
- second arg is not a class wrapper:
  - `TypeError: Java.cast target must be a Java class wrapper`
- wrapper creation failure:
  - internal error, consistent with `Java.use(...)`

No implicit downgrade or fallback is planned in this pass.

## Validation Strategy

### Desktop regression tests

Add tests in `tests/communication/test_js_runtime_native_attach.cpp` for:

- `Java.cast` binding exists on `Java`
- successful cast preserves the original receiver handle
- successful cast switches `$className` to the target class wrapper's class
- a casted wrapper can directly invoke a target-class method
- invalid object arg throws
- invalid class-wrapper arg throws

### Device smoke

Add one simple smoke script:

- `host/nook-py/java_cast_smoke.js`

The smoke should use an existing callback receiver object, cast it back to its own class wrapper, and invoke a known method:

```javascript
Java.ready(function () {
  var TextFragment = Java.use("com.demo.target.TextFragment");
  TextFragment.initView.overload("android.view.View").implementation = function (view) {
    var casted = Java.cast(this, TextFragment);
    send("java-cast-result:" + String(casted.formatBalance(10.0)));
    return this.initView.callOriginal(view);
  };
});
```

This does not prove hierarchy-aware casting, but it does prove the API surface, handle preservation, and recast-driven method lookup on a real device.

## Boundary

This design is intentionally narrower than Frida's long-term Java object model.

It is still worth doing now because it unlocks the most common near-term usage:

- receive a Java object wrapper from callback/result
- reinterpret it as a known target class
- call target class methods and fields through the existing wrapper system

If later work needs stricter safety, `instanceof` validation can be layered on top without changing the JS API shape.
