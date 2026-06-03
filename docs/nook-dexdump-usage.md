# Nook DexDump Usage

## What It Does

`nook-cli dexdump` dumps in-memory dex artifacts from a target process, modeled after `frida-dexdump` but using Nook's host/runtime transport.

Current scope:

- attach to an already running target
- spawn a package, resume it, optionally wait, then scan
- normal dex magic scan
- optional conservative deep scan
- chunked binary export over script messages
- optional host-side dex header repair

Current non-scope:

- gadget-specific dexdump workflow
- automatic unpacking or full dex reconstruction
- ART event-driven capture

## Prerequisites

`dexdump` currently runs through the standard rooted `nook-server` path.

You need:

- a rooted Android device
- a running `nook-server`
- `nook-cli` installed from `host/nook-py`

Example server startup:

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

## Basic Usage

Attach to an already running target and dump dex files:

```powershell
nook-cli dexdump com.demo.target -U
```

Write output to a custom directory:

```powershell
nook-cli dexdump com.demo.target -U -o .\dump\com.demo.target
```

Spawn first, then scan:

```powershell
nook-cli dexdump --spawn com.demo.target -U
```

Spawn, resume, wait 3 seconds, then scan with deep mode:

```powershell
nook-cli dexdump --spawn com.demo.target -U --deep --sleep 3000
```

Repair dumped dex headers before writing:

```powershell
nook-cli dexdump com.demo.target -U --fix-header
```

Emit machine-readable JSON:

```powershell
nook-cli dexdump com.demo.target -U --json
```

## Main Options

- `--spawn <package>`: spawn instead of attach
- `-o`, `--output`: output directory
- `--deep`: enable conservative deep-scan heuristics
- `--sleep <ms>`: wait after spawn resume before scanning
- `--fix-header`: repair dex header, checksum, and signature before writing
- `--dedupe md5|addr`: dedupe by dumped content or by candidate address
- `--max-results`: cap returned candidates
- `--min-dex-size`: ignore very small candidates
- `--max-dex-size`: cap accepted candidate size
- `--message-timeout`: RPC and chunk wait timeout
- `--agent-ready-timeout`: spawn agent-ready timeout
- `--json`: JSON result output

## Output Layout

Default output directory:

```text
.\<target>-dexdump\
```

Output files:

- `classes.dex`
- `classes2.dex`
- `classes3.dex`
- `metadata.json`

`metadata.json` records:

- target
- mode: `attach` or `spawn`
- timestamp
- deep enabled or disabled
- scan stats
- emitted artifacts with address, size, hash, source, confidence, and repair status

## Runtime Model

The command uses a split transport:

- RPC for control and discovery:
  - `searchdex(options)`
  - `beginmemorydump(address, size, options)`
- script messages for binary dump chunks:
  - `type: "dexdump-chunk"`
  - binary payload in `message.data`

This matters because Nook's RPC path is JSON-oriented; large dex blobs are not returned directly through RPC.

## Current Behavior Notes

- spawn mode resumes the target first, then waits for `--sleep`, then scans
- when the target is a package name, Nook prefers dex candidates whose containing range also carries that package's string markers
- default scan order is biased toward larger readonly regions, but the scanner stops after a bounded set of high-value ranges instead of walking the whole process
- deep mode is intentionally conservative and lower-confidence than normal magic scan
- host-side dedupe runs after binary export
- `--fix-header` rewrites:
  - magic
  - file size
  - header size
  - endian tag
  - SHA-1 signature
  - Adler32 checksum

## Limitations

- current first implementation is for rooted `nook-server`, not `nook-gadget`
- range path filtering is intentionally limited because the current runtime range objects do not expose stable mapping-path metadata
- deep scan is heuristic and should be treated as a recovery mode, not the primary path
- current naming is `classes*.dex`; original class-loader identity is not preserved yet
- dumped runtime dex can be structurally complete without being byte-identical to the original APK dex; ART/runtime rewriting can change internal bytes even when file size, ids tables, and app strings line up

## Troubleshooting

### `dexdump requires either <target> or --spawn <package>`

You passed neither or both. Use exactly one:

```powershell
nook-cli dexdump com.demo.target -U
```

or:

```powershell
nook-cli dexdump --spawn com.demo.target -U
```

### No dex artifacts were written

Check:

- the target really loaded dynamic dex content into memory
- the process was running long enough before scan
- `--sleep` is large enough for spawn mode
- `--deep` is enabled if the target is using broken-header dex layouts

### Dump stalls or times out

Raise:

- `--message-timeout`
- `--agent-ready-timeout`

Also confirm:

- `nook-server` is still running
- the target process did not exit
- the dump size is not excessively large for the current timeout
