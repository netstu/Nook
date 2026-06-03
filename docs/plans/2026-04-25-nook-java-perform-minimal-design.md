# Nook Minimal Java.perform Design

**Date:** 2026-04-25

**Status:** Approved

## Goal

Add the first Frida-style Java scripting path to `Nook` on Android:

- `Java.perform(fn)`
- `Java.use(className)`
- `Class.method.implementation = function (...) { ... }`

This first version is intentionally narrow. The target is not full Frida parity yet. The target is a stable, testable, end-to-end Java hook workflow that fits the existing `Nook` architecture and can be expanded later.

## Current Context

The project already has two important pieces:

1. a working QuickJS-based script runtime in `src/agent_runtime/js_runtime.cpp`
2. a native Java hook framework API in `include/nook/NookJavaHook.h` and `src/framework/NookJavaHook.cpp`

The recent native-hook work also established a pattern that should be reused here:

- keep the host protocol unchanged when possible
- keep the JS API Frida-like
- keep the first implementation narrow
- push complexity into a small bridge layer instead of bloating `js_runtime.cpp`

That means the first Java API should not try to model the entire Frida Java object system. It should only cover the smallest path that proves the runtime architecture is correct.

## User Constraints

The approved constraints for the first version are:

- support Android only
- prioritize “can run on device now” over breadth
- prefer the smallest viable API surface
- make later Frida-style expansion possible without rewriting the core

Explicitly out of scope for the first version:

- overload selection
- fields
- constructors
- `Java.choose`
- full object/class reflection
- complete Frida-compatible method wrappers

## Approaches Considered

### Approach 1: Minimal wrapper over existing `NookJavaHook*`

Implement:

- `Java.perform(fn)` as a runtime initialization gate
- `Java.use(className)` as a lightweight JS class wrapper
- `implementation` assignment as a deferred Java hook install

Pros:

- smallest change set
- reuses existing Java hook internals
- fastest path to on-device validation

Cons:

- API is narrow
- some Frida semantics must wait for later phases

### Approach 2: Build a full Java object model first

Implement:

- class wrappers
- method wrappers
- overload metadata
- instance wrappers
- original-call dispatch

Pros:

- better long-term surface immediately
- cleaner future semantics

Cons:

- much larger first step
- slower to debug
- increases risk before the basic bridge is proven

### Approach 3: Push Java API translation into the host

Have `nook-cli` or host Python translate high-level Java scripting into low-level RPC or attach requests.

Pros:

- smaller device-side change

Cons:

- breaks the Frida-style mental model
- splits semantics across host and agent
- becomes harder to maintain later

## Recommendation

Adopt **Approach 1**.

This is the best match for the current project stage:

- it gives a real `Java.perform()` entrypoint quickly
- it reuses the Java hook runtime that already exists
- it keeps the first bridge small enough to test thoroughly
- it does not block future growth into overloads and richer wrappers

## First-Version API Shape

### `Java.perform(fn)`

Behavior:

- ensures Java hook runtime is initialized
- invokes `fn` immediately once runtime is ready
- throws a clear error if Java hook support is unavailable in the current process

The first version does not need queueing, delayed callback replay, or thread hopping. It only needs a safe and explicit “Java runtime available now” gate.

### `Java.use(className)`

Behavior:

- returns a JS class wrapper for the requested class name
- class lookup is lazy at method-install time, not full reflection time
- wrapper exposes method placeholders through property access

The first version does not enumerate all methods. Instead, unknown property access should produce a method wrapper shell keyed by:

- class name
- method name

This keeps the implementation small and still supports the target script style.

### `Class.method.implementation = fn`

Behavior:

- installs a deferred Java hook for the named method
- stores the JS replacement callback in the runtime
- returns no special value beyond normal property assignment semantics

The first version assumes method-name uniqueness for the target class. If method resolution is ambiguous, installation should fail with a clear error saying overload selection is not implemented yet.

### Original method call

The first version uses:

```javascript
this.methodName.callOriginal(...)
```

instead of full Frida-style direct recursive dispatch.

Reason:

- it avoids ambiguous first-version semantics
- it makes recursion control explicit
- it is much easier to implement and test

This is an intentional first-version deviation from Frida.

## Internal Architecture

### 1. `js_runtime.cpp` owns the JS surface

`src/agent_runtime/js_runtime.cpp` should expose:

- global `Java`
- `Java.perform`
- `Java.use`
- wrapper objects for Java classes and Java methods

But `js_runtime.cpp` should not directly own Java hook installation state. It should only:

- parse JS requests
- allocate wrapper objects
- route operations into a dedicated Java bridge

### 2. New Java JS bridge layer

Add a new agent-runtime bridge, conceptually parallel to `nook_native_js_bridge`:

```text
src/agent_runtime/
  nook_java_js_bridge.h
  nook_java_js_bridge.cpp
```

Responsibilities:

- assign Java JS hook ids
- register JS callback metadata for installed Java hooks
- adapt `NookJavaHookHookDeferred(...)` to the JS runtime
- provide a `callOriginal(...)` path
- translate bridge-side events into a form the JS runtime can dispatch

This bridge should also expose injectable dependencies for unit tests, so most of the first version can be tested without bringing up real ART hook machinery.

### 3. Runtime callback dispatch stays agent-local

The Java hook callback should not go out to the host and back. It should follow the same core principle as the native runtime:

- hook fires in target process
- event is bridged inside agent runtime
- JS callback runs in the same agent runtime

That keeps latency low and preserves the Frida-like scripting model.

## Value Conversion Boundary

The first version should keep conversion narrow and explicit.

Input arguments to JS:

- `java.lang.String` -> JS string
- primitive `boolean` -> JS boolean
- primitive integer types -> JS number
- all other object references -> opaque lightweight Java object wrapper

Return values from JS:

- JS string -> Java `String` when method return type is `String`
- JS boolean -> Java `boolean`
- JS integer/number -> matching primitive integer return path
- `null` -> Java null reference for object return types

If a value cannot be converted safely in the first version, the hook should fail with a clear runtime error instead of guessing.

## Method Resolution Rule

The first version must define one simple rule:

- if exactly one method with the requested name exists on the target class, use it
- otherwise fail and report that overload resolution is not implemented yet

This rule keeps behavior deterministic and avoids silently hooking the wrong overload.

## Error Handling

The first version should use explicit errors for the following cases:

- `Java` API used on unsupported platform
- Java runtime initialization failure
- class not found
- method not found
- multiple overloads with same name
- unsupported argument conversion
- unsupported return conversion
- `callOriginal(...)` used on a method wrapper without an active invocation context

Errors should be worded so the next missing feature is obvious, for example:

- `Java.use overload resolution is not implemented yet for com.demo.Foo.bar`
- `Java hook return conversion not implemented for type '[B'`

## Testing Strategy

### Unit-level runtime tests

Primary target:

- extend `tests/communication/test_js_runtime_native_attach.cpp`

The first version should add tests that prove:

1. `Java.perform` exists and invokes its callback
2. `Java.use` exists and returns a wrapper object
3. assigning `.implementation` routes through a bridge install function
4. duplicate hook install cleanup works on unload
5. `callOriginal(...)` routes through bridge callback trampoline
6. unsupported overload case throws the expected error

### Bridge tests

Add a focused bridge test file:

```text
tests/communication/test_java_js_bridge.cpp
```

It should cover:

- hook id assignment
- callback registration
- bridge dependency injection
- original-call dispatch
- cleanup and unhook

### On-device smoke test

Add:

```text
host/nook-py/java_perform_smoke.js
```

Expected smoke workflow:

```javascript
Java.perform(function () {
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  LoginFragment.verifyPasswordNative.implementation = function (password) {
    send({ type: "send", payload: "java-hook:" + password });
    return this.verifyPasswordNative.callOriginal(password);
  };
});
```

This is the minimum success signal for the first phase.

## Build and Integration Impact

Expected build updates:

- add the new Java JS bridge source file(s) to the desktop/unit-test compile path
- add the same bridge source file(s) to `build/android/Android.mk`

No protocol change is required for the first version.

No host CLI feature change is required beyond adding a new smoke script and docs.

## Out of Scope

Do not include these in the first implementation:

- overload API
- constructor wrapping
- field read/write
- `Java.choose`
- Java class enumeration
- automatic direct `this.method(...)` original dispatch
- generalized object proxy system

## Success Criteria

The first version is complete when all of the following are true:

1. `Java.perform` and `Java.use` exist in the agent JS runtime
2. a script can assign `.implementation` for `com.demo.target.LoginFragment.verifyPasswordNative`
3. the JS replacement callback runs on device
4. `this.verifyPasswordNative.callOriginal(...)` returns the original result
5. the script can be loaded, unloaded, and reloaded cleanly
6. ambiguity fails clearly instead of silently choosing an overload

## Next Step After This Design

After this minimal path is stable, the next phase should be:

1. overload selection
2. better object wrappers
3. direct method-call parity improvements
4. field support
5. `Java.choose`

That sequence keeps the architecture incremental while moving closer to Frida semantics.
