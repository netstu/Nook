# Agent-Owned Stable Spawn Zygote Route Decision Logs

## Context

After introducing `ApplyZygoteControlRouting()` as the dedicated host-side
zygote route entry helper, the next practical need was observability.

When reconnecting to real device-side `zygote-control` behavior, the most useful
host-side questions are:

- did the route abort immediately?
- did it defer to fallback?
- did it commit the zygote-controlled route?

Those decision points should be visible from one helper, not reconstructed by
reading a mix of lower-level lifecycle logs.

## Change

Added unified host-side route decision logs inside
`ApplyZygoteControlRouting()` for the three high-value exits:

- `abort`
- `defer`
- `commit`

These use the existing stable log shape:

- `FormatZygoteControlSpawnDecisionLog(...)`

## Why It Matters

This is not another structural refactor. It is a practical preparation step for
the next phase of device-side work.

Now, when `zygote-control` is exercised on device, the host-side route helper
itself emits an unambiguous decision breadcrumb for the three main outcomes.
That should shorten the feedback loop when correlating host logs with actual
device behavior.

## Verification

Fresh host-side compile and test:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_zygote_route_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_zygote_route_green.exe"
```
