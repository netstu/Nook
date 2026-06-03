# Nook Java.performNow Refactor Design

## Goal

Refactor `Java.performNow(fn)` so it is no longer its own direct execution path and instead becomes a thin immediate-execution wrapper over:

- `Java.vm.perform(fn)`

This is the final cleanup step needed to unify Nook's Java execution layering after introducing `Java.vm.perform(fn)` and refactoring `Java.perform(fn)`.

## Why This Next

Nook now already has:

- `Java.vm.perform(fn)`
- `Java.perform(fn)` expressed through:
  - `Java.ready(fn)`
  - `Java.vm.perform(fn)`

The remaining duplicate execution path is:

- `Java.performNow(fn)`

If that still calls `fn()` directly, then Nook continues to carry two different immediate Java execution mechanisms:

- `Java.vm.perform(fn)`
- `Java.performNow(fn)`

That duplication is unnecessary and weakens the architecture.

## Public Target

The public API remains unchanged:

```javascript
Java.performNow(function () {
  var System = Java.use("java.lang.System");
  return System.currentTimeMillis();
});
```

The public behavior should remain unchanged:

- non-function input throws
- callback executes immediately
- callback does not wait for `Java.ready(...)`

What changes is the internal delegation path.

## Proposed Layering

After this refactor:

- `Java.vm.perform(fn)`
  - the single VM/thread execution base
- `Java.performNow(fn)`
  - a thin immediate wrapper over `Java.vm.perform(fn)`
- `Java.perform(fn)`
  - `Java.ready(...)` + `Java.vm.perform(fn)`

That gives one clean execution core and two user-facing convenience layers.

## Proposed Semantics

### `Java.performNow(fn)`

Phase-1 refactor behavior:

- `fn` must be a function, otherwise throw `TypeError`
- call `Java.vm.perform(fn)` immediately
- do not queue through `Java.ready(...)`

Equivalent shape:

```javascript
Java.performNow = function (fn) {
  if (typeof fn !== "function") {
    throw new TypeError("Java.performNow requires a function");
  }

  return Java.vm.perform(fn);
};
```

## Implementation Approaches Considered

### Option 1: Keep `performNow(fn)` independent

Pros:

- zero churn

Cons:

- preserves duplicate execution logic
- weakens the new `Java.vm.perform(fn)` foundation

### Option 2: Refactor `performNow(fn)` into a thin wrapper over `Java.vm.perform(fn)`

Pros:

- cleanest layering
- minimal code change
- no public API change

Cons:

- requires explicit tests proving delegation

### Option 3: Remove `performNow(fn)` entirely

Pros:

- simplest internal surface

Cons:

- breaks existing users
- not aligned with current staged Frida-compatibility approach

## Recommendation

Choose Option 2.

Reasoning:

- it completes the layering cleanup without changing public shape
- it gives Nook one clear VM execution primitive
- it is the natural follow-up after the `Java.perform(fn)` refactor

## Testing Strategy

### Desktop regression

Cover:

- non-function rejection still works with the same error text
- immediate execution behavior still works
- internal execution now routes through `Java.vm.perform(fn)`

The delegation test is the most important one.

### Device smoke

Re-run the existing `java_perform_now_smoke.js` or a focused equivalent to verify:

- public behavior did not change
- Java framework access still works

## Boundaries

This pass does not attempt:

- broader `Java.vm` APIs
- changes to `Java.ready(fn)`
- changes to main-thread helpers
- changes to `Java.perform(fn)` behavior

## Success Criteria

- `Java.performNow(fn)` still behaves the same externally
- internally it delegates to `Java.vm.perform(fn)`
- Nook now has a single VM execution core for Java callback execution
