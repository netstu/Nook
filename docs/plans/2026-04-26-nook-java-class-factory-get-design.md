# Nook Java.ClassFactory.get(loader) Design

## Goal

Add a Frida-aligned `Java.ClassFactory.get(loader)` API on Android and make the returned factory support `factory.use(className)`.

## Frida Alignment

This feature should follow Frida's public API shape:

- `Java.use(className)` uses the default class factory
- `Java.enumerateClassLoaders(...)` returns loader wrappers
- `Java.ClassFactory.get(loader)` returns a loader-scoped factory
- `factory.use(className)` resolves classes through the specified loader

This pass intentionally follows that design instead of introducing a Nook-specific
`Java.use(className, loader)` or a global loader mutation API.

## Scope

This pass implements:

- `Java.ClassFactory.get(loader)`
- a returned factory object with:
  - `factory.use(className)`

The resulting class wrapper should support the same core behavior as `Java.use(...)`:

- direct method invocation
- overload resolution
- field resolution
- method `.implementation = fn`

This pass does not include:

- `Java.classFactory.loader`
- `Java.setClassLoader(loader)`
- `factory.$new(...)`
- `factory.choose(...)`
- `factory.retain(...)`
- `factory.cast(...)`
- `Java.enumerateClassLoadersSync()`

## Design Options Considered

### Approach A: Frida-style factory object with loader-scoped `use()`

`Java.ClassFactory.get(loader)` returns a factory object. `factory.use(className)` produces wrappers that carry an internal loader handle.

Pros:

- matches Frida's public design
- keeps default `Java.use(...)` semantics unchanged
- scales naturally to future `factory.$new()` / `factory.choose()`

Cons:

- requires plumbing loader handle through the native bridge

### Approach B: Global loader switching

Expose a `Java.setClassLoader(loader)`-style API and make later `Java.use(...)` calls resolve through the new loader.

Pros:

- simpler JS surface

Cons:

- diverges from the intended Frida public design
- introduces hard-to-debug global state
- risks colliding with `Java.ready(...)` default loader semantics

### Approach C: Extend `Java.use()` with an extra loader argument

Example:

- `Java.use("com.demo.target.TextFragment", loader)`

Pros:

- minimal JS API count

Cons:

- not Frida-like
- overloads `Java.use(...)` with unrelated concerns

## Recommended Design

Use Approach A.

## Wrapper Model

`factory.use(className)` should return the same wrapper shape as `Java.use(className)`, but with one extra internal property:

- `__nookJavaLoaderHandle`

This loader handle should be propagated into:

- field resolution
- method overload resolution
- direct Java invocation
- hook installation for `.implementation`

Default wrappers created by `Java.use(...)` should carry loader handle `0`.

## Native Bridge Changes

The current bridge APIs only key off `class_name`. That is not enough for loader-scoped resolution.

This pass should add `loader_handle` to the relevant records and request types:

- `JavaJsHookRequest`
- `JavaJsHookRecord`
- `JavaJsFieldRecord`
- `JavaJsMethodRecord`

Default Android behavior should become:

- if `loader_handle == 0`:
  - keep current `JavaHook::FindClass(...)` behavior
- if `loader_handle != 0`:
  - resolve through the explicit loader using `JavaHookLoaderResolver::LoadClassWithLoader(...)`
  - and then continue with the same reflection / JNI path

## Hook Installation Boundary

To preserve `.implementation = fn` semantics on factory-based wrappers, loader-scoped hook install must also accept the loader handle.

This pass therefore needs loader-aware installation in the Java hook path, not just loader-aware invocation.

## Validation Strategy

### Desktop regression

Use fake dependencies to verify:

- `Java.ClassFactory.get` binding exists
- rejects non-loader inputs
- returned factory exposes `.use`
- loader-scoped wrapper carries the explicit loader handle through resolve/invoke paths
- loader-scoped `.implementation` install forwards the loader handle

### Device smoke

Use a loader from `Java.enumerateClassLoaders(...)`, then:

- `var cf = Java.ClassFactory.get(loader);`
- `var TextFragment = cf.use("com.demo.target.TextFragment");`
- validate:
  - wrapper class name
  - direct call through a retained / cast object later if needed

For the first smoke, it is sufficient to prove:

- factory acquisition works
- `factory.use(...)` produces a working wrapper

## Boundary

This pass is about making loader-scoped `use()` real.

Once this lands, the follow-up path is clear:

- `factory.$new(...)`
- `factory.choose(...)`
- `factory.retain(...)`
- possibly `Java.classFactory.loader` if we later decide to mirror more of Frida's convenience surface
