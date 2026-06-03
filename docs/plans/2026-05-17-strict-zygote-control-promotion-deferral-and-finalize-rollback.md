# Strict Zygote-Control Promotion Deferral And Finalize Rollback

Date: 2026-05-17

## Goal

Continue tightening the strict zygote-control spawn path so that:

- control-stage `AGENT_READY` only establishes ownership and does not prematurely trigger full-agent promotion
- spawn state is rolled back cleanly if `FinalizeSpawn()` fails after early child ownership has already been observed
- handler regression tests stop depending on timing accidents

## Problems Found

### 1. Control-ready was still too close to promotion timing

The earlier handler changes split control-ready from runtime-ready, but the full regression suite was still unstable.

The actual remaining issue was not a new protocol bug. It was two smaller issues around the changed timing model:

- some tests still assumed single-frame output where spawn failure paths can now legitimately produce more than one frame
- test fakes were reading and writing `FakeInjector` state from multiple threads without synchronization

This produced non-deterministic failures such as:

- `consumed == bytes.size()` assertion failures in `ParseSingleFrame()`
- occasional `std::bad_alloc`

### 2. Finalize failure left early ownership behind

`HandleAgentReady()` may resolve the pending spawn early and temporarily bind the host / create a suspended spawn entry before `ExecuteSpawnRequest()` reaches `FinalizeSpawn()`.

If `FinalizeSpawn()` then failed, `ExecuteSpawnRequest()` returned an error but did not roll back that early state.

That left stale state behind:

- host still bound to the child pid
- suspended spawn entry still present

This was caught by:

- `TestSpawnRequestFinalizeFailureDoesNotBindHostOrKeepPendingSpawn`

## Fixes

### 1. Keep control-ready as ownership only, promotion after finalize

The already-landed split remains the intended model:

- `server/server_handlers.cpp`
  - control-stage `AGENT_READY` resolves pending spawn and records ownership only
  - it no longer advances spawn state to runtime-ready
  - it no longer triggers immediate child promotion from `HandleAgentReady()`
- `server/spawn_controller.cpp`
  - late promotion remains owned by the spawn controller after `FinalizeSpawn()`

This preserves the intended ordering:

1. observe authoritative child
2. finish `FinalizeSpawn()`
3. bind stable spawn ownership
4. only then schedule full-agent promotion if the child is still control-only

### 2. Roll back host binding and suspended state on finalize failure

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)

On `FinalizeSpawn()` failure we now also:

- `UnbindHostSession(session.GetId())`
- `ClearSpawnSuspended(authoritative_pid)`
- `ClearPendingSpawn(spawn_token)`

So the failure path no longer leaks partial spawn ownership.

### 3. Make spawn handler tests thread-safe

Updated:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `FakeInjector` now protects shared mutable fields with `std::mutex`
- helper reads use accessor snapshots instead of racing on raw fields
- the finalize-failure test now parses multi-frame output correctly instead of assuming a single frame

This removed timing-sensitive UB from the regression tests.

## Verification

Rebuilt and passed:

- `build/test_server_handlers.exe`
- `build/test_server_handlers_spawn_ready_subset.exe`

Build command used:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_spawn_ready_subset.exe
```

## Current Status

After this round:

- strict zygote-control handler semantics are cleaner
- finalize-failure rollback matches the expected state model
- the regression suite for these paths is green again

This does not mean strict zygote-control is fully productized on device yet, but it removes another chunk of handler-side ambiguity before the next real-device iteration.

## Follow-up On 2026-05-17

The strict timing fix was then promoted from the focused subset test into the main handler regression suite.

Updated:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Added official coverage for the now-required behavior:

- when strict/control-stage `AGENT_READY` arrives before `FinalizeSpawn()` returns, the server must promote the child to the embedded full agent before finalize completes
- that path must move the suspended spawn entry to `kWaitingRuntimeReady`
- the later spawn response must still succeed cleanly

Verification after promotion into the full suite:

- `build/test_server_handlers.exe`
- `build/test_server_handlers_spawn_ready_subset.exe`

Both were rebuilt and passed after the formal regression was added, so the strict startup-hook timing fix is no longer only protected by the temporary focused subset.

## Follow-up On 2026-05-17 Authoritative Runtime Preference And Finalize Recovery Boundary

Two more ownership-boundary issues were tightened after the earlier finalize rollback work:

- runtime-stage agent sessions must remain authoritative even if a late control-only session arrives for the same pid / process
- `FinalizeSpawn()` failure recovery must not mirror restored shell ownership back into compatibility-only `spawn_state`

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_session_registry_authoritative_preference.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry_authoritative_preference.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Changes:

- `WaitForAuthoritativeAgentSessionByIdentity(...)` now prefers a runtime-capable agent session over a later control-only session for the same child identity
- handler authoritative-ready bookkeeping now treats runtime-ready as the stable source of truth instead of allowing late control-ready traffic to pull ownership backward
- `FinalizeSpawn()` failure recovery now restores only `shell_owner_state` and any zygote-control transaction snapshot; it no longer writes the restored owner back into `spawn_state`

Why this matters:

- spawn ownership and session authority now converge on the same boundary:
  - runtime session = authoritative
  - shell owner state = owner teardown metadata
  - `spawn_state` = compatibility/session-local shell data only
- this avoids reintroducing stale owner information through rollback paths after the runtime-vs-control split was already corrected

Verification:

- passed:
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_server_handlers_spawn_ready_subset.exe`
- rebuilt and pushed:
  - [nook-server](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/single-server-package/arm64-v8a/nook-server)
- packaged server sha256:
  - `c95d1f9d311bcd5f0459a057b45dee055b1ecd525be91b824968114ddc0fb7c7`
- device-side visible runtime files after push:
  - `nook-server`
  - `server.err`
  - `server.out`
