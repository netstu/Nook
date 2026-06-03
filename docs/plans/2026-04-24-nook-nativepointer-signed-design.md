# Nook NativePointer Signed Access Design

**Goal**

Add the next minimal Frida-like `NativePointer` capability by supporting signed scalar memory access:

- `readS8()`
- `readS16()`
- `readS32()`
- `readS64()`
- `writeS8(value)`
- `writeS16(value)`
- `writeS32(value)`
- `writeS64(value)`

This extends the existing `readU*` / `writeU*` path without expanding scope to bulk memory helpers or formatting helpers.

## Context

The current QuickJS runtime already exposes:

- `ptr(...)`
- `NULL`
- `Memory.alloc(...)`
- `Memory.allocUtf8String(...)`
- `NativePointer.readPointer()`
- `NativePointer.readU8()/readU16()/readU32()/readU64()`
- `NativePointer.writePointer()/writeU8()/writeU16()/writeU32()/writeU64()`
- minimal `uint64(...)` / `int64(...)`

This means the unsigned half of the scalar memory model already exists. The next smallest useful step toward Frida-like script ergonomics is to fill in the signed half, especially for:

- negative integer reads from native structures
- signed sentinel values such as `-1`
- complete `NativePointer` scalar read/write symmetry

## Scope

In scope:

- add `readS8()`, `readS16()`, `readS32()`, `readS64()`
- add `writeS8()`, `writeS16()`, `writeS32()`, `writeS64()`
- keep the existing readable/writable range validation
- return JS `number` for signed 8/16/32-bit reads
- return minimal `Int64` object for `readS64()`
- accept `number`, `string`, `int64(...)`, and `uint64(...)` for `writeS64(...)`

Out of scope:

- `Memory.copy`, `Memory.dup`, `Memory.protect`, `Memory.scan`
- `hexdump(...)`
- float or double support
- arithmetic methods on `Int64` / `UInt64`
- byte-array or string write helpers
- changing protocol, host SDK, or server behavior

## Recommended API

Expected script shape:

```javascript
const block = Memory.alloc(16);
block.writeS8(-1);
block.writeS16(-2);
block.writeS32(-3);
block.writeS64(int64('-4'));

send({
  type: 'send',
  payload: [
    block.readS8(),
    block.add(2).readS16(),
    block.add(4).readS32(),
    block.add(8).readS64().toString()
  ].join(':')
});
```

Expected payload:

```text
-1:-2:-3:-4
```

## Architecture

The design should reuse the existing `JsNativePointerRead(...)` and `JsNativePointerWrite(...)` dispatcher path instead of introducing a second signed-only implementation.

### Read Path

Add new read method bindings in `MakeNativePointer(...)`:

- `readS8`
- `readS16`
- `readS32`
- `readS64`

The dispatcher already uses `magic` to select width. Extend that with signed variants:

- signed 8/16/32-bit reads:
  - load raw bytes
  - reinterpret with the correct signed type
  - return a JS `number`
- signed 64-bit reads:
  - load raw bytes into `int64_t`
  - preserve the exact bit pattern
  - wrap it with `MakeInteger64Object(..., true)`

### Write Path

Add new write method bindings in `MakeNativePointer(...)`:

- `writeS8`
- `writeS16`
- `writeS32`
- `writeS64`

Write behavior:

- `writeS8/S16/S32`:
  - accept JS number input
  - parse via QuickJS integer conversion
  - narrow to the destination signed width
  - write the two's-complement representation
- `writeS64`:
  - reuse the existing 64-bit parsing helpers
  - accept `number|string|int64(...)|uint64(...)`
  - preserve raw bits when the value is negative

## Error Handling

No new memory safety behavior should be introduced here.

The current runtime behavior remains:

- null pointer access throws
- unreadable ranges throw `TypeError`
- unwritable ranges throw `TypeError`

New type errors should be narrow and explicit:

- `writeS8 value must be a number`
- `writeS16 value must be a number`
- `writeS32 value must be a number`
- `writeS64 value must be a number, string, or Int64/UInt64`

## Testing Strategy

This change should follow the existing runtime test style and stay fully local to the JS runtime test binary.

Add failing tests first for:

1. signed 8/16/32 reads sign-extend correctly
2. signed 8/16/32 writes round-trip correctly
3. `readS64()` returns an `Int64` object whose `toString()` matches the exact signed value
4. `writeS64(int64('-1'))` round-trips to `-1`

Then run:

- targeted runtime test binary
- existing broader runtime test binary
- Python CLI regression
- Android NDK build

## Why This Step First

This is the smallest high-value API increment after the current unsigned memory work:

- it closes an obvious symmetry gap in `NativePointer`
- it builds directly on code already added for `Int64` / `UInt64`
- it avoids pulling in much larger `Memory.*` or formatting APIs too early

That keeps the implementation incremental and aligned with the stated goal: become more Frida-like without trying to jump straight to the entire API surface in one change.
