# Nook SoDump Usage

## What It Does

`nook-cli sodump` dumps an in-memory shared object from a target process and writes:

- the raw loaded image
- an optional repaired ELF image
- per-dump JSON metadata

This is the Nook-native equivalent of the common `frida_dump` workflow:

1. find a loaded module by name
2. dump the mapped bytes from process memory
3. repair the dumped ELF image
4. save artifacts on the host

Current first-version scope:

- attach mode through rooted `nook-server`
- spawn mode through rooted `nook-server`
- gadget attach mode through `--gadget`
- exact module-name lookup
- module listing when `--module` is omitted
- ELF64 repair for loaded `PT_LOAD` segments plus synthesized dynamic section headers

Current non-scope:

- automatic dump of every module in the process
- ELF32 repair
- full packed-loader reconstruction
- full `SoFixer` parity for all relocation and section-rebuild edge cases

## Prerequisites

You need one of these runtime paths:

- rooted device with `nook-server`
- gadgetized APK running in `listen` mode, then host attach with `--gadget`

### Rooted server path

Example startup:

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

### Gadget path

Patch the APK in gadget `listen` mode and install it, then start the app and attach with `--gadget`.

## Basic Usage

Attach to a running target and dump a module:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so
```

Spawn first, then dump after a short delay:

```powershell
nook-cli sodump --spawn com.demo.target -U --module libfoo.so --sleep 3000
```

Attach through gadget mode:

```powershell
nook-cli sodump -U --gadget com.demo.target --module libfoo.so
```

List currently loaded modules instead of dumping:

```powershell
nook-cli sodump com.demo.target -U
```

Skip repair and keep only the raw dump plus metadata:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so --no-fix
```

If the dumped image came from a shell or split loader and its `PT_DYNAMIC` metadata is missing from the mapped region, provide the original on-disk ELF as a repair reference:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so --base-so E:\refs\libfoo.so
```

Emit machine-readable JSON:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so --json
```

## Main Options

- `--spawn <package>`: spawn instead of attach
- `--module <name>`: exact shared-object module name
- `--base-so <path>`: original on-disk ELF used to recover a missing `PT_DYNAMIC` segment
- `-o`, `--output`: output directory
- `--sleep <ms>`: wait after spawn resume before dumping
- `--fix`: run the host-side ELF repair step
- `--no-fix`: skip repair
- `--message-timeout`: RPC and chunk wait timeout
- `--agent-ready-timeout`: spawn agent-ready timeout
- `--debug`: extra repair diagnostics
- `--json`: JSON result output

## Output Layout

Default output directory:

```text
.\<target>-sodump\
```

For `libfoo.so`, the output files are:

- `libfoo.so.raw.so`
- `libfoo.so.fix.so`
- `libfoo.so.json`

The JSON records:

- target
- mode: `attach` or `spawn`
- timestamp
- module name, path, base, and size
- raw artifact file name, size, and hash
- fix applied / success flags
- repaired artifact file name and hash when available
- repair warnings
- synthesized section count
- repair error when repair failed

## Runtime Model

`sodump` uses the same split transport model as `dexdump`:

- RPC for control:
  - `listmodules()`
  - `findmodule(name)`
  - `beginmoduledump(name, options)`
- script messages for binary chunks:
  - `type: "sodump-chunk"`
  - binary payload in `message.data`

This avoids trying to return large binary blobs directly through the JSON RPC path.

## Current Behavior Notes

- if `--module` is omitted, `sodump` lists loaded modules and does not create an output directory
- spawn mode resumes the target first, then waits for `--sleep`, then dumps
- gadget attach mode uses the existing `--gadget` socket route
- if raw dump succeeds but repair fails, the command still preserves:
  - `.raw.so`
  - `.json`
  and returns a failure status

## Current Repair Scope

The current first-pass repair module is intentionally conservative.

It currently does:

- validate ELF64 magic and endianness
- parse ELF64 program headers and dynamic entries
- for real memory dumps, rewrite all program headers to loaded-image offsets (`p_offset = p_vaddr`, `p_paddr = p_vaddr`)
- for real memory dumps, expand each `PT_LOAD` range to the next loadable segment or file end, matching `SoFixer`'s dump-normalization behavior
- normalize runtime-shifted `PT_DYNAMIC` offsets before parsing
- when dumped `PT_DYNAMIC` is outside all `PT_LOAD` ranges, optionally recover it from `--base-so`
- parse SysV hash metadata when `DT_HASH` is present and use it to size `.hash` and `.dynsym`
- parse GNU hash metadata when `DT_GNU_HASH` is present and use it to size `.gnu.hash` and bound `.dynsym`
- size `.gnu.version` from known dynsym count when possible
- size `.gnu.version_r` from parsed `DT_VERNEED` linkage when possible
- synthesize a minimal section table for common dynamic sections when tags are present:
  - `.dynamic`
  - `.dynsym`
  - `.dynstr`
  - `.gnu.hash`
  - `.rel[a].dyn`
  - `.rel[a].plt`
  - `.gnu.version`
  - `.gnu.version_r`
  - `.init_array`
  - `.fini_array`
- synthesize coarse segment sections from remaining `PT_LOAD` ranges:
  - `.text*`
  - `.plt*`
  - `.rodata*`
  - `.data*`
- when PLT relocation metadata is available, prefer a `SoFixer`-style layout:
  - `.rela.plt` / `.rel.plt`
  - `.plt`
  - `.text&ARM.extab`
  - trailing `.data`
- emit repair metadata including warnings and synthesized section count

It does not yet do:

- ELF32 support
- dynamic-section-driven symbol/relocation rebuild at `SoFixer` depth
- full split-loader / custom-loader reconstruction beyond `PT_DYNAMIC` recovery

When parsed hash or version metadata is malformed, `sofix` falls back to the older address-order heuristic and records a warning instead of failing the whole repair.

So the repaired artifact is meant to be a better analysis input than the raw dump, not yet a claim of universal rebuild completeness.

## Troubleshooting

### `sodump requires either <target> or --spawn <package>`

Use exactly one of:

```powershell
nook-cli sodump com.demo.target -U --module libfoo.so
```

or:

```powershell
nook-cli sodump --spawn com.demo.target -U --module libfoo.so
```

### `module 'libfoo.so' not found`

The target process does not currently have that module loaded. First list modules:

```powershell
nook-cli sodump com.demo.target -U
```

Then pick the exact module name.

### Raw dump exists but repair failed

This means the transport path worked but the current first-pass repair module could not rebuild the image. The raw dump is still preserved. Try:

- `--no-fix` if you only want the loaded image
- `--base-so <path>` if the packed sample keeps the real dynamic table only in the on-disk ELF
- a target with a simpler loaded ELF layout
- filing the failing sample for repair-module expansion

### Dump stalls or times out

Raise:

- `--message-timeout`
- `--agent-ready-timeout`

Also confirm:

- `nook-server` or gadget listen runtime is still alive
- the target process did not exit
- the target module was loaded before the dump started
