# Spawn routing state model

## Context

The outer spawn execution model already had:

- `phase`
- `phase_reason`

but routing progress was still only partially visible through:

- helper-local control flow
- `outcome.route_attempt`, which mostly reflects the last route result surface

That was not enough for the next step, where routing ownership and fallback boundaries need to be expressed as outer state instead of being inferred after the fact.

## Change

Added `SpawnRoutingState` to `SpawnExecutionState`.

Current states:

- `kNotStarted`
- `kRunning`
- `kCommittedFromZygoteControl`
- `kCommittedFromSymbi`
- `kCommittedFromLegacy`
- `kDeferredToTerminal`

This is intentionally narrow. It does not try to model every backend-internal step. It only records what the outer spawn orchestration has already decided.

## Current wiring

`BuildSpawnExecutionState(...)` seeds:

- `routing_state = kNotStarted`

`ApplySpawnRoutingAttempts(...)` now updates:

- `kRunning` after entering routing
- `kCommittedFromZygoteControl` on zygote-control success
- `kCommittedFromSymbi` on symbi success
- `kCommittedFromLegacy` on legacy success
- `kDeferredToTerminal` when routing finishes without a committed backend and terminal classification must decide the final result

## Why this matters

This gives the outer model one more piece it was missing:

- `phase` tells where execution is
- `phase_reason` tells why the phase moved
- `routing_state` tells what routing has already decided

That is a better base for the next refactor, where zygote-control route boundaries and fallback ownership should become explicit host-side state instead of remaining helper-local behavior.

## Tests

Updated white-box coverage to assert:

- initial `routing_state`
- committed routing state on zygote-control success
- deferred routing state when terminal classification is required

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_routing_state_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_routing_state_green.exe"
```

## Next

The next useful step is to turn the current routing helper into an explicit route-attempt progression model, so the host state can say not only "routing committed/deferred" but also which route windows were actually entered or skipped under policy.
