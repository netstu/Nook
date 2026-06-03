# Agent-Owned Stable Spawn Control Plane Compat Step

## Date

2026-05-15

## Scope

This step migrates the server-side zygote control plane one layer closer to the
target `agent-owned stable spawn` model without changing the default stable
spawn backend.

The stable real-device path remains:

- default stable spawn -> `legacy-ncore`
- `zygote-control` remains non-default / experimental
- single-file package build remains authoritative

## What Changed

### 1. Dedicated spawn control frames are now first-class on the server path

`server/zygote_control_rpc.*` now supports:

- `kSpawnInstall`
- `kSpawnUninstall`

as direct control-plane requests to an already control-ready zygote agent
session.

This means the server no longer has to model every zygote-control operation as:

- `kRpcRequest`
- `method = nook.spawn.*`

when a typed control message is available.

### 2. Compatibility fallback is preserved

The migration is intentionally soft:

- if a control-ready session is immediately present:
  - try dedicated spawn control message first
- if the dedicated path fails in a compatibility-shaped way:
  - fall back to the old RPC wrapper path
- if no immediate control-ready session exists:
  - keep using the old RPC path directly

Compatibility-shaped failures currently include cases such as:

- decode mismatch
- timeout
- unknown/unsupported message style
- null/invalid response shape

This preserves old test semantics and avoids forcing the new path into places
that still only model RPC invokers.

### 3. Added explicit test hooks for sender injection

`server/zygote_control_rpc.h` now exposes test-only helper entrypoints:

- `InstallZygoteForkHookWithSendersForTest(...)`
- `UninstallZygoteForkHookWithSendersForTest(...)`
- `SendSpawnInstallToControlSessionForTest(...)`
- `SendSpawnUninstallToControlSessionForTest(...)`

These exist so control-plane behavior can be tested without pretending
`Session::SendRequest()` is virtual.

That matters because an earlier attempt at testing this layer by subclassing
`Session` was structurally invalid.

## Why This Matters

The target architecture is not just "agent does more work".

It also requires the server/control-plane boundary to stop depending on
stringly-typed RPC wrappers for operations that are actually spawn lifecycle
primitives.

This step reduces semantic drift between:

- current server-side zygote control orchestration
- future agent-owned stable spawn control ownership

without forcing a premature backend switch on the user-visible stable spawn
path.

## Validation

Passed:

- `build/test_zygote_control_rpc.exe`
- `tests/communication/test_agent_connection.exe`
- `tests/headers/test_zygote_spawn_state.exe`
- `tools/build_single_server_package.ps1 -ForceRebuild`

New coverage added in `tests/communication/test_zygote_control_rpc.cpp`:

- dedicated spawn install preferred when control-ready session exists
- dedicated spawn install falls back to RPC on compat timeout
- dedicated spawn uninstall preferred when control-ready session exists
- dedicated spawn uninstall falls back to RPC on compat timeout

## Current Runtime Impact

No intended default backend change.

What changed is the zygote-control control-plane behavior when a control-ready
session is already available:

- server can now speak typed spawn control messages first
- RPC remains the compatibility safety net

## Next Step

Continue shrinking the remaining RPC-wrapper dependency inside the
`zygote-control` orchestration path, then only after that move the default
stable spawn routing decision closer to the target `agent-owned` path.

This keeps the working `legacy-ncore` default intact while control-plane
ownership converges toward the Frida-like direction.
