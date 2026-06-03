# Nook Embedded Agent Delivery Design

## Context

Nook has already reached a better deployment shape:

- `nook-server` can be built with a static C++ runtime and launched directly
- `libnook-agent.so` can also be built with a static C++ runtime and injected without needing `libc++_shared.so`
- `nook-server` now defaults to resolving `libnook-agent.so` from its own directory when `NOOK_AGENT_PATH` is not set

This still leaves one Frida gap: the operator must push both `nook-server` and `libnook-agent.so` to the device.

## Goal

Make the default operator workflow require pushing only `nook-server`.

Target usage:

```powershell
adb push nook-server /data/local/tmp/nook-test/nook-server
adb shell
su
cd /data/local/tmp/nook-test
./nook-server
```

The server should ensure a usable `libnook-agent.so` exists locally before attach/spawn needs it.

## Non-Goals

1. Do not redesign the path-based injector interface
2. Do not implement in-memory / memfd-based agent loading
3. Do not remove manual `NOOK_AGENT_PATH`
4. Do not remove standalone `libnook-agent.so` builds

## Approaches Considered

### Option 1: Keep external agent file and add better docs

Pros:

- trivial

Cons:

- does not close the Frida UX gap

### Option 2: Embed `libnook-agent.so` into `nook-server`, extract to sibling path on startup

Pros:

- closes the manual push gap
- keeps the existing `InjectAgent(pid, so_path)` interface
- lowest architectural risk

Cons:

- requires a two-stage build for server packaging
- increases `nook-server` size

### Option 3: Inject from memory without a filesystem path

Pros:

- most Frida-like internal architecture

Cons:

- much larger injector redesign
- not necessary for the current UX goal

## Decision

Use Option 2.

## Delivery Model

### Build-time

1. Build `libnook-agent.so`
2. Convert that binary into a generated C/C++ header containing a byte array
3. Rebuild `nook-server` with that generated header linked in

This is a packaging-stage dependency, not a runtime dependency.

### Runtime

1. `NOOK_AGENT_PATH` still wins if explicitly provided
2. Otherwise, `nook-server` resolves its own executable directory
3. It checks for `./libnook-agent.so`
4. If the file exists and matches the embedded blob, reuse it
5. If it is missing or stale, rewrite it from the embedded blob
6. The resulting sibling path becomes `config.agent_path`

## Why This Is Safe

1. The injector API still receives a normal filesystem path
2. Existing attach/spawn server logic stays intact
3. Existing manual deployment remains possible through `NOOK_AGENT_PATH`

## File Strategy

### Generated asset header

Add a generated header such as:

- `server/generated/nook_embedded_agent_blob.h`

Contents:

- `kNookEmbeddedAgentBlob[]`
- `kNookEmbeddedAgentBlobSize`

### Build helper

Add a packaging helper script that:

1. builds `nook_agent`
2. generates the blob header
3. builds `nook_server`

This avoids introducing a circular dependency inside `ndk-build` itself.

## Runtime API Changes

Add server-side helpers in `server_runtime.*` for:

1. executable path detection
2. sibling agent path resolution
3. embedded-blob materialization with overwrite/reuse semantics

Keep `server_main.cpp` thin by delegating all path/extraction logic there.

## Validation

### Unit-level

1. environment override still wins
2. executable-directory fallback still resolves correctly
3. missing file is created from embedded bytes
4. stale file is replaced
5. matching file is reused

### Device-level

1. push only `nook-server`
2. start `./nook-server`
3. verify `libnook-agent.so` appears beside it
4. run `nook-cli attach ...`

## Risks

1. Server size increase
   - acceptable
2. Generated-header drift
   - mitigated by a dedicated packaging script
3. Partial writes
   - mitigated by temp-file + rename workflow

## Follow-up

Once this is stable, the next incremental improvement would be:

- version/hash-aware reuse metadata

But the first implementation should stay simple and deterministic.
