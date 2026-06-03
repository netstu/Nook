# Nook DexDump Design

## Context Retention Rules

These notes exist to prevent context compression from erasing verified device findings.

- Do not replace a previously verified sample conclusion with a later inconclusive rerun unless the new run has a clean transport state and a reproducible command transcript.
- If a sample fails while `nook-server`, `frida-server`, attach, or process enumeration is already timing out, classify that run as transport-state noise first, not as a dexdump parity conclusion.
- When parity work diverges, prefer copying `frida-dexdump` behavior directly over adding new heuristics or new ranking rules.
- Any future context compression should preserve the sample ledger and the “Do Not Regress” section below.
- `com.jkx5da.client` is the primary dexdump sample. `cn.n1ng.aboutbear` is only the positive regression guard. `com.ad2001.frida0x1` is a historical hook sample and not the current dexdump priority target.

## Verified Sample Ledger

This section records the latest trustworthy sample outcomes and the command shape that produced them.

### `cn.n1ng.aboutbear`

Verified working with Nook spawn mode.

Representative command:

```bash
PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-spawn6d
```

Latest verified command:

```bash
PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-spawn7b
```

Verified outcome:

- Nook wrote 1 dex artifact successfully.
- Latest successful metadata path:
  `build/compare-nook-aboutbear-spawn7b/metadata.json`
- The successful run found a dex candidate in the first scan window and stopped on `--max-results 1`.

Interpretation:

- This sample is the primary positive regression guard for Nook dexdump.
- Any change that breaks this sample is a regression even if it improves another sample.

### `com.jkx5da.client`

This is the primary dexdump sample.

Verified Nook run:

```bash
PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 -o build/compare-nook-jkx5da-spawn6a
```

Verified Nook outcomes:

- Historical failure:
  `build/compare-nook-jkx5da-spawn6a/metadata.json`
  showed first-window timeout plus failed reattach.
- Reproduced again on `2026-06-01`:
  `build/compare-nook-jkx5da-spawn-current/metadata.json`
  also timed out on window `0..4`.
- Direct probing on `2026-06-01` established the device-side root cause:
  on anonymous `r--` ranges in this process, `readCString()` and `readU8()` succeed, but `readByteArray()` and `Memory.scanSync()` fail.
  That means the old Nook fallback path was spending its time in repeated `scanSync/readByteArray` failures instead of making forward progress.
- Verified fixed run on `2026-06-01`:

```bash
PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-spawn7b
```

- Fixed-run metadata path:
  `build/compare-nook-jkx5da-spawn7b/metadata.json`
- Fixed-run outcome:
  Nook scanned 48 ranges with zero scan errors, found 6 candidates, and wrote 4 dex artifacts before hitting `--max-results 4`.
- The dumped artifacts now include app-specific content such as the literal package string `com.jkx5da.client`, so this is no longer a framework-only false positive.

Verified Frida comparison run on `2026-06-01`:

```bash
frida-dexdump -H 127.0.0.1:27043 -N com.jkx5da.client -o build/compare-frida-jkx5da-attach7a
```

Notes for that run:

- `frida-server` had to be restarted cleanly first on port `27043`.
- `frida-dexdump` does not create the output directory automatically on this setup, so `build/compare-frida-jkx5da-attach7a` had to be created before rerunning.
- Frida then reported `77` candidates and successfully wrote `42` dex files, with some additional candidates still failing on access violations during export.

Current Nook vs Frida comparison:

- Frida output directory:
  `build/compare-frida-jkx5da-attach7a`
- Nook output directory:
  `build/compare-nook-jkx5da-spawn7b`
- Nook currently matches `3` Frida-exported artifacts by exact dumped file hash:
  `3907576`, `6957836`, and `1172` bytes.
- Frida exported `39` additional dumped files beyond the current Nook `spawn7b` set.
- Nook also exported a `55332`-byte app dex containing `com.jkx5da.client`; Frida exported two different `55332`-byte files in the same run, but their final dumped file hashes did not exactly match Nook's raw file bytes.

Follow-up host/runtime alignment work completed on `2026-06-01`:

- Host scan ordering was changed back to Frida-style enumeration order instead of host-side size reordering.
- `md5` dedupe now uses the final dumped file bytes, while metadata also preserves `raw_hash`.
- Related regression tests were added and passed in:
  `host/nook-py/tests/test_dexdump.py`

Current device-side blocker after those host changes:

- `nook-server` startup helper was hardened so device startup no longer aborts on `adb logcat -c` failure and now stages push through `/data/local/tmp/nook-server.push.tmp` before root copy.
- After that fix, `nook-server` starts cleanly again.
- The next blocking failure is no longer dexdump itself but target-app spawn:
  `NookServer: spawn-result event=fail package=com.jkx5da.client ... detail=start_target_app failed`
- Server log showed:
  `StartTargetApp: ... force-stop ret=0 exited=1 start ret=0 started=0`
  followed by Android log:
  `ActivityManager: Failure starting process com.jkx5da.client`

Interpretation:

- Current optimization headroom still exists on dexdump completeness.
- But the immediate blocker to collecting a fresh full Nook baseline is now the spawn/app-start path, not the dexdump search/export path.

Interpretation:

- This sample is the main parity and stability target for current dexdump work.
- The key runtime fix was to add a direct-memory fallback for `readByteArray`/`scanSync` support paths when Android self-read syscalls fail on otherwise readable mappings.

### `com.ad2001.frida0x1`

This remains useful for general hook/runtime checks, but it is no longer the primary dexdump target.

Interpretation:

- Do not spend dexdump iteration time on this sample until `com.jkx5da.client` is stable.

## Do Not Regress

These points were learned the hard way and should survive any later rewrite.

- Do not introduce speculative global scan-order rewrites unless they are backed by a verified device win on `com.ad2001.frida0x1` and do not break `cn.n1ng.aboutbear`.
- Do not enable aggressive “fast full-process scan” on spawn mode by default. It previously broke the stable `aboutbear` sample by destabilizing the session before fallback scanning.
- Do not treat a failed rerun on a dirty device state as stronger evidence than an earlier clean successful run.
- Do not optimize for theoretical completeness before restoring direct `frida-dexdump` parity on the concrete failing sample.
- Do not let `com.ad2001.frida0x1` displace `com.jkx5da.client` as the main dexdump decision sample again.

## Implementation Priority

When resuming work, use this priority order:

1. Keep `cn.n1ng.aboutbear` passing as the regression guard.
2. Restore `com.jkx5da.client` by copying `frida-dexdump` behavior where possible and only adding Nook-specific fallback when the runtime primitives differ.
3. Only after `com.jkx5da.client` is stable, spend time on secondary samples or additional heuristics.

## Goal

Add a `nook-cli dexdump` capability that aligns with `frida-dexdump`'s core workflow:

- attach to an existing target process and dump in-memory dex artifacts
- spawn a target package, resume it, wait for a configurable settle window, and dump in-memory dex artifacts
- support a normal scan path and an optional deep scan path for broken-header dex recovery
- keep the implementation scoped to `attach` and `spawn`
- do not couple the first dexdump design to `nook-gadget`

This feature is intentionally modeled after `frida-dexdump` as a host plus agent flow:

- host side coordinates session setup, script load, RPC calls, script-message binary export, dedupe, repair, and file output
- device-side script scans memory ranges, verifies candidates, and exports raw bytes

## Non-Goals

- gadget-specific packaged startup dexdump flow
- ART event-driven capture in the first implementation slice
- full dex repair or a generalized unpacking framework
- introducing TypeScript as a required part of the device-side pipeline

## Reference Model

`frida-dexdump` relies on Frida for:

- attach and spawn session management
- device-side script injection
- device-side memory APIs such as `Process.enumerateRanges()`, `Memory.scanSync()`, and `NativePointer.readByteArray()`
- host to device RPC

Its feature logic is relatively small:

- search memory for dex candidates
- verify candidates heuristically
- export bytes for each candidate
- minimally fix the dex header
- dedupe and write `classes*.dex`

Nook should mirror that product shape while improving validation, scan selectivity, metadata, and error handling.

Important runtime constraint:

- current Nook RPC responses are JSON-only and are not suitable for direct large binary return values
- therefore the first Nook dexdump implementation must keep discovery/control on RPC and move raw dex byte export onto the existing `script message` binary `data` channel

## User-Facing CLI

Add a first-class subcommand:

```bash
nook-cli dexdump <target> [options]
nook-cli dexdump --spawn <package> [options]
```

Recommended initial options:

- `-U`, `--usb`
- `--serial`
- `--host`
- `--port`
- `--timeout`
- `-o`, `--output`
- `--deep`
- `--sleep <ms>`
- `--fix-header`
- `--json`
- `--dedupe md5|addr`
- `--agent-ready-timeout`
- `--message-timeout`

Expected usage examples:

```bash
nook-cli dexdump com.demo.target -U -o .\dump\com.demo.target
nook-cli dexdump --spawn com.demo.target -U --deep --sleep 3000
```

## High-Level Architecture

The design follows Nook's existing host and QuickJS runtime split.

### Host Side

Add a Python coordinator module:

- `host/nook-py/nook/dexdump.py`

Responsibilities:

- parse normalized dexdump options
- establish an attach or spawn session through existing Nook host APIs
- load the dexdump device script
- call device RPC exports
- dedupe candidates
- repair dumped dex headers
- emit output files and metadata
- print user-facing results

### Device Side

Add a QuickJS script:

- `host/nook-py/dexdump.js`

Responsibilities:

- enumerate candidate memory ranges
- scan ranges for dex signatures
- optionally perform deep scan candidate recovery
- validate candidate headers and map structures
- estimate export size
- export raw memory for a requested candidate

### Existing Runtime Dependencies

This design assumes the existing Nook device runtime already provides:

- `Process.enumerateRanges(...)`
- `Memory.scanSync(...)`
- `Memory.protect(...)`
- `NativePointer.read*()` helpers
- `rpc.exports`
- `send(message, data)` / script message callbacks with binary payloads

The current architecture already documents those primitives in `docs/architecture.md`.

## Execution Model

The command runs as a two-phase pipeline:

1. candidate discovery
2. candidate export and emit

### Attach Flow

1. parse CLI arguments into normalized `DexDumpOptions`
2. connect to the selected device or USB transport
3. attach to the target process
4. load `dexdump.js`
5. call `searchdex(options)`
6. receive candidate list and scan stats
7. filter and dedupe candidates host-side
8. call `beginmemorydump(addr, size, options)` for each selected candidate
9. receive one or more binary script-message chunks for the requested dump token
10. repair, hash, dedupe, and emit dex files
11. write `metadata.json`

### Spawn Flow

1. parse CLI arguments into normalized `DexDumpOptions`
2. connect to the selected device or USB transport
3. spawn the target package through the existing Nook host path
4. resume the process
5. wait for `sleep` or settle delay
6. load `dexdump.js`
7. continue with the same discovery and export pipeline used by attach

This matches the shape of `frida-dexdump`, which resumes first and then sleeps before scanning, but Nook should keep the timing boundaries explicit in host-side orchestration.

## RPC Contract

The first design uses one RPC export for discovery and one RPC export to initiate a binary export:

```javascript
rpc.exports = {
  searchdex: function (options) {},
  beginmemorydump: function (address, size, options) {}
};
```

### `searchdex(options)`

Input fields:

- `deep`
- `max_results`
- `min_dex_size`
- `max_dex_size`
- `include_system`
- `debug`

Return shape:

```json
{
  "results": [
    {
      "addr": "0x7abc123000",
      "size": 503120,
      "real_size": 503120,
      "fallback_size": 524288,
      "source": "magic-scan",
      "deep": false,
      "header_ok": true,
      "maps_ok": true,
      "confidence": "high",
      "range_path": "/memfd:jit-cache",
      "range_base": "0x7abc100000",
      "range_size": 1048576
    }
  ],
  "stats": {
    "ranges_total": 312,
    "ranges_scanned": 41,
    "ranges_skipped": 271,
    "magic_hits": 12,
    "deep_hits": 3,
    "verified": 4,
    "errors": 1
  }
}
```

### `beginmemorydump(address, size, options)`

Input fields:

- `address`
- `size`
- optional `chunk_size`
- optional `try_protect`

Return shape:

```json
{
  "token": "dump-0001",
  "size": 503120,
  "chunk_size": 65536,
  "chunks": 8
}
```

After `beginmemorydump(...)` returns, the device script emits script messages carrying binary chunks:

```javascript
send(JSON.stringify({
  type: "dexdump-chunk",
  token: "dump-0001",
  index: 0,
  chunks: 8,
  size: 65536,
  eof: false
}), chunkArrayBuffer);
```

Final chunk semantics:

- every chunk uses `type: "dexdump-chunk"`
- `token` identifies the active export request
- `index` is zero-based
- `chunks` is the expected total chunk count
- `eof` is `true` only on the last chunk

If the export fails after the RPC returned success, the device script emits a terminal error message instead of silently hanging:

```javascript
send(JSON.stringify({
  type: "dexdump-error",
  token: "dump-0001",
  error: "readByteArray unreadable pointer"
}));
```

## Device Script Internal Design

`dexdump.js` should be structured as a small library of focused functions instead of a single scanning routine.

### Recommended Function Layout

- `normalizeOptions(options)`
- `enumerateCandidateRanges(options)`
- `shouldScanRange(range, options)`
- `scanMagicCandidates(range, options)`
- `scanDeepCandidates(range, options)`
- `buildMagicCandidate(match, range, options)`
- `buildDeepCandidate(hit, range, options)`
- `verifyDexAt(base, range, deepMode, options)`
- `readDexHeader(base)`
- `verifyHeaderFields(header, base, range)`
- `verifyIdOffsets(header)`
- `resolveMapList(base, header, range)`
- `resolveDexSize(base, range, options)`
- `dedupeCandidate(seen, candidate)`
- `beginMemoryDump(address, size, options)`
- `emitDumpChunks(token, bytes, chunkSize)`
- `ensureReadable(base, size)`

### Range Selection

Unlike `frida-dexdump`, Nook should not blindly scan every `r--` range and only filter after a match. The script should filter ranges before scanning whenever possible.

Preferred candidate ranges:

- anonymous ranges
- app-related mappings
- suspicious runtime-backed mappings such as `memfd`

Default exclusions:

- `/system/`
- `/apex/`
- `/data/dalvik-cache/`

### Normal Scan Path

The normal scan path searches for dex magic:

```text
64 65 78 0a 30 ?? ?? 00
```

Each hit is turned into a candidate and passed through stronger validation before it is returned.

### Deep Scan Path

The deep scan path exists to recover broken-header dex artifacts. It should remain separate from the normal path and mark all results with lower confidence unless stronger structure validation succeeds.

The implementation may start with a backtracking heuristic similar in spirit to `frida-dexdump`, but Nook should not copy its exact offset assumptions blindly. The design should support multiple deep candidate strategies later.

### Validation

This is the most important place where Nook should improve on `frida-dexdump`.

Validation should combine:

- minimum header coverage
- `header_size == 0x70`
- valid endian tag
- reasonable `file_size`
- reasonable `*_ids_off` values
- `map_list` range and size validation when present

The result should distinguish:

- `header_ok`
- `maps_ok`
- `confidence`

### Size Resolution

Preferred export size order:

1. `map_list` derived end if valid
2. `file_size`
3. range-based fallback size in deep recovery cases

The script should return both a primary export size and a fallback size when those differ.

### Export Path

Binary export must use script messages instead of RPC return values.

`beginmemorydump()` should support chunked reads so the implementation remains stable on larger dex artifacts.

Recommended behavior:

- validate base and size
- try direct reads
- if needed, repair read permission for overlapping unreadable pages
- read in chunks
- emit one binary script message per chunk
- return only token plus export metadata over RPC

## Host-Side Processing

After `searchdex()` returns, the host should not immediately dump everything without further structure.

Recommended host pipeline:

1. parse the device response
2. reject obvious duplicate address plus size candidates
3. sort by confidence and source
4. export the primary size for each chosen candidate
5. optionally attempt fallback size if configured later
6. compute content hash
7. dedupe by content
8. reassemble chunks in token plus index order
9. repair header
10. emit files and metadata

## Output Format

Default output directory:

```text
./<target>-dexdump/
```

Output files:

- `classes.dex`
- `classes2.dex`
- `classes3.dex`
- `metadata.json`

`metadata.json` should record:

- target
- mode: `attach` or `spawn`
- timestamp
- deep enabled or disabled
- scan stats
- emitted dex artifacts with:
  - address
  - size
  - hash
  - source
  - confidence
  - header repair status

## Header Repair Strategy

The first implementation only needs minimal repair, but the architecture should leave room for a stronger repair path later.

### Minimal Repair

- repair magic if needed
- repair `file_size`
- repair `header_size`
- repair `endian_tag`

### Future Full Repair

- recompute Adler32 checksum
- recompute SHA-1 signature
- validate and repair section consistency more strictly

Host-side repair should be implemented behind a dedicated helper instead of being embedded inline in the main command flow.

## Improvements Over `frida-dexdump`

The design intentionally keeps the same product shape while improving several weak points in the reference project.

### 1. Stronger Validation

`frida-dexdump` relies on weaker heuristics and includes field-offset assumptions that should not be copied directly. Nook should implement explicit header and map validation.

### 2. Pre-Scan Filtering

`frida-dexdump` scans all `r--` ranges and only filters some system-backed matches afterwards. Nook should reject obviously irrelevant ranges before scanning.

### 3. Better Error Reporting

`frida-dexdump` swallows scan-time exceptions silently. Nook should count scan errors and retain enough metadata to debug range-specific failures.

### 4. Earlier Dedupe

`frida-dexdump` mostly dedupes after dumping bytes back to the host. Nook should dedupe candidates both in the device script and again by content on the host.

### 5. Structured Candidate Metadata

`frida-dexdump` essentially returns address and size. Nook should return source, confidence, header state, map state, and range path so the host can make better export decisions.

### 6. Chunked Export Over Script Messages

`frida-dexdump` uses Frida's own transport abstractions. Nook should explicitly use chunked binary script messages because the current RPC channel is JSON-oriented.

### 7. Cleaner Timing Boundaries

`frida-dexdump` uses a coarse resume plus sleep strategy. Nook can still expose the same user-facing timing model while keeping the orchestration boundaries explicit for future upgrades.

### 8. Repair as a Dedicated Host Stage

`frida-dexdump` performs a small inline fixup function. Nook should keep repair modular so future checksum or signature repair does not reshape the rest of the pipeline.

## Language and Build Decision

The device-side implementation should target plain JavaScript executed by Nook's QuickJS runtime.

Current decision:

- do not require TypeScript
- do not add a TS build chain for the first dexdump implementation
- keep the architecture compatible with a future optional TS-to-JS workflow if the script surface becomes large enough to justify it

This keeps the implementation aligned with Nook's current runtime model and avoids introducing build complexity that does not change runtime capability.

## Implementation Slices

Recommended order:

1. add `nook-cli dexdump` command surface
2. add `dexdump.js` with normal magic scan
3. wire host-side binary chunk export, hash, minimal fix, and emit
4. add spawn plus settle-delay handling
5. add deep scan path
6. add richer metadata and error reporting

## Open Follow-Ups

- whether deep scan should initially ship behind a conservative default
- whether fallback-size export should be exposed in the first user-facing CLI
- whether host-side full checksum and signature repair belongs in the initial implementation plan or a later follow-up
- whether a future ART event-driven capture mode should reuse the same output and repair pipeline

## Implementation Status: 2026-05-27

The first implementation slice is now partially realized in the repository.

Implemented:

- `nook-cli dexdump` command surface
- attach and spawn flows
- host-side output directory creation, dedupe, metadata emission, and optional header repair
- device-side `searchdex()` and `beginmemorydump()` exports
- chunked binary export over script messages
- runtime support for `send(message, ArrayBuffer)` so binary payloads can reach the host

Important implementation adjustments relative to the original design:

- the export path uses script-message binary `data`, not RPC binary return values
- current memory-range objects should be treated as `{ base, size, protection }`; stable mapping-path filtering is not available in the first implementation
- range pre-filtering therefore relies mainly on protection and size instead of path-classification rules from the earlier design draft

Still deferred:

- gadget-specific dexdump workflow
- richer fallback-size export policy
- class-loader-aware artifact naming
- event-driven ART capture modes

## Verification Update: 2026-06-01

After the first anonymous-`r--` read fix, a second device-side crash source was verified on real hardware:

- `NativePointer.readU8()` / `readU16()` / `readU32()` style scalar reads were still doing raw pointer reads
- during chunk-scan fallback this could still raise `SIGBUS` inside the target process
- this affected both the main sample and the `aboutbear` regression guard

Runtime follow-up applied in `src/agent_runtime/js_runtime.cpp`:

- added `TryReadMemoryBytesSafely()`
- changed direct fallback reads to use temporary `SIGBUS` / `SIGSEGV` guarded byte-copy instead of raw `memcpy`
- switched scalar `NativePointer.read*()` helpers onto the same safe-read path

Rebuilt and verified on device on `2026-06-01`:

### `cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-reboot3`
- metadata:
  `build/compare-nook-aboutbear-reboot3/metadata.json`
- result:
  - `scan_aborted = false`
  - `verified = 1`
  - `errors = 0`
  - exported 1 dex:
    - size `8303636`
    - md5 `1c4073249d1aaa4ffb51d489bd0a80ef`
- no `SIGBUS` crash was observed in logcat for this run

### `com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-reboot3`
- metadata:
  `build/compare-nook-jkx5da-reboot3/metadata.json`
- result:
  - `scan_aborted = true`
  - abort reason:
    `target process exited during scan timeout at window 160..164`
  - `verified = 2`
  - exported 2 dex:
    - `3907576` bytes, md5 `f95fd9fa70799ff59394df8342f0a7c5`
    - `6957836` bytes, md5 `1b8585a2953394eb19507ae008e31ecf`
- unlike the earlier failing runs, this serial verification no longer failed in `start_target_app` and did not reproduce the earlier `remote_memfd_stage` collision

Current status after this update:

- the target-process `SIGBUS` issue from scalar read fallback appears fixed for the verified samples above
- `cn.n1ng.aboutbear` is green again
- `com.jkx5da.client` has improved from 1 artifact to 2 verified artifacts in this rebooted environment
- the remaining gap versus Frida is now primarily incomplete scan coverage before the target exits, not immediate spawn failure or deterministic read-path crash

### 2026-06-01 regression notes

Verified:

- `readUtf8String/readCString` now use the safe byte-read path instead of raw `*(char*)` dereference
- host-side `dexdump` unit tests still pass after the runtime change
- `nook-server` rebuilds and pushes cleanly

Not yet solved:

- `cn.n1ng.aboutbear` still reproduces a later `SIGBUS` during `searchdex` on the current mainline scan path
- the failing crash is not explained by the `readCString` fix alone
- I do not yet have a stable host-side reordering that both preserves `aboutbear` and improves `com.jkx5da.client`

### 2026-06-01 `aboutbear` narrowed repro

Further real-device isolation on `2026-06-01` showed the remaining crash happens even before full `searchdex` logic:

- `host/nook-py/debug_probes/dex_probe_rpc.js` was used with `nook.cli call --spawn`
- `runnoop`, `runranges`, `runscanfirstrange`, and `runscanmanyranges` all completed without `SIGBUS`
- `runrangecstringreads` alone reproduced:
  - `Fatal signal 7 (SIGBUS), code 1 (BUS_ADRALN), fault addr 0x785`
- `runrangeu32reads` alone also reproduced the same `SIGBUS`

This ruled out `Memory.scanSync` as the direct trigger and re-focused the bug on the low-level `NativePointer.readCString()` / scalar `readU32()` safety path itself under Android.

Follow-up change applied in `src/agent_runtime/js_runtime.cpp`:

- on Android, `TryReadMemoryBytesSafely()` now fails closed if `process_vm_readv` / `/proc/self/mem` cannot complete
- it no longer falls back to in-process pointer walking via `TryDirectReadWithSignalGuard()` on Android

Reasoning:

- `aboutbear` advertises some ranges as readable enough to pass the mapping check
- but repeated scalar/string probing across those ranges still destabilizes the process if the runtime falls back to direct pointer reads
- for `dexdump`, returning `false` and skipping the candidate is preferable to killing the target process

### 2026-06-01 default-output quality follow-up

Host-side result quality was then adjusted to better match `frida-dexdump` behavior under bounded export counts such as `--max-results 4`.

Problem that was verified on-device:

- windowed scan mode was exporting `new_candidates` immediately after each scan window
- this allowed an early `target_hint_hit` candidate with:
  - `real_size = 55332`
  - `declared_size = 13857572`
  - `fallback_size = 13860816`
- to occupy one of the first four output slots before later full-size candidates had been discovered

Host-side changes applied in `host/nook-py/nook/dexdump.py`:

- added a host-side pathological-truncation heuristic for candidates whose observed export size is tiny but whose declared/fallback size is much larger
- updated export ranking so such candidates lose their previous `target_hint_hit` priority boost
- changed windowed scan export policy so early stop only happens once enough non-pathologically-truncated candidates exist to fill `max-results`
- changed windowed export from per-window `new_candidates` dumping to dumping from the global discovered-candidate set once the early-stop condition is satisfied

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- rank test for pathologically truncated `target_hint_hit` candidates
- attach/windowed-flow test proving we do not stop after the first window when the limit would otherwise be filled by a truncated candidate

Verified on-device on `2026-06-01`:

`cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-rankfix2`
- result:
  - still green
  - `scan_aborted = false`
  - wrote 1 dex artifact successfully

`com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-0601-rankfix2-rerun`
- result:
  - `scan_aborted = false`
  - `verified = 7`
  - default top-4 output no longer contains the `55332`-byte truncated candidate
  - output sizes became:
    - `5348352`
    - `6957836`
    - `9525468`
    - `9625540`

Current interpretation:

- this is a real improvement in default output quality because obviously truncated early artifacts are no longer stealing output slots
- exact Frida parity is still incomplete
- in the latest rerun, only the `6957836` artifact matched the recorded `frida-dexdump` output set by exact dumped-file hash
- so the remaining gap is now less about host-side early-stop pollution and more about candidate discovery/selection parity on the main sample

### 2026-06-01 grace-window follow-up

Further real-device comparison showed another host-side parity issue remained even after truncated-candidate deprioritization:

- on `com.jkx5da.client`, default `--max-results 4` was still stopping too early once the first four globally best candidates seen so far had been found
- that early stop happened after scanning only the first `16` sliced ranges
- increasing `--max-results` to `8` proved that additional high-confidence candidates such as:
  - `3907576` bytes, md5 `f95fd9fa70799ff59394df8342f0a7c5`
  - `4970972` bytes, md5 `1e601519248ec46adc913293d7d3d20a`
- appeared in the next scan window (`16..20`)

This established that the remaining default-output gap was still partly a host early-stop policy problem, not a device validation problem.

Host-side change applied in `host/nook-py/nook/dexdump.py`:

- added `scan_grace_windows` host option with default value `1`
- once the discovered non-truncated candidate count reaches `max-results`, windowed scan now keeps scanning for one additional window before exporting and stopping
- tests that specifically require immediate stopping now set `scan_grace_windows=0`

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- `test_attach_mode_scans_one_extra_window_before_stopping_at_result_limit`
- existing early-stop tests updated to pin explicit `scan_grace_windows=0` behavior where appropriate

Verified on-device on `2026-06-01`:

`com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-0601-grace1`
- result:
  - `scan_aborted = false`
  - `ranges_scanned = 20`
  - `verified = 10`
  - default top-4 output became:
    - `3614496`, md5 `f5abe01326bfe646ffa4d6ebdfd6c76b`
    - `3907576`, md5 `f95fd9fa70799ff59394df8342f0a7c5`
    - `4970972`, md5 `1e601519248ec46adc913293d7d3d20a`
    - `5348352`, md5 `1f369812dddf251aec99f66cf3ff9fc3`

Exact Frida matches in that top-4 set:

- `3907576` matched Frida `classes.dex`
- `4970972` matched Frida `classes36.dex`

So after the grace-window change, the default top-4 set now matches `2` recorded `frida-dexdump` outputs by exact dumped-file hash, up from `1`.

`cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-grace1-rerun`
- result:
  - still green
  - `scan_aborted = false`
  - wrote 1 dex artifact successfully

Current interpretation after this follow-up:

- the default-output path is materially closer to `frida-dexdump` on the primary sample than before
- the remaining gap is now more likely in candidate discovery coverage and slice ordering beyond the first extra grace window
- this host change is worth keeping because it improved the main sample without regressing the `aboutbear` guard

### 2026-06-01 size-band diversity follow-up

After the grace-window change, another host-side default-output issue remained on `com.jkx5da.client`:

- default `--max-results 4` could still over-concentrate on one cluster of large complete dex files
- this pushed out the `6957836` candidate even though it appeared earlier than some larger candidates and matched a recorded `frida-dexdump` artifact exactly

Real-device evidence collected from direct window probing on `2026-06-01`:

- window `8..12` produced:
  - two truncated `55332` candidates
  - one `9899528` candidate
- window `12..16` produced:
  - `9625540`
  - `9525468`
  - `6957836`
  - `5348352`
- window `16..20` produced:
  - `4970972`
  - `3907576`
  - `3614496`

This showed the problem was no longer missing discovery. It was host-side selection among already discovered high-confidence complete candidates.

Host-side change applied in `host/nook-py/nook/dexdump.py`:

- added `_select_export_candidates(...)`
- before export, candidates are still globally ranked by the existing safety-oriented sort
- when there are more candidates than `max-results`, host selection now prefers one candidate from each coarse size band before filling remaining slots
- truncated candidates are still excluded from the diversity-first pass and remain deprioritized as before

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- `test_select_export_candidates_prefers_size_band_diversity_before_fill`

Verified on-device on `2026-06-01`:

`com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-0601-diverse4`
- result:
  - `scan_aborted = false`
  - `ranges_scanned = 20`
  - `verified = 10`
  - default top-4 output became:
    - `3614496`, md5 `f5abe01326bfe646ffa4d6ebdfd6c76b`
    - `4970972`, md5 `1e601519248ec46adc913293d7d3d20a`
    - `5348352`, md5 `1f369812dddf251aec99f66cf3ff9fc3`
    - `6957836`, md5 `1b8585a2953394eb19507ae008e31ecf`

Exact Frida matches in that top-4 set:

- `4970972` matched Frida `classes36.dex`
- `6957836` matched Frida `classes08.dex`

`cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-diverse4-rerun`
- result:
  - still green
  - `scan_aborted = false`
  - wrote 1 dex artifact successfully

Current interpretation after this follow-up:

- default top-4 output is now more balanced across candidate size clusters
- this restored the Frida-matching `6957836` artifact to the default set while keeping the earlier `4970972` match
- exact default top-4 parity is still incomplete, but the current result set is materially more Frida-like than the previous large-file-only top-4

### 2026-06-01 rejected variant: larger representative per size band

A follow-up host experiment attempted to improve the size-band selector by choosing the largest candidate inside each size band instead of taking the first candidate in rank order.

This variant passed unit tests but regressed the primary real-device sample:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-0601-diverse4b`
- regressed default top-4 output became:
  - `3614496`
  - `4970972`
  - `5348352`
  - `9899528`

Regression impact:

- the exact Frida-matching `6957836` artifact dropped out of the default set again
- only `4970972` remained as an exact recorded Frida match in the top-4 set

Decision:

- reject this variant
- keep the earlier size-band diversity implementation that preserved the `6957836` Frida match in the default output set

### 2026-06-01 follow-up: preserve earlier discovery within each size band

Further real-device comparison on `com.jkx5da.client` showed one remaining host-side default-selection issue:

- after the first size-band diversity change, the default top-4 set became:
  - `3614496`
  - `4970972`
  - `5348352`
  - `6957836`
- this preserved the Frida-matching `6957836`, but it still dropped the exact Frida-matching `3907576`

Direct window-probe evidence already showed that discovery order among the relevant complete candidates was:

- window `12..16`
  - `6957836`
  - `5348352`
- window `16..20`
  - `4970972`
  - `3907576`
  - `3614496`

So the remaining mismatch was not that `3907576` was discovered late relative to `3614496`. It was that the host-side size-band selector still chose the smaller member of the `< 4 MB` band because the global export rank includes size as a tie-breaker.

Host-side change applied in `host/nook-py/nook/dexdump.py`:

- added `_export_candidate_quality_rank(...)`
- changed the size-band diversity pass so each band representative is chosen by quality-only rank:
  - truncated vs non-truncated
  - target-hint priority for non-truncated candidates
  - confidence
  - `maps_ok`
  - `ids_ok`
- when quality is tied, the selector now keeps the earlier discovered candidate in that band instead of replacing it with a later smaller one
- constrained the size-band diversity pass to `max-results >= 4` only
  - smaller limits continue to use the plain global export rank
  - this avoids regressing attach/window tests that intentionally expect a pure rank-based top-2 selection after the grace-window scan

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- extended `test_select_export_candidates_prefers_size_band_diversity_before_fill` with both `3907576` and `3614496` in the same band to prove the earlier discovered `3907576` survives

Verified on-device on `2026-06-01`:

`com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 4 --debug -o build/compare-nook-jkx5da-0601-diverse4c`
- result:
  - `scan_aborted = false`
  - `ranges_scanned = 20`
  - `verified = 10`
  - default top-4 output became:
    - `3907576`, md5 `f95fd9fa70799ff59394df8342f0a7c5`
    - `4970972`, md5 `1e601519248ec46adc913293d7d3d20a`
    - `5348352`, md5 `1f369812dddf251aec99f66cf3ff9fc3`
    - `6957836`, md5 `1b8585a2953394eb19507ae008e31ecf`

Exact Frida matches in that top-4 set:

- `3907576` matched Frida `classes.dex`
- `4970972` matched Frida `classes36.dex`
- `6957836` matched Frida `classes08.dex`

`cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-diverse4c`
- result:
  - still green
  - `scan_aborted = false`
  - wrote 1 dex artifact successfully

Current interpretation after this follow-up:

- the default top-4 set for the primary sample is now closer to the recorded `frida-dexdump` output than the previous stable variants
- the selection logic still keeps the earlier `6957836` Frida match while restoring the earlier-window `3907576` Frida match
- this is still not exact Frida behavior because Nook intentionally keeps a safety-oriented host selector instead of blindly exporting raw discovery order, but the default result quality is materially improved

### 2026-06-01 unlimited-mode follow-up: preserve partial progress during long scans

After the default top-4 selection work, the next blocker was `--max-results 0` on `com.jkx5da.client`.

Before this follow-up:

- Nook could discover many candidates during long windowed scans
- but once a late scan window timed out and reattach failed, the run often ended with no dumped artifacts at all
- the main causes were:
  - host was still doing target-package hint scans in unlimited mode even though `frida-dexdump` does not use that behavior
  - host only exported at the end of the scan, so any late-session failure could discard all earlier progress

Real-device findings on `2026-06-01`:

- with the earlier unlimited-mode path, `com.jkx5da.client` reached:
  - `63` candidates before timeout in `build/compare-nook-jkx5da-0601-max0c`
  - then `84` candidates with larger window sizes in the ad hoc probes
  - then `104` candidates in an ad hoc probe once target-package hint scanning was disabled
- targeted batch probing showed:
  - some later windows time out only in long full runs
  - some later windows also succeed when isolated in a fresh process
  - some other later windows still time out when isolated, so the problem is mixed and not explained by one bad candidate alone
- a same-session fallback after reattach failure looked acceptable in fake tests but failed on the real device with:
  `WinError 10038`
  so the safe production behavior is to keep already exported artifacts and stop cleanly instead of continuing to drive a broken session

Host-side changes applied in `host/nook-py/nook/dexdump.py`:

- `build_search_options(...)` now clears `target_package` when `max-results == 0`
  - this removes the extra target-hint pattern scans from unlimited-mode runs
  - it does not affect the limited-result default path that still benefits from `target_hint_hit`
- windowed unlimited-mode scans now export newly discovered candidates incrementally after each successful window
  - this preserves useful dumped files even if a later window times out and recovery fails
- reattach after timeout still uses the extended scan timeout
- if reattach ultimately fails, the run now stops with preserved partial artifacts instead of ending with an empty output directory

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- `test_build_search_options_omits_target_package_for_unlimited_mode`
- `test_attach_mode_preserves_incremental_exports_when_reattach_retry_fails`
- `test_split_single_scan_range_for_retry_halves_slice_and_preserves_parent_metadata`
- `test_attach_mode_retries_timed_out_minimum_slice_with_smaller_subslices`

Verified on-device on `2026-06-01`:

`com.jkx5da.client`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0h`
- result:
  - `scan_aborted = true`
  - `scan_abort_reason = "reattach failed after scan timeout at window 128..132: operation timed out"`
  - `ranges_scanned = 128`
  - `verified = 106`
  - `artifact_count = 15`

Exact recorded Frida matches in that preserved partial output set:

- `6957836` matched Frida `classes08.dex`
- `3907576` matched Frida `classes.dex`
- `4970972` matched Frida `classes36.dex`
- `1172` matched Frida `classes02.dex`
- `2603324` matched Frida `classes35.dex`
- `1400872` matched Frida `classes33.dex`
- `1208152` matched Frida `classes32.dex`

This is a material improvement over the earlier unlimited-mode behavior, where the same class of late timeout could leave the output directory empty.

Follow-up verified on-device on `2026-06-01` after adding sub-slice retry and explicit elapsed/skip metadata:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0i`
- result:
  - `elapsed_ms = 193046`
  - `scan_aborted = true`
  - `scan_abort_reason = "reattach failed before continuing scan: operation timed out"`
  - `ranges_scanned = 128`
  - `ranges_skipped = 4`
  - `verified = 108`
  - `artifact_count = 15`
- comparison to `max0h`:
  - preserved artifact hashes stayed identical (`15` artifacts, same `7` exact Frida matches)
  - verified candidate count increased from `106` to `108`
  - metadata now records `elapsed_ms`, making Nook runtime measurable without inferring from filesystem timestamps

Interpretation of `max0i`:

- the new sub-slice retry path is finding additional verifiable candidates before the late-session failure point
- the main remaining blocker is no longer losing partial output or missing elapsed timing data
- the remaining blocker is recovery after late-window timeout once reattach itself becomes unavailable

Follow-up verified on-device on `2026-06-01` after deprioritizing raw-anonymous ranges in unlimited mode:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0j`
- result:
  - `elapsed_ms = 197406`
  - `ranges_scanned = 704`
  - `ranges_skipped = 4`
  - `verified = 137`
  - `artifact_count = 17`
  - first late fatal timeout moved from `128..132` to `704..708`
- interpretation:
  - pushing raw-anonymous ranges behind split parent-backed ranges materially increased pre-timeout scan coverage
  - two additional dumped artifacts appeared, but they were low-quality duplicates (`maps_ok = false`) rather than new exact Frida matches

Follow-up verified on-device on `2026-06-01` after isolating raw-anonymous ranges into single-range batches:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0k`
- result:
  - `elapsed_ms = 196406`
  - `ranges_scanned = 711`
  - `ranges_skipped = 1`
  - `verified = 139`
  - `artifact_count = 17`
  - fatal timeout moved again to isolated single-range batch `711..712`
- interpretation:
  - separating risky raw-anonymous ranges kept adjacent stable slices from being lost in the same timed-out batch
  - exact Frida hash matches remained `7`, so the current host-side heuristics are improving coverage but not yet recovering additional Frida-parity artifacts

`cn.n1ng.aboutbear`

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-max0h-guard`
- result:
  - still green
  - wrote `1` dex artifact successfully

Current interpretation after this follow-up:

- the default limited-result path remains the best parity surface for predictable top results
- the unlimited-mode path is still not fully equivalent to `frida-dexdump`
- but it is no longer all-or-nothing on the primary sample, and it now preserves a nontrivial exact-match subset before late-window failure

### 2026-06-01 follow-up: truncated export fallback and boundary-overlap scan slices

Further work on the primary sample `com.jkx5da.client` focused on two concrete gaps:

1. Nook was exporting obviously truncated candidates only at `real_size` in non-deep mode, while the same candidate metadata showed much larger `declared_size` / `fallback_size`.
2. Nook slices large scan ranges into `1 MB` windows to avoid `searchdex` timeouts, but those slices previously had no overlap, so any `dex\n035\0` magic starting in the last `7` bytes of a slice could be missed even though `frida-dexdump` would see it during whole-range `Memory.scanSync`.

Host-side changes applied in `host/nook-py/nook/dexdump.py`:

- `_candidate_export_sizes(...)` now recognizes pathologically truncated candidates in non-deep mode and tries:
  - `fallback_size`
  - `declared_size`
  - then the original small `real_size`
- `_split_scan_ranges(...)` now gives each non-final slice a `7` byte forward overlap in `scan_size` while keeping `size` as the unique-coverage width
- `_split_single_scan_range_for_retry(...)` now preserves the same overlap behavior when a timed-out slice is halved for retry
- `_scan_range_slice_index(...)` now keys slice order off the unique coverage width (`size`) instead of the overlapped scan width

Regression coverage added in `host/nook-py/tests/test_dexdump.py`:

- `test_candidate_export_sizes_prioritize_expected_size_for_pathologically_truncated_non_deep_candidate`
- `test_split_scan_ranges_adds_forward_overlap_to_preserve_boundary_matches`
- updated overlap expectations in:
  - `test_split_scan_ranges_preserves_parent_range_metadata`
  - `test_split_single_scan_range_for_retry_halves_slice_and_preserves_parent_metadata`

Verified on-device on `2026-06-01`:

`com.jkx5da.client`, non-deep unlimited after truncated-export change:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0n`
- result:
  - `elapsed_ms = 409234`
  - `ranges_scanned = 738`
  - `verified = 148`
  - `artifact_count = 24`
- impact:
  - artifact count increased from `17` (`max0m`) to `24`
  - exact raw-file Frida hash matches stayed at `7`
  - exact matches after applying Frida-style `fix_header()` offline increased to `8`
  - the extra exported `13857572` / `13860816` artifacts did not hash-match the recorded Frida outputs, so this change improved coverage but did not restore parity by itself

`com.jkx5da.client`, deep unlimited after the same export change:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --deep --max-results 0 --debug -o build/compare-nook-jkx5da-0601-deep0a`
- result:
  - `elapsed_ms = 631969`
  - `ranges_scanned = 741`
  - `verified = 149`
  - `artifact_count = 37`
  - all exported artifacts still came from `magic-scan`; `deep_hits = 0`
- impact:
  - `--deep` mostly added fallback-size exports for already known candidates
  - exact raw-file Frida hash matches still stayed at `7`
  - exact matches after offline `fix_header()` were still only `8`
  - this shows the remaining gap is not “deep mode disabled”; it is still candidate parity / late-scan parity

`com.jkx5da.client`, non-deep unlimited after adding slice overlap:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn com.jkx5da.client -U --sleep 5000 --max-results 0 --debug -o build/compare-nook-jkx5da-0601-max0o`
- result:
  - `elapsed_ms = 411547`
  - `ranges_scanned = 744`
  - `verified = 150`
  - `artifact_count = 24`
- impact:
  - slice overlap recovered additional candidates at scan time (`verified 148 -> 150`, `ranges_scanned 738 -> 744`)
  - exact raw-file Frida hash matches still stayed at `7`
  - exact matches after offline `fix_header()` still stayed at `8`
  - therefore slice-boundary misses were real, but they are not the dominant remaining cause of the parity gap

`cn.n1ng.aboutbear` guard after these changes:

- command:
  `PYTHONPATH=host/nook-py python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 5000 --max-results 1 -o build/compare-nook-aboutbear-0601-max0o-guard`
- result:
  - still green
  - wrote `1` dex artifact successfully

Current interpretation after this follow-up:

- the two new fixes are valid:
  - truncated non-deep exports were real and now preserved
  - slice-boundary misses were real and now covered
- but neither fix closed the main parity gap on `com.jkx5da.client`
- the remaining blocker is still that Nook only reaches about `7` exact raw-file Frida matches (`8` after Frida-style header repair) before late-session timeout / reattach failure, while the recorded Frida output set contains many additional unique dumped dex bodies
- the next useful step is not more generic host heuristics; it is to inspect candidate parity directly:
  - record device-side candidate ledgers for debug runs
  - compare those candidate `(addr, size)` pairs against Frida search output on the same app
  - determine which later Frida outputs are genuinely undiscovered by Nook versus discovered-but-exported-differently
