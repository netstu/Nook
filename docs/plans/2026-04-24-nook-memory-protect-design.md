# Nook Memory Protect Design

**Goal**

Add a first minimal `Memory.protect(address, size, protection)` API so QuickJS scripts can change page protections on native memory regions.

This is the next foundational memory primitive after:

- scalar memory access
- `Memory.copy(...)`
- `Memory.dup(...)`
- `hexdump(...)`

and it is a prerequisite for later patching-oriented features such as write-then-restore workflows, code modification helpers, and more Frida-like low-level APIs.

## Context

The design document already defines:

```javascript
function protect(address: NativePointer, size: number, protection: string): boolean;
```

The current runtime does not yet expose any direct page-permission control. However, the native codebase already contains the required underlying building blocks:

- `mprotect` usage on Android/Linux paths
- `VirtualProtect` usage on Windows paths
- page-alignment logic in native hook code

So this step is mainly about exposing a safe, minimal JS-facing wrapper rather than inventing a brand new capability.

## Scope

In scope:

- add `Memory.protect(address, size, protection): boolean`
- support complete three-character protection strings
- support:
  - `NativePointer`
  - pointer string
  - numeric pointer input, via the same parsing model already used elsewhere
- map protection strings onto platform page protection flags
- perform page alignment internally

Out of scope:

- `Memory.queryProtection(...)`
- `Process.enumerateRanges(...)`
- verifying post-state through `/proc/self/maps`
- richer permission objects
- exposing previous protection state
- patch helpers built on top of `protect`

## Recommended API

Examples:

```javascript
const block = Memory.alloc(4096);
send({ type: 'send', payload: String(Memory.protect(block, 4096, 'r--')) });
send({ type: 'send', payload: String(Memory.protect(block, 4096, 'rw-')) });
```

Expected payloads:

```text
true
true
```

## Protection String Model

The `protection` argument should be exactly 3 characters:

- position 0: `r` or `-`
- position 1: `w` or `-`
- position 2: `x` or `-`

Supported examples:

- `---`
- `r--`
- `rw-`
- `r-x`
- `rwx`

Rejected examples:

- `rx`
- `read`
- `R--`
- `rwxp`
- `abc`

This matches the expected Frida-style mental model and avoids special-case whitelists.

## Behavior

### Argument validation

- `address` must resolve to a non-zero pointer
- `size` must be a positive number
- `protection` must be a valid 3-character protection string

Invalid arguments should raise JS exceptions.

### System call behavior

- on success, return `true`
- on system-call failure, return `false`

This keeps script-side control simple and avoids over-designing error transport at this stage.

### Page alignment

The implementation should not expect callers to pass page-aligned addresses.

Instead:

1. align `address` down to page start
2. align `address + size` up to page end
3. apply the protection over the full page range

This is the same practical behavior native hook code already depends on.

## Architecture

Implementation should live in `src/agent_runtime/js_runtime.cpp`.

Recommended pieces:

1. parse pointer input
2. parse and validate protection string
3. compute page-aligned range
4. dispatch to:
   - `mprotect(...)` on Android/Linux
   - `VirtualProtect(...)` on Windows
5. return JS boolean

The platform-specific mapping should stay local to the runtime implementation. There is no need to introduce a new shared framework abstraction yet.

## Error Handling

Use narrow runtime errors for invalid caller input:

- `Memory.protect requires address, size, and protection`
- `Memory.protect address must be a non-zero pointer value`
- `Memory.protect size must be a positive number`
- `Memory.protect protection must be a 3-character string`
- `Memory.protect protection must use only r, w, x, and -`

System-call failures should not throw; they should return `false`.

## Testing Strategy

Add local runtime tests first:

1. invalid protection strings throw
2. zero size throws
3. null pointer throws
4. `Memory.protect(block, 4096, 'r--')` returns `true`
5. `Memory.protect(block, 4096, 'rw-')` returns `true`

This phase intentionally avoids tests that would try to write into a truly read-only page from the same process, because those are more likely to turn into process-crashing tests than useful regression checks.

After runtime tests pass, extend `memory_api_smoke.js` with a short `protect:true:true` message on a page-sized allocation.

## Why This Step

This is the right next API because:

- it is foundational for later patching and replace-style workflows
- it maps closely to existing native infrastructure
- it is much lower risk than jumping straight to `scan(...)`
- it aligns with the long-term Frida-like API direction without forcing extra abstractions today
