# Nook NativePointer Byte Array Design

**Goal**

Add:

- `NativePointer.readByteArray(length): ArrayBuffer`
- `NativePointer.writeByteArray(value): NativePointer`

This fills a practical gap between scalar reads/writes and `Memory.copy(...)`, and enables direct binary dump / patch workflows from JS.

## Scope

In scope:

- `readByteArray(length)` returning a real `ArrayBuffer`
- `writeByteArray(value)` accepting:
  - `ArrayBuffer`
  - JS numeric arrays like `[0x41, 0x42]`
- readable / writable range validation before dereference

Out of scope:

- typed array direct support beyond `.buffer`
- `readVolatile` / `writeVolatile`
- partial writes

## Behavior

### `readByteArray(length)`

- `length` must be a positive number
- unreadable pointers throw
- return value is an `ArrayBuffer` copy of the requested bytes

### `writeByteArray(value)`

- `value` may be:
  - `ArrayBuffer`
  - array of byte numbers `0..255`
- destination range must be writable
- returns the original `NativePointer`

## Testing Strategy

Add runtime tests for:

1. read returns expected bytes
2. write from `ArrayBuffer` works
3. write from number array works
4. unreadable pointer read throws
5. unwritable pointer write throws
