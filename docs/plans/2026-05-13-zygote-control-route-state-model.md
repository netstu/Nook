# Zygote-control route state model

## Context

The outer host-side routing surface had already become much more explicit:

- `routing_state`
- `routing_progress`
- `current_route_step`
- `routing_windows`

But the most important route boundary in the current architecture, `zygote-control`, still only existed as helper-local control flow plus `SpawnOutcome` side effects.

That made it harder to talk about the zygote-control route itself as a first-class part of the outer host state.

## Change

Added `SpawnZygoteControlRouteState` to `SpawnExecutionState`.

Current values:

- `kNotStarted`
- `kSkipped`
- `kEntered`
- `kCommitted`
- `kDeferredToFallback`
- `kAborted`

## Current wiring

This state is currently updated only from the outer routing helper:

- before route entry: `kNotStarted`
- policy skip: `kSkipped`
- route entered: `kEntered`
- success commit: `kCommitted`
- fallback-eligible failure: `kDeferredToFallback`
- hard/strict failure path returning immediately: `kAborted`

It does not yet replace `SpawnOutcome` or route-attempt helper semantics.
It gives the host-side state a direct readable surface for the zygote-control route boundary.

## Why this matters

This is the first step where zygote-control routing semantics stop being purely helper-local behavior.

Now the outer state can explicitly answer:

- did zygote-control run at all
- was it skipped by policy
- did it commit
- did it abort
- did it defer to fallback

That is exactly the kind of state needed before deeper zygote-control convergence can move from ad hoc flow to explicit host-side transitions.

## Tests

Updated white-box coverage to assert:

- initial `kNotStarted`
- zygote-control success yields `kCommitted`
- fallback-eligible zygote-control failure yields `kDeferredToFallback`

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_zygote_route_state_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_state_green.exe"
```

## Next

The next useful step is to move more of the zygote-control route classification semantics behind explicit host-side updates, so the outer state can eventually describe the full zygote-control route lifecycle without depending on helper-local inference.
