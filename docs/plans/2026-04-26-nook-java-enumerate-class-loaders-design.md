# Nook Java.enumerateClassLoaders Design

## Goal

Add a minimal Frida-style `Java.enumerateClassLoaders({ onMatch, onComplete })` API on Android.

## Scope

This pass only covers the smallest useful loader-enumeration behavior:

- expose `Java.enumerateClassLoaders(callbacks)`
- require:
  - `callbacks.onMatch(loader)`
  - `callbacks.onComplete()`
- return Java object wrappers for discovered `java.lang.ClassLoader` instances
- de-duplicate repeated loaders before JS callback dispatch
- always call `onComplete()` once enumeration finishes

This pass does not include:

- `Java.ClassFactory.get(loader)`
- `Java.enumerateClassLoadersSync()`
- loader metadata beyond the wrapper itself
- ART-side raw class-loader walking
- non-Android support

## Approaches Considered

### Approach A: Java/JNI-side loader enumeration

Collect a stable minimal set of loaders through Java/JNI:

- application class loader if ready
- current thread context class loader
- system class loader
- each discovered loader's parent chain

Pros:

- much lower crash risk than a fresh ART raw-object walker
- naturally returns real `ClassLoader` Java objects
- sufficient for the next follow-up step: `Java.ClassFactory.get(loader)`

Cons:

- not a full Frida-equivalent ART class-loader census
- may miss obscure loaders not reachable from the initial set

### Approach B: ART-side class-loader traversal

Walk ART internals directly to discover all class loaders.

Pros:

- closer to Frida's lower-level architecture
- potentially more complete

Cons:

- significantly higher implementation and validation risk
- poor fit immediately after the loaded-class walker crash we just fixed

### Approach C: Bundle loader enumeration with `ClassFactory.get(loader)`

Build both features together.

Pros:

- user-visible payoff is immediate

Cons:

- harder to debug because enumeration and loader-scoped resolution land at once
- larger surface area for regressions

## Recommended Design

Use Approach A for this pass.

The JS runtime should mirror the existing `Java.choose(...)` / `Java.enumerateLoadedClasses(...)` pattern:

- validate callback shape
- call a narrow native enumeration dependency
- de-duplicate loaders in JS using their wrapper handle
- call `onMatch(loaderWrapper)` for each unique loader
- call `onComplete()` once

The native bridge should:

- expose `EnumerateJavaClassLoaders(...)`
- on Android, enumerate a stable set of `ClassLoader` objects through JNI
- promote each discovered loader to a temporary global ref
- describe each result as a `JavaJsValueKind::kObject` with class name `java.lang.ClassLoader` or subclass name

## Android Backend Strategy

Start from the loaders Nook already knows how to reach safely:

- cached application class loader from `Java.ready(...)`
- `Thread.currentThread().getContextClassLoader()`
- `ClassLoader.getSystemClassLoader()`

Then walk `ClassLoader.getParent()` until null for each one.

This yields a stable and useful loader set without new ART raw-memory logic.

## Validation Strategy

### Desktop regression

Desktop builds will use an injected fake dependency that returns:

- one application loader handle
- one duplicate of that same loader
- one system loader handle

Coverage should include:

- binding exists
- argument validation
- duplicate native results are de-duplicated before JS callback dispatch
- `onComplete()` fires after all matches

### Device smoke

Add:

- `host/nook-py/java_enumerate_class_loaders_smoke.js`

Smoke should prove:

- `typeof Java.enumerateClassLoaders === 'function'`
- at least one loader wrapper is returned
- `loader.$className` is a `ClassLoader` type
- `onComplete()` is signaled

## Boundary

This pass intentionally stops at enumeration.

Once this lands, the next step is straightforward:

- `Java.ClassFactory.get(loader)`

That follow-up can reuse the loader wrappers returned here instead of inventing a parallel loader representation.
