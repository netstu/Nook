# Nook Process FindRangeByAddress Design

**Goal**

Add `Process.findRangeByAddress(address)` so scripts can resolve a single pointer back to its containing memory range.

This complements the newly added:

- `Process.enumerateRanges(protection)`
- `Memory.scanSync(...)`
- `Memory.scan(...)`

and completes the most common Frida-like memory discovery chain.

## Scope

In scope:

- add `Process.findRangeByAddress(address): RangeDetails | null`
- reuse the existing minimal range shape:
  - `base`
  - `size`
  - `protection`

Out of scope:

- file/path metadata
- nearest-range or partial-overlap queries
- separate module-aware helpers

## Recommended API

Example:

```javascript
const p = Memory.alloc(16);
const range = Process.findRangeByAddress(p);
send({
  type: 'send',
  payload: range === null ? 'missing' : `${range.base}:${range.protection}`
});
```

Miss example:

```javascript
const range = Process.findRangeByAddress(ptr('0x1'));
// => null
```

## Behavior

### Validation

- `address` must resolve to a non-zero pointer
- invalid pointer-like input should throw

### Lookup semantics

- if the address falls within a mapped range, return that containing range
- if no range contains the address, return `null`

Containment is:

- `range.base <= address < range.base + range.size`

## Architecture

Implementation should stay in `src/agent_runtime/js_runtime.cpp`.

Recommended approach:

1. factor current range enumeration helper into an all-ranges collector
2. keep `Process.enumerateRanges(...)` filtering on top of it
3. implement `Process.findRangeByAddress(...)` by scanning the collected native ranges for containment

No extra runtime state is required.

## Testing Strategy

Add runtime tests for:

1. binding exists
2. dedicated RW test page resolves to its containing range
3. invalid pointer input throws
4. unreadable / unmapped pointer like `ptr('0x1')` returns `null`

After runtime tests pass, extend the smoke script with one simple `find-range` message for an in-process allocation.
