# Nook Java.perform Refactor Design

## Goal

Refactor `Java.perform(fn)` so it is built on top of the new `Java.vm.perform(fn)` primitive instead of maintaining an independent execution path.

The target is not to change the user-facing high-level behavior abruptly. The target is to clean up the layering so Nook moves closer to Frida's long-term Java execution model.

## Why This Next

Nook now has:

- `Java.ready(fn)`
- `Java.performNow(fn)`
- `Java.vm.perform(fn)`
- `Java.isMainThread()`
- `Java.scheduleOnMainThread(fn)`

That means the project finally has the pieces needed to separate:

- lifecycle readiness
- VM/thread execution
- user-facing convenience helpers

Right now `Java.perform(fn)` still represents an older mixed layer. The next useful step is to express it as:

- readiness gating via `Java.ready(fn)`
- execution via `Java.vm.perform(fn)`

This is the cleanest path toward Frida alignment without reopening a full Java API redesign.

## Public Target

The public API remains:

```javascript
Java.perform(function () {
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
});
```

The visible behavior should remain:

- non-function input throws
- if Java/app/class-loader is ready, callback effectively runs immediately
- otherwise callback runs later when ready

What changes is the internal execution path.

## Proposed Layering

After this refactor:

- `Java.vm.perform(fn)`
  - handles VM/thread-level execution only
- `Java.ready(fn)`
  - handles lifecycle/app/class-loader readiness only
- `Java.perform(fn)`
  - becomes the composition of those two layers

That gives a much cleaner model:

- `perform` is no longer a custom mixed primitive
- `vm.perform` becomes the single execution base
- later `performNow` can also be collapsed toward the same base if desired

## Proposed Semantics

### `Java.perform(fn)`

Phase-1 refactor behavior:

- `fn` must be a function, otherwise throw `TypeError`
- call `Java.ready(...)`
- when the ready callback fires, invoke `Java.vm.perform(fn)`

Equivalent shape:

```javascript
Java.perform = function (fn) {
  if (typeof fn !== "function") {
    throw new TypeError("Java.perform requires a function");
  }

  Java.ready(function () {
    return Java.vm.perform(fn);
  });
};
```

## Expected Behavior By Scenario

### Attach to already-ready app

- `Java.ready(...)` fires immediately
- `Java.vm.perform(fn)` runs immediately after that
- user-observed behavior remains effectively immediate

### Spawn before app-ready

- `Java.ready(...)` queues the callback
- after lifecycle/class-loader readiness is reached, the wrapped callback executes through `Java.vm.perform(fn)`
- user-observed behavior remains "run when ready"

### Error handling

- invalid `fn` input is rejected at `Java.perform(...)`
- execution failures inside the callback continue to use the existing JS exception path
- `Java.vm.perform(...)` remains responsible for VM/thread execution failures

## Implementation Approaches Considered

### Option 1: Keep `Java.perform(fn)` independent

Pros:

- smallest code churn

Cons:

- preserves duplicated semantics
- wastes the new `Java.vm.perform(...)` foundation
- makes future Frida alignment harder

### Option 2: Refactor `Java.perform(fn)` to compose `Java.ready(...)` and `Java.vm.perform(fn)`

Pros:

- clean layering
- minimal public behavior change
- directly follows the new architecture direction

Cons:

- requires care in tests so internal delegation is proven instead of only external behavior

### Option 3: Refactor both `Java.perform(fn)` and `Java.performNow(fn)` together

Pros:

- broader internal unification

Cons:

- larger regression surface
- not necessary for this step

## Recommendation

Choose Option 2.

Reasoning:

- it captures the intended architecture with minimal public churn
- it respects the user's "first `Java.vm`, then `Java.perform`" sequencing
- it gives immediate value without broadening into a full helper-family rewrite

## Testing Strategy

### Desktop regression

Cover:

- non-function rejection still works
- immediate behavior still works when ready path is immediate
- delayed behavior still works when ready path queues
- internal execution is now routed through `Java.vm.perform(...)`

The last point matters most, because otherwise the refactor would only preserve behavior without proving the new layering.

### Device smoke

Use the existing `Java.perform(...)` smoke or a focused new smoke to verify:

- bindings unchanged
- callback still executes
- basic Java access still works

Device smoke does not need to prove every lifecycle edge case if desktop regression already covers the internal delegation path.

## Boundaries

This pass does not attempt:

- a full rewrite of `Java.performNow(fn)`
- any `Java.vm.getEnv()` work
- changes to `Java.ready(fn)` lifecycle policy
- changes to main-thread helpers

## Success Criteria

- `Java.perform(fn)` user-facing behavior remains stable
- internal execution is delegated through `Java.vm.perform(fn)`
- host tests prove both behavioral compatibility and the new layering
- this becomes the new clean base for any later `Java.performNow(fn)` refactor
