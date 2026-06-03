# Agent-Owned Stable Spawn Compatibility Fallback Gating

## Date

2026-05-15

## Scope

This step tightens the agent-side child activation decision boundary for the
experimental `zygote-control` / future `agent-owned stable spawn` path.

It does **not** switch the default stable spawn backend.

The current real-device stable default remains:

- default stable spawn -> `legacy-ncore`
- `zygote-control` remains non-default / experimental
- single-file `nook-server` packaging remains authoritative

## What Changed

### 1. Added an explicit compatibility-fallback gate in `NookZygoteSpawn`

`src/framework/NookZygoteSpawn.*` now exposes:

- `IsCompatibilitySpawnFallbackAllowed(...)`

This helper encodes one simple rule:

- if the controller is `Idle`, compatibility sources may still participate
- if the controller is `Armed` or `Consumed`, compatibility sources are blocked

That makes controller ownership explicit instead of implicit.

### 2. Agent-side activation matching now respects controller ownership

`src/framework/nook_zygote_control.cpp` previously did:

1. try controller match
2. if controller did not match, continue to env / fast-config fallback

That meant an active controller-backed transaction could still coexist with
compatibility activation sources.

After this step:

1. try controller match
2. if controller owns an active transaction (`Armed` / `Consumed`), stop there
3. only when controller is `Idle` may env / fast-config compatibility matching run

### 3. Added focused state tests

`tests/headers/test_zygote_spawn_state.cpp` now covers:

- compatibility fallback allowed while controller is `Idle`
- compatibility fallback blocked while controller is `Armed`
- compatibility fallback blocked while controller is `Consumed`

## Why This Matters

The target `agent-owned stable spawn` model cannot have two equally valid child
activation authorities at the same time.

If the controller has already armed a transaction but env / fast-config can
still independently activate a child, then ownership is still mixed and later
state cleanup remains ambiguous.

This step makes the intended rule explicit:

- controller-active path: one owner
- compatibility path: only when no controller-owned transaction exists

That moves Nook closer to the Frida-like direction where the long-lived zygote
agent owns the spawn transaction, while compatibility mechanisms remain only as
fallback infrastructure.

## Validation

Passed:

- `tests/headers/test_zygote_spawn_state.exe`
- `build/test_ninjector_spawn_injector.exe`
- `build/test_zygote_control_rpc.exe`
- `tests/communication/test_agent_connection.exe`
- `tools/build_single_server_package.ps1 -ForceRebuild`

## Current Runtime Impact

No intended user-visible change for the current stable default route.

The effect is limited to the experimental / future agent-owned path:

- once controller ownership exists, agent-side child activation no longer falls
  through to env / fast-config compatibility sources

The current default `legacy-ncore` path is unaffected because it does not rely
on the controller being armed.

## Next Step

Continue shrinking the remaining mixed ownership points around zygote hook
lifecycle and finalize semantics, then only switch default stable routing after
the experimental path is internally single-owner end to end.
