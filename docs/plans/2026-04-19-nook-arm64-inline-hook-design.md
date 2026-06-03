# Nook Arm64 Inline Hook Design

**Date:** 2026-04-19

**Status:** Approved

## Goal

Add a first inline hook backend to `Nook` for `arm64/aarch64` while preserving the project structure that was just established for native hook work:

- `src/framework/` for public entrypoints
- `src/native_hook/core/` for shared runtime helpers
- `src/native_hook/plt_hook/` for PLT-specific code
- `src/native_hook/inline_hook/` for inline-specific code

The new design must explicitly separate PLT hook and inline hook at the public API layer instead of hiding backend selection behind one generic native hook function.

## Problem Statement

`Nook` currently has a working PLT hook path, but it does not yet provide an inline hook backend. The previous `NookNativeHook` API shape also made backend intent ambiguous because the caller could not clearly express whether they wanted:

- import table / relocation slot replacement
- function entry inline patching

That ambiguity becomes a design problem once both backends exist. PLT hook and inline hook have different:

- target requirements
- runtime behavior
- failure modes
- restoration semantics

Inline hook also needs stateful unhook support, because it modifies function entry instructions and must preserve:

- original bytes
- trampoline memory
- rewritten instruction length

Therefore the public API should make the backend explicit.

## External References Reviewed

The design direction is based on the observed patterns in the reference projects the user provided:

- `Dobby`: minimal address-based inline hook API with `original` as trampoline
- `shadowhook` (`android-inline-hook`): explicit support for both address hook and symbol-name hook
- `ReZeroHook`: practical demonstration of a small `arm64` inline hook built from instruction rewrite, trampoline allocation, `mprotect`, and cache flush

The adopted design borrows the useful ideas, but does not embed those frameworks directly into `Nook`.

## Design Principles

- Keep PLT hook and inline hook separate in the public API.
- Keep `framework/core/backend` layering consistent with the current repository structure.
- Implement only `arm64/aarch64` in the first inline hook version.
- Keep the first implementation intentionally small and controlled.
- Return the inline hook trampoline via `original`.
- Require an opaque handle for inline unhook.
- Avoid bringing in `Dobby` or `shadowhook` as core runtime dependencies for the first version.

## Public API Design

The public API will be split into two header surfaces.

### PLT Hook API

Create `include/nook/NookPltHook.h` with an explicit PLT-only API:

```c
NookStatus NookPltHookInitialize(void);
NookStatus NookPltHookIsAvailable(int* available);
NookStatus NookPltHookSymbol(const char* module_name,
                             const char* symbol_name,
                             void* replacement,
                             void** original);
```

Characteristics:

- PLT hook remains symbol-oriented
- no hook handle is required
- no unhook API is introduced in the first PLT split
- behavior stays aligned with the current working PLT implementation

### Inline Hook API

Create `include/nook/NookInlineHook.h` with an explicit inline-only API:

```c
NookStatus NookInlineHookInitialize(void);
NookStatus NookInlineHookIsAvailable(int* available);
NookStatus NookInlineHookAddress(void* target_address,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle);
NookStatus NookInlineHookSymbol(const char* module_name,
                                const char* symbol_name,
                                void* replacement,
                                void** original,
                                void** hook_handle);
NookStatus NookInlineUnhook(void* hook_handle);
```

Characteristics:

- supports direct address hook
- supports symbol-name hook by resolving the symbol to an address first
- returns trampoline via `original`
- returns an opaque `hook_handle` used for restore/unhook

### Compatibility Header

`include/nook/NookNativeHook.h` should stop acting as the long-term public surface for backend selection.

Recommended first-step compatibility policy:

- keep the file present so existing includes do not break immediately
- update it to include the new split headers or provide compatibility wrappers
- document that new code should prefer `NookPltHook.h` and `NookInlineHook.h`

## Target File Layout

### Public Headers

```text
include/nook/NookPltHook.h
include/nook/NookInlineHook.h
include/nook/NookNativeHook.h   (compatibility layer)
```

### Framework Layer

```text
src/framework/NookPltHook.cpp
src/framework/NookInlineHook.cpp
```

Responsibilities:

- public API entrypoints
- argument validation
- one-time initialization
- backend dispatch to internal helpers

### Shared Native Hook Core

```text
src/native_hook/core/
  native_hook_symbol_resolver.h/.cpp
  executable_memory.h/.cpp
  patch_utils.h/.cpp
```

Exact filenames can still be adjusted, but the shared layer should own:

- module and symbol resolution reused by backends
- executable memory helpers
- instruction cache flush wrapper
- page protection helpers
- shared status translation / small runtime helpers

### Inline Hook Backend

```text
src/native_hook/inline_hook/
  inline_hook_impl.h/.cpp
  arm64_instruction_relocator.h/.cpp
  trampoline_allocator.h/.cpp
  inline_hook_record.h/.cpp
```

Responsibilities:

- hook record lifecycle
- trampoline allocation and release
- target patch generation
- instruction relocation for `arm64`
- restore path for unhook

## Inline Hook Data Model

Each installed inline hook needs an internal record similar to:

```text
target_address
replacement_address
trampoline_address
patched_length
original_bytes[]
active flag
```

This record is opaque to callers. `hook_handle` should simply point to this internal record.

## Arm64 Inline Hook Execution Flow

### Address Hook

1. Validate `target_address`, `replacement`, `original`, `hook_handle`
2. Read enough target instructions to cover the patch jump length
3. Relocate the copied instructions into trampoline memory
4. Append a jump from trampoline tail to `target + patched_length`
5. Patch target entry with a jump to `replacement`
6. Flush instruction cache
7. Return trampoline through `original`
8. Return record pointer through `hook_handle`

### Symbol Hook

1. Resolve `module_name + symbol_name` to a code address
2. Delegate to the same internal address-hook path

### Unhook

1. Validate `hook_handle`
2. Restore saved original bytes to target entry
3. Flush instruction cache
4. Release or retire trampoline state
5. Mark record inactive

## Arm64 Instruction Relocation Scope

The first version should support the common PC-relative instructions needed for normal function-entry relocation:

- `ADR`
- `ADRP`
- `B`
- `BL`
- `B.cond`
- `CBZ`
- `CBNZ`
- `TBZ`
- `TBNZ`
- `LDR literal`

Anything outside this supported subset should fail cleanly with an internal error rather than silently producing an unsafe trampoline.

## Patch Strategy

The first version should use a simple absolute jump patch strategy suitable for `arm64`.

Recommended implementation shape:

- write a fixed-size entry patch at the target
- make `patched_length` at least the size required by that entry patch
- restore exactly that saved byte range on unhook

The concrete instruction sequence is an implementation detail, but the design assumes:

- writable executable page transition through `mprotect`
- cache flush after trampoline generation and after target patch

## Error Handling

PLT hook and inline hook should continue returning `NookStatus`.

For the first inline hook version, map internal failures to existing status values where possible:

- invalid arguments -> `NOOK_STATUS_INVALID_ARGUMENT`
- unsupported architecture or unsupported instruction pattern -> `NOOK_STATUS_NOT_SUPPORTED`
- allocation or patching failure -> `NOOK_STATUS_INTERNAL_ERROR`

If current public status codes are insufficient, extend `NookStatus` conservatively rather than leaking backend-specific integers.

## Initialization Policy

The two backends should initialize independently:

- `NookPltHookInitialize()` sets up PLT hook readiness only
- `NookInlineHookInitialize()` sets up inline hook readiness only

The framework layer may still lazily initialize on first use, matching the current `Nook` style.

## Testing Strategy

Testing should be split into host-side logic checks and Android runtime verification.

### Host-Side Tests

- structure manifest updated for new files
- public header compile checks for new headers
- `arm64` relocator tests for supported instruction rewriting
- inline hook record lifecycle tests
- trampoline allocator tests

### Android Runtime Tests

Add a dedicated inline hook example payload that:

- resolves or directly references a test target function
- installs inline hook
- confirms replacement is reached
- confirms `original` trampoline remains callable
- confirms unhook restores original behavior

The first Android integration target should stay narrow and deterministic, similar to the existing `strcmp` validation flow.

## Out of Scope

The first version does not include:

- `arm32`
- multi-hook chaining on the same address
- interceptor or instrumentation mode
- linker callbacks for future ELF loads
- third-party runtime integration of `Dobby` or `shadowhook`

## Expected Outcome

After this design is implemented, `Nook` will have:

- explicit public separation between PLT hook and inline hook
- a first-party `arm64` inline hook backend
- unhook support for inline hook
- project structure that remains stable when `arm32` is added later
