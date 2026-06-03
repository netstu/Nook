# Agent-Owned Stable Spawn Success Route Commit Helper

## Context

After pending owner construction was extracted into `BuildPendingSpawnCommit()`,
the three successful route handlers were still each deciding their own commit
shape:

- zygote-control success
- symbi success
- legacy success

That meant the data model was becoming owner-first, but success-path entry still
branched per backend before converging only at `CommitSuccessfulSpawnOutcome()`.

## Change

Extracted explicit success commit helper:

- `ApplySuccessfulRouteCommit()`

This helper now owns the common successful route commit contract:

- record `route_attempt`
- optionally set fallback policy
- publish committed pid
- build pending owner state
- commit active owner

Backend-specific route handlers now only decide:

- whether their backend succeeded
- which route/backend they represent
- whether fallback policy needs to be annotated
- which owned session payload is relevant

They no longer manually shape the successful commit path themselves.

## Why It Matters

This is another step from backend-specific spawn lifecycle toward
owner/session-oriented lifecycle.

The host-side happy path is now expressed as:

1. route handler decides backend result
2. route success enters common success commit helper
3. helper builds pending owner
4. helper commits active owner

That reduces duplication and makes the next refactor easier, where more lifecycle
authority can move out of route handlers and into explicit owner/session
operations.

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
  -o build/test_ninjector_spawn_injector_route_commit_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `ApplySuccessfulRouteCommit`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_route_commit_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_route_commit_green.exe"
```
