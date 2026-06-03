# Agent-Owned Stable Spawn Pure Build State and Late Zygote Attempt

## Context

After fixing admission-before-zygote-side-effects ordering, one more structural
ordering issue remained:

- `BuildSpawnExecutionState()` still executed `TrySpawnViaZygoteControl()`
- therefore a function named "build execution state" was still causing route
  side effects

This blurred the lifecycle boundary and made the host-side control flow harder
to reason about. It also meant the zygote-control attempt was happening before
the routing phase had explicitly entered the zygote-control route.

## Change

Made `BuildSpawnExecutionState()` a pure state-construction helper again:

- it now builds policy + initial routing/phase state only
- it does not execute `TrySpawnViaZygoteControl()`

Moved the actual zygote-control attempt into `ApplySpawnRoutingAttempts()`,
immediately after the routing phase explicitly enters the zygote-control route.

That means zygote side effects now happen only inside the route phase, and only
after:

1. request validation
2. admission gate
3. execution-state construction
4. explicit route entry

## Why It Matters

This is another concrete stability-oriented step, not just a structural cleanup.

The host-side lifecycle boundaries are now more honest:

- build state means build state
- enter route means enter route
- try zygote-control means actually attempt zygote-control

That makes further reasoning about `zygote-control` correctness materially
easier and removes another class of "side effect happens too early" bugs from
the host path.

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
  -o build/test_ninjector_spawn_injector_build_state_red.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_build_state_red.exe"
```

Observed failure:

- `BuildSpawnExecutionState()` produced a successful zygote attempt and/or
  triggered zygote-control side effects during state construction

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_build_state_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_build_state_green.exe"
```
