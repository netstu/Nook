# Nook Java Loader-Aware Hook API Design

## Goal

Add loader-aware public Java hook APIs so deferred and immediate Java hook installation can target a specific `ClassLoader`, not just the default application loader.

## Problem

Current behavior is only partially loader-aware:

- `Java.ClassFactory.get(loader).use(className)` already carries `loader_handle` through:
  - overload resolution
  - field resolution
  - direct invocation
  - `.implementation` request creation
- but once `.implementation = fn` crosses into native hook installation, the loader information is dropped

The current native path is:

- `InstallJavaJsHook(...)`
- `NookJavaHookHookDeferred(...)`
- `PendingJavaHookRegistry`
- `InstallNow(...)`
- `JavaHook::HookMethod(...)`
- `JavaHook::FindClass(...)`

The class lookup at the bottom uses the default application loader path, so `factory.use(...).implementation` is not truly loader-scoped.

## Design Options

### Option 1: Bridge-only workaround

Keep the public hook API unchanged and try to resolve loader-specific classes before entering the native hook layer.

Pros:

- smallest surface change

Cons:

- deferred retry still keys off class name only
- loader identity is still lost in pending installs
- fixes the symptom, not the root cause

### Option 2: Internal-only loader-aware hook entrypoints

Add loader-aware framework/internal entrypoints but keep the public `include/nook/NookJavaHook.h` surface unchanged.

Pros:

- smaller public API delta

Cons:

- external users cannot build on the same loader-aware primitive
- does not meet the requirement to make the public C API reusable

### Option 3: Public loader-aware hook API

Add new public C APIs and plumb loader identity all the way through the deferred hook pipeline.

Pros:

- fixes the real architectural boundary
- keeps old APIs source-compatible
- gives future features a stable loader-aware primitive
- aligns the hook layer with the loader-aware JS layer already added

Cons:

- touches framework, pending registry, and JavaHook core

## Recommended Design

Use Option 3.

## Public API Shape

Keep existing APIs unchanged and add loader-aware variants:

- `NookJavaHookHookWithLoader(...)`
- `NookJavaHookHookDeferredWithLoader(...)`
- `NookJavaHookFindClassWithLoader(...)`

Old APIs remain as shorthand for `loader_handle == 0`.

The loader handle is represented as `jobject` in the public C API, matching JNI conventions and avoiding a second pointer abstraction.

## Internal Plumbing

### Framework layer

Add loader-aware internal entrypoints:

- `InstallNow(..., jobject loader, ...)`
- existing `InstallNow(...)` forwards with `loader == nullptr`

### Pending registry

Persist loader identity in `PendingJavaHookRegistry::Request`:

- `loader_handle`

Deduplication key becomes:

- class
- method
- signature
- staticness
- callback
- loader_handle

### Retry / deferred install

`ProcessPendingRequests(...)` must pass the stored loader through to `InstallNow(...)`.

The retry scheduler can stay class-name based for now. That is acceptable because the pending request itself now carries the actual loader used at install time.

## JavaHook layer

Add explicit loader-aware primitives:

- `JavaHook::FindClassWithLoader(JNIEnv* env, jobject loader, const char* className)`
- `JavaHook::HookMethodWithLoader(const char* className, jobject loader, const char* methodName, const char* shorty, bool isStatic, HookCallback callback)`

Behavior:

- if `loader == nullptr`, preserve current `FindClass(...)` / application-loader behavior
- if `loader != nullptr`:
  - bootstrap classes may still be resolved directly through `env->FindClass(...)`
  - non-bootstrap classes resolve through `JavaHookLoaderResolver::FindLoadedClassWithLoader(...)`
  - fallback to `JavaHookLoaderResolver::LoadClassWithLoader(...)`

## JS / agent runtime impact

No public JS API shape change in this pass.

The existing `JavaJsHookRequest.loader_handle` plumbing remains the source of truth. The change here is that the default native install path must finally consume it.

## Testing Strategy

### Host tests

Add red tests first for:

- native default install path forwards `loader_handle` into the loader-aware C/framework hook entrypoint
- pending registry dedup distinguishes different loader handles
- loader-aware public binding preserves old behavior when loader is null

### Android verification

Rebuild arm64, push fresh binaries, and validate with:

- existing `java_class_factory_smoke.js`
- a new smoke that installs `.implementation` through `Java.ClassFactory.get(loader)` and verifies the hook fires

## Boundary

This pass only makes hook installation loader-aware.

It does not add:

- `Java.setClassLoader(...)`
- `Java.classFactory.loader`
- loader-aware `factory.cast(...)`
- loader-aware `factory.retain(...)`
- loader-aware `factory.choose(...)`
