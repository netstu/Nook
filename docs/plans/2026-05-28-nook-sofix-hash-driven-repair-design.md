# Nook SoFix Hash-Driven Repair Design

## Goal

Upgrade `host/nook-py/nook/sofix` from mostly layout-based section recovery to dynamic-linker-informed recovery for ELF64 dumps, with the first focus on `DT_HASH` and `DT_GNU_HASH`.

The immediate target is better section sizing and symbol-table boundary recovery for:

- `.hash`
- `.gnu.hash`
- `.dynsym`
- `.gnu.version`
- `.gnu.version_r`

## Problem

The current `sofix` path already reconstructs a usable ELF for common arm64 dumps, but several important section sizes still come from "next known address" heuristics. That is acceptable for first-pass repair, but it leaves precision on the table when:

- the dumped image contains both SysV and GNU hash metadata
- the dynamic layout contains gaps or reordered regions
- a packed or loader-modified image makes "next address" inference unstable

The current implementation is strongest at:

- phdr normalization for loaded-memory views
- `PT_DYNAMIC` recovery from `base-so`
- SoFixer-style `.plt` / `.text&ARM.extab` / trailing `.data` synthesis

The current weak point is that it does not yet use hash-table semantics to infer symbol count or exact table spans.

## Reference Insight

The Kanxue write-up on SoFixer-style ELF recovery emphasizes two ideas that matter here:

1. trust the dynamic table, not section headers
2. recover symbol/linkage structure from runtime linker metadata

For `Nook`, that translates into:

- treat `DT_HASH` / `DT_GNU_HASH` as authoritative linkage metadata
- derive section boundaries from parsed hash contents before falling back to address-order heuristics

## Scope

This phase should add:

- SysV hash parsing (`DT_HASH`)
- GNU hash parsing (`DT_GNU_HASH`)
- hash-informed `.hash` / `.gnu.hash` section sizing
- hash-informed `.dynsym` size inference
- hash-informed `.gnu.version` sizing
- structured debug warnings when hash metadata is malformed or inconsistent

This phase should not add:

- ELF32 support
- symbol-name-driven decrypt / unpack logic
- full linker-grade relocation graph reconstruction
- runtime `dlopen` / memory-access behavior changes

## Recommended Approach

Use a three-tier inference order for dynamic-linker-backed sections:

1. exact parse from dynamic structure contents
2. conservative semantic bound from related dynamic metadata
3. existing next-address heuristic as final fallback

This keeps current success cases intact while improving precision for richer samples.

## Architecture

### 1. Hash Parsers in `elf.py`

Add small, explicit ELF64 helpers for:

- parsing SysV hash headers and computing total byte size
- parsing GNU hash headers and computing conservative byte size
- deriving lower / upper dynsym index bounds from hash structures

These helpers should stay side-effect free and operate on:

- `data`
- `offset`
- `image_size`
- optional dynsym sizing context

They should raise or return structured "unknown" results only for truly malformed layouts.

### 2. Hash-Aware Inference in `rebuilder.py`

Update `rebuilder.py` so dynamic recovery no longer treats `.hash`, `.gnu.hash`, and `.dynsym` independently.

The inference order should be:

- if `DT_HASH` exists and parses cleanly:
  - size `.hash` exactly
  - derive dynsym count from `nchain`
- else if `DT_GNU_HASH` exists and parses cleanly:
  - size `.gnu.hash` exactly
  - derive a conservative dynsym upper bound from chain walk
- else:
  - keep the current next-address heuristic

For `.gnu.version`:

- when dynsym count is known, size it as `dynsym_count * 2`
- otherwise keep the existing heuristic path

For `.gnu.version_r`:

- prefer bounded parse from `DT_VERNEED` + `DT_VERNEEDNUM`
- otherwise keep heuristic fallback

### 3. Failure Policy

Malformed hash metadata should not fail the entire repair by default.

Instead:

- exact parser fails -> emit warning
- fall back to the current heuristic
- only raise when the ELF is fundamentally unreadable

This keeps `sofix` aligned with the current practical reverse-engineering workflow: preserve best-effort output first, precision second.

## Data Flow

1. parse ELF header and phdrs
2. normalize loaded-memory phdr view
3. parse dynamic entries
4. build a dynamic map
5. build a hash-derived inference context:
   - exact `.hash` size if possible
   - exact / conservative `.gnu.hash` size if possible
   - dynsym count if possible
   - version bounds if possible
6. synthesize section table using this context
7. append section headers and emit warnings

## Testing Strategy

Focus on synthetic ELF64 fixtures first.

Required new tests:

- `DT_HASH` sample where `.hash` size and dynsym count can be computed exactly
- `DT_GNU_HASH` sample where dynsym upper bound must come from chain walk
- malformed `DT_HASH` sample that falls back with warning
- malformed `DT_GNU_HASH` sample that falls back with warning
- `DT_VERNEED` sample that sizes `.gnu.version_r` from linked structure instead of next-address ordering

Keep existing real-sample parity checks as regression evidence:

- `aboutbear/libSecShell.so`
- `libtmessages.49.so.dump.so`

## Success Criteria

This phase is complete when:

- `.hash` section size comes from real SysV hash parsing when present
- `.gnu.hash` section size comes from real GNU hash parsing when present
- `.dynsym` size prefers hash-derived symbol count over "next address"
- `.gnu.version` size follows dynsym count when known
- malformed hash layouts degrade with warnings, not silent wrong exactness claims
- existing `sofix`, `sodump`, and CLI tests still pass

## Expected Outcome

After this phase, `Nook sofix` should still intentionally preserve more GNU metadata than upstream `SoFixer`, but the recovered ELF should be materially closer to a true dynamic-linker view and less dependent on address-order coincidence.
