# Nook Hexdump Design

**Goal**

Add a first minimal global `hexdump(...)` helper so QuickJS scripts can format raw bytes from:

- `NativePointer`
- `ArrayBuffer`

into a predictable plain-text hexadecimal string.

This is the next logical step after `Memory.copy(...)` and `Memory.dup(...)`, and improves script-side debugging without yet committing to a full Frida-compatible display format.

## Context

The runtime now already supports:

- `Memory.alloc(...)`
- `Memory.allocUtf8String(...)`
- `Memory.copy(...)`
- `Memory.dup(...)`
- scalar `NativePointer.read*()` / `write*()` access

That means scripts can already access and duplicate raw memory. What is still missing is a convenient formatter that turns bytes into a readable dump string without requiring every script to manually loop over data.

The design document's long-term shape is:

```javascript
hexdump(target, {
  offset?: number,
  length?: number,
  header?: boolean,
  ansi?: boolean
})
```

But this step only implements the smallest useful subset.

## Scope

In scope:

- add global `hexdump(target, options?)`
- support `target` as:
  - `NativePointer`
  - `ArrayBuffer`
- support `options.offset`
- support `options.length`
- return plain `string`
- format one line per 16 bytes
- output only hexadecimal bytes separated by spaces

Out of scope:

- `options.header`
- `options.ansi`
- address column
- ASCII column
- width customization
- pretty colorized terminal output
- integrating `hexdump` into CLI display behavior

## Recommended API

Expected pointer usage:

```javascript
const ptrValue = Memory.allocUtf8String('hello');
send({ type: 'send', payload: hexdump(ptrValue, { length: 5 }) });
```

Expected payload:

```text
68 65 6c 6c 6f
```

Expected `ArrayBuffer` usage:

```javascript
const ptrValue = Memory.allocUtf8String('hello');
const blob = Memory.dup(ptrValue, 5);
send({ type: 'send', payload: hexdump(blob) });
```

Expected payload:

```text
68 65 6c 6c 6f
```

## Behavior

### `NativePointer` input

- `length` is required
- `offset` defaults to `0`
- the runtime adds `offset` to the pointer value
- it validates the requested range is readable before formatting

### `ArrayBuffer` input

- `offset` defaults to `0`
- `length` defaults to `byteLength - offset`
- both values must remain within the buffer bounds

### Output format

For this first version:

- bytes are emitted in lowercase hex
- each byte is two digits
- bytes are separated by a single space
- each line contains 16 bytes maximum
- multiple lines are joined with `\n`

Example for 20 bytes:

```text
00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
10 11 12 13
```

## Architecture

Implementation should stay in `src/agent_runtime/js_runtime.cpp`.

Suggested structure:

1. parse `target`
2. parse optional `options`
3. resolve one byte span:
   - `const uint8_t* data`
   - `size_t length`
4. format the bytes into one `std::string`
5. return a JS string

This should not allocate runtime-owned native memory. For pointer input, it formats bytes directly from the validated memory region. For `ArrayBuffer` input, it formats bytes directly from the buffer data pointer returned by QuickJS.

## Error Handling

Use narrow exceptions consistent with the current runtime:

- `hexdump requires a target`
- `hexdump pointer target requires length`
- `hexdump offset must be a number`
- `hexdump length must be a number`
- `hexdump unsupported target`
- `hexdump unreadable pointer`
- `hexdump array buffer range out of bounds`

This keeps the helper safe and predictable.

## Testing Strategy

Add local runtime tests first:

1. `hexdump(ArrayBuffer)` returns the expected one-line hex string
2. `hexdump(NativePointer, { length })` returns the expected one-line hex string
3. `hexdump(NativePointer)` without `length` throws
4. `hexdump(ptr('0x1'), { length: 4 })` throws unreadable-pointer error
5. multi-line formatting is correct for lengths above 16 bytes

After the runtime tests pass, extend `host/nook-py/memory_api_smoke.js` with one minimal `hex:` message so real-device validation covers the new helper.

## Why This Minimal Version First

This version is intentionally small because:

- it is immediately useful
- it reuses the now-stable `Memory.dup(...)` foundation
- it avoids prematurely freezing a richer text layout
- `header` and `ansi` can be added later without changing the core function signature

That makes it the right incremental step toward a more Frida-like scripting surface.
