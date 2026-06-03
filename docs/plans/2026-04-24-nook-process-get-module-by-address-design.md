# Nook Process GetModuleByAddress Design

**Goal**

Add `Process.getModuleByAddress(address)` so scripts can resolve an in-process address back to its containing loaded module.

This builds directly on the newly added `Module.enumerateModules()` API and is the next practical step toward Frida-style module queries.

## Scope

In scope:

- add `Process.getModuleByAddress(address): Module | null`
- reuse the same minimal module object shape already returned by `Module.enumerateModules()`
- normalize tagged arm64 userspace pointers before lookup

Out of scope:

- `ModuleMap`
- module name fuzzy matching
- export/import enumeration by module object

## Recommended API

Example:

```javascript
const address = Module.getExportByName('libnook-agent.so', 'NookInlineHookAddress');
const module = Process.getModuleByAddress(address);
```

For this phase, the return object matches `Module.enumerateModules()` entries:

- `name`
- `base`
- `size`
- `path`

## Behavior

- invalid pointer-like input throws
- unmapped or non-module-backed address returns `null`
- address inside a loaded module returns the containing module object
- tagged arm64 userspace pointers should still resolve to the same module

## Architecture

Implementation should stay minimal:

1. reuse the current native loaded-module collection helper
2. scan the resulting module records for containment
3. return the same JS module object shape used by `Module.enumerateModules()`

No new platform-specific module parsing should be introduced beyond what already exists for enumeration.

## Testing Strategy

Add runtime tests for:

1. binding exists
2. current executable base resolves to the current executable module
3. `ptr('0x1')` returns `null`
4. invalid non-pointer input throws
5. tagged pointer form of a known module base still resolves

After local tests pass, extend the device smoke with one message confirming that `Process.getModuleByAddress(Module.getBaseAddress('libnook-agent.so'))` returns `libnook-agent.so`.
