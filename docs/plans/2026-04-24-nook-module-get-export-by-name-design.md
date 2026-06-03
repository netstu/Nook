# Nook Module GetExportByName Design

**Goal**

Add `Module.getExportByName(moduleName, exportName)` as the strict companion to the existing `Module.findExportByName(...)`.

This closes the current gap between nullable and strict `Module.*` lookup APIs and aligns the export lookup surface with the already added `findBaseAddress/getBaseAddress` pair.

## Scope

In scope:

- add `Module.getExportByName(moduleName, exportName): NativePointer`
- reuse the current loaded-module export resolver
- keep current `findExportByName(...)` semantics unchanged

Out of scope:

- widening `findExportByName(...)` to support `null` module search
- module enumeration
- new export metadata

## Recommended API

Example:

```javascript
const address = Module.getExportByName('libnook-agent.so', 'NookInlineHookAddress');
Interceptor.attach(address, {
  onEnter(args) {}
});
```

`findExportByName(...)` remains the nullable probe API.
`getExportByName(...)` is the strict API that throws when resolution fails.

## Behavior

- invalid non-string arguments throw
- successful resolution returns a `NativePointer`
- missing or filtered export throws

For this step, the miss path may stay generic. The runtime does not currently distinguish between:

- missing module
- missing symbol
- symbol filtered out as unsafe for the current resolver path

## Architecture

Implementation should stay minimal:

1. reuse the same native export lookup helper already used by `findExportByName(...)`
2. expose a second JS binding with strict miss semantics
3. keep all pointer wrapping through the existing `MakeNativePointer(...)`

No new native resolver code is needed for this step.

## Testing Strategy

Add runtime tests for:

1. binding exists
2. hit returns a `NativePointer`
3. miss throws

After local tests pass, extend the device smoke with one strict export lookup on a stable `libnook-agent.so` symbol and verify it matches `findExportByName(...)`.
