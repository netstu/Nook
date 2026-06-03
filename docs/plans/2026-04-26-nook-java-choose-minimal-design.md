# Nook Java.choose Minimal Design

## Goal

Add a minimal Frida-style `Java.choose(className, callbacks)` API to Nook so scripts can actively enumerate live Java objects of a target class and receive them as existing Java wrappers.

## Scope

This pass intentionally implements only the smallest useful `Java.choose()` behavior:

- accept a class name string
- accept a callbacks object with:
  - `onMatch(instance)`
  - `onComplete()`
- on Android, enumerate matching instances for the target class
- wrap each match as the current Java wrapper type and pass it to `onMatch`
- call `onComplete` once enumeration finishes, even if there were zero matches

This pass does not include:

- `stop` / early terminate semantics
- additional filters
- class-loader controls exposed to JS
- batch return values
- non-Android runtime support

## Current Runtime Shape

Relevant current behavior:

- `Java.use(...)` already builds lazy class and instance wrappers through `CreateJavaUseWrapper(...)`
- `Java.cast(...)` and `Java.retain(...)` already proved the wrapper factory can be reused for repackaging live objects
- the Java bridge already has enough JNI and ART infrastructure to resolve classes and convert `jobject` results into wrapper-shaped `JavaJsValue`

The missing piece is object enumeration. The first implementation should keep that behind a narrow native-side dependency and avoid exposing heap-walker details to JS.

## API Shape

Target API:

```javascript
Java.choose("com.demo.target.TextFragment", {
  onMatch(instance) {
    send("match:" + instance.$className);
  },
  onComplete() {
    send("complete");
  }
});
```

Behavior:

- `className` is a string
- `callbacks` is an object
- `onMatch` is required
- `onComplete` is required in this minimal pass for deterministic completion reporting
- return value is `undefined`

## Error Handling

Hard-fail rules:

- missing `className`:
  - `TypeError: Java.choose requires class name and callbacks`
- `className` is not a string:
  - `TypeError: Java.choose class name must be a string`
- `callbacks` is not an object:
  - `TypeError: Java.choose callbacks must be an object`
- `onMatch` missing or not a function:
  - `TypeError: Java.choose onMatch must be a function`
- `onComplete` missing or not a function:
  - `TypeError: Java.choose onComplete must be a function`
- non-Android runtime:
  - `InternalError: Java.choose is only available on Android`
- native enumeration failure:
  - `InternalError` with native-side error text

## Implementation Shape

Recommended first implementation: hybrid minimal version.

- JS layer:
  - validates arguments
  - passes enumeration results into callbacks
- native layer:
  - receives class name
  - enumerates matching live instances
  - returns object handles and class metadata

This keeps the JS API Frida-like without committing to a large ART-specific JS surface.

## Validation Strategy

### Desktop regression tests

Add tests for:

- binding exists: `typeof Java.choose === 'function'`
- argument validation errors
- with an injected fake dependency:
  - `onMatch` receives wrapped objects
  - multiple matches are supported
  - `onComplete` fires after all matches

Because desktop builds have no ART heap enumeration, tests should use a dependency callback that returns a controlled list of fake objects.

### Device smoke

Add one simple smoke script:

- `host/nook-py/java_choose_smoke.js`

Suggested validation shape:

```javascript
Java.ready(function () {
  Java.choose("com.demo.target.TextFragment", {
    onMatch(instance) {
      send({
        type: "send",
        payload: "java-choose-match:" +
                 instance.$className + ":" +
                 String(instance.formatBalance(10.0))
      });
    },
    onComplete() {
      send({ type: "send", payload: "java-choose-complete" });
    }
  });
});
```

This does not need to prove match counts or stop behavior. It only needs to prove:

- a live object can be found
- the object is surfaced as a normal wrapper
- direct method invocation on the match works
- completion is signaled

## Boundary

This pass is intentionally not the full Frida `Java.choose()` feature set.

It is still the right next step because the current object workflow now supports:

- `Java.use(...)`
- direct invoke
- `Java.cast(...)`
- `Java.retain(...)`

`Java.choose(...)` completes the minimal object workflow by adding an active discovery path without forcing a large heap-enumeration API design up front.
