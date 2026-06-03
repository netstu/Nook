# Nook Process EnumerateRanges Design

**Goal**

Add a first minimal `Process.enumerateRanges(protection)` API so scripts can enumerate mapped memory ranges before scanning or patching them.

This is the missing link needed to make the current memory APIs useful in a more Frida-like workflow:

1. `Process.enumerateRanges(...)`
2. `Memory.scan(...)` / `scanSync(...)`
3. hook or patch selected addresses

## Scope

In scope:

- add a global `Process` object
- add `Process.enumerateRanges(protection): RangeDetails[]`
- return minimal range objects:
  - `base`
  - `size`
  - `protection`
- support exact three-character protection filters such as:
  - `r--`
  - `rw-`
  - `r-x`
  - `rwx`
  - `---`

Out of scope:

- `Process.findRangeByAddress(...)`
- `Process.enumerateMallocRanges()`
- file/path metadata for ranges
- thread enumeration
- debugger detection

## Recommended API

Example:

```javascript
const ranges = Process.enumerateRanges('r--');
send({
  type: 'send',
  payload: `${ranges.length}:${ranges[0].base}:${ranges[0].protection}`
});
```

Expected range object shape:

```javascript
{
  base: ptr('0x12345678'),
  size: 4096,
  protection: 'r--'
}
```

## Behavior

### Validation

- `protection` must be exactly 3 characters
- each position must be one of:
  - `r` or `-`
  - `w` or `-`
  - `x` or `-`

Invalid filters should throw JS exceptions.

### Enumeration semantics

- only mapped ranges should be returned
- the protection filter is exact for the returned Frida-style 3-character string
- return value is a JS array
- empty result is `[]`

### Platform mapping

On Linux / Android:

- parse `/proc/self/maps`
- normalize the first three permission characters into Frida-style `rwx`

On Windows:

- iterate regions with `VirtualQuery`
- derive Frida-style `rwx` from `PAGE_*` protection flags
- skip uncommitted regions

## Architecture

Implementation should live in `src/agent_runtime/js_runtime.cpp`.

Recommended pieces:

1. a helper that validates Frida-style protection strings
2. a helper that collects native range records:
   - Linux/Android: `/proc/self/maps`
   - Windows: `VirtualQuery`
3. a helper that converts a native record into a JS object
4. `JsProcessEnumerateRanges(...)`

The first version does not need a shared cross-module abstraction.

## Testing Strategy

Add runtime tests for:

1. `typeof Process.enumerateRanges === 'function'`
2. invalid protection filter throws
3. a dedicated RW test page appears in `Process.enumerateRanges('rw-')`
4. after `Memory.protect(page, size, 'r--')`, the page appears in `Process.enumerateRanges('r--')`

After runtime tests pass, extend the smoke script with one small range-count message.

## Why This Step

This is the right next API because:

- it unlocks practical use of `Memory.scan(...)`
- it matches a core Frida discovery workflow
- it can be implemented with existing platform primitives
- it stays focused without dragging in unrelated `Process` features yet
