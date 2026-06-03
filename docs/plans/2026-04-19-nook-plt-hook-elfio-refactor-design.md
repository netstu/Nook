# Nook PLT Hook ELFIO Refactor Design

**Date:** 2026-04-19

**Status:** Approved

## Goal

Refactor `Nook`'s PLT/GOT hook implementation with a low-risk, incremental approach: first fix existing correctness issues, then replace the handwritten ELF metadata parsing with `third_party/elfio`, while keeping the runtime patching path and public API stable.

## Problem Statement

The current PLT hook path works, but it couples several responsibilities in one implementation:

- module discovery from `/proc/self/maps`
- in-memory ELF parsing
- symbol lookup through handwritten ELF/GNU hash logic
- relocation scanning
- runtime GOT slot patching with `mprotect`

That coupling makes the implementation harder to validate and evolve. There are also existing correctness risks in the current code path:

- the non-PLT relocation loop in `elf_reader.cpp` reads the first relocation entry repeatedly instead of iterating correctly
- `get_module_base()` may call `fclose()` on a null file handle
- `hookInternally()` does not restore the original page protection after patching
- the page protection logic assumes a single page is always enough

The goal of this refactor is not to redesign the hook mechanism. The goal is to preserve the current runtime patch strategy and reduce parser complexity and risk.

## Current Architecture

The current native PLT hook call chain is:

1. `NookNativeHookHookSymbol()` in `src/native_hook/NookNativeHook.cpp`
2. `ElfHooker::get_module_base()` in `src/native_hook/elfhooker/tools.cpp`
3. `ElfReader::parse()` in `src/native_hook/elfhooker/elf_reader.cpp`
4. `ElfReader::hook()` in `src/native_hook/elfhooker/elf_reader.cpp`
5. `ElfReader::hookInternally()` to patch the runtime slot

This means one class (`ElfReader`) is responsible for both:

- understanding ELF metadata
- deciding which relocation slots to patch
- performing the hook write

That is the main design problem.

## Design Principles

- Keep the public API unchanged.
- Keep the runtime patching model unchanged.
- Separate metadata parsing from memory patching.
- Make the first phase purely correctness-focused.
- Use `ELFIO` only where it reduces handwritten parser complexity.
- Do not replace `/proc/self/maps`-based module discovery in this refactor.

## Target Architecture

After the refactor, the implementation should be split into two internal layers.

### Runtime Patch Layer

This layer remains responsible for runtime state and memory writes:

- loaded module discovery
- runtime base address calculation
- slot address calculation from relocation offsets
- page protection changes
- saving the original function pointer
- writing the replacement pointer

This layer should remain close to the current `hookInternally()` and module-discovery logic, but with the identified correctness fixes applied.

### ELF Metadata Layer

This new layer is responsible only for metadata extraction:

- reading the target ELF file from disk
- enumerating dynamic symbols
- enumerating relocation entries
- identifying the relocation entries that reference the requested symbol
- returning relocation offsets and relocation types to the runtime patch layer

This layer should use `third_party/elfio`, similar to the existing usage pattern in `src/java_hook/JVM.cpp`, but specialized for PLT/GOT hook metadata extraction rather than symbol-address recovery.

## Scope Boundaries

### In Scope

- fix correctness issues in the current PLT hook implementation
- introduce an `ELFIO`-based parser for dynamic symbols and relocation entries
- keep `NookNativeHookHookSymbol()` as the stable entry point
- keep the current runtime patching strategy
- add focused tests for parser results and runtime patch safety

### Out of Scope

- redesigning the public native hook API
- replacing runtime module lookup with `xdl` or another loader abstraction
- implementing a new inline hook engine
- rewriting the hook implementation to patch code instead of GOT/PLT slots
- broad build-system cleanup outside what is needed for the refactor

## Proposed File Layout

The exact names can still be adjusted, but the internal split should look like this:

```text
src/
  native_hook/
    NookNativeHook.cpp
    elfhooker/
      module_match.h
      module_match.cpp
      tools.h
      tools.cpp
      runtime_patch.h
      runtime_patch.cpp
      elfio_image_parser.h
      elfio_image_parser.cpp
      elf_reader.h
      elf_reader.cpp
```

Recommended role for each file:

- `tools.*`: module lookup, base address helpers, shared hashing helpers if still needed
- `runtime_patch.*`: page protection, slot rewrite, original pointer capture
- `elfio_image_parser.*`: `ELFIO`-based symbol/relocation extraction
- `elf_reader.*`: temporary compatibility adapter, progressively reduced until it can be removed

## Migration Strategy

### Phase 1: Correctness and Safety Fixes

Do not introduce `ELFIO` changes yet.

Apply targeted fixes to the current implementation:

- fix relocation iteration in the non-PLT relocation loop
- make `get_module_base()` robust when `fopen()` fails
- restore page permissions after patching
- compute the correct protected page range for the slot being written

This phase should not change the public API or the overall control flow.

### Phase 2: Add the ELFIO Metadata Parser

Add a new parser that:

- loads the target ELF file path from disk
- reads dynamic symbol tables
- reads relocation sections
- returns a list of relocation entries matching the requested symbol

The parser should return metadata only. It must not perform `mprotect`, slot writes, or runtime pointer updates.

### Phase 3: Switch the Hook Path to the New Split

Change the internal implementation of `NookNativeHookHookSymbol()` so that it:

1. resolves the loaded module and its runtime base
2. asks the `ELFIO` metadata layer for relocation matches
3. converts relocation offsets into runtime addresses
4. delegates slot rewriting to the runtime patch layer

At the end of this phase, `ElfReader` should either:

- become a thin compatibility wrapper over the new components, or
- be removed if no callers depend on it anymore

## Data Flow

The new internal data flow should be:

1. API call: `NookNativeHookHookSymbol(module_name, symbol_name, replacement, original)`
2. Runtime layer resolves the loaded module path and base address
3. Metadata layer loads the ELF file from disk using `ELFIO`
4. Metadata layer returns matching relocation records
5. Runtime layer computes each slot address as `runtime_bias + relocation_offset`
6. Runtime layer patches the selected slot and stores the original pointer

This preserves the current hook model while making the parse phase independently testable.

## Testing Strategy

Tests should be split by responsibility.

### Phase 1 Tests

Add or expand focused tests for:

- relocation iteration over multiple entries
- missing-module handling in `get_module_base()`
- page-protection restoration behavior
- repeated hook attempts on the same slot

These tests can be small native unit tests and should avoid requiring device injection.

### Phase 2 Tests

Add parser-only tests for the new `ELFIO` layer:

- load a known ELF file
- confirm symbol lookup works for `.dynsym`
- confirm relocation enumeration works for `.rel.plt/.rela.plt` and `.rel.dyn/.rela.dyn`
- confirm relocation filtering returns the expected symbol/type pairs

These should run as host-side tests.

### Phase 3 Tests

Keep an integration-level validation path:

- build succeeds for Android
- `NookNativeHookHookSymbol()` returns success for the target module and symbol
- the original pointer is populated
- the existing `strcmp` test payload still works in the target app

## Risks

### Runtime/File Layout Mismatch

`ELFIO` reads the file image from disk, while patching happens against the in-memory mapped image. This mismatch is manageable, but only if the refactor preserves the runtime base/bias conversion step explicitly.

### Over-Refactoring

If `ELFIO` is allowed to absorb runtime responsibilities, the refactor becomes harder to reason about and debug. The metadata/runtime split must remain strict.

### Test Gaps

If the new parser is introduced without parser-only tests, failures will still appear only at injection time. That would defeat the purpose of the refactor.

## Recommendation

Proceed with the incremental mixed approach:

- fix the current code first
- introduce `ELFIO` as a metadata parser only
- keep runtime patching in the existing native hook layer

This gives the best balance of stability, maintainability, and verifiability for the current `Nook` codebase.
