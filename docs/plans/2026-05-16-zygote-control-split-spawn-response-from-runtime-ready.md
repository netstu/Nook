## Goal

Reduce `zygote-control` spawn timeout / white-screen risk by aligning server-side spawn
handshake with the existing host two-stage model:

1. `SpawnResponse` confirms an authoritative gate-held child exists.
2. Runtime-ready remains a separate phase for script operations.

## Root cause found

Before this change, the server only resolved a pending spawn when it received a
runtime-stage `AGENT_READY`.

That meant:

- `ExecuteSpawnRequest(...)` blocked on `WaitForPendingSpawn(...)`
- `SpawnResponse` was delayed until runtime initialization completed
- for `zygote-control`, runtime initialization could itself be delayed by bootstrap hooks,
  app lifecycle timing, or gate-held startup
- host side then observed `wait spawn response timed out`
- the app could stay white-screened because the spawn gate was never handed back to the host

This was inconsistent with `HostSpawnClient`, which already models spawn as:

1. wait for `SpawnResponse`
2. then wait for runtime-stage `AGENT_READY`

## What changed

- `HandleAgentReady(...)` in [server/server_handlers.cpp] now resolves pending spawn when
  any authoritative `AGENT_READY` carries a matching `spawn_token`, not only runtime-stage
  ready.
- Control-stage `AGENT_READY` still does **not** get forwarded to the bound host.
- Runtime-stage `AGENT_READY` still remains the only transition that unlocks:
  - cached runtime ready replay
  - `SpawnTransactionState::kReadyForScriptLoad`
  - script create/load operations

So the split is now:

- pending spawn resolution -> control-ready or runtime-ready authoritative handshake
- script/runtime operations -> runtime-ready only

## Why this matters

This removes one major source of `zygote-control` instability without weakening runtime
gating semantics:

- spawn response no longer waits on full runtime bootstrap
- runtime ready still gates script operations
- the host can resume ownership of the spawned child earlier and deterministically

## Verification

Added focused subset regression:

- [tests/communication/test_server_handlers_spawn_ready_subset.cpp]

It verifies:

- spawn request still succeeds on runtime ready
- timeout path still reports authoritative-ready timeout when no ready arrives
- control-stage `AGENT_READY` still does not forward to host
- control-stage `AGENT_READY` with matching spawn token now resolves pending spawn

## Next

- propagate the new two-stage contract through the full server handler regression suite
- then continue on the actual `zygote-control` lifecycle stability work:
  install, rollback, finalize-clear, and spawn gate release timing
