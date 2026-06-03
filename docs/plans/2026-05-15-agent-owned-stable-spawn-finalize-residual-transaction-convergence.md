# Agent-Owned Stable Spawn Finalize Residual Transaction Convergence

## Date

2026-05-15

## Scope

This step continues converging the experimental `zygote-control` finalize path
toward an owner/session model without changing the default stable backend.

The default real-device route remains:

- stable spawn default -> `legacy-ncore`
- `zygote-control` remains opt-in / experimental
- single visible deployment artifact remains `nook-server`

## What Changed

### 1. Finalize fallback probing now uses residual transaction identity directly

`server/ninjector_spawn_injector.{h,cpp}` previously carried residual
zygote-control finalize state on two surfaces:

- `FinalizeSession::owned_zygote_transaction`
- `FinalizeSession::has_residual_zygote_control_targets`

That boolean was a shadow of state already represented inside the transaction
itself.

After this step:

- `FinalizeWithoutOwnedBackend(...)` takes a residual
  `ZygoteControlOwnedTransaction`
- residual fallback probing derives its decision from
  `HasResidualZygoteControlTargets(transaction)`
- the actual cleanup/fallback probe consumes the residual transaction directly

So finalize fallback authority now follows the transaction that carries target
identity instead of a separate boolean shadow.

### 2. `BuildFinalizeSession()` now surfaces residual zygote-control state as a transaction

If finalize does not own the active backend directly, `BuildFinalizeSession()`
now extracts any residual matching zygote-control transaction immediately into:

- `FinalizeSession::owned_zygote_transaction`

This means the finalize call boundary now receives the identity-bearing residual
transaction directly instead of reconstructing it later behind a secondary
boolean gate.

### 3. `TakeActiveOwnerForFinalize()` no longer exports residual state on a separate boolean channel

The helper still reports:

- active owner extraction
- foreign-owner presence

but it no longer needs to synthesize residual target presence into a distinct
flag for callers that can already inspect the transaction identity directly.

## Why This Matters

The target `agent-owned stable spawn` model needs finalize semantics to be
owner-first and transaction-first.

As long as finalize behavior depends on:

- a transaction carrying real target identity
- plus a second boolean saying "residual targets exist"

the state surface stays split and harder to reason about.

This step removes one more duplicated authority surface and makes finalize
fallback decisions depend more directly on the same transaction record that will
eventually anchor the full agent-owned control-plane lifecycle.

## Validation

Passed locally:

- `build/test_ninjector_spawn_injector_finalize_identity.exe`

Adjusted coverage:

- `BuildFinalizeSession()` now verifies residual zygote-control state is carried
  through `owned_zygote_transaction`
- finalize fallback tests now pass residual transaction identity directly
  instead of a separate residual boolean

## Runtime Impact

No intended user-visible change on the default stable spawn route.

The effect is limited to internal experimental finalize semantics:

- residual finalize fallback uses transaction identity directly
- one more duplicated zygote-control state surface is removed

## Next Step

Continue collapsing remaining duplicated owner/session state around active owner
and finalize routing so the experimental `zygote-control` lifecycle is driven by
explicit owner records instead of mixed owner records plus shadow booleans.
