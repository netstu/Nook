# Nook SoDump Design

## Goal

Bring `.so` dumping to Nook with practical parity to the cloned `frida_dump` workflow while keeping the result native to Nook:

- one `nook-cli sodump ...` command
- server mode and gadget mode support
- raw in-memory `.so` dump export
- automatic repaired output generation
- structured metadata for repeatable validation

The target is not to shell out to `frida_dump` or permanently depend on `SoFixer`. Those repositories are reference implementations for workflow and ELF rebuild behavior.

## Problem Statement

Nook already has a working host/device export pattern from `dexdump`, but it does not yet provide a `.so`-focused workflow. Today the user would need to:

- identify a target process manually
- identify a module manually
- export bytes with an ad hoc script
- run an external rebuild tool separately
- track output provenance manually

That is weaker than the cloned `frida_dump` usage, where the normal path is:

1. find a module by name
2. dump the loaded memory image
3. rebuild the dumped ELF
4. pull the repaired artifact back

Nook needs the same end-to-end workflow, but integrated into its own CLI, message transport, and result layout.

## Constraints

- reuse Nook's existing host/device script transport instead of adding a new binary channel
- preserve the current CLI split between normal attach/spawn usage and gadget attach usage
- do not make `SoFixer` an external runtime dependency
- keep first-version scope tight enough to validate on real arm64 Android targets
- avoid breaking current `dexdump`, `gadget`, and attach behavior

## Reference Scope

This work should align with `frida_dump` on the parts that matter operationally:

1. enumerate modules from the target process
2. select a module by name
3. dump the mapped bytes from memory
4. rebuild a usable ELF artifact from the raw dump
5. save deterministic outputs on the host

This work should align with `SoFixer` on the parts that matter technically:

1. rebuild program header file offsets and file sizes from the loaded image
2. recover dynamic-table-backed metadata needed for downstream analysis
3. emit a repaired artifact that can be opened by common ELF tooling

## Recommended Approach

Use a native three-layer design:

- `host/nook-py/nook/sodump.py`
  Host orchestration, output management, metadata, and rebuild invocation.
- `host/nook-py/nook/sodump.js`
  In-process module discovery and chunked memory export.
- `host/nook-py/nook/sofix/`
  Nook-owned ELF repair/rebuild module inspired by `SoFixer`.

This keeps the transport and UX consistent with `dexdump` and keeps repair logic under Nook control.

## Architecture

### 1. Host Orchestration

`sodump.py` should follow the same high-level lifecycle as `dexdump.py`:

1. resolve target mode: attach, spawn, or gadget attach
2. create/load the script
3. query the script for module metadata
4. start chunked export for the selected module
5. collect raw bytes on the host
6. write `raw.so`
7. pass the raw image plus base metadata into the repair module
8. write `fix.so` if repair succeeds
9. write JSON metadata regardless of repair outcome

The host is the right place for naming, result policy, hashing, and failure reporting.

### 2. Device-Side Export

`sodump.js` should stay narrow in scope:

- enumerate modules with `Process.enumerateModules()`
- find a module by exact module name
- return `name`, `path`, `base`, `size`, and `arch`
- memory-protect the range when needed
- export bytes in chunks through the existing script-message path

The device script should not try to implement repair policy or file naming.

### 3. ELF Repair Module

The first Nook-owned repair module should focus on the loaded ELF image use case:

- parse the raw loaded ELF image
- rebuild `PT_LOAD` file offsets and sizes from the loaded view
- recover dynamic-section-derived section-like metadata where possible
- emit a repaired ELF image

The first version should prioritize ELF64 on arm64. The public interface should still be versioned and structured so ELF32 support can be added later without redesigning the CLI.

## CLI Design

`sodump` should be a top-level command, not a `gadget` subcommand.

Primary usage:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so
nook-cli sodump --spawn com.demo.target -U --module libfoo.so
nook-cli sodump -U --gadget com.demo.target --module libfoo.so
```

First-version options:

- `--module`
  Exact module name to dump.
- `--output`
  Output directory.
- `--spawn`
  Spawn a package before dumping.
- `--fix` / `--no-fix`
  Enable or disable repair. Default on.
- `--json`
  Machine-readable summary output.
- `--debug`
  Extra repair diagnostics.

## Output Layout

Outputs should be deterministic and colocated:

```text
.\<target>-sodump\
  libfoo.so.raw.so
  libfoo.so.fix.so
  libfoo.so.json
```

The JSON should include:

- target and mode
- module name and path
- module base and size
- raw artifact path, size, and hash
- fixed artifact path, size, and hash
- repair applied / success flags
- timestamp

## Failure Policy

The command should preserve useful artifacts even on partial failure.

- module not found: fail the command
- raw dump succeeds, repair fails: keep `raw.so` and metadata, return failure
- repair succeeds with degraded metadata: keep `fix.so`, mark degraded state in JSON
- attach path fails: report the chosen route failure clearly without hidden mode switching

This mirrors practical reverse-engineering needs: the raw dump is still valuable even if rebuilding is incomplete.

## Validation Plan

Validation should cover the same axes Nook already uses for runtime tooling:

1. local unit tests for CLI argument normalization and export handling
2. local unit tests for the repair module on synthetic ELF inputs
3. manual device validation in:
   - server attach mode
   - spawn mode
   - gadget attach mode

First real-device success criteria:

- dump a named module from a live target
- write a raw artifact and JSON metadata
- produce a repaired artifact for a normal arm64 shared object
- keep `dexdump` and gadget flows unaffected

## Non-Goals

The first version should not attempt:

- automatic dumping of all modules in a process
- linker-level reconstruction from discontiguous mappings
- full parity for all packed/protected loader tricks
- full ELF32 plus ELF64 support in the same pass
- embedding or shipping upstream `SoFixer` binaries
