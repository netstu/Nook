# Nook ModuleMap Design

**Goal**

Add a minimal `ModuleMap` object so scripts can build a snapshot of loaded modules and perform repeated address-to-module queries without re-enumerating on each call.

This sits on top of the existing `Module.enumerateModules()` and `Process.getModuleByAddress(...)` work.

## Scope

In scope:

- add `new ModuleMap()`
- snapshot loaded modules at construction time
- add methods:
  - `has(address): boolean`
  - `find(address): Module | null`
  - `get(address): Module`
  - `values(): Module[]`

Out of scope:

- automatic refresh
- mutation tracking
- filter callbacks in the constructor
- `update()`

## Recommended API

Example:

```javascript
const map = new ModuleMap();
const module = map.get(Module.getBaseAddress('libnook-agent.so'));
```

`ModuleMap` is snapshot-based for this phase.
If modules are loaded later, callers create a new `ModuleMap`.

## Behavior

- constructor takes no arguments for now
- `has(address)` returns `true`/`false`
- `find(address)` returns a module object or `null`
- `get(address)` returns a module object or throws when not found
- `values()` returns an array of module objects from the captured snapshot
- invalid pointer-like input to `has/find/get` throws

## Architecture

Implementation should stay light:

1. create one module snapshot array at construction time from `Module.enumerateModules()`
2. attach per-instance methods implemented as closures over that snapshot
3. reuse existing module object shape `{ name, base, size, path }`
4. normalize tagged arm64 pointers before lookup

This avoids introducing a new custom JS class or opaque native object while still providing per-instance snapshots.

## Testing Strategy

Add runtime tests for:

1. binding exists
2. constructor snapshot can resolve the current executable base through `has/find/get`
3. `values()` returns at least one module
4. miss behavior:
  - `has(ptr('0x1')) === false`
  - `find(ptr('0x1')) === null`
  - `get(ptr('0x1'))` throws

After local tests pass, extend the device smoke with one message confirming that a fresh `ModuleMap` resolves `libnook-agent.so`.
