# Nook Memory Copy And Dup Design

**Goal**

Add the next minimal `Memory` capability after scalar `NativePointer` access:

- `Memory.copy(dst, src, n)`
- `Memory.dup(address, size)`

This closes the gap between single-value memory access and block-level memory manipulation, and gives later APIs such as `hexdump(...)` a simpler foundation.

## Context

The current runtime already supports:

- `Memory.alloc(size)`
- `Memory.allocUtf8String(text)`
- scalar `NativePointer.read*()` / `write*()` access
- readable and writable range validation

That is enough for pointer-oriented scripting, but not enough for common Frida-like workflows where a script needs to:

- copy one memory block to another
- snapshot a region before patching or inspection
- move data around without manually looping in JS

`Memory.copy(...)` and `Memory.dup(...)` are the smallest useful block-level additions.

## Scope

In scope:

- add `Memory.copy(dst, src, n): void`
- add `Memory.dup(address, size): ArrayBuffer`
- keep the current safety model:
  - reject unreadable source ranges
  - reject unwritable destination ranges
- support only `NativePointer` / pointer-string / numeric pointer inputs already accepted by `ptr(...)`
- support real QuickJS `ArrayBuffer` return values for `dup`

Out of scope:

- `Memory.protect(...)`
- `Memory.scan(...)` / `scanSync(...)`
- `hexdump(...)`
- `writeByteArray(...)`
- `ArrayBuffer.wrap(...)`
- async memory operations

## Recommended API

Expected script shape:

```javascript
const src = Memory.allocUtf8String('hello-copy');
const dst = Memory.alloc(32);

Memory.copy(dst, src, 11);
send({ type: 'send', payload: dst.readUtf8String() });

const blob = Memory.dup(src, 5);
send({ type: 'send', payload: blob.byteLength });
```

Expected results:

- first message payload: `hello-copy`
- second message payload: `5`

## Architecture

### `Memory.copy(...)`

Implementation should stay inside `src/agent_runtime/js_runtime.cpp` beside the existing `Memory.alloc(...)` bindings.

Behavior:

1. parse `dst`, `src`, and `n`
2. reject null pointers when `n > 0`
3. validate:
   - source range is readable
   - destination range is writable
4. perform `std::memmove(...)`

`memmove` is preferable to `memcpy` here because it safely handles overlapping regions without adding API complexity.

### `Memory.dup(...)`

Behavior:

1. parse `address` and `size`
2. if `size == 0`, return an empty `ArrayBuffer`
3. validate the source range is readable
4. allocate a new QuickJS-owned `ArrayBuffer`
5. copy the requested bytes into that buffer
6. return the `ArrayBuffer`

This keeps ownership simple:

- duplicated data becomes JS-managed
- runtime-owned native allocation tracking does not need to change

## Error Handling

Use narrow JS exceptions consistent with the current runtime style:

- `Memory.copy requires dst, src, and size`
- `Memory.copy size must be a number`
- `Memory.copy source unreadable`
- `Memory.copy destination unwritable`
- `Memory.dup requires address and size`
- `Memory.dup size must be a number`
- `Memory.dup unreadable source`

As with current memory APIs, the goal is to raise JS exceptions instead of crashing the target process.

## Testing Strategy

Add local runtime tests first:

1. `Memory.copy(...)` copies bytes between valid regions
2. `Memory.copy(...)` handles overlapping ranges correctly
3. `Memory.copy(...)` rejects unreadable or unwritable ranges
4. `Memory.dup(...)` returns an `ArrayBuffer` with the requested length
5. `Memory.dup(...)` preserves the expected byte content
6. `Memory.dup(...)` rejects unreadable source ranges

After the runtime tests pass, extend the existing `memory_api_smoke.js` so real-device validation also covers block copy and duplication.

## Why This Step First

This should come before `hexdump(...)` because:

- it is a lower-level capability
- it is useful on its own
- `hexdump(...)` can later consume `ArrayBuffer` returned by `Memory.dup(...)`
- it keeps the API growth incremental instead of jumping directly to formatting helpers
