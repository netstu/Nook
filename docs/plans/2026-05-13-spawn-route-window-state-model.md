# Spawn route window state model

## Context

`SpawnExecutionState` already tracked:

- `phase`
- `phase_reason`
- `routing_state`

That still left one gap in the outer host model:

it could tell the final routing decision, but not which route windows were actually entered and which were skipped by policy.

This matters because the next convergence step is about route progression and ownership boundaries, not just terminal outcome.

## Change

Added `SpawnRouteWindowState` and `SpawnRouteWindows` to `SpawnExecutionState`.

Current per-window states:

- `kNotConsidered`
- `kSkippedByPolicy`
- `kEntered`
- `kProbeOnly`

Tracked windows:

- `zygote_control`
- `symbi`
- `legacy`

## Current meaning

This is still an outer orchestration view, not a backend-internal state machine.

Examples:

- zygote-control disabled or explicit symbi request: `zygote_control = kSkippedByPolicy`
- symbi fallback disabled by policy: `symbi = kSkippedByPolicy`
- a real backend attempt was performed: corresponding window = `kEntered`
- legacy was only probed to capture terminal route error surface: `legacy = kProbeOnly`

## Why this matters

This is the first place where the host-side state can clearly say:

- which route windows were opened
- which ones were skipped before entry
- whether legacy was actually attempted or only probed for classification

That reduces the amount of behavior that still lives only in helper-local control flow.

## Tests

Updated white-box coverage to assert:

- initial route-window state seeding
- zygote-control success enters only the zygote-control window
- deferred classification path records entered/skipped windows correctly

Verified with:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_route_windows_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_route_windows_green.exe"
```

## Next

The next useful step is to introduce an explicit route progression surface in `SpawnExecutionState`, so the host model can represent the ordered routing walk itself instead of only the final routing state plus per-window observations.
