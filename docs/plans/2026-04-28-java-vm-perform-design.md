# Nook Java.vm.perform Design

## Goal

Add the first minimal `Java.vm` surface to Nook, starting with:

- `Java.vm.perform(fn)`

The immediate target is not full Frida `Java.vm` parity. The target is a small but correct VM-level execution entrypoint that can later become the foundation for `Java.performNow(fn)` and `Java.perform(fn)` alignment.

## Why This Next

Nook now already has a meaningful Java user-facing surface:

- `Java.ready(fn)`
- `Java.performNow(fn)`
- `Java.isMainThread()`
- `Java.scheduleOnMainThread(fn)`
- `Java.registerClass(...)`
- `Java.use(...)`, method invoke, overload selection, field access, `choose/cast/retain`

The next missing layer is architectural, not just another helper:

- there is still no explicit VM-level execution primitive
- current helpers mix together:
  - thread/VM availability
  - app/class-loader readiness
  - user-facing convenience

Frida exposes that layering much more clearly through `Java.vm` and `Java.perform(...)`.

So the next useful step is to introduce the smallest possible `Java.vm` entrypoint instead of immediately expanding more user-facing helpers.

## Public Target

Phase-1 should expose:

```javascript
Java.vm.perform(function () {
  var System = Java.use("java.lang.System");
  return System.currentTimeMillis();
});
```

Expected behavior in this phase:

- `Java.vm` exists as an object
- `Java.vm.perform` exists as a function
- it executes immediately
- it does not wait for app-ready or class-loader-ready lifecycle state
- it only guarantees that Java bridge execution is available on the current thread

## Proposed Semantics

### `Java.vm.perform(fn)`

- `fn` must be a function, otherwise throw `TypeError`
- execute synchronously
- do not queue through `Java.ready(...)`
- do not imply app lifecycle readiness
- do not imply application class-loader readiness
- if the underlying Java bridge cannot provide a usable Java environment, throw

In other words:

- `Java.vm.perform(fn)` solves VM/thread execution
- `Java.ready(fn)` solves lifecycle timing
- `Java.perform(fn)` can later be re-expressed as lifecycle gating plus VM execution

## Relationship To Existing APIs

### Phase-1

Do not change existing stable behavior yet:

- keep `Java.performNow(fn)` as-is
- keep `Java.perform(fn)` as-is
- add `Java.vm.perform(fn)` in parallel

### Intended Later Evolution

Once `Java.vm.perform(fn)` is stable:

- `Java.performNow(fn)` should become a thin wrapper over `Java.vm.perform(fn)`
- `Java.perform(fn)` should eventually combine:
  - readiness/lifecycle policy
  - `Java.vm.perform(fn)` for the actual execution step

That gives Nook a cleaner Frida-like layering without forcing a large refactor in this pass.

## Implementation Approaches Considered

### Option 1: JS-only bootstrap wrapper

Implementation shape:

- expose `Java.vm = { perform: function (fn) { ... } }` in bootstrap
- internally call `fn()` directly, or forward to current `Java.performNow(fn)`

Pros:

- smallest public API step
- low implementation cost
- easy to test

Cons:

- does not create a real lower-level VM boundary
- later `Java.performNow` / `Java.perform` refactors would still need another internal reshaping step

### Option 2: Minimal native VM bridge plus JS bootstrap surface

Implementation shape:

- add a minimal native callback entry for `Java.vm.perform(fn)`
- JS bootstrap handles only validation and public API shape
- native side guarantees entry into JS with Java bridge available

Pros:

- clearer layering
- better long-term Frida direction
- creates a real internal base for later refactors

Cons:

- slightly larger scope now
- requires runtime/native test coverage

### Option 3: Refactor `Java.performNow(fn)` first and export `Java.vm.perform(fn)` on top

Pros:

- reduces duplication in the short term

Cons:

- touches already stable public behavior immediately
- increases regression risk

## Recommendation

Choose Option 2.

Reasoning:

- the user explicitly wants long-term Frida alignment
- `Java.vm.perform(fn)` is more valuable as a true lower-level primitive than as a temporary alias
- this still keeps scope narrow because only `perform(fn)` is added, not a full `Env` API

## Architecture Direction

Phase-1 should add:

- a minimal native/runtime-backed VM execution helper
- a bootstrap-exposed `Java.vm` object
- `Java.vm.perform(fn)` with function validation and synchronous callback invocation

This phase should not add:

- `Java.vm.getEnv()`
- `Java.vm.tryGetEnv()`
- explicit attach state APIs
- detach control
- any `Env` wrapper surface

## Testing Strategy

### Host / desktop regression

Add coverage for:

- `typeof Java.vm === 'object'`
- `typeof Java.vm.perform === 'function'`
- non-function argument rejection
- immediate execution order
- callback can access minimal Java bridge functionality

The host tests should stay focused on VM-execution semantics, not broader lifecycle behavior.

### Device smoke

Add a dedicated smoke script proving:

- bindings exist
- callback fires immediately
- callback can call a safe framework Java API
- callback does not depend on `Java.ready(...)`

Good candidates:

- `java.lang.System.currentTimeMillis()`
- `android.app.ActivityThread.currentApplication()`

## Boundaries

This pass does not attempt:

- full Frida `Java.vm` parity
- `Env` object modeling
- lifecycle redesign of `Java.perform(fn)`
- class-loader policy changes
- main-thread policy changes

## Success Criteria

- `Java.vm.perform(fn)` exists and works on host tests
- device smoke proves the callback executes and Java bridge calls succeed
- no regression to existing `Java.performNow(fn)` or `Java.ready(fn)` behavior
- the runtime now has a clear Frida-aligned VM execution layer to build on
