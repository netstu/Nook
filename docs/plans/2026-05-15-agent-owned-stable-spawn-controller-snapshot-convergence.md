# Agent-Owned Stable Spawn Controller Snapshot Convergence

## Date

2026-05-15

## Scope

This step continues tightening the experimental `agent-owned stable spawn`
control ownership inside the zygote agent.

It does **not** switch the default stable spawn backend.

The current real-device stable default remains:

- default stable spawn -> `legacy-ncore`
- `zygote-control` remains non-default / experimental
- single-file `nook-server` remains the deployment model

## What Changed

### 1. Added controller snapshot accessors

`src/framework/NookZygoteSpawn.*` now exposes:

- `GetActiveSpawnSnapshot(...)`

This returns the currently active controller-owned:

- `target_package`
- `spawn_token`

for both:

- `Armed`
- `Consumed`

and rejects:

- `Idle`

### 2. Zygote runtime logging now prefers controller-owned state

`src/framework/nook_zygote_control.cpp` previously logged armed target/token
state by first consulting compatibility sources such as:

- `NOOK_TARGET_PACKAGE`
- `NOOK_SPAWN_TOKEN`
- fast spawn snapshot buffers

After this step:

- if controller owns an active transaction, logs read target/token state from
  the controller first
- compatibility sources remain only as fallback when controller is `Idle`

This aligns observability with ownership.

### 3. Agent-owned install no longer writes a redundant fast snapshot

`HandleSpawnInstallRequest()` previously:

1. armed the controller
2. also populated fast snapshot state through `UpdateFastSpawnConfig(...)`

That duplicated active transaction state inside the agent.

After this step:

- agent-owned install arms the controller
- fast snapshot state is cleared instead of refreshed

So the active transaction now has one authoritative in-agent snapshot source:

- the controller

## Why This Matters

The earlier step blocked compatibility activation when controller ownership was
active, but the agent still retained a second local copy of the same target
package/token through fast snapshot state.

That meant activation ownership was cleaner, but internal observability and
state layout were still partially duplicated.

This step removes another layer of ambiguity:

- controller owns activation semantics
- controller also owns the active target/token snapshot

That is closer to the intended Frida-like model where the zygote-resident agent
owns the armed spawn transaction directly instead of mirroring it across several
side structures.

## Validation

Passed:

- `tests/headers/test_zygote_spawn_state.exe`
- `build/test_ninjector_spawn_injector.exe`
- `build/test_zygote_control_rpc.exe`
- `tools/build_single_server_package.ps1 -ForceRebuild`

## Current Runtime Impact

No intended user-visible change to the current stable default route.

The effect is limited to the experimental / future agent-owned path:

- active target/token state inside the zygote agent now converges on the
  controller instead of being duplicated into fast snapshot state during
  `SPAWN_INSTALL`

## Next Step

Continue reducing mixed ownership around:

- shutdown cleanup
- uninstall/finalize semantics
- experimental zygote-control route bookkeeping

Then switch default stable routing only after the experimental path is clean
enough to replace legacy ownership without hidden secondary state.
