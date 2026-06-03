# Nook DexDump Parity Design

## Goal

Bring `nook-cli dexdump` up to practical parity with `frida-dexdump` for real APKs while preserving Nook's current command surface, chunked binary export path, and structured metadata.

The immediate success target is simple:

- `frida0x1` must keep working
- `cn.n1ng.aboutbear` must yield the original main `classes.dex`
- default mode should stay usable without turning every scan into a full-process brute-force walk

## Problem Statement

The current implementation proves the transport path works, but the search pipeline is still too weak for real apps:

- it often finds only tiny helper dex blobs
- it can miss the main APK dex entirely
- the current range cap is too aggressive for some real processes
- size resolution still trusts `file_size` or range fallback more than a validated `map_list`
- deep search is narrower than `frida-dexdump`

`aboutbear` demonstrates the gap clearly:

- APK `classes.dex`: `8224872` bytes
- current Nook dump artifacts: a handful of `6 KB` to `56 KB` dex files
- no dumped artifact matches the original APK hash

## Constraints

- keep the existing `nook-cli dexdump` CLI shape
- keep the current JSON RPC plus binary script-message export model
- do not couple this work to gadget-specific flows
- preserve current metadata output and host-side repair pipeline
- default behavior should remain reasonably fast on device

## Reference Parity Scope

This work should align with `frida-dexdump` on the parts that matter for real recovery:

1. `map_list` validation and real-size derivation
2. broader deep-search entry points
3. fallback export when `real_size` and range-tail size differ
4. scanning of relevant larger `r--` ranges that the current heuristic can skip

Nook should keep its own improvements:

- structured candidate metadata
- chunked export with explicit tokens
- host-side JSON metadata
- explicit error counters

## Root Cause

The miss is not a transport failure. It is a discovery-quality failure.

The current device-side script differs from `frida-dexdump` in three important ways:

1. it uses a bounded `max_range_size` that can exclude useful `r--` regions
2. it resolves `real_size` too conservatively and does not derive the end from `map_list`
3. its deep-search coverage is narrower than the reference implementation

As a result, Nook currently prefers small valid dex fragments over the real app dex.

## Design

### 1. Range Selection

Keep pre-scan filtering, but relax it enough to stop excluding real app dex mappings.

New default strategy:

- scan `r--` ranges by default
- keep `rw-` ranges behind `--deep`
- increase the default `max_range_size` ceiling so real `20 MB` to `32 MB` readonly regions are not skipped
- continue sorting smaller ranges first, but do not let the cap exclude realistic app-backed readonly slabs

Recommended default ceiling:

- derive from `max_dex_size`
- allow a multiplier over `max_dex_size`
- cap at `64 MB`, not `16 MB`

This keeps a guardrail without reproducing the current false-negative behavior.

### 2. Header and Map Validation

Port the useful `frida-dexdump` logic, but keep Nook's structured return values.

Device-side validation should add:

- `getMapsAddress()`
- `getMapsEnd()`
- `verifyByMaps()`
- `verifyIdsOff()`
- `resolveRealDexSize()`

Export size priority becomes:

1. validated `map_list` end
2. valid `file_size`
3. range-tail fallback

Candidate metadata should expose all three when available:

- `declared_size`
- `real_size`
- `fallback_size`

### 3. Search Modes

Normal mode:

- keep magic scan: `64 65 78 0a 30 ?? ?? 00`
- add range-base verification path where appropriate, like the reference tool

Deep mode:

- keep current broken-header path
- add `header_size` backtracking from `70 00 00 00`
- verify through `map_list`
- require sane id offsets before accepting a broken-header candidate
- when `real_size != fallback_size`, emit both sizes as export options, but keep only one candidate record with both fields

This keeps the candidate list cleaner than `frida-dexdump` while preserving recovery ability.

### 4. Host Export Policy

Keep the current host pipeline, but make it smarter about which size to dump first.

Default export policy:

- dump `real_size` first
- if host receives a valid dex whose content hash is new, keep it
- when `--deep` is enabled and `fallback_size > real_size`, optionally try fallback export for medium/low-confidence candidates

This aligns with the reference tool's practical behavior without dumping every fallback blindly in normal mode.

### 5. Metadata

Retain current metadata and add fields needed for diagnosis:

- `declared_size`
- `real_size`
- `fallback_size`
- `maps_ok`
- `ids_ok`
- `range_protection`

That gives enough evidence when a candidate is valid but not the app's primary dex.

## CLI Behavior

The current CLI surface stays stable:

- `nook-cli dexdump <target>`
- `nook-cli dexdump --spawn <package>`
- `--deep` remains the switch for broader scanning and fallback export behavior

No breaking CLI changes are required for this parity pass.

## Validation Plan

Real-device validation must cover both a toy target and a real APK:

1. `com.ad2001.frida0x1`
2. `cn.n1ng.aboutbear`

Success criteria:

- `frida0x1` still dumps usable dex artifacts
- `aboutbear` yields a dumped dex whose size and SHA256 match the original APK `classes.dex`
- host tests still pass

## Non-Goals

- full ART event-driven dex capture
- class-loader labeling
- gadget-specific automatic dexdump
- checksum/signature repair beyond the current minimal repair path
