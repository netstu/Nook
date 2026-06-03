# Nook Java Field Access Design

**Date:** 2026-04-26

## Goal

Add the first minimal Frida-style Java field access surface to Nook so scripts can read and write common Java fields through:

```javascript
Java.perform(function () {
  var MainActivity = Java.use("com.demo.target.MainActivity");
  var value = MainActivity.interceptCount.value;
  MainActivity.interceptCount.value = 123;
});
```

## Scope

This design intentionally targets the smallest field API that is immediately useful and consistent with the current Java hook bridge:

- field wrapper shape: `Java.use(...).fieldName.value`
- supports static and instance fields
- supports these field types only:
  - `boolean`
  - `int`
  - `long`
  - `float`
  - `double`
  - `java.lang.String`

Out of scope for this step:

- arbitrary object field wrapping
- array fields
- field enumeration APIs
- constructors
- `Java.choose(...)`

## User-Facing API

### Static fields

```javascript
Java.perform(function () {
  var MainActivity = Java.use("com.demo.target.MainActivity");
  send({
    type: "send",
    payload: "before:" + String(MainActivity.interceptCount.value)
  });
  MainActivity.interceptCount.value = 77;
  send({
    type: "send",
    payload: "after:" + String(MainActivity.interceptCount.value)
  });
});
```

### Instance fields

Instance fields will be accessible through `this.<field>.value` inside an instance method hook callback:

```javascript
Java.perform(function () {
  var AdWallFragment = Java.use("com.demo.target.AdWallFragment");
  AdWallFragment.loadAd.implementation = function (adType, position) {
    var before = this.adCount.value;
    this.adCount.value = before + 1;
    return this.loadAd.callOriginal(adType, position);
  };
});
```

This avoids needing `Java.choose(...)` just to validate the first instance-field path.

## Architecture

### JS runtime layer

`CreateJavaUseWrapper(...)` currently assumes every unknown property is a method wrapper. That is enough for `Java.use(...).method.implementation`, but not for fields.

The new design keeps the lazy-proxy model and adds a second wrapper family:

- method wrappers:
  - existing behavior
- field wrappers:
  - `$className`
  - `$fieldName`
  - `$signature`
  - `$isStatic`
  - `$owner`
  - `.value` getter / setter

Resolution remains lazy. The first field access will resolve metadata through a native bridge helper and cache the result on the class wrapper or instance callback receiver.

### Native bridge layer

`nook_java_js_bridge.*` already owns Java method resolution and original-call bridging. Field access belongs in the same layer.

Add a minimal field bridge with:

- field metadata resolution
- field read
- field write
- existing scalar/string conversion reuse through `JavaJsValue`

Expected new record shape:

- class name
- field name
- JNI signature
- static flag

The bridge should use reflection as the resolution path so private demo-app fields remain usable in the same way current method hooks already rely on Java-side inspection.

## Type Conversion Rules

Supported field signatures map to current `JavaJsValue` kinds:

- `Z` -> `kBoolean`
- `I` -> `kInt32`
- `J` -> `kInt64`
- `F` -> `kFloat`
- `D` -> `kDouble`
- `Ljava/lang/String;` -> `kString`

For the JS side:

- booleans become JS booleans
- numeric fields become JS numbers
- strings become JS strings

No BigInt promise is made in this step. `long` stays on the current pragmatic JS-number path, matching the current method bridge boundary.

## Validation Strategy

### Local tests

Extend `tests/communication/test_js_runtime_native_attach.cpp` with:

- static field wrapper shape
- static field read
- static field write
- instance field wrapper shape
- instance field read/write inside a callback-owned receiver

### Device smoke

Add a focused smoke script using:

- static field: `MainActivity.interceptCount`
- instance field: `AdWallFragment.adCount` inside `loadAd(...)`

This gives one static and one instance validation target without requiring new runtime object-discovery APIs.

## Risks

### Property ambiguity

Today unknown properties are treated as methods. Adding field wrappers introduces ambiguity between:

- actual field names
- actual method names

The minimal design accepts that ambiguity and resolves lazily at use time:

- plain property access still returns a wrapper object
- method-specific behavior is only used when calling method APIs like `.implementation` / `.overload(...)`
- field-specific behavior is only used when reading or writing `.value`

This keeps the current API stable without inventing new syntax.

### Instance ownership

Instance field access outside a callback needs a receiver object. That receiver is not generally available yet.

So the first implementation will guarantee instance field support inside Java hook callbacks through `this.<field>.value`, while keeping direct out-of-band instance-object workflows for a later `Java.choose(...)` phase.

## Recommendation

Build the minimal `.value` field wrapper now, keep the type matrix narrow, and validate:

- one static field end-to-end
- one instance field inside a hook callback

That gives a real Frida-style usability gain without widening into object wrappers, constructors, or field enumeration yet.
