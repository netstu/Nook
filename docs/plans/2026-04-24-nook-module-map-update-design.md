# Nook ModuleMap.update Design

**Goal**

Extend `ModuleMap` with a minimal `update()` method so an existing instance can refresh its internal module snapshot after new modules are loaded.

This keeps the current lightweight `ModuleMap` API but removes the need for callers to always allocate a new instance after late `dlopen()` activity.

## Scope

In scope:

- add `ModuleMap.prototype.update()`-equivalent behavior on existing instances
- rebuild the module snapshot from `Module.enumerateModules()`
- return the same `ModuleMap` instance from `update()`
- keep `has/find/get/values` working against the refreshed snapshot

Out of scope:

- constructor filters
- auto-refresh on module load
- incremental diffs or change events
- native opaque class machinery

## Recommended API

Example:

```javascript
const map = new ModuleMap();
const refreshed = map.update();
const module = refreshed.get(Module.getBaseAddress('libnook-agent.so'));
```

`update()` should mutate the instance snapshot in place and return `this`.

## Behavior

- `new ModuleMap()` still takes no arguments
- `update()` takes no arguments
- `update()` refreshes the per-instance snapshot
- `update()` returns the same object for chaining
- `values()` returns module objects cloned from the current snapshot after refresh
- lookup methods continue to normalize tagged arm64 pointers before matching

## Architecture

The current closure-captured snapshot is too rigid for in-place refresh. For this phase:

1. store the current snapshot array on the JS instance
2. make `has/find/get/values` read the snapshot from `this`
3. implement `update()` by calling `Module.enumerateModules()` and replacing the stored snapshot
4. continue reusing the existing module object shape and clone helpers

This preserves the lightweight JS-object approach while enabling mutation.

## Testing Strategy

Add runtime tests for:

1. `typeof new ModuleMap().update === 'function'`
2. `update()` returns the same instance
3. `values()` still returns modules after `update()`

After local tests pass, extend the memory smoke with one message proving `update()` returns the same object and preserves lookups for `libnook-agent.so`.
