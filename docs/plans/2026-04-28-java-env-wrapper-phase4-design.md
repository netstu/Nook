# Nook Java Env Wrapper Phase 4 Design

## Goal

Extend the `Env` wrapper with the next Frida-aligned relationship helper:

- `env.isInstanceOf(obj, klass)`

Phase 4 should not also add string class-name overloads, pointer-based class arguments, or extra class-name helpers.

## Why This Next

Phase 3 already proved that:

- `env.isSameObject(a, b)` works
- Java object wrappers can be compared by real JNI identity instead of raw handle text
- Android JNI calls remain safe when the actual JNI operation owns a live local `JavaEnv jenv`

The next useful step is object-to-class relationship checking.

This matters because:

- Frida users expect `Java.use("...")` class wrappers to participate in instance checks
- it builds naturally on the existing Java wrapper model
- it is the smallest next step after object identity before broader class/ref helpers

## Public Target

Phase 4 should support:

```javascript
Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new();

  send({
    type: "send",
    payload: String(env.isInstanceOf(instance, TextFragment))
  });
});
```

And:

```javascript
Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  var instance = TextFragment.$new();

  send({
    type: "send",
    payload: String(env.isInstanceOf(instance, LoginFragment))
  });
});
```

## Proposed Public Shape

### `env.isInstanceOf(obj, klass)`

- first argument must be a Nook Java object wrapper
- second argument must be a Java class wrapper returned by `Java.use(...)`
- returns `true` when the object is an instance of the class
- returns `false` otherwise
- rejects malformed input with a clear type error

Phase 4 should not accept:

- string class names
- `NativePointer` class refs
- `env.getObjectClass(...)` return values as the second argument

## Implementation Approaches Considered

### Option 1: Accept only Java object wrapper + Java class wrapper

Pros:

- closest to Frida usage style
- reuses Nook's existing class wrapper model
- keeps argument validation explicit and predictable

Cons:

- callers cannot directly feed `env.getObjectClass(...)` into this helper

### Option 2: Accept class wrapper or string class name

Pros:

- shorter script surface in some cases

Cons:

- adds implicit class resolution work
- moves away from Frida's higher-level wrapper-based API style
- increases regression surface too early

### Option 3: Accept class wrapper or class pointer

Pros:

- superficially composes with `env.getObjectClass(...)`

Cons:

- mixes high-level and low-level API shapes in one method
- makes validation and error reporting harder
- is less Frida-like

## Recommendation

Choose Option 1.

Reasoning:

- it is the cleanest Frida-aligned public shape
- it builds directly on wrapper semantics already proven by `Java.cast(...)`, `getObjectClass(...)`, and `isSameObject(...)`
- it keeps phase 4 focused on one relationship primitive

## Android Lifetime Model

Phase 4 should keep the same corrected Android rule:

- `env.handle` remains diagnostic only
- JNI work must happen while a local `JavaEnv jenv` is alive at the actual call site
- methods must not rely on a previously captured `JNIEnv*` remaining valid

This applies to `isInstanceOf(...)` too.

## `env.isInstanceOf(obj, klass)`

This is the right next helper because it:

- checks a real Java relationship instead of wrapper metadata
- validates the class-wrapper path already used elsewhere in the runtime
- stays narrow enough to debug cleanly if behavior differs from Frida

Phase 4 should:

- parse `obj` as a Java object wrapper
- parse `klass` as a Java class wrapper
- resolve the target class name from the wrapper
- perform JNI `IsInstanceOf(...)`
- return a JS boolean

Phase 4 should not yet add:

- a way to convert `env.getObjectClass(...)` pointers back into class wrappers
- class-name overloads
- interface/assignability helper APIs beyond this one method

## Testing Strategy

### Host / desktop regression

Add focused tests proving:

- `env.isInstanceOf(instance, TextFragment)` returns the callback-provided boolean
- the runtime forwards the object handle and target class name into the JNI helper
- non-Java-object input is rejected
- non-class-wrapper second input is rejected

The host runtime should use one narrow test-only callback for:

- object/class relationship checking

Avoid building broader fake JNI behavior.

### Device smoke

Add a focused smoke proving:

- `env.isInstanceOf(instance, TextFragment)` returns `true`
- `env.isInstanceOf(instance, LoginFragment)` returns `false`

## Boundaries

Phase 4 does not attempt:

- string class-name overloads
- class-pointer second arguments
- class-name helpers
- reference lifetime helpers on `Env`

## Success Criteria

- `env.isInstanceOf(obj, klass)` works
- the second parameter is validated as a Java class wrapper
- Android attach lifetime remains correct at the actual JNI call site
- the `Env` wrapper remains small and Frida-aligned
