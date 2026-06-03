# Nook Module FindBaseAddress/GetBaseAddress Design

**Goal**

Add Frida-style `Module.findBaseAddress(name)` and `Module.getBaseAddress(name)` so scripts can resolve a loaded module to its base pointer before scanning or range inspection.

This fills the gap between the current export lookup API and the already shipped range APIs.

## Scope

In scope:

- add `Module.findBaseAddress(name): NativePointer | null`
- add `Module.getBaseAddress(name): NativePointer`
- support currently loaded modules only
- keep behavior consistent across Windows host tests and Android/Linux device runtime

Out of scope:

- module enumeration
- path metadata return values
- implicit module loading

## Recommended API

Example:

```javascript
const base = Module.findBaseAddress('libtarget.so');
if (base !== null) {
  const range = Module.findRangeByAddress(base);
}
```

`findBaseAddress(...)` is the nullable convenience API.
`getBaseAddress(...)` is the strict variant that throws when the module is missing.

## Behavior

- non-string input throws
- missing module:
  - `findBaseAddress(...)` returns `null`
  - `getBaseAddress(...)` throws
- loaded module returns a `NativePointer` base address

## Architecture

Implementation should stay small:

1. add one native helper that resolves a loaded module base by name
2. expose it through two `Module.*` bindings with different miss semantics
3. reuse existing `NativePointer` wrapping without introducing a new module object shape

Platform strategy:

- Android/Linux: reuse `ElfHooker::get_module_info(...)`
- Windows: use `GetModuleHandleA(...)` for loaded-module lookup during local tests

## Testing Strategy

Add runtime tests for:

1. bindings exist
2. current test executable name resolves to a non-null base
3. missing module returns `null` for `findBaseAddress(...)`
4. missing module throws for `getBaseAddress(...)`

After local tests pass, extend the device smoke with one `libnook-agent.so` base-address lookup.
