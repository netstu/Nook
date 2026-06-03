# Nook Module FindRangeByAddress Design

**Goal**

Add `Module.findRangeByAddress(address)` as a Frida-style convenience API for resolving an address to its containing memory range through the `Module` namespace.

This does not introduce new range metadata. It provides a familiar API surface for scripts that already work through `Module.*`.

## Scope

In scope:

- add `Module.findRangeByAddress(address): RangeDetails | null`
- return the same minimal range shape already used by `Process`:
  - `base`
  - `size`
  - `protection`

Out of scope:

- module ownership metadata
- file/path details
- module name resolution for the range

## Recommended API

Example:

```javascript
const address = Module.findExportByName('libdemo.so', 'target');
const range = Module.findRangeByAddress(address);
```

For the current phase, the result shape matches `Process.findRangeByAddress(...)`.

## Behavior

- invalid pointer-like input throws
- non-zero unmapped address returns `null`
- mapped address returns the containing range object

## Architecture

Implementation should stay trivial:

1. reuse the existing native range lookup helper
2. expose it through a `Module.findRangeByAddress` binding

No new platform code should be added for this step.

## Testing Strategy

Add runtime tests for:

1. binding exists
2. hit on a dedicated test page mapping
3. `ptr('0x1')` returns `null`

After runtime tests pass, extend the smoke script with one simple `module-find-range` message.
