# Nook Java.enumerateLoadedClasses ART Design

## Goal

Add a Frida-like `Java.enumerateLoadedClasses({ onMatch, onComplete })` API on Android, backed by ART loaded-class enumeration instead of the existing application `ClassLoader` shortcuts.

## Scope

This pass intentionally implements only the smallest useful loaded-class enumeration behavior:

- expose `Java.enumerateLoadedClasses(callbacks)`
- require:
  - `callbacks.onMatch(name)`
  - `callbacks.onComplete()`
- enumerate loaded Java classes on Android through an ART-side backend
- surface class names to JS as dot-style strings such as `com.demo.target.TextFragment`
- de-duplicate repeated classes before JS callback dispatch
- always call `onComplete()` once enumeration finishes, even when zero classes are found

This pass does not include:

- `Java.enumerateLoadedClassesSync()`
- `Java.enumerateClassLoaders()`
- JS-visible class-loader metadata
- early-stop semantics
- per-loader filtering
- non-Android support

## Why This Route

The user explicitly wants the more Frida-like route, not the simpler application-`ClassLoader`-only implementation.

Nook already has enough ART bootstrap state to make this realistic:

- `ArtInternals::RuntimeInstance`
- `ArtInternals::RunTimeSpec.classLinker`
- runtime / ClassLinker offset probing
- safe JNI string and class description helpers

So this pass should keep the JS API tiny while adding a native dependency that performs ART-backed class enumeration.

## API Shape

Target API:

```javascript
Java.enumerateLoadedClasses({
  onMatch(name) {
    send("class:" + name);
  },
  onComplete() {
    send("complete");
  }
});
```

Behavior:

- `callbacks` must be an object
- `onMatch` is required
- `onComplete` is required in this first pass
- `onMatch` receives one argument: the class name string
- return value is `undefined`

## Error Handling

Hard-fail rules:

- missing callbacks:
  - `TypeError: Java.enumerateLoadedClasses requires callbacks`
- callbacks is not an object:
  - `TypeError: Java.enumerateLoadedClasses callbacks must be an object`
- `onMatch` missing or not a function:
  - `TypeError: Java.enumerateLoadedClasses onMatch must be a function`
- `onComplete` missing or not a function:
  - `TypeError: Java.enumerateLoadedClasses onComplete must be a function`
- non-Android runtime:
  - `InternalError: Java.enumerateLoadedClasses is only available on Android`
- native enumeration failure:
  - `InternalError` with native-side error text

If zero classes are found:

- this is not an error
- `onComplete()` still fires

## Backend Shape

Recommended implementation split:

- JS runtime:
  - validates arguments
  - invokes a narrow native-side enumeration dependency
  - dispatches each class name into `onMatch`
  - dispatches `onComplete` once
- native bridge:
  - exposes `EnumerateLoadedJavaClasses(...)`
  - on Android, performs ART-backed enumeration
  - returns a de-duplicated vector of dot-style class names

This keeps the JS API Frida-like and avoids leaking ART structure details into scripts.

## Android Strategy

The Android backend should be a true ART-side route, not a plain `ClassLoader.loadClass(...)` approximation.

First-pass strategy:

- reuse runtime bootstrap state already detected by `JavaHook.cpp`
- find the relevant ART structures through `RuntimeInstance` and `classLinker`
- perform loaded-class traversal in native code
- convert each discovered class into a dot-style name using existing JNI helpers
- de-duplicate names before returning to JS

Important boundary:

- this pass aims to be Frida-like in architecture, but it does not need to re-create every Frida optimization or every ART-version fallback up front
- the requirement is “ART-backed loaded class enumeration that works on the test device,” not “byte-for-byte Frida parity”

## Validation Strategy

### Desktop regression

Desktop builds cannot run ART enumeration, so host tests should use an injected fake enumeration dependency.

Coverage should include:

- binding exists:
  - `typeof Java.enumerateLoadedClasses === 'function'`
- argument validation
- `onMatch` receives multiple class names
- duplicate native results are de-duplicated before JS callback dispatch
- `onComplete` fires after all matches

### Device smoke

Add:

- `host/nook-py/java_enumerate_loaded_classes_smoke.js`

Suggested validation shape:

```javascript
Java.ready(function () {
  send({
    type: "send",
    payload:
      "java-enum-classes-bindings:" +
      (typeof Java.enumerateLoadedClasses) + ":" +
      String(Java._invokeResolverVersion)
  });

  Java.enumerateLoadedClasses({
    onMatch(name) {
      if (name.indexOf("com.demo.target.") === 0) {
        send({ type: "send", payload: "java-enum-classes-match:" + name });
      }
    },
    onComplete() {
      send({ type: "send", payload: "java-enum-classes-complete" });
    }
  });
});
```

This smoke only needs to prove:

- the ART route runs successfully
- app classes are included in the result
- class names are stable dot-style strings
- completion is signaled

## Boundary

This pass intentionally stops at asynchronous loaded-class enumeration.

Once this lands, Nook’s Frida-like Java discovery workflow becomes much stronger:

- `Java.ready(...)`
- `Java.use(...)`
- `Java.choose(...)`
- `Java.enumerateLoadedClasses(...)`

That is enough to support a large portion of exploratory Android scripting before taking on sync enumeration, class-loader enumeration, or more complex class-factory behavior.
