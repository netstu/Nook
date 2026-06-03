# Nook Hexdump Style Design

**Goal**

Extend the current minimal `hexdump(...)` helper with:

- `options.header`
- `options.ansi`

and move the output closer to Frida's common hexdump style:

- address column
- hex byte column
- ASCII column

without yet adding extra formatting knobs.

## Context

The current runtime already supports:

- `hexdump(ArrayBuffer, { offset?, length? })`
- `hexdump(NativePointer, { offset?, length })`
- plain lowercase hexadecimal output
- 16 bytes per line

That is functionally useful, but still well below Frida's typical debugging experience. For real reverse-engineering use, the visual layout matters:

- users expect to correlate bytes with addresses
- printable strings should be recognizable immediately in the ASCII view
- ANSI coloring is often expected in terminal-oriented workflows

This phase upgrades the display while keeping the API surface intentionally small.

## Scope

In scope:

- support `options.header?: boolean`
- support `options.ansi?: boolean`
- add address column
- add ASCII column
- keep line width fixed at 16 bytes
- keep `target`, `offset`, and `length` behavior from the current implementation

Out of scope:

- configurable bytes per line
- configurable grouping
- uppercase output mode
- custom color themes
- truncation / summary mode
- exact byte-for-byte Frida clone across every edge case

## Recommended API

Examples:

```javascript
const p = Memory.allocUtf8String('hello');
send({ type: 'send', payload: hexdump(p, { length: 5, header: true }) });
```

```javascript
const blob = Memory.dup(p, 5);
send({ type: 'send', payload: hexdump(blob, { ansi: true }) });
```

## Output Model

### Common line structure

Each data line should contain:

1. address column
2. hex byte column
3. ASCII column

Conceptual shape:

```text
00000000  68 65 6c 6c 6f                                   hello
```

For a real pointer:

```text
7f12345678  68 65 6c 6c 6f                                   hello
```

### Address column

- `NativePointer` input:
  - use the real runtime address plus per-line offset
- `ArrayBuffer` input:
  - use logical offsets rather than fake native pointers
  - the first line starts at `offset`

This keeps the layout consistent without lying about the source data.

### Hex column

- 16 bytes per line
- lowercase hex
- two digits per byte
- single spaces between bytes
- short last lines are padded so the ASCII column still aligns

### ASCII column

- printable ASCII bytes (`0x20` to `0x7e`) render as characters
- everything else renders as `.`

This is the standard and most readable compromise.

### Header

When `header: true`, prepend one title line that visually labels the byte positions.

Example:

```text
          00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
00000000  68 65 6c 6c 6f                                   hello
```

The exact spacing may differ slightly, but the goal is clear visual alignment, not pixel-perfect mimicry.

### ANSI coloring

When `ansi: true`, apply lightweight ANSI styling:

- address column: dim or cyan-like
- ASCII column: green-like
- hex bytes: default or neutral

Color must be optional and must not alter the non-ANSI layout beyond inserted escape sequences.

## Architecture

The implementation should continue to use one `JsHexdump(...)` entrypoint in `src/agent_runtime/js_runtime.cpp`.

Recommended internal flow:

1. parse `target`
2. parse `offset`, `length`, `header`, `ansi`
3. resolve a normalized byte-span view:
   - `const uint8_t* data`
   - `size_t length`
   - `uint64_t base_address`
   - `bool base_is_real_address`
4. pass that view into a formatting helper
5. formatting helper emits:
   - optional header line
   - one line per 16 bytes
   - optional ANSI sequences

Keeping the formatting logic unified prevents divergence between pointer and buffer inputs.

## Error Handling

No major new categories of runtime errors are needed here.

The existing validation should remain:

- pointer input still requires `length`
- unreadable pointer ranges still fail
- invalid `ArrayBuffer` slices still fail

For the new options:

- `header` must be boolean-coercible through the runtime's existing property access pattern
- `ansi` must be boolean-coercible through the same path

If omitted, both default to `false`.

## Testing Strategy

Add local runtime tests first:

1. `hexdump(ArrayBuffer, { header: true })` includes a correct header line
2. `hexdump(ArrayBuffer, { ansi: true })` includes ANSI escape sequences
3. `hexdump(NativePointer, { length, header: true })` includes address and ASCII columns
4. non-printable bytes render as `.`
5. mixed printable and non-printable bytes render correctly

After that, extend `memory_api_smoke.js` with one short `hex-styled:` message so the feature is visible on device without flooding logs.

## Why This Step

This is the right follow-up to the minimal `hexdump(...)` helper because:

- it upgrades a working API rather than inventing a new one
- it targets debugging ergonomics directly
- it moves the output substantially closer to Frida
- it keeps future work focused: if later refinements are needed, they build on the same formatter instead of replacing it
