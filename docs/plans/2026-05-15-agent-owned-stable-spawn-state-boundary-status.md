# Agent-Owned Stable Spawn State Boundary Status

## Date

2026-05-15

## Scope

This update does not switch the default stable spawn backend.

It tightens the agent-side spawn state semantics so the later migration from
`legacy-ncore` ownership to `agent-owned stable spawn` can happen with less
state ambiguity.

## What Changed

### 1. Separated package-match from state-consume

`src/framework/NookZygoteSpawn.*` now exposes a read-only helper:

- `GetArmedSpawnTokenForNiceName(...)`

This helper:

- verifies the controller is still `Armed`
- verifies `nice_name` exactly matches the armed target package
- returns the armed `spawn_token`
- does **not** consume the controller state

This is intentionally different from:

- `TryConsumeForNiceName(...)`

which still performs:

- match
- token extraction
- `Armed -> Consumed`

### 2. Reused the new helper from the zygote runtime path

`src/framework/nook_zygote_control.cpp` now uses a two-phase controller path:

1. read token from the armed controller without consuming
2. consume only immediately before preparing inherited child activation

This reduces the risk that controller state is consumed too early before child
activation is actually committed.

### 3. Unified non-controller target matching semantics

`src/framework/NookZygoteSpawn.*` now also exposes:

- `MatchSpawnTargetAndToken(...)`

This helper centralizes the exact-match contract for:

- `target_package`
- `nice_name`
- `spawn_token`

It is now reused by:

- armed controller matching
- environment-backed matching
- fast-config matching

This does not yet remove the fallback sources, but it makes their acceptance
rules consistent with the controller-backed path.

### 4. Reduced activation-path semantic drift

Before this round:

- specialize-style paths already used explicit controller consumption
- some native child activation paths only performed match + activate

After this round:

- controller-backed activation paths consistently separate:
  - match/token lookup
  - explicit consume
  - child activation

This closes another gap between the current mixed backend and the target
agent-owned ownership model.

## Why This Matters

The current stable default path is still:

- `zygote-control` disabled by default
- stable spawn routed to `legacy-ncore`
- child activation completed through the already-working inherited agent flow

But the target architecture is:

- main agent owns the stable zygote-side spawn transaction
- child activation decisions use one consistent controller model
- legacy `ncore` becomes explicit fallback only

To get there safely, the state machine needs a clean distinction between:

- "this child matches the currently armed package"
- "this spawn transaction has now been consumed"

Without that distinction, later failures in child activation can incorrectly
burn the one-shot controller state and make fallback/cleanup logic harder to
reason about.

## Validation

White-box tests passed:

- `tests/headers/test_zygote_spawn_state.cpp`

Added coverage for:

- token lookup without consumption
- mismatch rejection without consumption
- exact target/token match acceptance
- missing token rejection
- mismatch rejection for non-controller match paths

Build validation passed:

- `tools/build_single_server_package.ps1 -ForceRebuild`

## Current Runtime Impact

No intended user-visible behavior change.

This step is preparatory only:

- default stable spawn remains on the current known-good backend
- attach behavior is unchanged
- `zygote-control` remains opt-in/experimental

## Next Step

Continue moving activation decision ownership into the main agent-side control
module, then switch the default stable server routing only after the agent-side
path is feature-complete enough to replace the remaining `legacy-ncore`
ownership points.
