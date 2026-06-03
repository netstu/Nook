# Nook Java.ClassFactory.openClassFile Design

## Goal

Add a Frida-aligned `Java.ClassFactory.get(loader).openClassFile(filePath)` path for Nook.

This phase only targets the most useful public shape:

- `factory.openClassFile(path)`
- returned object exposes `load()`
- `load()` is scoped to the selected `factory` loader instead of mutating global default-loader state

## Reference

Frida JavaScript API documents both:

- `Java.openClassFile(filePath)`
- `Java.ClassFactory` with a loader-scoped `openClassFile(filePath)`

Reference:

- https://frida.re/docs/javascript-api

The important semantic direction is:

- `Java.openClassFile(...)` is a global/default-factory convenience
- `ClassFactory.openClassFile(...)` is the loader-scoped version

## Current Nook Context

Nook already has these pieces working:

- `Java.openClassFile(path).load()`
- `Java.setClassLoader(loader)`
- `Java.ClassFactory.get(loader)`
- loader-scoped `use / choose / cast / retain / $new`

That means the missing gap is no longer loader plumbing itself. The gap is public API shape:

- Nook can do the work
- but scripts still cannot express it in the same Frida-style class-factory workflow

## Confirmed Scope

This pass will add:

- `Java.ClassFactory.get(loader).openClassFile(filePath)`
- return value with `load()`
- `load()` creating a `DexClassLoader` whose parent is the current factory loader
- `load()` returning the new loader wrapper

This pass will not add:

- `Java.classFactory`
- `factory.cacheDir`
- `factory.tempFileNaming`
- full `getClassNames()` support
- a new native bridge
- changes to the already validated `Java.openClassFile(...)` behavior

## Chosen Design

### Public API

```javascript
Java.performNow(function () {
  var loader = null;

  Java.enumerateClassLoaders({
    onMatch: function (candidate) {
      if (loader === null &&
          String(candidate.$className).indexOf("PathClassLoader") !== -1) {
        loader = candidate;
      }
    },
    onComplete: function () {}
  });

  var factory = Java.ClassFactory.get(loader);
  var dex = factory.openClassFile("/data/app/.../base.apk");
  var dexLoader = dex.load();
  var scopedFactory = Java.ClassFactory.get(dexLoader);
  var TextFragment = scopedFactory.use("com.demo.target.TextFragment");
});
```

### Semantics

- `factory.openClassFile(path)` validates `path` exactly like `Java.openClassFile(path)`
- it returns an object with:
  - `load()`
- `load()`:
  - gets current `Application`
  - gets code cache path
  - uses the current factory loader as the parent loader
  - constructs `dalvik.system.DexClassLoader`
  - returns that loader wrapper
- unlike `Java.openClassFile(path).load()`, this loader-scoped version should not call `Java.setClassLoader(...)`

That last point is the key semantic difference. `ClassFactory` should stay explicit and scoped.

## Implementation Strategy

Implement this in the QuickJS bootstrap only.

Inside `Java.ClassFactory.get(loader)`, extend the returned factory object with:

- `openClassFile(filePath)`

Its implementation can reuse the same underlying Java calls already used by `Java.openClassFile(...)`:

- `android.app.ActivityThread.currentApplication()`
- `app.getCodeCacheDir().getAbsolutePath()`
- `dalvik.system.DexClassLoader`

But instead of:

- using `app.getClassLoader()` as parent
- calling `Java.setClassLoader(loader)`

it should:

- use the current factory's `loaderHandle` / loader wrapper as parent
- return the created loader wrapper directly

## Why This Design

This is the smallest change that makes the public API more Frida-like without reopening already solved loader-state questions.

It also keeps the model clean:

- `Java.openClassFile(...)` remains the global/default-factory path
- `Java.ClassFactory.get(loader).openClassFile(...)` becomes the explicit loader-scoped path

That is a better long-term direction than forcing loader-scoped behavior through `Java.setClassLoader(...)` side effects.

## Alternatives Considered

### Option 1: Reuse `Java.openClassFile(...).load()` directly and call `Java.setClassLoader(...)`

Pros:

- very small patch

Cons:

- pollutes global loader state
- does not actually behave like a loader-scoped class-factory path
- makes later Frida alignment harder

Rejected.

### Option 2: Add `factory.openClassFile(path).load()` in bootstrap only

Pros:

- matches Frida-facing API direction
- reuses existing bridge pieces
- no new native bridge needed
- keeps loader scope explicit

Cons:

- does not yet provide the richer `ClassFactory` object model

Chosen.

### Option 3: Full Frida `ClassFactory` parity now

Pros:

- best API completeness

Cons:

- too much scope for this pass
- would mix public-shape alignment with unrelated runtime completeness work

Rejected for now.

## Validation Plan

### Host tests

Add host coverage for:

- `typeof Java.ClassFactory.get(loader).openClassFile === 'function'`
- non-string rejection
- `load()` using the selected factory loader path
- `load()` returning a loader wrapper
- no requirement to mutate the default loader

### Device smoke

Add a real-device smoke that:

- selects a real `PathClassLoader`
- creates `factory = Java.ClassFactory.get(loader)`
- calls `factory.openClassFile(app.getPackageCodePath()).load()`
- verifies returned wrapper is a `DexClassLoader`
- verifies `Java.ClassFactory.get(returnedLoader).use('com.demo.target.TextFragment')` works

## Out of Scope

Not part of this pass:

- `getClassNames()`
- class enumeration from an opened dex
- `Java.classFactory`
- cache/temp naming customization
- changing `Java.openClassFile(...)` into a shared generic object model

## Expected Outcome

After this pass, Nook will have a cleaner Frida-style loader workflow:

- global/default path:
  - `Java.openClassFile(path).load()`
- loader-scoped path:
  - `Java.ClassFactory.get(loader).openClassFile(path).load()`

This closes another public API gap without expanding the native bridge surface.
