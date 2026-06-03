# Agent-Owned Stable Spawn Zygote Routing Entry Helper

## Context

After cleaning up host-side ordering and orchestration, the core
`zygote-control` host-side routing entry was still embedded inline inside
`ApplySpawnRoutingAttempts()`:

- enter zygote-control route
- run `TrySpawnViaZygoteControl()`
- apply route result
- commit success or defer to fallback

This was exactly the block most relevant to reconnecting the host path with the
real device-side `zygote-control` chain, so it needed to be isolated as a
single high-level entry helper.

## Change

Extracted:

- `ApplyZygoteControlRouting()`

This helper now owns the complete host-side zygote-control route entry flow:

1. enter zygote-control route
2. perform the zygote attempt
3. apply zygote route result
4. commit successful route or defer to fallback

`ApplySpawnRoutingAttempts()` now delegates the zygote-control route to this
helper instead of embedding the entire block inline.

## Why It Matters

This is the most directly relevant host-side refactor for the next phase of real
device-side work.

Now, if `zygote-control` still misbehaves on device, there is a single explicit
host-side entry helper to inspect for:

- route ordering
- side-effect timing
- defer vs commit decisions
- error propagation into fallback

That is a much better place to stand before reconnecting the refactored host
path with the real zygote-control device chain.

## Verification

Red:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_zygote_route_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `ApplyZygoteControlRouting`

Green:

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
