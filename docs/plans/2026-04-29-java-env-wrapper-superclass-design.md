# Java Env superclass and assignability design

Date: 2026-04-29

## Goal

Add the next two low-risk Frida-aligned `Env` helpers:

- `env.getSuperclass(classWrapper)`
- `env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)`

These helpers must stay inside the current Nook `Env` architecture boundary and avoid any live-`JNIEnv*` lifetime assumptions.

## Why these two next

After the monitor rollback, the immediate rule is clear:

- keep adding helpers that remain valid across independent JNI re-entry
- do not add helpers whose semantics depend on one stable attached-thread session

`GetSuperclass` and `IsAssignableFrom` fit the safe bucket:

- both are single-shot JNI queries
- neither depends on local-frame lifetime
- neither requires a cross-call pair
- both are part of the low-level Frida/JNI surface

## Public shape

### `env.getSuperclass(classWrapper)`

- accepts one Java class wrapper
- returns another Java class wrapper when a superclass exists
- returns `null` when no superclass exists
- rejects non-class-wrapper input

### `env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)`

- accepts two Java class wrappers
- returns `true` / `false`
- follows JNI/Java `Class.isAssignableFrom(...)` semantics
- rejects non-class-wrapper input

## Input model

This phase stays strict:

- do not accept arbitrary object wrappers
- do not auto-convert objects to `getObjectClass(...)`

Reason:

- current `Env` helper semantics already distinguish object wrappers from class wrappers
- `isInstanceOf(...)` already expects an explicit class wrapper
- strict input keeps the API unambiguous and the tests narrow

## Returned class wrapper model

`getSuperclass(...)` must return a normal Nook Java class wrapper, not just a raw pointer.

Implementation approach:

- query the superclass `jclass`
- resolve its Java class name inside the same JNI call
- reuse the caller's loader handle for the returned wrapper
- construct the return value through the existing wrapper path

If the superclass is `null`, return JS `null`.

## Options considered

### Option 1: strict class-wrapper-only API

Pros:

- smallest surface
- least ambiguity
- matches existing wrapper conventions

Cons:

- slightly less convenient than object-or-class overloads

### Option 2: accept class wrappers and object wrappers

Pros:

- more ergonomic

Cons:

- mixed calling convention
- more validation branches
- larger test surface

### Option 3: expose raw class pointers only

Pros:

- closer to bare JNI

Cons:

- inconsistent with current public `Env` style
- forces callers to rebuild wrappers manually

## Recommendation

Choose option 1.

## Testing strategy

Desktop first with narrow host callbacks:

- `getSuperclass(...)` returns a wrapper with:
  - expected `$className`
  - expected loader handle
  - receiver handle `0`
- `getSuperclass(...)` returns `null` when callback reports no superclass
- `isAssignableFrom(...)` returns `true`
- invalid class-wrapper inputs are rejected for both helpers

Android smoke second:

- binding shape exists
- `env.getSuperclass(Java.use("java.lang.String"))` yields `java.lang.Object`
- `env.isAssignableFrom(Java.use("java.lang.Object"), Java.use("java.lang.String"))` yields `true`

## Success criteria

- no monitor-style lifetime issue
- helpers are class-wrapper-safe and loader-aware
- desktop regression passes
- Android smoke passes on fresh binaries
