# Nook Memory ScanSync Design

**Goal**

Add a first minimal `Memory.scanSync(address, size, pattern)` API so QuickJS scripts can search native memory ranges for byte patterns.

This is the next practical memory primitive after:

- scalar reads and writes
- `Memory.copy(...)`
- `Memory.dup(...)`
- `Memory.protect(...)`
- `hexdump(...)`

and it is one of the most commonly used Frida-style workflows for locating code and data before hooking or patching.

## Scope

In scope:

- add `Memory.scanSync(address, size, pattern): MemoryScanMatch[]`
- support pointer-like `address` inputs already accepted elsewhere
- support exact hex-byte tokens such as `48`, `89`, `5c`
- support full-byte wildcards using `??`
- return JS objects shaped like:
  - `{ address: NativePointer, size: number }`

Out of scope:

- async `Memory.scan(...)`
- nibble wildcards such as `?4` or `4?`
- mask-based binary patterns
- scanning unreadable mixed ranges page-by-page
- `Process.enumerateRanges(...)` driven convenience helpers

## Recommended API

Examples:

```javascript
const text = Memory.allocUtf8String('hello hello');
const matches = Memory.scanSync(text, 11, '68 65 6c 6c 6f');
send({
  type: 'send',
  payload: matches.map(m => `${m.address}:${m.size}`).join('|')
});
```

Wildcard example:

```javascript
const matches = Memory.scanSync(text, 5, '68 65 ?? 6c 6f');
```

## Pattern Model

The first version accepts a whitespace-separated string of byte tokens:

- exact byte: two hex digits, for example `41`
- wildcard byte: `??`

Accepted examples:

- `68 65 6c 6c 6f`
- `48 8b ?? ?? 89`
- `00`

Rejected examples:

- empty string
- `4`
- `0x41`
- `GG`
- `?4`
- `4?`

This keeps the parser simple and still covers the common Frida scanning workflow.

## Behavior

### Argument validation

- `address` must resolve to a non-zero pointer
- `size` must be a positive number
- `pattern` must be a non-empty valid pattern string
- the target range must be readable

Invalid arguments should raise JS exceptions.

### Return value

- returns a JS array
- each match object contains:
  - `address`
  - `size`
- empty result is `[]`

`size` should equal the pattern byte length, not the scanned range length.

## Architecture

Implementation should live in `src/agent_runtime/js_runtime.cpp`.

Recommended pieces:

1. parse pointer and size
2. parse pattern string into a vector of `(is_wildcard, byte_value)`
3. validate readability of the full range
4. run a simple linear scan
5. build a JS array of match objects

The scan algorithm can be the simplest correct byte-by-byte matcher for the first version. There is no need for Boyer-Moore or SIMD yet.

## Error Handling

Use narrow caller-facing errors:

- `Memory.scanSync requires address, size, and pattern`
- `Memory.scanSync address must be a non-zero pointer value`
- `Memory.scanSync size must be a positive number`
- `Memory.scanSync pattern must be a non-empty string`
- `Memory.scanSync pattern contains invalid token`
- `Memory.scanSync unreadable range`

## Testing Strategy

Add local runtime tests first:

1. exact match returns two hits for `"hello hello"`
2. wildcard pattern matches `"hello"`
3. unmatched pattern returns an empty array
4. invalid pattern throws
5. unreadable range throws

After runtime tests pass, extend `memory_api_smoke.js` with one exact-match example on a string allocated in-process.

## Why This Step

This is the right next API because:

- it directly supports real hook setup workflows
- it is heavily used in Frida-style scripts
- it can be implemented safely on top of the existing readability checks
- it creates a reusable core for a later async `Memory.scan(...)`
