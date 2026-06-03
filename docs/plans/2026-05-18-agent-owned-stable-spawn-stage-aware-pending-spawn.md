# Agent-Owned Stable Spawn: Stage-Aware Pending Spawn

## Goal

Take the next concrete step toward `agent-owned stable spawn` without changing the
default backend:

- keep default stable route on `legacy-ncore`
- keep `--symbi` experimental
- keep `--strict-zygote-control` separate
- make server-side pending-spawn handoff explicitly record whether the child became
  authoritative at:
  - control-ready
  - runtime-ready

This shrinks the amount of lifecycle inference done later in `spawn_controller`.

## Problem

After the earlier split between:

1. `SpawnResponse`
2. runtime-ready / script-ready

the registry still modeled pending spawn resolution as only:

- `ready`
- `pid`

That meant the rest of the server had to re-infer whether the authoritative child had
resolved at control-stage or runtime-stage by consulting other side state like:

- `IsAgentRuntimeReady(pid)`
- cached `AGENT_READY`
- late-promotion conditions

That worked, but it was still an implicit boundary.

For `agent-owned stable spawn`, the spawn child should progressively become the
authoritative source of readiness state. The host/server should not have to reconstruct
that boundary from multiple side channels.

## Change

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

### 1. Pending spawn now records ready stage explicitly

Added:

- `PendingSpawnReadyStage`
  - `kNone`
  - `kControlReady`
  - `kRuntimeReady`

and extended `PendingSpawnEntry` with:

- `ready_stage`
- `resolved_process_name`

### 2. `ResolvePendingSpawn(...)` now accepts agent identity and stage

Instead of only:

- spawn token
- pid

it now also records:

- resolved process name
- authoritative `AGENT_READY` stage

The stage upgrade is monotonic:

- control-ready can establish the first authoritative child
- runtime-ready can upgrade the same pending spawn
- later control-ready must not regress it back from runtime-ready

### 3. `HandleAgentReady(...)` writes the explicit stage boundary into the registry

When `AGENT_READY` carries a matching `spawn_token`, the pending spawn now captures:

- pid
- process name
- control/runtime stage

This keeps the authoritative child handoff attached to the child-originated ready event
itself, instead of requiring later code to reconstruct it.

### 4. `spawn_controller` now consumes the explicit pending-spawn stage

After `WaitForPendingSpawn(...)` returns, `ExecuteSpawnRequest(...)` reads the resolved
pending-spawn record and uses `ready_stage` as a first-class input for post-spawn state:

- if resolved at runtime stage:
  - mark `kReadyForScriptLoad`
  - replay cached runtime `AGENT_READY` if available
- otherwise:
  - keep `kWaitingRuntimeReady`

This is a small but real move away from shell/controller inference and toward
agent-originated readiness state.

## Why this matters for agent-owned stable spawn

This does not switch the default backend.

It does make the lifecycle model cleaner:

- pending spawn is no longer just "some pid became ready"
- it is now "this child became authoritative at this stage"

That is the right direction for later work where:

- the child/agent owns more of the stable spawn lifecycle
- legacy shell-owned cleanup becomes fallback/compatibility logic instead of the main
  truth source

## Verification

### Passed

- targeted handler subset:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage.exe`
- full single-server package rebuild:
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

Resulting package:

- [nook-server](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/single-server-package/arm64-v8a/nook-server)
- size:
  - `8124376`

### Added regression coverage

- control-stage `AGENT_READY` resolves pending spawn with:
  - `ready_stage = kControlReady`
  - correct resolved process name
- runtime-stage `AGENT_READY` upgrades the same pending spawn to:
  - `ready_stage = kRuntimeReady`
- monotonic stage behavior is preserved in the registry

### Known note

Running the full `test_session_registry.cpp` executable in this environment still hit an
older unrelated assertion around control-ready session lookup. That failure is outside the
pending-spawn path changed here, so this step was validated with:

- the new direct registry assertions added in that file
- the passing handler subset
- the full package rebuild

## Next

1. Use `ready_stage` and child-originated identity as the primary input when deciding
   late-promotion / runtime replay edges, instead of re-checking multiple side channels.
2. Continue shrinking shell-owned state so default stable spawn can eventually consume the
   same explicit child-owned readiness boundary.
3. Only after that boundary is clean, move the next real step of `agent-owned stable spawn`
   into the backend lifecycle itself.

## Follow-up On 2026-05-19 Spawn Session Selection Tightening

Updated:

- [server/server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_runtime.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_runtime.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Problem:

- spawn-bound paths still had two pid-level fallback leaks:
  - `ResolveSpawnGateAgentSession(...)` could release a suspended spawn gate through a
    pid-current session even when the spawn transaction's explicit identity did not match
  - `HandleAgentReady(...)` could accept a control-stage `AGENT_READY` whose
    `process_name` did not match the known suspended-spawn control identity, because the
    path eventually fell back to generic pid-current acceptance

This was not just theoretical:

- one new regression reproduced a suspended spawn whose gate resolver incorrectly accepted a
  pid-bound session with mismatched identity
- another new regression reproduced a known-control suspended spawn where a foreign
  control-stage `AGENT_READY` could become authoritative

Change:

### 1. Spawn gate resolution now stays inside transaction identity

`ResolveSpawnGateAgentSession(...)` now behaves as:

- if there is a suspended spawn entry:
  - prefer control-ready lookup by explicit authoritative control identity
  - prefer runtime-ready lookup by resolved runtime identity
  - do not fall back to generic pid-authoritative/current session when the suspended entry
    already defines the transaction identity
- only when there is no suspended spawn entry does it fall back to the coarse pid registry

This removes one remaining place where a gate-held spawn could be resumed via the wrong
session just because the pid slot was occupied.

### 2. Known spawn control identity mismatch is rejected early

In `HandleAgentReady(...)`:

- suspended-spawn context detection is now derived from the explicit transaction fields
  instead of only `target_process_name`
- when handling control-stage `AGENT_READY`, the server now explicitly drops the event if:
  - the spawn transaction already has a known control identity
  - and the incoming `process_name` matches neither:
    - `target_process_name`
    - nor `authoritative_process_name`

The key detail is that this was narrowed to an identity mismatch check only.

It does **not** reject valid control/runtime handoff paths where:

- the incoming process name matches the target child process
- but the control session is not yet the current pid-bound session

That preserves existing finalize-time and late-promotion behavior while still blocking the
foreign control-session case.

### 3. Suspended-spawn runtime expectation now respects stage

The suspended-spawn fallback path inside `HandleAgentReady(...)` now resolves expected
runtime identity as:

- `target_process_name` by default
- `authoritative_process_name` only once the suspended transaction is already at
  `kRuntimeReady`

This avoids misclassifying the normal control-stage `zygote64 -> target-app` transition as
an identity mismatch just because the current authoritative control identity still belongs
to the pre-runtime stage.

Why this matters:

- more of spawn mainline routing now follows transaction-owned identity instead of pid-global
  occupancy
- control-stage ownership is less likely to be poisoned by foreign sessions
- gate release and script routing keep working with the existing legit control->runtime
  handoff paths

Verification:

- `build/test_server_runtime_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`

Added regressions:

- spawn gate resolver must reject pid fallback for suspended-spawn mismatched identity
- known-control suspended spawn must reject mismatched control-stage `AGENT_READY`

## Follow-up On 2026-05-19 Spawn Host->Agent Runtime Routing Tightening

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Problem:

- `ResolveBoundAgentSessionForHostRequest(...)` still had a coarse fallback:
  - when a suspended spawn was already in runtime-owned states
  - and the matching runtime-ready agent session was gone
  - host-side script requests could still fall back to `FindAgentSessionByPid(pid)`

That meant a runtime-disconnected spawn could theoretically drift back toward whatever
pid-bound session remained, instead of staying inside the transaction's runtime boundary.

Change:

- `ResolveBoundAgentSessionForHostRequest(...)` now treats suspended spawn runtime-owned
  states as identity-strict:
  - if a matching runtime identity exists, return it
  - if the transaction is already at:
    - `authoritative_ready_stage == kRuntimeReady`
    - or `state == kReadyForScriptLoad`
    - or `state == kScriptLoadDispatched`
    and no matching runtime session exists, return `nullptr`
  - only pre-runtime suspended states still allow the older generic pid fallback

Why this matters:

- host->agent script routing for spawn now behaves like the rest of the transaction-owned
  boundary work:
  - runtime-owned phases require the runtime-owned identity
  - loss of that runtime identity keeps the spawn blocked instead of silently degrading
    toward control/pid-global routing

Regression note:

- the focused test originally started as "do not fall back to control session after runtime
  disconnect"
- after validating current registry semantics, it was aligned to the actual contract:
  runtime disconnect should move the spawn back into a blocked runtime-wait state rather than
  routing the request anywhere

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`

## Follow-up On 2026-05-19 Agent->Host Runtime-Disconnect Response Coverage

Updated:

- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Problem checked:

- after tightening host->agent runtime routing, the remaining concern was whether
  `agent->host` response forwarding still had any coarse pid-based leakage
  once the runtime-owned session disconnected and only a control session remained

Added focused regressions for suspended-spawn runtime-owned state where:

- runtime session had existed and then disconnected
- control session still occupied the pid slot
- incoming response/message came from that control session

Covered message types:

- `SCRIPT_CREATE_RESP`
- `RPC_RESPONSE`
- `SCRIPT_UNLOAD_RESP`
- `SCRIPT_MESSAGE`

Observed result:

- all three were already clean
- `IsAcceptedAgentSessionForHostResponse(...)` did not forward any of them through the
  control session after runtime disconnect
- `SCRIPT_MESSAGE` needed a more precise read:
  - it also does not forward through the control session
  - but under current registry semantics it is intentionally cached after runtime disconnect,
    because runtime session removal demotes the suspended spawn back to
    `kWaitingRuntimeReady`
  - that means the correct contract is:
    - no host forward
    - cache until the next runtime-ready boundary

Why this matters:

- this confirms the current transaction-owned routing gap was on the host->agent request
  side, not the agent->host response side
- response forwarding for runtime-owned spawn phases is now backed by explicit focused
  regressions, reducing the chance of future accidental pid-fallback reintroduction

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-19 Attach Runtime-Ready Token Isolation

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/injector.h)
- [server/injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/injector.cpp)
- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [server/ninjector_compat.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.h)
- [server/ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Problem:

- attach timeout followed by re-attach on the same pid could still observe a late runtime-ready
  from the previous inject attempt
- pid/process-name identity alone was not enough to distinguish:
  - old timed-out attach runtime-ready
  - current attach runtime-ready

Change:

- attach now reuses `spawn_token` as a generic ready token and threads it through:
  - `HandleAttachRequest(...)`
  - `Injector::InjectAgent(...)`
  - ninjector compat / embedded inject paths
- server now registers a pending attach keyed by that ready token
- `HandleAgentReady(...)` accepts runtime-ready for attach only when:
  - token matches the active pending attach, or
  - there is no pending attach conflict and the ready is not classified as stale/orphaned
- `WaitForRuntimeReadyAgentSessionByToken(...)` now gates attach completion on the cached
  runtime-ready frame's token, instead of only pid/process-name identity

Why this matters:

- attach now has the same explicit ready-generation boundary that spawn already had
- a timed-out first attach can no longer satisfy a second attach just because it targets the
  same process identity
- this shrinks one more place where the server had to infer lifecycle from global pid state
  instead of request-scoped ownership

Test note:

- once attach success began replaying the accepted runtime `AGENT_READY`, the reattach
  regression test had to stop assuming a single response frame
- the test now validates:
  - stale tokenless runtime-ready does not complete the second attach
  - token-matching runtime-ready does complete it
  - attach success can legitimately produce `AttachResponse + replayed AGENT_READY`

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `./build/test_server_handlers_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `./build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Pending-Attach Host-Lifecycle Cleanup

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Problem:

- attach now has explicit pending-attach generations keyed by ready token
- but `RemoveHostSession(...)` still only cleared:
  - pid bindings
  - pending spawn entries
  - owned suspended spawn state
- pending attach entries owned by a closed host session could survive and become stale
  attach-generation residue

Change:

- `RemoveHostSession(...)` now also erases `pending_attaches_` entries whose
  `host_session_id` matches the closed host
- added a direct registry regression to lock that lifecycle down

Why this matters:

- attach generations now terminate with host lifetime, not just timeout / success paths
- this removes another stale side channel that could later confuse attach-token ownership
- it is the same direction as the broader transaction-owned cleanup work:
  request-scoped state should die with the owning request/session

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `./build/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `./build/test_server_handlers_current.exe`

## Follow-up On 2026-05-18 Spawn-Suspended Authoritative Stage

The first step above was immediately advanced one layer deeper so the explicit child-owned
ready boundary is no longer trapped only inside `PendingSpawnEntry`.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `SpawnSuspendedEntry` now also carries:
  - `authoritative_ready_stage`
  - `authoritative_process_name`
- `MarkSpawnSuspended(...)` can seed or upgrade that authoritative child-owned state
- `UpdateSpawnSuspendedAuthoritativeReady(...)` lets `HandleAgentReady(...)` upgrade an
  already-bound suspended spawn even when no pending-spawn record is involved anymore
- `MaybeBindHostAndMarkSpawnSuspendedFromPendingSpawn(...)` now propagates the explicit
  pending-spawn stage/name into the suspended spawn entry
- `ExecuteSpawnRequest(...)` also seeds the suspended entry from the resolved pending-spawn
  record before late-promotion logic runs
- `MaybePromoteLateBoundControlReadyChild(...)` now uses the suspended entry's
  authoritative stage as the primary signal, with registry-wide ready bits only as fallback

Why this matters:

- the late-promotion decision now follows the spawn transaction's own recorded child stage
  more directly
- the suspended spawn entry becomes the durable per-spawn boundary between:
  - control-ready authoritative child
  - runtime-ready script-capable child
- this is closer to the intended `agent-owned stable spawn` model than reading multiple
  global side maps every time

Added regression coverage:

- control-stage pending-spawn resolution also seeds suspended spawn authoritative stage/name
- runtime-stage upgrade propagates into the suspended spawn entry
- runtime-stage `AGENT_READY` can upgrade an existing suspended spawn entry even without a
  still-live pending-spawn record
- late-promotion success-path keeps the suspended entry tagged as control-originated while
  transitioning its transaction state to `kWaitingRuntimeReady`

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage3.exe`

## Follow-up On 2026-05-18 Runtime-Ready Replay Ordering

Another real lifecycle leak remained after the authoritative-stage work:

- once control-ready had resolved the pending spawn
- the host was already bound to the child pid before `FinalizeSpawn()` returned
- if runtime-ready arrived during that window, `HandleAgentReady(...)` forwarded
  runtime-stage `AGENT_READY` to the host immediately

That broke the intended outer contract:

1. `SpawnResponse`
2. runtime `AGENT_READY`

and also risked a duplicate runtime-ready because `spawn_controller` would later replay the
cached runtime `AGENT_READY` again after finalize.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `HasPendingSpawnForPid(int pid)` to the registry
- `HandleAgentReady(...)` now detects:
  - runtime-stage ready
  - bound host
  - pending spawn response still in flight for that pid
- in that window, runtime-stage `AGENT_READY` is held instead of forwarded
- the cached runtime-ready is then replayed once by `spawn_controller` after
  `SpawnResponse`

Why this matters:

- restores the intended host contract:
  - `SpawnResponse -> runtime AGENT_READY`
- removes one duplicate-ready path

## Follow-up On 2026-05-18 Runtime Disconnect Identity Fallback

Another asymmetry remained in the registry after the stage-aware pending-spawn work:

- control-ready session and runtime session can temporarily share the same pid
- runtime registration overwrites the pid's current `process_name`
- if the runtime session disconnects and the registry falls back to the still-pinned
  control-ready session, the pid identity must also fall back

Without that identity rollback:

- `FindAgentSessionByProcessName(...)`
- `FindControlReadyAgentSessionByIdentity(...)`
- later strict/zygote-control cleanup that keys off pid + process identity

could still see the stale runtime target process name even though the authoritative live
session had already reverted to the control-ready peer.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- added a dedicated `agent_control_process_names_` map
- capture the control identity once for each pinned control-ready pid
- when `RemoveAgentSessionByPidIfMatches(...)` demotes a pid from runtime session back to
  control session:
  - clear the stale runtime name binding
  - restore the control pid/name binding
  - keep stage fallback on `kControl`

Added regression coverage:

- runtime session removal now proves:
  - pid session falls back to the pinned control-ready session
  - runtime process-name binding is removed
  - control process-name binding is restored
  - control lookup by the stale runtime identity no longer succeeds

Verification:

- `build/test-bin/test_session_registry_runtime_identity_regression.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_runtime_identity.exe`

## Follow-up On 2026-05-19 Finalize-Failure Agent-State Cleanup

Another retry-poisoning gap remained in the spawn failure path:

- a spawn child could already reach runtime `AGENT_READY`
- the registry would then hold:
  - agent session
  - authoritative/runtime ready bits
  - cached runtime ready frame
  - pid/process identity
- if `FinalizeSpawn()` failed afterwards, the old code only cleared:
  - host binding
  - pending spawn
  - suspended spawn entry

That meant a failed spawn transaction could still leave a live pid-level runtime identity in
the registry, which is exactly the kind of stale state that later strict/experimental
retries can accidentally reuse.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- on `FinalizeSpawn()` failure, `ExecuteSpawnRequest(...)` now also calls:
  - `registry->RemoveAgentSessionByPid(authoritative_pid)`
- this makes failure cleanup symmetric for the authoritative child pid:
  - remove agent session / ready bits / cached ready frame / process identity
  - then unbind host and clear pending/suspended spawn state

Added regression coverage:

- finalize-failure spawn test now proves the registry no longer keeps:
  - `FindAgentSessionByPid(pid)`
  - `FindAgentSessionByProcessName(target)`
  - authoritative/runtime ready bits
  - cached `AGENT_READY`

Verification:

- `build/test-bin/test_server_handlers_finalize_cleanup_regression.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_after_finalize_cleanup.exe`

## Follow-up On 2026-05-19 Orphan Spawn-Token Ready Rejection

There was still one late-arrival path that could poison retries even after the
finalize-failure cleanup:

- a failed or expired spawn transaction could already have its `pending_spawn` cleared
- the host could already be unbound from that pid
- the child might still connect later and send `AGENT_READY` carrying the old `spawn_token`

Before this fix, `HandleAgentReady(...)` would still accept that late runtime-ready as a
normal global agent registration when:

- the token no longer matched any live pending spawn
- no suspended spawn entry still owned that pid
- no host was bound to that pid

That created an orphan runtime agent session in the registry with no live spawn owner,
which is exactly the kind of stale state that can leak into the next retry.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `HandleAgentReady(...)` now rejects an `AGENT_READY` with `spawn_token` when all of the
  following are true:
  - no matching pending spawn exists
  - no suspended spawn context exists for that pid
  - no host is currently bound to that pid
- valid post-handoff cases are still allowed:
  - live pending spawn
  - still-suspended spawn
  - already-bound host

Added regression coverage:

- orphan late `AGENT_READY` with stale `spawn_token` now proves:
  - no pid-level agent session is registered
  - no process-name identity is registered
  - no authoritative/runtime-ready bits are set
  - no cached `AGENT_READY` frame is stored

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_orphan_spawn_token.exe`
- `build/test-bin/test_server_handlers_after_orphan_spawn_token.exe`

## Follow-up On 2026-05-19 Host-Owned Suspended-Pid Cleanup Symmetry

After the orphan `spawn_token` fix, one more retry-poisoning family remained:

- the server still had several places where a host session moved away from an old
  gate-held spawn pid
- those paths already cleared:
  - host binding
  - suspended spawn entry
  - cached script messages
- but they did not clear the old pid's registered agent/runtime identity

That meant an abandoned suspended spawn child could still survive in the registry after the
host was gone or rebound somewhere else.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- added a shared `ClearAgentStateLocked(int pid)` helper
- the helper is now used when an owned suspended spawn pid is abandoned through:
  - `RemoveHostSession(...)`
  - `BindHostToPid(...)` rebind away from the old pid
  - `UnbindHostSession(...)`
  - `BindHostToResolvedPendingSpawn(...)` rebind from old pid to resolved pid

This makes the cleanup symmetric for host-owned suspended spawn children:

- clear suspended entry
- clear script-message cache
- clear agent session / ready bits / cached ready frame / process identity

Added regression coverage:

- host removal clears owned suspended pid agent state
- direct host rebind clears old suspended pid agent state
- host unbind clears old suspended pid agent state
- resolved-pending-spawn rebind clears old suspended pid agent state

Verification:

- `build/test-bin/test_session_registry_remove_host_owned_agent_cleanup.exe`
- `build/test-bin/test_session_registry_bind_host_cleanup_regression.exe`
- `build/test-bin/test_session_registry_unbind_host_cleanup_regression.exe`
- `build/test-bin/test_session_registry_bind_resolved_pending_cleanup_regression.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_after_remove_host_cleanup.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_after_unbind_host_cleanup.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_after_bind_resolved_cleanup.exe`

## Follow-up On 2026-05-19 Owned-Zygote-Control Mapping Cleanup

One more stale-state path remained on the strict/zygote-control side:

- `owned_zygote_control_processes_` is a separate side map keyed by process name
- normal agent/session teardown cleared the agent/runtime identity maps
- but it did not automatically clear the owned zygote-control target mapping when that pid
  was truly gone

That meant an unexpectedly disconnected zygote-control agent could still leave behind a
stale ownership record for `zygote64` / `usap*`, which is precisely the kind of residue
that can confuse strict retries.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- extended pid-level teardown to also remove any `owned_zygote_control_processes_` entries
  whose stored pid matches the pid being fully cleared
- this applies when the pid is actually torn down, not when it temporarily rebounds to a
  surviving fallback session on the same pid

Added regression coverage:

- removing the last agent session for an owned zygote-control pid now proves:
  - `IsOwnedZygoteControlProcess(process_name)` becomes false
  - `IsOwnedZygoteControlTarget(pid, process_name)` becomes false

Verification:

- `build/test-bin/test_session_registry_owned_zygote_cleanup_regression.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_after_owned_zygote_cleanup.exe`
- `build/test-bin/test_zygote_control_rpc_disconnect_owned_cleanup.exe`
- keeps the authoritative child-owned stage in the server while finalize is still in flight

Added regression coverage:

- when control-ready resolves pending spawn
- and finalize is blocked
- and runtime-ready arrives before finalize release
- host must receive:
  - exactly one `SpawnResponse`
  - then exactly one runtime `AGENT_READY`
  - and nothing earlier

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage4.exe`

## Follow-up On 2026-05-18 Post-Finalize Stage Re-read

The post-finalize path still had one more stale-input seam:

- `ExecuteSpawnRequest(...)` captured `resolved_pending_spawn.ready_stage` before finalize
- `FinalizeSpawn()` could be blocked long enough for the child to upgrade from control
  to runtime in `spawn_suspended`
- after finalize, the controller still preferred the older snapshot unless the global
  runtime bit happened to be set

That left a stale-snapshot hole in the last phase of spawn completion.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a local post-finalize resolver that re-reads `spawn_suspended`
- after `FinalizeSpawn()` returns, controller now prefers:
  1. `spawn_suspended.authoritative_ready_stage`
  2. then the earlier `pending_spawn` snapshot
  3. then the global runtime bit as fallback
- `spawn success` logging now reports the re-read stage instead of only the stale snapshot

Why this matters:

- the spawn completion boundary now follows the child-owned authoritative state all the way
  through finalize
- a runtime upgrade that happens while finalize is blocked can now be observed consistently
  after finalize without relying on a global bit race

Added regression coverage:

- when `spawn_suspended` is manually upgraded to runtime during blocked finalize
- and a cached runtime `AGENT_READY` is present
- the host receives:
  - `SpawnResponse`
  - then the runtime `AGENT_READY`
- the post-finalize spawn state is `ReadyForScriptLoad`

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage5.exe`

## Follow-up On 2026-05-18 Post-Finalize Spawn-Suspended Preference

The post-finalize phase was still not fully aligned with the child-owned boundary:

- `ExecuteSpawnRequest(...)` took a snapshot of `resolved_pending_spawn.ready_stage`
- after `FinalizeSpawn()` returned, that snapshot could still lag behind the current
  `spawn_suspended` entry

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a post-finalize helper that re-reads `spawn_suspended`
- if `spawn_suspended.authoritative_ready_stage` is already `runtime`, the controller now
  prefers that over the earlier `pending_spawn` snapshot
- the old snapshot remains a fallback, and global runtime ready is only the last fallback
- this lets runtime upgrades made while finalize is blocked become visible immediately after
  finalize completes

Added regression coverage:

- if `spawn_suspended` is manually upgraded to runtime during blocked finalize
- and a cached runtime ready frame exists
- then the host gets:
  - `SpawnResponse`
  - followed by the cached runtime `AGENT_READY`
- and the spawn state ends as `ReadyForScriptLoad`

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage5.exe`

## Follow-up On 2026-05-18 Script-Message Ordering

After fixing runtime-ready ordering, one more leak remained:

- if runtime-ready arrived during blocked finalize
- the runtime-side session could also emit `SCRIPT_MESSAGE`
- `HandleScriptMessage(...)` used to forward immediately whenever a host was already bound

That allowed:

- `SCRIPT_MESSAGE`

to race ahead of:

1. `SpawnResponse`
2. runtime `AGENT_READY`

which breaks the intended spawn-ready boundary.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `HandleScriptMessage(...)` now checks the spawn cache window before host forwarding
- `ShouldCacheScriptMessageForPid(...)` was narrowed to:
  - true when `spawn_suspended` is still in a script-blocked state
  - or when a matching `pending_spawn` still exists for that pid
- `ReadyForScriptLoad` no longer causes unconditional caching by itself

Why this matters:

- early script messages now stay behind the same spawn-ready boundary as runtime-ready
- normal post-ready script traffic still forwards immediately once the spawn handshake is over

Added regression coverage:

- runtime-ready plus script message during blocked finalize:
  - host must see nothing early
  - then `SpawnResponse`
  - then runtime `AGENT_READY`
  - then cached `SCRIPT_MESSAGE`
- ready spawn with no pending spawn still forwards `SCRIPT_MESSAGE` immediately

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage7.exe`

## Follow-up On 2026-05-18 Script Replay Follows Runtime Ready

One final ordering leak remained in the cached script-message path:

- `spawn_controller` still replayed cached `SCRIPT_MESSAGE` as soon as `SpawnResponse`
  was sent, even if the spawn was only control-ready
- that meant control-only spawns could surface script traffic before runtime readiness

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `spawn_controller` now replays cached `SCRIPT_MESSAGE` only in the runtime-ready branch
- `HandleAgentReady(...)` now replays cached `SCRIPT_MESSAGE` right after forwarding
  runtime-stage `AGENT_READY` to the host
- control-ready still caches script messages, but does not replay them early

Why this matters:

- cached script traffic now follows the same authoritative runtime boundary as `AGENT_READY`
- the host now sees the same order the agent actually becomes script-capable

Added regression coverage:

- runtime-ready during finalize still replays runtime `AGENT_READY` and cached script
  messages only after `SpawnResponse`
- control-only spawn with cached script message does not replay it at `SpawnResponse`
- the cached script message is replayed only after runtime `AGENT_READY`
- ready spawns still forward script messages immediately

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage8.exe`

## Follow-up On 2026-05-18 Post-Finalize Runtime Authority Tightening

Another side-channel remained after the stage-aware pending-spawn and
spawn-suspended work:

- after `FinalizeSpawn()`
- `ExecuteSpawnRequest(...)` still accepted the global
  `IsAgentRuntimeReady(pid)` bit as sufficient to advance the spawn into
  `kReadyForScriptLoad`

That meant a transaction-local authoritative stage of only `control` could still be
silently promoted by a global runtime-ready side effect, which is the opposite of the
intended child-owned boundary.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `ExecuteSpawnRequest(...)` now only advances post-finalize spawn state to
  `kReadyForScriptLoad` when the resolved authoritative post-finalize stage is
  explicitly `kRuntimeReady`
- the global `IsAgentRuntimeReady(pid)` bit no longer upgrades post-finalize spawn
  state on its own

Added regression coverage:

- a spawn resolved only at control-stage, plus a raw global
  `MarkAgentRuntimeReady(pid)` side effect, must still remain in
  `kWaitingRuntimeReady`
- only the transaction's authoritative stage upgrade may transition it to
  `kReadyForScriptLoad`

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage9.exe`

## Follow-up On 2026-05-18 Late-Promotion No Longer Trusts Stale Global Runtime Bits

The late-promotion path still had one more stale side channel:

- `MaybePromoteLateBoundControlReadyChild(...)` treated the global
  `IsAgentRuntimeReady(pid)` / `IsAgentControlReady(pid)` state as sufficient to
  decide whether a spawn transaction was already runtime-ready
- that allowed a stale runtime-ready bit from a previous lifecycle to suppress the
  promotion of the current control-ready child

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- late-promotion now uses only the current spawn entry's authoritative stage:
  - `kControlReady`
  - `kRuntimeReady`
- global ready bits no longer veto late promotion for the current spawn transaction

Added regression coverage:

- a stale global runtime-ready bit must not prevent a newly resolved control-ready
  child from being promoted and injected

Verification:

- targeted subset passed:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage10.exe`

## Follow-up On 2026-05-18 Attach Path No Longer Trusts Stale Global Runtime Bits

The same stale-side-channel pattern also existed in attach handling:

- `HandleAttachRequest(...)` treated `IsAgentRuntimeReady(pid)` alone as enough to
  reuse the current runtime agent session
- that let a stale global runtime-ready bit short-circuit attach even when the pid had
  no active agent session anymore

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- attach reuse now requires both:
  - `IsAgentRuntimeReady(pid)`
  - a live current agent session for that pid
- stale runtime-ready state by itself no longer skips injection

Added regression coverage:

- a stale runtime-ready bit without a live agent session must still force attach to
  inject and wait for a real runtime-ready agent

Verification:

- targeted server-handler test passed:
  - `build/test-bin/test_server_handlers_stage_attach_stale.exe`

## Follow-up On 2026-05-18 Control-Ready Registration No Longer Depends On Stale Runtime Bits

One more bind-path leak remained in `HandleAgentReady(...)`:

- the control-ready branch used `runtime_already_recorded` based on
  `IsAgentRuntimeReady(pid)` alone to decide whether to register the current agent
  session and process name
- that allowed a stale runtime-ready bit to suppress the current control-ready
  session binding
- but a live runtime session for the same pid must still keep control handling from
  regressing that real runtime session

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- control-ready now treats runtime as already recorded only when:
  - the runtime-ready bit is set, and
  - a live agent session is already bound for that pid
- this preserves real runtime-session stability while preventing stale runtime bits
  from suppressing current control-ready binding

Added regression coverage:

- stale runtime-ready without a live session must still allow control-ready binding
- a real runtime-bound pid must still keep runtime session ownership intact

Verification:

- targeted server-handler test passed:
  - `build/test-bin/test_server_handlers_stage_control_stale.exe`

Additional verification after settling the surrounding handler expectations:

- full focused handler suite passed:
  - `build/test-bin/test_server_handlers_clean.exe`

## Post-fix note

One failure during this round looked like a Windows memory-access dialog, but it was
actually the test binary aborting on an `assert` after the attach path was tightened to
require authoritative process identity. The manual ready-state fixtures in
`test_server_handlers.cpp` now seed `RegisterAgentProcessName(...)` where they are
intended to model a reused runtime agent session, so the replay path matches the real
server state again.

## Follow-up On 2026-05-18 Attach Wait No Longer Trusts Stale Runtime Bits

The attach path still had one remaining stale-side-channel after the earlier reuse
hardening:

- attach reuse was already gated by real identity
- but the injection path still waited on `WaitForAgentRuntimeReady(pid)`
- a stale pid-level runtime-ready bit could therefore unblock attach before the
  matching runtime agent session for the current process identity had actually arrived

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_server_handlers_attach_runtime_identity_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_attach_runtime_identity_focus.cpp)
- [tests/communication/test_session_registry_runtime_identity_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry_runtime_identity_focus.cpp)

Changes:

- added runtime-identity registry helpers that require:
  - pid
  - process identity
  - authoritative runtime stage
- `HandleAttachRequest(...)` now waits for the matching runtime-ready session instead
  of a bare pid-level runtime-ready bit
- preserved the existing late-control protection:
  - control-ready must still not overwrite a pid that already owns a runtime session
- tightened attach runtime reuse to require a live session:
  - stale dead sessions no longer satisfy runtime-ready lookup by pid/identity
  - attach should not unblock on a dead session that still has pid-level runtime bits

Verification:

- focused session-registry runtime-identity wait wrapper passed:
  - `build/test-bin/test_session_registry_runtime_identity_focus.exe`
- focused server-handler attach runtime-identity wrapper passed:
  - `build/test-bin/test_server_handlers_attach_runtime_identity_focus.exe`

## Follow-up On 2026-05-18 Atomic Pending-Spawn Host Bind Prevents Finalize-Rollback Rebind

The spawn finalize-failure path still had a race with `HandleAgentReady(...)`:

- `ResolvePendingSpawn(...)` could succeed
- `HandleAgentReady(...)` could then bind the host session from a copied pending entry
- `ExecuteSpawnRequest(...)` could later fail `FinalizeSpawn(...)` and unbind the host
- the late agent-ready thread could still rebind the host after that cleanup

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp)

Changes:

- added `BindHostToResolvedPendingSpawn(...)` to make pending-spawn host binding
  atomic with the pending-spawn presence check
- `HandleAgentReady(...)` now uses the atomic registry bind helper instead of binding
  from a stale copied pending entry
- finalize-failure rollback no longer races with a late agent-ready bind

Verification:

- focused finalize-cleanup wrapper passed:
  - `build/test-bin/test_server_handlers_spawn_finalize_cleanup_focus.exe`
- full server-handler suite passed:
  - `build/test-bin/test_server_handlers_clean_v6.exe`
- full session-registry suite passed:
  - `build/test-bin/test_session_registry_full_v8.exe`

Packaging:

- single-server package rebuilt successfully:
  - `tools/build_single_server_package.ps1 -ForceRebuild`

## Follow-up On 2026-05-18 Attach No Longer Accepts Spawn-Only Routing Flags

Real-device attach validation showed a confusing host-side behavior:

- plain attach worked:
  - `nook-cli -U com.ad2001.frida0x8 -l ...`
- but Frida-style top-level attach still accepted:
  - `--symbi`
  - `--strict-zygote-control`
- those flags are spawn-routing controls, not attach semantics
- the old CLI normalization path silently dropped them for top-level attach, which made
  failure analysis noisy and looked like an attach backend regression

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- Frida-style top-level attach normalization now preserves the user-provided
  `spawn_symbi` / `strict_zygote_control` bits instead of discarding them immediately
- added explicit host-side validation:
  - attach
  - repl attach
  - `call --attach`
  now reject spawn-only routing flags up front
- validation runs before banner/device creation so the CLI no longer prints a partial
  startup sequence and then wanders into a misleading attach timeout

Verification:

- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_main_rejects_symbi_for_frida_style_top_level_attach`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_main_rejects_spawn_backend_flags_for_call_attach`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_parser_supports_frida_style_top_level_attach`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_parser_supports_symbi_for_frida_style_spawn`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_call_command_can_attach_load_and_call_rpc`

Decision:

- `--symbi` remains spawn-only
- `--strict-zygote-control` remains spawn-only
- attach stays on the normal attach injection path until there is a real attach-specific
  backend design, instead of pretending these spawn knobs do something there

## Follow-up On 2026-05-18 Runtime-Stage Spawn Identity Gate

The next stale-state gap was inside the spawn lifecycle itself.

Even after tightening attach identity, spawn still had a weaker runtime boundary:

- cached runtime `AGENT_READY` replay after finalize still used pid-first lookup
- runtime-stage `AGENT_READY` could still try to upgrade a pending spawn using only:
  - pid
  - spawn token
- strict / zygote-control control-stage handoff legitimately uses transient names like
  `zygote64`
- but runtime-stage handoff must converge to the target process identity, otherwise a
  wrong-name runtime frame can pollute ready-state and cause partial or mistimed script
  load

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `GetAgentReadyFrameByIdentity(...)` so cached `AGENT_READY` replay can require:
  - pid
  - current process identity
  - decodable matching ready payload
- attach replay now uses the identity-aware cached ready lookup instead of plain
  pid-only replay
- spawn finalize replay now also uses identity-aware cached ready lookup
- `ResolvePendingSpawn(...)` now keeps the existing control-stage allowance:
  - control-stage may still resolve with transient names like `zygote64`
  - runtime-stage resolution must match the pending spawn target process name
- `HandleAgentReady(...)` now gates runtime-stage suspended-spawn upgrade and
  pending-spawn resolution on the pending spawn target identity when a spawn token is
  present
- result:
  - control-stage zygote ownership remains legal
  - runtime-stage wrong-name ready frames no longer upgrade the spawn transaction or
    get replayed as authoritative runtime ready

Verification:

- `build/test-bin/test_session_registry_identity_v3.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage11.exe`
- `build/test-bin/test_server_handlers_attach_runtime_identity_focus_v2.exe`
- `powershell -ExecutionPolicy Bypass -File .\\tools\\build_single_server_package.ps1 -ForceRebuild`

Artifact:

- rebuilt package:
  - [nook-server](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/single-server-package/arm64-v8a/nook-server)

## Follow-up On 2026-05-18 Spawn-Suspended Target Identity Split

The previous runtime-stage identity gate still had one structural weakness.

`SpawnSuspendedEntry` only carried:

- `authoritative_ready_stage`
- `authoritative_process_name`

That overloaded one field with two different meanings:

- current authoritative process identity
- eventual target app identity

Those are not the same during strict / zygote-control control-stage ownership:

- control-stage may legitimately be `zygote64`
- runtime-stage must eventually converge to the target app name

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `SpawnSuspendedEntry` now carries an explicit `target_process_name`
- `MarkSpawnSuspended(...)` now seeds both:
  - current authoritative identity
  - expected target identity
- binding a resolved pending spawn also propagates the pending target app name into the
  suspended transaction entry
- runtime-stage `HandleAgentReady(...)` now resolves its expected identity in this order:
  - live pending spawn target name
  - suspended transaction target name
- runtime-stage no longer depends on a still-live pending spawn record to know what
  process identity is allowed to upgrade the transaction
- finalize-time cached runtime replay now prefers the suspended transaction target name
  over the transient authoritative control-stage name

Why this matters:

- control-stage ownership can still be `zygote64`
- runtime-stage target matching remains stable even after pending-spawn cleanup
- this is closer to the intended transaction-owned model for `agent-owned stable spawn`
  than inferring target identity from whichever authoritative process happened to be
  current at the moment

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage12.exe`
- `build/test-bin/test_session_registry_identity_v4.exe`
- `build/test-bin/test_server_handlers_attach_runtime_identity_focus_v3.exe`

## Follow-up On 2026-05-18 Late Runtime Promotion Guard

One more stale-state leak remained in `HandleAgentReady(...)`.

Even after earlier identity checks, a mismatched runtime-stage `AGENT_READY` could still
reach the late host/replay branch and do two incorrect things:

- promote the suspended spawn state to `kReadyForScriptLoad`
- forward runtime-ready / replay cached script messages to the bound host

That happened because the late branch still keyed off:

- `runtime_ready`
- `HasPendingSpawnForPid(pid)`

and did not require the runtime frame to match the target process identity.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- introduced `eligible_runtime_ready = runtime_ready && runtime_spawn_process_name_matches`
- late spawn-state promotion to `kReadyForScriptLoad` now requires
  `eligible_runtime_ready`
- runtime-stage host forwarding and cached `SCRIPT_MESSAGE` replay now also require
  `eligible_runtime_ready`
- mismatched runtime-stage ready frames are explicitly logged and dropped at the late
  host/replay boundary instead of mutating transaction state

Why this matters:

- wrong-name runtime frames can no longer upgrade a target-bound suspended spawn after
  the pending-spawn record is already gone
- the transaction-owned ready path now stays aligned with the same runtime identity gate
  used earlier during registration and resolution

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage14.exe`
- `build/test-bin/test_session_registry_identity_v4.exe`
- `build/test-bin/test_server_handlers_attach_runtime_identity_focus_v3.exe`

## Follow-up On 2026-05-18 Script Message Sender Identity Gate

The last remaining contamination path was on the agent-to-host `SCRIPT_MESSAGE` path.

Before this fix, `HandleScriptMessage(...)` only used the sender pid:

- if the pid was in a spawn-blocked state, it cached the message
- if a host was bound, it forwarded the message

That was too loose for strict / zygote-control handoff because a stale or mismatched
runtime session could still set the pid on its `Session` object and inject messages into
the cache window.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `HandleScriptMessage(...)` now checks the sender against the current agent session for
  that pid when the registry already knows one
- mismatched sessions are dropped before cache or forward logic runs
- this keeps the cached replay window aligned with the current agent ownership model

Why this matters:

- a wrong runtime agent can no longer poison the cached script message list for a
  target-bound spawn
- the runtime handoff stays tied to the same current-session boundary used by the rest of
  the spawn state machine

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage15.exe`
- `build/test-bin/test_session_registry_identity_v4.exe`
- `build/test-bin/test_server_handlers_attach_runtime_identity_focus_v3.exe`

## Follow-up On 2026-05-18 Agent-To-Host Response Session Boundary

The same stale-session problem also existed on agent-to-host response frames.

Before this fix, these handlers still trusted only `session.GetPeerPid()`:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_LOAD_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

So an old or mismatched session for the same pid could still forward responses back to
the host even after ownership had already moved to another agent session.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- introduced a shared current-agent-session acceptance helper in `server_handlers.cpp`
- `SCRIPT_MESSAGE` now uses the same helper instead of re-implementing the rule inline
- `SCRIPT_CREATE_RESP`, `SCRIPT_LOAD_RESP`, `SCRIPT_UNLOAD_RESP`, and `RPC_RESPONSE`
  now drop frames from non-current agent sessions before any host forwarding

Why this matters:

- stale control/runtime sessions can no longer race valid responses into the host side
- the host now only consumes agent-originated traffic from the same current session that
  owns script/rpc operations for that pid

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_session_registry_identity_v4.exe`
- `build/test-bin/test_server_handlers_attach_runtime_identity_focus_v3.exe`

## Follow-up On 2026-05-18 Host-Owned Spawn Transaction Cleanup

There was still one transaction-lifetime leak on the host side.

Before this fix, `RemoveHostSession()` only cleared:

- the host session entry
- `pid -> host` bindings
- owned pending-spawn records

But it did not clear host-owned suspended spawn transactions or their cached
`SCRIPT_MESSAGE` frames. That meant a host disconnect during spawn could leave behind:

- a stale `spawn_suspended_entries_` gate
- stale cached script messages for that abandoned transaction

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `RemoveHostSession()` now tracks pids owned by the removed host session
- for those pids it clears:
  - host-owned suspended spawn entries
  - cached script message frames
- the cleanup is restricted to transactions owned by the removed host session, so other
  bound hosts are preserved

Why this matters:

- a dead host can no longer leave a stale blocked spawn state behind
- later attach/spawn flows will not inherit cached script messages from an abandoned
  transaction
- this is closer to a real transaction-owned lifecycle instead of pid-owned residue

Verification:

- `build/test-bin/test_session_registry_identity_v5.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_server_handlers_full_stage18.exe`

## Follow-up On 2026-05-18 Host Rebind Transaction Cleanup

There was one more host-owned transaction leak adjacent to host removal.

Before this fix, `BindHostToPid()` removed the old `pid -> host` mapping when the same
host session rebound to a different pid, but it did not clear host-owned suspended spawn
state or cached script messages for the old pid.

That meant a single host could carry stale transaction residue across rebind:

- old suspended spawn state on the previous pid
- old cached `SCRIPT_MESSAGE` frames on the previous pid

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `BindHostToPid()` now tracks old pids released by the rebind
- for each released pid, if the suspended spawn entry is owned by the rebinding host,
  it is cleared
- cached script message frames for those released pids are also cleared

Why this matters:

- a host session can no longer drag stale spawn state from one pid to the next
- transaction ownership now follows the current host binding instead of leaving pid-local
  residue behind after rebind

Verification:

- `build/test-bin/test_session_registry_identity_v6.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_server_handlers_full_stage18.exe`

## Follow-up On 2026-05-18 Host Unbind Transaction Cleanup

There was one final asymmetric cleanup path on the host lifecycle side.

Before this fix, `UnbindHostSession()` only removed `pid -> host` bindings. It did not
clear host-owned suspended spawn state or cached script messages for the released pid.

So after fixing:

- `RemoveHostSession()`
- `BindHostToPid()`

there was still one remaining way for a host-owned transaction to leak:

- explicit `UnbindHostSession()`

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `UnbindHostSession()` now tracks released pids
- for each released pid, if the suspended spawn entry is owned by that host session,
  it is cleared
- cached script message frames for those released pids are also cleared

Why this matters:

- host-owned transaction cleanup is now symmetric across:
  - host removal
  - host rebind
  - host unbind
- transaction residue is no longer left behind just because cleanup happened through a
  different host lifecycle path

Verification:

- `build/test-bin/test_session_registry_identity_v7.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_server_handlers_full_stage18.exe`

## Follow-up On 2026-05-18 Runtime Session Removal State Fallback

There was still a high-value agent lifecycle inconsistency on session removal.

Before this fix, if a runtime session was current for a pid and then disconnected while a
control-ready session still remained pinned for that pid, `RemoveAgentSessionByPidIfMatches()`
would:

- rebind `agent_sessions_[pid]` back to the surviving session

but it would not also downgrade the associated readiness state. That left behind stale
runtime-owned metadata such as:

- `agent_runtime_ready_[pid]`
- runtime-stage `agent_ready_stages_[pid]`
- cached runtime `AGENT_READY` frame

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- when `RemoveAgentSessionByPidIfMatches()` falls back to a surviving control-ready
  session for the same pid, it now:
  - sets ready stage back to `kControl`
  - clears the runtime-ready bit
  - clears the cached runtime `AGENT_READY` frame

Why this matters:

- current session ownership and readiness metadata are now consistent after runtime
  disconnect
- later attach/spawn logic will no longer observe a false runtime-ready state after the
  runtime session is already gone

Verification:

- `build/test-bin/test_session_registry_identity_v8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_server_handlers_full_stage18.exe`

## Follow-up On 2026-05-18 Spawn State Fallback After Runtime Disconnect

After fixing the raw session fallback, there was still a handler-visible inconsistency.

If the current runtime session disconnected and registry ownership fell back to a pinned
control-ready session, the session pointer and ready metadata would no longer claim
runtime ownership, but a previously ready spawn transaction could still remain at:

- `kReadyForScriptLoad`
- or later states derived from runtime ownership

That left a bad split-brain condition:

- registry current session = control-ready fallback
- spawn transaction state = runtime-ready / script-load-ready

In that state, host script operations could still be treated as runnable and end up
targeting the wrong side of the fallback.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- when `RemoveAgentSessionByPidIfMatches()` falls back from a runtime session to a
  surviving control-ready session, it now also downgrades any suspended spawn entry for
  that pid:
  - authoritative ready stage -> `kControlReady`
  - spawn state -> `kWaitingRuntimeReady` unless it was still `kWaitingAgentReady`

Why this matters:

- current session ownership and spawn transaction state now converge to the same control
  fallback view
- host script operations stop being incorrectly treated as runtime-loadable after the
  runtime session has already gone away

Verification:

- `build/test-bin/test_session_registry_identity_v8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`
- `build/test-bin/test_server_handlers_full_stage19.exe`

## Follow-up On 2026-05-18 Post-Disconnect Request Gating

There was one more handler-level leak after runtime disconnect fallback.

After downgrading current ownership from runtime to control-ready fallback and restoring
spawn state to `kWaitingRuntimeReady`, `SCRIPT_CREATE` and `SCRIPT_LOAD` were already
protected because they explicitly gate on suspended spawn state.

But two host-to-agent request paths still ignored that gate:

- `SCRIPT_POST`
- `RPC_REQUEST`

So after runtime disconnect they could still be forwarded to the surviving control-ready
session, even though runtime ownership had already been revoked.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptPost(...)` now drops `SCRIPT_POST` while the pid is still in a spawn-blocked
  state
- `HandleRpcRequest(...)` now drops `RPC_REQUEST` while the pid is still in a spawn-blocked
  state

Why this matters:

- host-to-agent traffic now respects the same runtime-readiness gate across:
  - script create
  - script load
  - script post
  - rpc request
- control-ready fallback can no longer receive post-runtime traffic that should only run
  once runtime ownership is re-established

Verification:

- `build/test-bin/test_server_handlers_full_stage20.exe`
- `build/test-bin/test_session_registry_identity_v8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`

## Follow-up On 2026-05-18 Script Unload Post-Disconnect Gate

`SCRIPT_UNLOAD` still had the same post-disconnect hole as `SCRIPT_POST` and
`RPC_REQUEST`.

After runtime ownership was revoked and spawn state was downgraded to the blocked
`kWaitingRuntimeReady` view, `SCRIPT_UNLOAD` could still be forwarded to the surviving
control-ready fallback session even though the runtime-owned script context was already
gone.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptUnload(...)` now drops the request while the pid is still in a
  spawn-blocked state

Why this matters:

- post-runtime host-to-agent requests now consistently respect the same runtime gate
- control-ready fallback can no longer receive unload traffic that should only be issued
  against a live runtime-owned agent session

Verification:

- `build/test-bin/test_server_handlers_full_stage21.exe`
- `build/test-bin/test_session_registry_identity_v8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`

## Follow-up On 2026-05-18 Immediate Error Responses For Blocked Requests

After adding post-disconnect request gating, two request types were still too weak from
the host-side API perspective.

They were no longer being misrouted to the control-ready fallback, but they were only
being dropped:

- `RPC_REQUEST`
- `SCRIPT_UNLOAD`

That prevented wrong-side execution, but it still left the host without an immediate
protocol-level answer.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- blocked `RPC_REQUEST` now returns an immediate `RPC_RESPONSE` failure with:
  - code `-5`
  - message `spawned pid is not ready for rpc request`
- blocked `SCRIPT_UNLOAD` now returns an immediate `SCRIPT_UNLOAD_RESP` failure with:
  - code `-5`
  - message `spawned pid is not ready for script unload`

Why this matters:

- host-side callers now get a deterministic protocol response instead of a silent drop
- post-disconnect semantics are both safe and explicit:
  - no misrouting to control fallback
  - no hanging on missing response frames

Verification:

- `build/test-bin/test_server_handlers_full_stage22.exe`
- `build/test-bin/test_session_registry_identity_v8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_stage16.exe`

## Follow-up On 2026-05-18 Late Control-Stage Session Poisoning

After the request/response boundary cleanup, one more session-ownership edge remained.

If a pid had already established a current runtime-owned agent session, and some older
or non-current session later emitted another control-stage `AGENT_READY`, the handler
would still accept parts of that control-stage write path.

That was dangerous for one specific reason:

- `RegisterControlReadyAgentSession(...)` could be overwritten by a late non-current
  sender
- later, if the current runtime session disconnected, the fallback path could observe
  the wrong pinned control session
- that weakens the exact control/runtime separation we are trying to preserve before
  moving deeper into strict `zygote-control`

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `HandleAgentReady(...)` now explicitly drops a control-stage `AGENT_READY` when:
  - runtime ownership is already recorded for that pid, and
  - the sender is not the current authoritative agent session
- added a focused regression proving that:
  - the pinned control fallback is not replaced by a late non-current control sender
  - runtime removal still falls back to the original control session
  - spawn state downgrades back to `kWaitingRuntimeReady` / `kControlReady` as expected

Why this matters:

- runtime ownership and pinned control fallback are now consistent under late duplicate
  control traffic
- strict `zygote-control` follow-up work can rely on a cleaner invariant:
  once runtime ownership is established, old control-session echoes cannot rewrite the
  fallback anchor

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage17.exe`
- `build/test-bin/test_server_handlers_full_stage23.exe`
- `build/test-bin/test_session_registry_identity_v9.exe`

## Follow-up On 2026-05-19 Missing-Owned-Session Uninstall Cleanup

Another strict `zygote-control` retry-poisoning seam remained in the helper-side
ownership lifecycle.

`UninstallZygoteForkHookWithSendersForTest(...)` already knew when the current target was
explicitly owned:

- `owned_target == true`

and also knew when there was no longer an immediate control session for that owned target:

- `!HasImmediateControlSession(registry, zygote_pid, process_name)`

But that branch only returned:

- `zygote control-ready agent session not found ...`

without clearing the owned target mapping first.

That left one stale retry hazard:

1. a strict/zygote-control transaction owned `zygote64`
2. its control session disappeared before uninstall
3. uninstall failed fast, but the helper kept `owned_zygote_control_processes_[zygote64]`
4. the next strict retry still observed a poisoned owned target even though the old control
   session was already gone

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc_uninstall_owned_cleanup.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc_uninstall_owned_cleanup.cpp)

Changes:

- when uninstall sees:
  - an explicitly owned target
  - but no immediate control-ready session
- it still returns the same failure to the caller
- but now also clears:
  - `owned_zygote_control_processes_[process_name]`

Why this matters:

- missing-session uninstall is now treated as terminal cleanup for ownership state
- strict retries no longer inherit stale helper-side ownership from a dead control session
- this keeps the remaining strict `zygote-control` state machine closer to real live
  session identity instead of process-name residue

Verification:

- red/green focused regression:
  - `build/test-bin/test_zygote_control_rpc_uninstall_owned_cleanup.exe`
- existing owned-disconnect cleanup still passes:
  - `build/test-bin/test_zygote_control_rpc_disconnect_owned_cleanup.exe`

## Follow-up On 2026-05-19 Install Ready-Wait Owned Cleanup

The symmetric stale-ownership seam also existed on the install side.

`InstallZygoteForkHookWithSendersForTest(...)` already only called:

- `MarkOwnedZygoteControlProcess(...)`

when the final post-install `WaitForZygoteControlReadyWithTimeoutForTest(...)` succeeded.

But that still left one retry-poisoning gap:

1. an older strict transaction had already left `owned_zygote_control_processes_[zygote64]`
2. a new install transaction reached:
   - ready
   - status
   - install
3. the final post-install ready wait failed
4. the helper returned `false`, but the old owned mapping stayed in place

That meant a failed install transaction could still inherit the previous ownership state even
though the new transaction had just failed to re-establish control readiness.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc_install_ready_wait_owned_cleanup.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc_install_ready_wait_owned_cleanup.cpp)

Changes:

- after the final post-install ready wait:
  - success still marks owned
  - failure now clears any previous owned mapping for that process name
- the existing install ready-wait regression was strengthened to start with a pre-existing owned
  target and prove it is cleared on failure
- a focused wrapper was added so this exact case can be run without depending on the whole
  legacy zygote-control test binary

Why this matters:

- strict install failure no longer preserves stale ownership across retries
- install and uninstall now both converge on the same rule:
  - ownership is only retained while a live transaction successfully re-establishes it

Verification:

- red/green focused regression:
  - `build/test-bin/test_zygote_control_rpc_install_ready_wait_owned_cleanup.exe`
- uninstall cleanup still passes:
  - `build/test-bin/test_zygote_control_rpc_uninstall_owned_cleanup.exe`
- disconnect cleanup still passes:
  - `build/test-bin/test_zygote_control_rpc_disconnect_owned_cleanup.exe`

## Follow-up On 2026-05-19 Dedicated Install Hard-Failure Owned Cleanup

One more terminal install branch still preserved stale strict ownership.

After the earlier ready-wait cleanup, `InstallZygoteForkHookWithSendersForTest(...)` still
had this path:

1. live control-ready session exists
2. dedicated `spawn.install` message is chosen
3. dedicated install fails with a non-compat hard error
4. helper returns `false`
5. previous `owned_zygote_control_processes_[process_name]` entry remains untouched

That meant a failed dedicated install transaction could still leave behind the old
ownership record even though the current transaction had already terminated hard before the
fork hook was re-established.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc_install_hard_fail_owned_cleanup_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc_install_hard_fail_owned_cleanup_focus.cpp)

Changes:

- dedicated `spawn.install` hard failure now also clears any pre-existing owned
  zygote-control mapping for that process name before returning `false`
- the main zygote-control test file now includes a dedicated regression for this branch
- the focused wrapper runs only that case, using a live control-session test transport so
  the dedicated install sender path is actually exercised

Why this matters:

- strict ownership now converges across three terminal install/uninstall cases:
  - dedicated install hard failure
  - post-install ready-wait failure
  - uninstall without a remaining owned control session
- retries no longer inherit stale zygote-control ownership when the dedicated install path
  dies early

Verification:

- dedicated install hard-failure cleanup:
  - `build/test-bin/tzcihfof.exe`
- install ready-wait cleanup:
  - `build/test-bin/tzcirwoc.exe`
- uninstall missing-session cleanup:
  - `build/test-bin/tzciruoc.exe`
- disconnect cleanup:
  - `build/test-bin/tzcirdoc.exe`

## Follow-up On 2026-05-19 RPC Fallback Install-Failure Owned Cleanup

There was still one more terminal install branch keeping stale ownership alive.

After the dedicated install hard-failure cleanup, this path still remained:

1. live control-ready session exists
2. dedicated `spawn.install` is attempted
3. dedicated path soft-fails with a compat fallback error
4. helper falls back to generic `nook.spawn.installForkHook` RPC
5. generic install RPC fails
6. previous `owned_zygote_control_processes_[process_name]` entry remains

That left another retry-poisoning case where a transaction had already terminally failed,
but the old strict ownership record still survived because the failure happened in the
generic fallback leg instead of the dedicated leg.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc_install_rpc_fallback_fail_owned_cleanup_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc_install_rpc_fallback_fail_owned_cleanup_focus.cpp)

Changes:

- when the generic fallback `installForkHook` RPC fails, the helper now also clears any
  pre-existing owned zygote-control mapping before returning `false`
- the main zygote-control test file now includes an explicit regression for this branch
- a focused wrapper runs just this case so it can be validated independently of the larger
  legacy test binary

Why this matters:

- strict ownership cleanup now covers four validated terminal branches:
  - dedicated install hard failure
  - fallback install RPC failure
  - post-install ready-wait failure
  - uninstall without a remaining owned control session
- retries no longer depend on which install leg failed; both dedicated and fallback
  terminal failures now converge on the same ownership cleanup rule

Verification:

- fallback install RPC failure cleanup:
  - `build/test-bin/tzcirffoc.exe`
- dedicated install hard-failure cleanup:
  - `build/test-bin/tzcihfof.exe`
- install ready-wait cleanup:
  - `build/test-bin/tzcirwoc.exe`
- uninstall missing-session cleanup:
  - `build/test-bin/tzciruoc.exe`
- disconnect cleanup:
  - `build/test-bin/tzcirdoc.exe`

## Follow-up On 2026-05-19 Status-Rpc-Failure Owned Cleanup

One more earlier install-stage exit still preserved stale strict ownership.

Even after the dedicated/fallback install failure cleanups, the helper still had this
terminal path:

1. initial control-ready wait succeeds
2. install enters the explicit `nook.spawn.status` probe
3. status RPC fails
4. helper returns `false`
5. existing `owned_zygote_control_processes_[process_name]` mapping remains

That meant a transaction could abort before any install leg even started, but still leave
behind the previous strict ownership record.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc_install_status_fail_owned_cleanup_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc_install_status_fail_owned_cleanup_focus.cpp)

Changes:

- when the explicit `nook.spawn.status` RPC fails during install setup, the helper now also
  clears any pre-existing owned zygote-control mapping before returning `false`
- the main zygote-control test file now includes a dedicated regression for this earlier
  terminal branch
- a focused wrapper validates this case independently

Why this matters:

- strict ownership cleanup now covers the validated terminal install stages from earlier to
  later:
  - status RPC failure
  - dedicated install hard failure
  - fallback install RPC failure
  - post-install ready-wait failure
  - uninstall without a remaining owned control session
- retries no longer depend on how far the install transaction progressed before failing;
  all covered terminal branches now converge on the same ownership cleanup rule

Verification:

- status RPC failure cleanup:
  - `build/test-bin/tzcisfoc.exe`
- fallback install RPC failure cleanup:
  - `build/test-bin/tzcirffoc.exe`
- dedicated install hard-failure cleanup:
  - `build/test-bin/tzcihfof.exe`
- install ready-wait cleanup:
  - `build/test-bin/tzcirwoc.exe`
- uninstall missing-session cleanup:
  - `build/test-bin/tzciruoc.exe`
- disconnect cleanup:
  - `build/test-bin/tzcirdoc.exe`

## Follow-up On 2026-05-18 Child-Owned Spawn Gate Conservative Arming

One more race remained below the server-side stage model.

Even after the server-side authoritative child boundary was tightened, the child-owned
spawn path could still occasionally miss the earliest Java bootstrap gate:

- the child had already been selected by `symbi`
- the embedded full agent had already been injected into the child
- `NOOK_SPAWN_TOKEN` was present
- but `ShouldArmSpawnGateForCurrentProcess()` still returned `false` if `JNIEnv*`
  was not available yet

That left a small cold-start window where startup-time Java methods could run before
the child-side gate/bootstrap hooks had a chance to block app bootstrap, which matched
the observed "script loaded but only later hooks fire" symptom on `frida-0x1`.

Updated:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- [tests/headers/test_symbi_child_owned_spawn_context_regression.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_symbi_child_owned_spawn_context_regression.cpp)

Changes:

- spawned child processes with:
  - non-early process identity
  - `NOOK_SPAWN_TOKEN` present
  - `JNIEnv* == nullptr`
  now conservatively arm the in-process spawn gate instead of returning `false`
- normal attach/runtime processes without a spawn token still keep the old behavior
- added an explicit log marker:
  - `conservative spawn gate arming for spawned child without JNIEnv`

Why this matters:

- this does not change the server-side authoritative stage design
- it does close one child-local race underneath that design
- default `symbi-first` keeps the same child-owned architecture, but now holds app
  bootstrap more reliably until the Java-side gate has a chance to install

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_stage_current.exe`
- `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

## Follow-up On 2026-05-18 Atomic Pending-Spawn Rebind Cleanup Symmetry

There was still one asymmetric host-owned transaction cleanup seam in the pending-spawn
bind path.

`BindHostToResolvedPendingSpawn(...)` already performed the atomic:

- pending-spawn presence check
- host bind to the resolved pid

But unlike `BindHostToPid(...)`, it did not also clear transaction residue from any old
pid that the same host session had just been rebound away from.

That meant a resolved pending spawn could still leave behind host-owned residue on the
previous pid:

- stale `spawn_suspended_entries_`
- stale cached `SCRIPT_MESSAGE` frames

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `BindHostToResolvedPendingSpawn(...)` now tracks old pids released by the same-host
  rebind
- for each released pid, if the suspended spawn entry is owned by that host session, it
  is cleared
- cached script message frames for those released pids are also cleared

Why this matters:

- host-owned transaction cleanup is now symmetric across:
  - direct host bind
  - resolved pending-spawn bind
- a spawn transaction can no longer drag pid-local residue across the atomic
  pending-spawn handoff boundary
- this keeps the transaction-owned model cleaner before the next deeper
  `agent-owned stable spawn` steps

Verification:

- `build/test-bin/test_session_registry_bind_pending_cleanup.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_bind_pending_cleanup.exe`

## Follow-up On 2026-05-19 Zygote-Control Initial Ready-Wait Owned Cleanup

There was still one terminal stale-state seam left in the `zygote-control` install
path.

`InstallZygoteForkHookWithSendersForTest(...)` already cleared
`owned_zygote_control_processes_` on later hard-failure branches:

- dedicated `spawn.install` failure
- generic RPC fallback failure
- explicit `status` failure
- `clearForkHook` failure
- post-install ready wait failure

But the very first `WaitForZygoteControlReady(...)` gate at install entry still
returned early without clearing ownership when the current zygote target was already
marked as owned.

That meant a failed initial ready wait could leave stale ownership behind and poison
later strict install/uninstall state transitions, even though the install never really
started.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)

Changes:

- if the initial `WaitForZygoteControlReady(...)` gate fails and the target
  `(zygote_pid, process_name)` is currently owned, the install path now clears the
  owned zygote-control mapping before returning
- this makes the install path's terminal cleanup symmetric across all currently known
  early and late failure exits

Why this matters:

- strict `zygote-control` ownership no longer survives a failed pre-install ready gate
- the transaction model stays convergent even when the zygote control plane is absent
  or cold at install entry
- this removes the last red case from the focused owned-cleanup regression set for the
  current audit pass

Verification:

- `build/test-bin/tzcirocf.exe`
- `build/test-bin/tzcicfoc.exe`
- `build/test-bin/tzcisfoc.exe`
- `build/test-bin/tzcirffoc.exe`
- `build/test-bin/tzcihfof.exe`
- `build/test-bin/tzcirwoc.exe`
- `build/test-bin/tzciruoc.exe`
- `build/test-bin/tzcirdoc.exe`

## Follow-up On 2026-05-19 USAP Missing-Session Soft-Skip Alignment

While pulling the focused owned-cleanup cases back into the main
`test_zygote_control_rpc` binary, one more asymmetry surfaced on the uninstall side.

`UninstallZygoteForkHookWithSendersForTest(...)` already treated
`zygote control-ready agent session not found: usap32` as a soft skip on:

- the dedicated `spawn.uninstall` sender failure path
- the generic uninstall RPC failure path

But the earlier branch:

- `owned_target && !HasImmediateControlSession(...)`

still returned `false` for `usap*`, even though that situation is the same practical
"missing control-ready session" condition and cannot make forward progress.

Updated:

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)

Changes:

- when the uninstall path hits `owned_target && !HasImmediateControlSession(...)` for
  `usap*`, it now clears ownership and returns soft-success, matching the later
  dedicated/RPC missing-session compatibility behavior
- the old test expectation that this branch must attempt one failing RPC was removed,
  because with no immediate session the correct behavior is to soft-skip before RPC

Why this matters:

- uninstall semantics for `usap*` are now consistent across all missing-session exits
- owned zygote-control state cannot stay stuck just because the session was gone before
  the uninstall dispatch point

Verification:

- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-21 Cached Script Message Partial Replay Cleanup

Another small lifecycle leak was in cached `SCRIPT_MESSAGE` replay after runtime-ready.

Before this change, replay used an all-or-nothing cache rule:

- all cached frames were read with `GetScriptMessageFrames(pid)`
- if every send succeeded, the pid cache was cleared
- if any send failed, the whole cache stayed intact

That preserved data, but it could duplicate messages. If the first cached message had
already been delivered to the host and the second send failed, the first message would be
sent again on the next replay attempt.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `SessionRegistry::DropScriptMessageFramePrefix(pid, count)`
- `ReplayCachedScriptMessages(...)` now tracks how many cached frames were actually sent
- if replay partially fails, the successfully delivered prefix is removed
- the failed frame and all later frames remain cached for a later retry
- successful replay still clears the full cache

Why this matters:

- cached script messages now have acknowledgement-style cleanup semantics
- repeated spawn/runtime-ready retries should not duplicate already-forwarded cached
  script messages
- the cache still remains lossless for unsent messages when the host send path fails

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-21 Attach Replay Uses Same Cached Message Prefix Semantics

After the cached replay helper was changed to remove only the successfully delivered
prefix on partial failure, the next check was whether attach replay was covered by the
same contract.

Updated:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Change:

- added a full handler regression for attach replay with:
  - cached `AGENT_READY`
  - two cached `SCRIPT_MESSAGE` frames
  - host send failure after the first cached script message is delivered
- the test verifies that only the unsent second script message remains cached

Why this matters:

- spawn runtime-ready replay and attach replay now share the same tested cache
  acknowledgement semantics
- reconnect/retry paths should not duplicate already-forwarded cached script messages
  regardless of whether the host reached the target through spawn or attach

Verification:

- `build/test-bin/test_server_handlers_current.exe`

## Follow-up On 2026-05-21 Spawn Finalize Replay Uses Shared Cached Message Helper

One remaining replay path still had its own hand-written cached `SCRIPT_MESSAGE`
loop in `spawn_controller`.

The runtime-ready and attach paths had already converged on:

- `ReplayCachedScriptMessages(...)`

which removes only the successfully delivered prefix on partial send failure. The
spawn finalize replay path still used the older all-or-nothing logic:

- if all cached messages replayed, clear the cache
- if any send failed, keep the entire cache

That could duplicate a cached message that had already been delivered after
`SpawnResponse` and cached runtime `AGENT_READY`.

Updated:

- [server/server_handlers.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.h)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- exported `ReplayCachedScriptMessages(...)` as an internal server helper
- `spawn_controller` now uses the same helper after successful spawn finalize
- added a focused regression where:
  - `SpawnResponse` succeeds
  - cached runtime `AGENT_READY` succeeds
  - first cached `SCRIPT_MESSAGE` succeeds
  - second cached `SCRIPT_MESSAGE` fails
- the test verifies that only the unsent second message remains cached

Why this matters:

- spawn finalize replay, runtime-ready replay, and attach replay now share the same
  acknowledgement-style cached message semantics
- repeated spawn retries should not duplicate already-forwarded cached script messages
- this removes another shell/controller-specific replay fork from the spawn lifecycle

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_current.exe`

## Follow-up On 2026-05-21 Spawn Finalize Ready Replay Gates Script Replay

The previous step unified cached `SCRIPT_MESSAGE` replay, but spawn finalize still did
not check whether cached runtime `AGENT_READY` replay actually reached the host.

Before this change:

- `SpawnResponse` could be sent successfully
- cached runtime `AGENT_READY` replay could fail
- cached `SCRIPT_MESSAGE` frames would still be replayed immediately afterward

That violates the same ordering rule already enforced in attach and runtime-ready
paths: script messages must not outrun the ready frame.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- spawn finalize now records whether cached runtime `AGENT_READY` replay succeeded
- cached `SCRIPT_MESSAGE` replay only runs when the ready replay succeeded or when
  there was no cached ready frame to send
- added a regression where `SpawnResponse` succeeds, cached runtime `AGENT_READY`
  fails, and cached script messages must remain cached without another send attempt

Why this matters:

- spawn finalize now preserves the ready-before-script ordering boundary
- cached script messages are not lost and are not delivered ahead of runtime-ready
- this further aligns spawn finalize replay with attach replay and runtime-ready replay

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_current.exe`

## Follow-up On 2026-05-21 Pending Spawn Clear Cleanup

The next stale-state boundary was `ClearPendingSpawn(spawn_token)`.

Before this change, if a pending spawn had already been resolved by an
`AGENT_READY` but had not yet been bound to a host transaction, clearing the token
only removed the pending entry. The resolved pid's agent session, process-name
binding, runtime-ready flag, cached ready frame, and cached script messages could
remain visible to later spawn attempts.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `ClearPendingSpawn(...)` now inspects the removed pending entry.
- If the entry had resolved to a pid but no bound spawn context or host binding
  exists yet, the registry clears the pid's agent/session/cache state and marks
  the pid invalidated.
- If the pid is already owned by a bound spawn transaction, only the pending token
  is removed. This preserves successful in-flight paths where ordering is already
  controlled by `SpawnSuspendedEntry::response_pending`.
- Added a regression test for resolved-but-unbound pending spawn cleanup.

Why this matters:

- cancel, timeout, and failure paths no longer leave resolved orphan agents in the
  registry
- successful spawn paths still keep their bound agent state alive
- this further moves spawn ownership from global pending-token inference toward
  explicit per-pid transaction state

Verification:

- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-21 Resume Cache Boundary

Resume success ends the spawn gate lifecycle for that pid. Any cached
`SCRIPT_MESSAGE` frames are only valid while the spawn transaction is still waiting
for the ready boundary or replay boundary.

Before this change, `HandleResumeRequest(...)` cleared the suspended spawn entry but
left cached script messages behind. A later host attach/replay path could therefore
observe stale messages from the already-resumed spawn transaction.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- resume success now clears cached script messages for the resumed pid immediately
  after clearing the spawn-suspended entry
- added a regression test proving cached spawn messages do not survive successful
  resume

Why this matters:

- cached script messages now belong strictly to an active spawn transaction
- once the gate is released, no stale spawn messages can be replayed to a later host
- this further narrows the lifecycle boundary around agent-owned spawn state

Verification:

- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-21 Spawn Transaction Terminal Cleanup

`ClearSpawnTransactionByPid(pid)` is used by terminal paths such as spawn timeout,
missing pending entry, finalize failure, and spawn-response send failure.

Before this change it only cleared the suspended spawn entry, cached script messages,
and host binding. That left the pid's agent session, process-name mapping,
authoritative-ready state, runtime-ready state, and cached ready frame alive after
the transaction had already been abandoned.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

- `ClearSpawnTransactionByPid(...)` now also clears agent state for that pid, removes
  attach timeout side-state, marks the pid invalidated, and wakes agent-ready
  waiters.
- Added a regression test proving that a bound spawn transaction clear removes the
  pid's agent/session/cache state and host binding.

Why this matters:

- terminal spawn paths now have one consistent cleanup primitive
- abandoned child agents cannot be accidentally reused by a later spawn or attach
- this keeps the stable spawn state machine pid-owned instead of relying on pending
  tokens or stale process-name lookups

Verification:

- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-21 Host Disconnect Clears Resolved-But-Unbound Pending Spawn State

The next lifecycle seam was a narrow disconnect window between pending-spawn
resolution and host binding.

Problem:

- a spawn host could disconnect after `ResolvePendingSpawn(...)` recorded a child
  pid, but before `BindHostToResolvedPendingSpawn(...)` bound that pid to the host
- `RemoveHostSession(...)` removed the pending-spawn entry by host id
- because the child pid had not yet entered `pid_to_host_session_` and might not
  have a `SpawnSuspendedEntry`, the already-registered agent state for that pid could
  remain alive:
  cached `AGENT_READY`, runtime-ready bits, process-name mapping, script-message
  frames, and agent session

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Behavior now guaranteed:

- when `RemoveHostSession(...)` removes a pending spawn owned by that host, any
  already-resolved child pid is treated as owned cleanup scope
- the resolved child pid has agent state, cached ready frames, cached script messages,
  attach side state, and suspended-spawn residue cleared
- the pid is marked invalidated so a later context-free/stale `AGENT_READY` cannot
  reclaim the host identity after the request owner has gone away
- pending spawns belonging to other host sessions are unchanged

Regression added:

- `TestRemoveHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind()`

Verification:

- `E:\\MinGW\\ucrt64\\bin\\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `build\\test_session_registry_current.exe`

## Follow-up On 2026-05-21 Host Unbind Also Clears Resolved-But-Unbound Pending Spawn State

After fixing `RemoveHostSession(...)`, the same ownership seam still existed on explicit
unbind paths such as detach/failure cleanup.

Problem:

- `UnbindHostSession(...)` cleared pid bindings and pending attach state
- it did not remove pending spawns owned by the host session
- if a pending spawn had already resolved to a child pid before unbind, the pending
  entry and child agent state could survive after the host stopped owning the request

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Behavior now guaranteed:

- `UnbindHostSession(...)` removes pending spawns owned by that host session
- already-resolved pending-spawn child pids go through the same agent/cache cleanup
  as host removal
- pending-spawn waiters are notified when unbind clears the entry
- this makes host removal and host unbind symmetric for resolved-but-unbound spawn
  ownership

Regression added:

- `TestUnbindHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind()`

Verification:

- `E:\\MinGW\\ucrt64\\bin\\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `build\\test_session_registry_current.exe`
- `E:\\MinGW\\ucrt64\\bin\\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/handler/message_dispatcher.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build\\test-bin\\test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-20 Pending-Spawn Consumption Race Before Spawn Context Bind

Another runtime-ready race was still present in the authoritative spawn path.

Before this fix, `ExecuteSpawnRequest(...)` did this:

1. wait for authoritative pending-spawn readiness
2. immediately `TakePendingSpawn(spawn_token, ...)`
3. only afterwards bind host ownership and create/update the suspended spawn context

That created a real gap:

- the `pending_spawn` token had already disappeared
- but `spawn_suspended_entries_[pid]` was not established yet
- if a runtime-stage `AGENT_READY` arrived inside that gap, `HandleAgentReady(...)`
  could classify it as orphaned and drop it

This aligns with the device-side symptom family we have been seeing:

- spawn succeeded or partially succeeded
- app could come up
- but host still timed out waiting for runtime-ready, or later only partial hook/script
  behavior showed up because the authoritative runtime boundary had been missed once

Updated:

- [server/server_handlers.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.h)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a small test-only `ServerHandlerConfig::on_spawn_context_bound` hook so the
  race window can be pinned deterministically in unit coverage
- changed the authoritative spawn ordering so host ownership plus suspended spawn
  context are established through `BindHostToResolvedPendingSpawn(...)` before the
  pending-spawn token is consumed
- only after the suspended transaction exists do we `TakePendingSpawn(...)`
- added a regression where runtime-stage `AGENT_READY` is injected exactly after
  pending-spawn consumption but before finalize returns; it must still be retained
  and replayed after `SpawnResponse`

Why this matters:

- runtime-ready is no longer able to fall into a transient "no pending token, no
  suspended transaction" hole
- the spawn transaction owns the boundary earlier
- this is another concrete convergence step from token-map timing toward
  transaction-owned `agent-owned stable spawn`

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_runtime_current.exe`
- `build/test_ninjector_spawn_injector_current.exe`
- `build/test_zygote_control_rpc_current.exe`

## Follow-up On 2026-05-20 Strict Zygote-Control Route Semantics And Injector Test Reconnect

Returned to the local injector route coverage before going back to device-side strict
`zygote-control`.

Two separate issues had been conflated:

1. `test_ninjector_spawn_injector` had fallen behind current production signatures and
   could no longer be compiled cleanly
2. the strict route semantics had drifted across tests: some cases still treated
   `--strict-zygote-control` as "try zygote-control, then fallback", while the current
   route-state work needs strict mode to mean "pure zygote-control path, no symbi/legacy
   fallback"

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Changes:

- fixed injector API drift in both production and test code:
  - `inject_so_by_pid(pid, so_path, ready_token)`
  - `inject_embedded_agent_by_pid(pid, runtime_dir, ready_token)`
  - `InjectAgent(pid, agent_path, ready_token, error)`
- fixed the local zygote-helper fallback callsite shim so `inject_zygote_so_by_pid`
  and `inject_so_by_pid` can still be selected through one local lambda without
  mismatched function signatures
- recompiled the full local injector regression binary with the now-required
  `session_registry.cpp`, `session.cpp`, and `frame.cpp` dependencies
- tightened `ShouldAllowZygoteControlFallback(...)` so `strict_zygote_control=true`
  always returns `false`
- updated injector route tests to match the intended strict semantics:
  - strict soft install failures now abort instead of falling through to symbi/legacy
  - strict route-attempt failures now return a terminal zygote-control error instead of
    an empty error + fallback-allowed outcome
  - non-strict/default route tests keep the existing fallback coverage
- reconciled several older finalize/owner compatibility assertions with current
  production behavior:
  - zygote-control may retain compatibility token state in `active_spawn_owner_`
    without remaining an authoritative backend owner
  - residual zygote transactions do not automatically materialize a synthetic
    `owned_spawn_state.spawn_token` during finalize/retry snapshots
  - `BuildSpawnExecutionState(...)` now reflects the current default `symbi-first`
    policy instead of the older legacy-first expectation

Why this matters:

- strict `zygote-control` is now locally testable as a genuinely isolated route instead
  of a disguised fallback request
- route-state work can now move forward without mixed semantics inside the same test
  binary
- injector coverage is back on the mainline binary, so future strict-route changes can
  be validated before going to device

Verification:

- `build/test_ninjector_spawn_injector_current.exe`

## Follow-up On 2026-05-20 Zygote-Control RPC Mainline Reconnect

After moving back toward the strict device-side path, the next useful step was to
reconnect the main `zygote_control_rpc` regression binary instead of relying on the
smaller focus wrappers.

That surfaced three concrete issues:

1. several important ready-wait / owned-cleanup tests already existed in
   `test_zygote_control_rpc.cpp` but were not actually invoked from `main()`
2. the test helper that registered a control-ready session had not been updated to call
   `RegisterControlReadyAgentSession(...)`, so a number of "prefer control session"
   tests were building an invalid fixture
3. `SessionRegistry::WaitForControlReadyAgentSessionByIdentity(...)` still returned the
   raw authoritative `agent_sessions_[pid]` first on the `pid > 0` path, which could let
   a runtime session outrank a registered control-ready session even though the
   corresponding `FindControlReady...` helpers already preferred the control-ready slot

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)

Changes:

- reconnected missing main-runner coverage for:
  - ready-wait failure after fallback install
  - "do not mark owned when final ready-wait fails"
  - control-ready session preference over generic runtime session
  - runtime-authoritative ready-wait behavior when no dedicated control session exists
- fixed the control-ready session test helper to register both:
  - authoritative agent session
  - explicit control-ready session slot
- fixed `WaitForControlReadyAgentSessionByIdentity(...)` to use
  `ResolvePreferredControlReadySessionLocked(...)` on the primary `pid > 0` resolution
  path, matching the intended control-session preference semantics
- updated a couple of stale expectations in the main runner so they match the current
  wrapped ready-wait error shape and current runtime-authoritative control-capable
  semantics

Why this matters:

- the main zygote-control RPC runner now exercises the same ready-wait / owned-cleanup
  paths that are closest to the real strict device-side failures
- the session wait helper no longer lets a generic runtime session outrank a real
  control-ready session for zygote-control RPC selection
- this reduces one more source of mismatch between local RPC semantics and the strict
  spawn path that uses them

Verification:

- `build/test_zygote_control_rpc_current.exe`

## Follow-up On 2026-05-20 Strict Install-Hook Route And Legacy Clear Side-Channel Assumption

While tightening the local coverage around the strict device-side handshake, one easy
misread in the current code became explicit:

- the install-hook route still contains a local block that *would* append
  `"; rollback failed: clear zygote spawn control failed ..."` after install failure
- but that block only runs when `use_legacy_spawn_control_side_channel == true`
- and `ShouldUseLegacyZygoteSpawnControlSideChannel(has_install_hook)` currently returns
  `!has_install_hook`

That means:

- on the current install-hook-based strict route, the legacy clear side-channel rollback
  is not actually part of the active path
- install-hook failures currently surface as the install/ready-wait error itself, without
  an added legacy clear rollback suffix

Updated:

- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Changes:

- added a regression proving that strict install-hook failure currently does **not**
  invoke `clear_zygote_spawn_control(...)`
- kept the expected terminal error on the pure install/ready-wait message only

Why this matters:

- it prevents future confusion when comparing local behavior against device logs
- it also highlights a real architectural seam:
  install-hook strict routing and legacy side-channel cleanup are still partially
  co-located in code, but not both active on the same path
- if we later decide the install-hook strict route should own an explicit rollback, that
  needs to be designed deliberately, not inferred from the current dead local branch

Verification:

- `build/test_ninjector_spawn_injector_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Strict Finalize / Helper-Only Cleanup Coverage

After reconnecting the main injector route binary, one remaining local gap on the strict
path was finalize behavior for `helper_only_local_control`.

Updated:

- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Changes:

- added a regression proving that helper-only local-control finalize stops immediately on
  local helper uninstall failure and does not continue into
  `clear_zygote_spawn_control(...)`
- added a success-path regression proving helper-only local-control finalize clears both:
  - transaction-local `failure_state` / `lifecycle_state`
  - injector-global lifecycle/failure recorder state

Why this matters:

- the strict/helper-only path now has explicit local coverage for both:
  - early finalize abort at uninstall stage
  - recorder cleanup after successful finalize
- this removes one more source of ambiguity before returning to device-side strict
  `zygote-control` testing, especially around stale finalize state contaminating the next
  spawn/finalize attempt

Verification:

- `build/test_ninjector_spawn_injector_current.exe`

## Follow-up On 2026-05-20 Replay Send-Failure Convergence

Another response-side boundary was still consuming cached runtime output too eagerly
when replaying state to a host.

Two concrete cases were affected:

1. attach success replay:
   - `AttachResponse` is sent
   - cached `AGENT_READY` may be replayed
   - cached `SCRIPT_MESSAGE` frames may be replayed after that
2. runtime-ready host replay after spawn finalize:
   - runtime-stage `AGENT_READY` is forwarded to the host
   - cached `SCRIPT_MESSAGE` frames are replayed immediately after

Before this change, both paths still had a half-failed replay shape:

- if the leading ready frame send failed, later cached script messages could still be
  sent or consumed
- attach replay also still used destructive semantics for cached script messages before
  host delivery was fully known to succeed

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- attach replay now:
  - replays cached `AGENT_READY` first
  - stops immediately if that send fails
  - only replays cached `SCRIPT_MESSAGE` frames when the ready replay succeeded
  - only clears cached script-message frames after all replay sends succeed
- runtime-ready forward replay now:
  - forwards the runtime `AGENT_READY`
  - only replays cached `SCRIPT_MESSAGE` frames if that ready forward succeeded
  - preserves cached script-message frames on any replay send failure
- added focused regressions covering:
  - attach replay must preserve cached script messages when host replay send fails
  - attach replay must not continue into cached script-message replay after
    `AGENT_READY` replay failure

Why this matters:

- replay behavior is now ordered and atomic at the semantic level:
  if the lead ready frame is not delivered, later cached runtime output is not replayed
- cached script-message state is no longer destructively consumed on partial replay
  failure
- this removes another shell/transport race from the current pre-zygote-control
  stabilization track and further tightens the server-side ownership model

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-20 Script-Load Response Boundary And Replay Helper Dedup

Two final cleanup items were completed on the current server-side convergence track.

Updated:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

- added an explicit regression proving `SCRIPT_LOAD_RESP` still restores the suspended
  spawn state back to `kReadyForScriptLoad` even when the bound host exists but the
  host send fails
- factored the shared cached-script-message replay loop into a small helper used by:
  - attach success replay
  - runtime-ready replay after spawn-side forwarding
- kept the lead-ready-frame gating semantics unchanged:
  replay of cached script messages still only begins after the corresponding ready frame
  send has succeeded

Why this matters:

- the `SCRIPT_LOAD_RESP` boundary is now covered for all three relevant cases:
  - normal host forward
  - no usable host forward
  - host send failure
- replay semantics are less likely to drift between attach and runtime-ready paths
  because the cache replay loop is now implemented once
- this reduces the chance of reintroducing partial-replay cache consumption while the
  project later moves back to zygote-control / agent-owned spawn work

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Stale Agent Isolation After Host-Owned Spawn Cleanup

The final audit of `host close / unbind` against in-flight suspended spawn ownership
exposed a real server-side correctness bug.

Problem:

- when `RemoveHostSession()` or `UnbindHostSession()` cleared an owned suspended spawn,
  the registry also cleared the pid's current agent state
- however the old agent socket could still remain alive briefly
- if another host later rebound to the same pid before a fresh agent re-registered, the
  stale old agent's `SCRIPT_MESSAGE` could be incorrectly accepted and forwarded to the
  new host

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- introduced a small `invalidated_agent_pids_` guard in `SessionRegistry`
- this guard is set only when an owned suspended spawn is force-cleared during:
  - `RemoveHostSession()`
  - `UnbindHostSession()`
  - host rebind cleanup of an older owned suspended pid
- `RegisterAgentSession()` clears the invalidation marker when a fresh agent is
  legitimately re-registered
- `IsAcceptedCurrentAgentSession(...)` no longer uses the coarse "no current session,
  therefore accept" fallback for invalidated pids
- `HandleAgentReady(...)` now also rejects context-free `AGENT_READY` frames for
  invalidated pids so a stale orphan agent cannot reclaim current identity through a
  naked ready frame
- added a regression proving:
  - old host closes
  - stale old agent socket remains alive
  - new host rebinds the same pid
  - stale old agent `SCRIPT_MESSAGE` is dropped instead of forwarded
- added additional regressions proving the same invalidated-pid guard also blocks:
  - stale `RPC_RESPONSE` from the old agent after host close/rebind
  - context-free stale `AGENT_READY` attempting to reclaim current identity after host
    close/rebind

Why this matters:

- this closes a concrete cross-host isolation hole in the current server ownership model
- stale agent sockets can no longer leak messages into a rebound host session after
  suspended-spawn cleanup
- the protection is broader than script-message forwarding alone: stale responses and
  stale bare ready frames are also fenced off by the same ownership guard
- this is exactly the kind of lifecycle bug that would become much harder to reason
  about later once the project moves back into zygote-control / agent-owned spawn work

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_runtime_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Runtime-Ready Replay Audit And Forward Visibility

After tightening attach replay, the adjacent runtime-ready replay path was audited for
the same class of half-failed delivery:

- if the leading runtime `AGENT_READY` forward fails, cached `SCRIPT_MESSAGE` replay
  must not continue

This is now covered explicitly by regression in the main handler suite.

Updated:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

- added a runtime-ready replay regression proving:
  - one successful host send is allowed (`SpawnResponse`)
  - the next send attempt (`AGENT_READY`) may fail
  - cached `SCRIPT_MESSAGE` replay must not be attempted afterward
  - cached script-message frames must remain intact
- audited the remaining direct host forward paths and found no additional stateful
  convergence bugs in the current tree
- added send-failure logging for direct host-forward paths that are intentionally still
  fire-and-forget:
  - `SCRIPT_MESSAGE`
  - `SCRIPT_CREATE_RESP`
  - `SCRIPT_LOAD_RESP`
  - `SCRIPT_UNLOAD_RESP`
  - `RPC_RESPONSE`

Why this matters:

- replay ordering around runtime-ready is now covered on both attach and spawn-side
  boundaries
- the remaining direct response forwards are easier to diagnose during future device
  testing because silent host-send drops now leave an explicit server log trail
- this keeps the current work focused on server-side transaction ownership without
  prematurely widening semantics before the next real zygote-control / agent-owned step

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-19 Spawn Finalize Success Local Cleanup Independence

Another transaction-boundary leak remained in `ExecuteSpawnRequest(...)`.

On finalize success, the code still did:

- local post-finalize bookkeeping
- host-session presence check
- `SpawnResponse`
- cached runtime-ready / script replay

but the actual implementation still had one bad dependency:

- if the requesting `Session` was no longer considered a registered host at the moment
  finalize returned, the function dropped out before clearing the suspended spawn's
  `response_pending` bit

That meant the host-visible reply was correctly suppressed, but local transaction state
could remain stuck as:

- `SpawnSuspendedEntry.response_pending == true`
- authoritative ready already upgraded to runtime
- suspended state never fully converged for later script-load ownership

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a focused regression where:
  - `SPAWN_REQUEST` runs through finalize with a real suspended transaction
  - runtime-ready arrives before finalize completes
  - the request session is not a registered host when finalize returns
  - expected result is still:
    - no host-visible frames sent
    - `response_pending == false`
    - suspended state converged to `kReadyForScriptLoad`
    - pending-spawn cleared
- moved local finalize-success convergence ahead of the `IsRegisteredHostSession(...)`
  success-path early return
- kept host-visible work gated:
  - `SpawnResponse`
  - cached `AGENT_READY` replay
  - cached `SCRIPT_MESSAGE` replay

Why this matters:

- local spawn transaction closure no longer depends on whether the original request
  session is still eligible to receive a reply
- reply suppression and state convergence are now cleanly separated concerns
- this removes another shell/host-owned leak in the path toward
  `agent-owned stable spawn`

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Cached Script-Message Replay Must Be Acknowledged By Send Success

There was still a response-side replay loss on two spawn paths:

- runtime-ready replay in `HandleAgentReady(...)`
- post-finalize replay in `ExecuteSpawnRequest(...)`

Both paths previously did:

- `TakeScriptMessageFrames(pid)`
- iterate and `SendFrame(...)` each cached message to the host

That made cached replay destructive before transport delivery had actually succeeded.
If any host-side `SendFrame(...)` failed, the cached `SCRIPT_MESSAGE`s were already gone.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added:
  - `GetScriptMessageFrames(pid)` for non-destructive snapshot reads
  - `ClearScriptMessageFrames(pid)` for explicit acknowledgement-style deletion
- runtime-ready and post-finalize replay now use:
  - snapshot cached frames
  - send sequentially
  - clear cache only if all sends succeed
- added focused regressions covering:
  - runtime-ready replay to a host whose send fails
  - finalize-success replay to a host whose send fails
  - expected result in both cases: cached script messages remain available for later replay

Why this matters:

- replayed cached messages now follow "delivery attempt acknowledged by success" instead
  of "delivery assumed before write"
- host transport failure no longer silently drops spawn-held messages
- this is another concrete tightening of transaction-owned replay behavior before the
  real `agent-owned stable spawn` transition

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Host-Visible Immediate Errors For Agent Send Failures

After fixing `SCRIPT_LOAD`, the same transport-boundary problem still existed on three
other host-to-agent request paths:

- `SCRIPT_CREATE`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

They all shared the same bug shape:

- host ownership and target agent resolution succeeded
- server attempted `agent->SendFrame(...)`
- send failure produced no immediate host-visible error

This was not always a spawn-state leak like `SCRIPT_LOAD`, but it was still a broken
transaction boundary because the host-side request could hang silently even though the
server already knew dispatch had failed locally.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- reused a failing transport in focused handler regressions for:
  - `SCRIPT_CREATE`
  - `SCRIPT_UNLOAD`
  - `RPC_REQUEST`
- verified the pre-fix behavior:
  - target agent was resolved
  - send failed
  - host received no response frame at all
- updated each handler so that if `agent->SendFrame(...)` fails, the server now replies
  immediately to the host with the same local `-4 / "agent session not ready for bound pid"`
  error used by the existing no-agent-session path

Why this matters:

- host-visible request completion no longer depends on the transport write succeeding
  after local target resolution
- dispatch failure semantics are now consistent across the main script/rpc request paths
- this removes another class of silent hangs before the actual `agent-owned stable spawn`
  transition work

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-20 Script-Load Dispatch Send Failure State Leak

There was a handler-side state leak in `HandleScriptLoad(...)`.

The old flow did:

- validate host ownership and authoritative runtime session
- set spawn state to `kScriptLoadDispatched`
- `agent->SendFrame(frame)`

but it never checked whether `SendFrame(...)` actually succeeded.

So if the runtime agent session was selected successfully but the send failed at the
transport boundary, the host got no immediate error and the spawn transaction stayed
stuck in:

- `SpawnTransactionState::kScriptLoadDispatched`

From that point on, later script operations against the same spawn could be blocked by a
state transition that never really happened.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a focused regression using a transport whose `Send()` always fails
- verified the failure mode before the fix:
  - no host-visible `SCRIPT_LOAD_RESP`
  - spawn state stuck in `kScriptLoadDispatched`
- `HandleScriptLoad(...)` now:
  - sets `kScriptLoadDispatched`
  - checks `agent->SendFrame(frame)`
  - if send fails:
    - restores spawn state to `kReadyForScriptLoad`
    - sends an immediate host-visible `SCRIPT_LOAD_RESP`
      with `-4 / "agent session not ready for bound pid"`

Why this matters:

- dispatch intent and dispatch success are now separated correctly
- local spawn state no longer advances permanently on a failed transport write
- this closes another non-zygote transaction leak before the real
  `agent-owned stable spawn` work

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Spawn Timeout Vs Late Agent-Ready Race

There was still a real timeout race on the spawn path.

The old timeout branch in `ExecuteSpawnRequest(...)` did:

- `WaitForPendingSpawn(...)`
- timeout
- `FinalizeSpawn(...)`
- `ClearPendingSpawn(spawn_token)`

But a late authoritative `AGENT_READY` could still arrive during the finalize window and
run this path in `HandleAgentReady(...)` before the token was cleared:

- `ResolvePendingSpawn(spawn_token, pid, ...)`
- `BindHostToResolvedPendingSpawn(spawn_token, pid, ...)`
- create/update `SpawnSuspendedEntry`

That meant a timed-out spawn could still leave behind:

- `pid_to_host_session_[pid]`
- `SpawnSuspendedEntry`
- cached pre-ready script messages

even though the host had already received `spawn authoritative agent ready timed out`.

Updated:

- [server/server_handlers.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.h)
- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `ServerHandlerConfig.spawn_ready_timeout_ms` so tests can drive short,
  deterministic spawn-ready timeout races without waiting 15 seconds
- added `SessionRegistry::TakePendingSpawn(...)` to atomically fetch-and-remove a
  pending spawn entry by token
- added `SessionRegistry::ClearSpawnTransactionByPid(...)` to clear pid-bound spawn
  side effects in one place:
  - `SpawnSuspendedEntry`
  - `pid_to_host_session_`
  - cached spawn-held `SCRIPT_MESSAGE`s
- spawn success path now atomically `TakePendingSpawn(...)` after
  `WaitForPendingSpawn(...)` instead of doing a separate read followed by later clear
- timeout path now also `TakePendingSpawn(...)` and uses the resolved late pid, when
  present, to clear any bind/suspended side effects created by a late `AGENT_READY`
- added a focused regression where:
  - spawn-ready timeout is shortened to 10 ms
  - `FinalizeSpawn(...)` is held open
  - runtime-stage authoritative `AGENT_READY` arrives after the timeout but before
    finalize returns
  - expected result is still:
    - host receives timeout response
    - no pending spawn remains
    - no suspended spawn remains
    - no host-to-pid binding remains

Why this matters:

- timed-out spawns no longer leak transaction-owned state when authoritative readiness
  arrives just after the wait deadline
- spawn token consumption is now much closer to an owned transaction boundary instead of
  a loosely coordinated global map
- this is another concrete step toward making spawn lifecycle authoritative and
  self-contained before real `agent-owned stable spawn`

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Spawn Binding Requires A Real Registered Host

One more ghost-state leak remained in `ExecuteSpawnRequest(...)`.

After `WaitForPendingSpawn(...)` succeeded, the code always proceeded to:

- `BindHostToPid(session.GetId(), authoritative_pid)`
- `MarkSpawnSuspended(authoritative_pid, session.GetId(), ...)`
- `SetSpawnResponsePending(authoritative_pid, true)`

The problem is that `BindHostToPid(...)` is already guarded by the registry's current
host table, but `MarkSpawnSuspended(...)` was not.

So if a `SPAWN_REQUEST` came from a session that was no longer a registered host by the
time the authoritative `AGENT_READY` arrived:

- the bind silently did nothing
- but a fresh `SpawnSuspendedEntry` was still created anyway
- finalize then completed
- the reply was suppressed
- the server was left with a ghost suspended spawn owned by no real host

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added a focused regression where:
  - `SPAWN_REQUEST` is dispatched from a non-registered session
  - authoritative control/runtime `AGENT_READY` still arrive and unblock finalize
  - expected result is:
    - no host-visible frames
    - pending-spawn cleared
    - no `SpawnSuspendedEntry` left behind
- `ExecuteSpawnRequest(...)` now computes whether the request session is still a
  registered host before creating the spawn transaction
- suspended spawn state, `response_pending`, and late-promotion setup are now created
  only when that host-registration check still holds
- finalize failure cleanup now only unbinds the host session if the spawn transaction
  actually bound one

Why this matters:

- a spawn transaction now requires a real current host owner before any suspended state
  is created
- "reply cannot be delivered" no longer degrades into "transaction state still exists"
- this removes another host-owned ghost state ahead of real `agent-owned stable spawn`

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Remaining `HandleAgentReady(...)` Runtime-Trace Helper Boundary

There were still two helper functions in [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
carrying coarse pid-era semantics:

- `HasMismatchedRuntimeTraceForSpawnTarget(...)`
- `HasAcceptedRuntimeTrace(...)`

At first glance both looked like candidates for the same kind of identity tightening that
the surrounding suspended-spawn routing had already gone through. The remaining question
was whether they were still masking one more stale-runtime leak, or whether they were
coupled to an intentional fallback boundary.

What was verified:

- the dangerous stale-runtime cleanup path is the `mismatched runtime trace` branch, not
  the broader `accepted runtime trace` branch
- runtime-disconnect demotion is not what keeps `HasMismatchedRuntimeTraceForSpawnTarget(...)`
  alive:
  after runtime disconnect, `RemoveAgentSessionByPidIfMatches(...)` already clears the
  runtime-ready bit and demotes the suspended transaction back to
  `authoritative_ready_stage = kControlReady`
- by contrast, `HasAcceptedRuntimeTrace(...)` still needs its coarse
  pid-authoritative behavior for non-suspended late-control paths such as:
  "runtime-ready already established for this pid, then a late control-stage ready shows up
  outside a suspended-spawn transaction"

The real bug that remained was narrower:

- `HasMismatchedRuntimeTraceForSpawnTarget(...)` was allowed to fire based on any
  runtime-ready + authoritative pid session combination
- that made stale-runtime cleanup logic conceptually broader than the suspended-spawn
  transaction boundary it was supposed to protect

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

- `HasMismatchedRuntimeTraceForSpawnTarget(...)` now:
  - accepts the incoming candidate process name
  - treats runtime trace mismatch as identity-based when expected identity is known
  - can still detect mismatch against the incoming control-stage identity when needed
  - is only consulted when `HandleAgentReady(...)` is operating inside an actual
    suspended-spawn context
- `HasAcceptedRuntimeTrace(...)` was intentionally left on the wider pid-authoritative
  semantic after verification, because tightening it further regressed valid
  non-suspended late-control behavior

Why this matters:

- stale runtime-trace clearing is now explicitly scoped to suspended-spawn transaction
  handling instead of leaking into generic pid-bound `AGENT_READY` traffic
- this removes one more place where transaction cleanup could accidentally mutate
  non-spawn handler semantics
- the plan now has a sharper documented boundary:
  "identity tightening is correct here" versus
  "this pid-authoritative fallback is still intentionally coupled to current runtime
  continuity semantics"

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Missing-Agent Host Errors Are Now Symmetric

One remaining inconsistency was not about ownership resolution itself, but about what the
host sees after that resolution fails.

Before this step:

- `SCRIPT_CREATE`
- `SCRIPT_LOAD`

already returned an immediate host-visible error when a pid was bound but no acceptable
agent session existed:

- `-4: agent session not ready for bound pid`

But:

- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

only logged the condition and returned nothing.

That meant some host-side operations failed fast while others silently hung waiting for a
reply that would never come, even though the underlying state was the same.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptUnload(...)` now returns:
  - `ScriptUnloadResp`
  - `error.code = -4`
  - `error.message = "agent session not ready for bound pid"`
- `HandleRpcRequest(...)` now returns:
  - `RpcResponse`
  - `error.code = -4`
  - `error.message = "agent session not ready for bound pid"`
- added regressions for both cases

Why this matters:

- host-visible behavior is now aligned across script/RPC request classes when
  transaction-owned routing cannot resolve a usable agent session
- this removes one more source of silent waits and makes failure semantics more
  predictable for tooling
- it also better matches the larger cleanup goal: once routing is owned by explicit
  transaction/session boundaries, failure at that boundary should be explicit too

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Note On `SCRIPT_POST` Semantics

While aligning host-visible failure behavior across request types, `SCRIPT_POST` was
explicitly left alone on purpose.

Why:

- the protocol defines:
  - `kScriptPost`
- but does not define:
  - `ScriptPostResp`
  - `kScriptPostResp`
- host-side usage (`script.post(...)`) is modeled as fire-and-forget message delivery,
  not request/response RPC

Implication:

- it would be incorrect to force `SCRIPT_POST` into the same explicit response ladder
  used by:
  - `SCRIPT_CREATE`
  - `SCRIPT_LOAD`
  - `SCRIPT_UNLOAD`
  - `RPC_REQUEST`

So the current cleanup rule is:

- response-bearing host requests should fail explicitly and consistently
- `SCRIPT_POST` remains a one-way channel and may only log/drop on routing failure

This distinction should remain documented so future cleanup work does not accidentally
"normalize" `SCRIPT_POST` into a protocol shape it does not have.

## Follow-up On 2026-05-19 `SCRIPT_UNLOAD` / `RPC_REQUEST` Now Fail Explicitly On Early Validation Too

The earlier cleanup aligned the "no pid" and "no acceptable agent session" routing
boundaries for `SCRIPT_UNLOAD` and `RPC_REQUEST`, but those handlers were still behind
`SCRIPT_CREATE` / `SCRIPT_LOAD` on the very first validation steps.

Before this step:

- invalid `SCRIPT_UNLOAD`
- invalid `RPC_REQUEST`
- `registry == nullptr` for either request type

only produced logs and returned nothing.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptUnload(...)` now returns:
  - `-1: invalid script unload request`
  - `-2: session registry unavailable`
- `HandleRpcRequest(...)` now returns:
  - `-1: invalid rpc request`
  - `-2: session registry unavailable`
- added regressions for all four cases

Why this matters:

- host-driven request types now expose the same failure ladder consistently:
  - request decode failure
  - registry unavailable
  - no bound/owned pid
  - no acceptable agent session
  - spawn not ready
- this removes another class of silent early returns that would otherwise look like
  unexplained hangs from tooling
- it keeps the routing boundary cleanup coherent: once routing is explicit and
  transaction-owned, failures at every stage should be explicit too

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Missing-Pid Host Errors Are Now Symmetric Too

The previous cleanup aligned the "bound pid exists but no acceptable agent session"
failure path across request types. There was still one more asymmetry one step earlier in
the request pipeline.

Before this step:

- `SCRIPT_CREATE`
- `SCRIPT_LOAD`

already returned:

- `-3: host session is not bound to a pid`

when the host had no usable pid ownership at all.

But:

- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

still just logged and returned nothing.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptUnload(...)` now returns:
  - `ScriptUnloadResp`
  - `error.code = -3`
  - `error.message = "host session is not bound to a pid"`
- `HandleRpcRequest(...)` now returns:
  - `RpcResponse`
  - `error.code = -3`
  - `error.message = "host session is not bound to a pid"`
- added regressions for both cases

Why this matters:

- all major host-driven script/RPC request types now fail explicitly at both of the same
  routing boundaries:
  - no owned/bound pid
  - owned/bound pid but no acceptable agent session
- this removes another class of silent host-side hangs
- it continues the main theme of this cleanup: once routing is driven by explicit
  transaction/session ownership, boundary failures should surface explicitly and
  consistently

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 `SCRIPT_LOAD_RESP` State Recovery Does Not Depend On Host Presence

The previous `SCRIPT_LOAD_RESP` cleanup closed the normal happy-path round-trip, but
there was still a narrower leak when the suspended spawn entry outlived its original host
session.

Before this step, `HandleScriptLoadResp(...)` restored:

- `kScriptLoadDispatched -> kReadyForScriptLoad`

only after first resolving a host session. If the suspended owner host was already gone,
the handler returned early and the transaction stayed stuck in
`kScriptLoadDispatched`.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleScriptLoadResp(...)` now restores:
  - `SpawnTransactionState::kReadyForScriptLoad`

before checking whether a bound host still exists
- added regressions where:
  - the suspended spawn entry remains
  - the recorded owner host is gone
  - a rebound host is pid-bound but must not receive the response
  - state recovery must still happen locally

Why this matters:

- transaction state cleanup is now owned by the response acceptance boundary itself, not
  by whether a host is still available for forwarding
- this avoids stale "load dispatched" state surviving after host disconnect/rebind edge
  cases
- another small piece of lifecycle cleanup is now decoupled from coarse host/session
  availability

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 `SCRIPT_LOAD_RESP` Restores Ready State

There was also one small but real state-machine leak after request routing had already
been tightened.

For spawned processes, `HandleScriptLoad(...)` moved the suspended transaction from:

- `kReadyForScriptLoad`

to:

- `kScriptLoadDispatched`

before forwarding the request to the runtime agent.

But `HandleScriptLoadResp(...)` never moved it back.

That meant a successful `SCRIPT_LOAD_RESP` could leave the transaction permanently parked
in a mid-flight "load already dispatched" state even though the round-trip had completed.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- after a `SCRIPT_LOAD_RESP` is accepted from the authoritative session and a bound host
  exists, `HandleScriptLoadResp(...)` now restores the suspended spawn state to:
  - `SpawnTransactionState::kReadyForScriptLoad`
- added focused/full regressions proving that a spawned pid in
  `kScriptLoadDispatched` returns to `kReadyForScriptLoad` after a matching
  `SCRIPT_LOAD_RESP`

Why this matters:

- the suspended spawn state machine now closes the `SCRIPT_LOAD` round-trip instead of
  leaving a stale "in flight" marker behind
- later host-side script operations observe a steady ready-state boundary after load
  completion
- this is another small but concrete cleanup toward predictable transaction-owned spawn
  lifecycle semantics

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Host Request Ownership Prefers Suspended-Spawn Owner

Another transaction-boundary leak was still present on the host-to-agent request side.

Even after request routing had been tightened to use spawn-owned runtime identity,
`ResolveHostOwnedPidForRequest(...)` still resolved ownership in this order:

- `FindPidByHostSession(host_session_id)`
- then `FindOwnedSpawnPidByHostSession(host_session_id)`

That ordering was too loose for suspended spawns. If a second host rebound itself to the
same pid while the suspended spawn was still owned by the original host, the rebound host
could still send `SCRIPT_CREATE` / `SCRIPT_LOAD` / `RPC_REQUEST` into that foreign spawn
transaction through the coarse pid binding.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `ResolveHostOwnedPidForRequest(...)` now:
  - prefers `FindOwnedSpawnPidByHostSession(host_session_id)` first
  - only falls back to plain pid binding when no suspended spawn is owned by that host
  - rejects a plain pid binding if that pid is currently a suspended spawn owned by a
    different host session
- added a regression where:
  - host A owns suspended spawn pid `63034`
  - host B rebinds itself to pid `63034`
  - host B attempts `SCRIPT_CREATE`
  - expected result: immediate host-side error, no forwarding to the runtime agent

Why this matters:

- suspended spawn ownership is now enforced consistently for outbound host requests, not
  just for resume/response forwarding
- a later pid rebind can no longer bypass the spawn transaction owner recorded in
  `SpawnSuspendedEntry`
- this is another step from coarse pid ownership toward transaction-owned routing

Verification:

- `build/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_server_handlers_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Spawn Controller Runtime Identity Convergence

One more success-path dependency was still reading from a stale local
`resolved_pending_spawn` snapshot after finalize.

Specifically, `ExecuteSpawnRequest(...)` would decide the post-finalize replay identity
like this:

- start from `resolved_pending_spawn.process_name`
- then prefer `SpawnSuspendedEntry.target_process_name`
- only then fall back to `authoritative_process_name`

That ordering was wrong once the suspended transaction had already been upgraded to a
runtime-authoritative identity. In that case, replay could miss the cached runtime
`AGENT_READY` entirely, or replay the wrong identity, because the current transaction had
already moved from the original spawn target name to the authoritative runtime name.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `ResolvePostFinalizeRuntimeProcessName(...)`
- after finalize success, replay identity is now resolved from the current
  `SpawnSuspendedEntry` first
- when the transaction is already at `kRuntimeReady`, authoritative runtime process name
  now wins over the original target process name
- added a regression where the spawn begins as `com.demo.target`, but the suspended
  transaction is upgraded to runtime-authoritative `com.demo.runtime` before finalize
  returns; replay must use `com.demo.runtime`

Why this matters:

- post-finalize success handling now follows the current transaction state instead of a
  stale pre-finalize snapshot
- cached runtime-ready replay is now aligned with the authoritative runtime identity
- this is another concrete step toward `agent-owned stable spawn`, where the suspended
  spawn transaction remains the source of truth after finalize

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`
- `build/test_server_runtime_current.exe`

## Follow-up On 2026-05-19 Unified Spawn Runtime Identity Resolution

The spawn/runtime routing helpers were still duplicating the same identity choice in
multiple places, and the order was not consistent:

- some paths preferred `target_process_name`
- some paths only fell back to `authoritative_process_name` when runtime-ready was
  already proven
- some paths still kept their own local fallback order

That caused spawn-bound runtime routing to disagree across:

- `spawn_controller`
- `server_handlers`
- `server_runtime`

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_runtime.cpp)
- [tests/communication/test_server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_runtime.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `ResolveSpawnRuntimeProcessName(const SpawnSuspendedEntry&)`
- `kRuntimeReady` now resolves runtime identity from the authoritative runtime name
  first, then target name, then fallback authoritative name
- unified the runtime agent selection used by spawn replay, host-request routing, and
  spawn-gate selection
- added regressions proving that runtime-authoritative identity wins over the stale
  target name in:
  - `ResolveSpawnGateAgentSession(...)`
  - spawn-bound script create routing

Why this matters:

- runtime routing is now consistent across the server
- spawn-state identity no longer depends on which helper happened to be called
- this removes another class of stale-target-name bugs from `agent-owned stable spawn`

Verification:

- `build/test_server_runtime_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`

## Follow-up On 2026-05-19 Attach Cleanup Rebound Completion And Helper Convergence

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_components.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_components.cpp)

Problem:

- attach-side cleanup had already been tightened across host removal, unbind, shutdown,
  and direct agent teardown
- but `RemoveAgentSessionByPidIfMatches(...)` still had a rebound early-return path where
  attach-side state needed to be explicitly normalized before falling back to a surviving
  control-ready session
- separately, pid-scoped attach cleanup still existed in more than one open-coded branch
  inside `SessionRegistry`

Changes:

- completed the rebound path cleanup in
  `RemoveAgentSessionByPidIfMatches(...)`
  - runtime removal now clears pid-owned `pending_attaches_`
  - runtime removal now clears pid-owned `attach_timeout_pids_`
  before any rebound return
- added the direct regression:
  - `TestRemoveAgentSessionByPidIfMatchesClearsAttachSideStateOnRebound()`
- converged pid-scoped attach cleanup behind an internal helper:
  - `ClearAttachSideStateForPidLocked(int pid)`
- `RemoveAgentSessionByPid(...)` and `RemoveAgentSessionByPidIfMatches(...)` now use the
  same helper instead of duplicating attach cleanup loops
- added a component-level regression:
  - `TestSessionRegistryRuntimeRemovalReboundClearsAttachSideState()`

Why this matters:

- attach request ownership no longer depends on remembering to mirror cleanup in every
  runtime-to-control rebound branch
- stale attach timeout or pending-attach residue is less likely to survive a session
  collapse and interfere with later attach generations
- this is a small but important internal convergence step before continuing upward into
  broader lifecycle work for `agent-owned stable spawn`

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `./build/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_components.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_components_current.exe`
- `./build/test_server_components_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `./build/test_server_handlers_current.exe`

## Follow-up On 2026-05-19 Close-Path Validation For Latest Agent Peer PID

Updated:

- [tests/communication/test_session.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session.cpp)
- [tests/communication/test_server_components.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_components.cpp)

Problem:

- the next lifecycle boundary to validate was the real connection close path in
  `server_main`
- agent sessions are accepted with an initial socket peer pid, but later
  `AGENT_READY` processing may update `Session::peer_pid`
- if the close callback were to use a stale pid, close-time cleanup could target the
  wrong registry slot and leave request-scoped residue behind

Validation added:

- `TestSessionCloseCallbackUsesLatestPeerPid()`
  - locks that `Session` close callback observes the latest `SetPeerPid(...)` value,
    not an older one
- `TestSessionRegistryAgentClosePathUsesLatestPidBinding()`
  - models the effective `server_main` cleanup behavior by removing agent state using
    the session's latest peer pid
  - verifies pid-owned attach residue is cleared from that latest binding

Result:

- no production code change was required in this layer
- the current `server_main` close path is already aligned with the intended behavior:
  close-time agent cleanup resolves against the latest session pid

Why this matters:

- this closes one more uncertainty before returning to the main
  `agent-owned stable spawn` track
- the next work can assume the real disconnect path is not secretly cleaning the wrong
  pid after late `AGENT_READY` rebinding

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session.cpp src/communication/session/session.cpp src/communication/session/session_manager.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp -o build/test_session_current.exe`
- `./build/test_session_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_components.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_components_current.exe`
- `./build/test_server_components_current.exe`

## Follow-up On 2026-05-19 Release Gate Uses Spawn-Owned Agent Identity

Updated:

- [server/server_runtime.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_runtime.h)
- [server/server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_runtime.cpp)
- [server/server_main.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_main.cpp)
- [tests/communication/test_server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_runtime.cpp)

Problem:

- the spawn/request/response/runtime-ready paths had already moved toward
  transaction-owned runtime identity
- but `ReleaseSpawnGate(...)` in `server_main.cpp` still chose the target agent through:
  - `FindAuthoritativeAgentSessionByPid(pid)`
  - fallback `FindControlReadyAgentSessionByPid(pid)`
- that remained pid-global selection logic
- for a suspended spawn whose transaction already records a runtime child identity, the
  gate-release path should not be willing to talk to an arbitrary current pid occupant

Changes:

- added `ResolveSpawnGateAgentSession(SessionRegistry*, int)` in `server_runtime`
- when a pid is still spawn-suspended and the transaction is runtime-bound:
  - the helper now prefers the transaction's recorded runtime child identity
  - via `FindRuntimeReadyAgentSessionByIdentity(pid, target_or_authoritative_name)`
- if that runtime identity is expected but not present, the helper returns `nullptr`
  instead of silently falling back to a mismatched pid-global session
- non-runtime-bound cases keep the older fallback behavior:
  - authoritative pid session first
  - then control-ready pid fallback
- `ReleaseSpawnGate(...)` now uses this helper

Added regression coverage:

- `TestResolveSpawnGateAgentSessionPrefersSpawnRuntimeIdentity()`
  - runtime-bound suspended spawn with only a mismatched runtime occupant must resolve
    to `nullptr`
- `TestResolveSpawnGateAgentSessionFallsBackWhenNotSpawnRuntimeBound()`
  - non-runtime-bound case still falls back to the expected control/authoritative pid
    session

Why this matters:

- gate release now follows the same child identity boundary already used by the tighter
  spawn request/response/runtime-ready routing
- this removes one more shell/pid-global shortcut from the critical suspended-spawn
  lifecycle
- it is a concrete step toward `agent-owned stable spawn`, where the transaction's child
  identity increasingly becomes the source of truth

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_runtime.cpp server/server_runtime.cpp server/session_registry.cpp src/communication/io/io_loop.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_runtime_current.exe`
- `.\build\test_server_runtime_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `.\build\test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `.\build\test_server_handlers_current.exe`

Note:

- a full Windows-side compile of `server_main.cpp` still trips unrelated existing
  compatibility issues in other paths (`std::strcmp` / `setenv` usage and separate
  ninjector signature mismatches)
- the runtime helper itself and the main registry/handler regression base are green

## Follow-up On 2026-05-19 Attach Side-State Lifecycle Cleanup

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_components.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_components.cpp)

Problem:

- after attach gained request-scoped ready tokens, several cleanup paths still only removed:
  - host bindings
  - agent session maps
  - pending spawn state
- but did not consistently clean attach-scoped side state:
  - `pending_attaches_`
  - `attach_timeout_pids_`

That left multiple stale-generation risks depending on which exit path fired:

- host close
- host unbind / detach
- shutdown
- agent socket disappearance

Changes:

- `RemoveHostSession(...)` now clears:
  - owned `pending_attaches_`
  - owned `attach_timeout_pids_`
- `UnbindHostSession(...)` now clears:
  - owned `pending_attaches_`
  - pending-attach-associated timeout markers
  - but intentionally does **not** blanket-clear timeout markers for normal attach-timeout
    late-ready protection
- `Shutdown()` now clears:
  - `pending_attaches_`
  - `attach_timeout_pids_`
- `WaitForRuntimeReadyAgentSessionByToken(...)` now exits early when the owning pending
  attach entry is cleared instead of waiting for the full timeout budget
- `RemoveAgentSessionByPid(...)` / `RemoveAgentSessionByPidIfMatches(...)` now clear:
  - `pending_attaches_` for the same pid
  - `attach_timeout_pids_` for the same pid

Why this matters:

- attach request-scoped state now dies with the lifecycle that actually owns it
- the timeout marker remains long enough to reject late orphaned attach ready events,
  but no longer survives unrelated host / agent teardown indefinitely
- this removes another class of shell/global residue that would otherwise blur
  request ownership, which is exactly the wrong direction for the larger
  `agent-owned stable spawn` convergence

Regression coverage added:

- host removal clears owned pending attach entries
- host removal clears owned attach-timeout pid markers
- host unbind clears owned pending attach entries and their associated timeout markers
- shutdown clears pending attach and timeout state
- pending-attach wait exits early when its token entry is cleared
- agent removal clears attach side state in both:
  - full session-registry tests
  - lightweight server-components tests closer to socket-close semantics

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `./build/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `./build/test_server_handlers_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_components.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_components_current.exe`
- `./build/test_server_components_current.exe`

## Follow-up On 2026-05-19 Attach Timeout Late Runtime-Ready Residue

One more ownership leak remained outside the spawn-token paths.

`HandleAttachRequest(...)` correctly unbound the host when attach timed out, but a later
runtime-stage `AGENT_READY` from that same pid could still arrive and get registered into
the global agent/runtime maps.

That left stale residue after a failed attach:

- `FindAgentSessionByPid(pid)` became non-null again
- process-name lookup recovered a hostless runtime agent
- runtime-ready bits and cached ready frames were rebuilt even though the host-side
  attach transaction had already failed

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- strengthened `TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateAgentReadyArrives()`
  to assert that a late runtime-ready after attach timeout must not recreate any pid,
  process-name, runtime-ready, or cached-ready registry state
- added a narrow handler-side rejection in `HandleAgentReady(...)` for:
  - runtime-stage ready
  - no bound host
  - no pending-spawn context
  - no suspended-spawn context
  - no spawn token
  - not an explicitly owned zygote-control target
- when that condition hits, the pre-registered current session is rolled back with
  `RemoveAgentSessionByPidIfMatches(...)` before returning

Why this matters:

- attach timeout is now a real transaction boundary instead of a temporary host-side error
- a hostless late runtime-ready can no longer silently repopulate global agent state
- the fix stays narrow enough to avoid breaking legitimate hostless strict/zygote-control
  control agents

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_current.exe`
- `./build/test_server_handlers_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_current.exe`
- `./build/test_session_registry_current.exe`

Follow-up refinement:

The first cut at this cleanup used a generic "hostless ready" rejection, which turned out
to be too broad because valid control-stage pre-runtime paths can also be hostless.

The final version narrows the rule to explicit failed-attach residue:

- `SessionRegistry` now tracks a minimal pid-level `attach_timeout` mark
- `HandleAttachRequest(...)` sets that mark only when the attach path times out and unbinds
  the host
- successful bind / successful attach / accepted live agent registration clear the mark
- `HandleAgentReady(...)` now drops late no-token ready only when:
  - there is no spawn context
  - there is no bound host
  - the pid is explicitly marked as a prior attach-timeout pid
  - the target is not an owned zygote-control process

This preserves legitimate hostless control-ready flows while still rejecting failed-attach
late arrivals.

Additional boundary tightened in the same area:

Even after late `AGENT_READY` cleanup, there was still a short rebind window where:

1. an attach had already timed out for pid `P`
2. the old late agent connection still existed with `peer_pid = P`
3. a new host later rebound to pid `P`
4. before any new accepted agent session was registered, the old connection could still
   pass `IsAcceptedCurrentAgentSession(...)` because `FindAgentSessionByPid(P) == nullptr`

That allowed stale post-timeout business frames such as:

- `SCRIPT_MESSAGE`
- `SCRIPT_CREATE_RESP`

to be forwarded into the newly rebound host.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `SessionRegistry` now keeps a minimal pid-level `attach_timeout` mark until a new live
  agent registration or successful attach clears it
- `BindHostToPid(...)` no longer clears that mark early
- `IsAcceptedCurrentAgentSession(...)` now rejects "no current pid session yet" traffic for
  attach-timeout pids, so old stale agent connections cannot use the empty-current-session
  gap to forward frames
- added regressions proving that after attach timeout, a stale agent cannot forward:
  - `SCRIPT_MESSAGE`
  - `SCRIPT_CREATE_RESP`
  into a later rebound host

Why this matters:

- attach-timeout isolation now covers both readiness frames and later business traffic
- host rebind no longer opens a transient hole where the old dead agent can speak into the
  next attach lifecycle

Registry follow-up:

Because the new attach-timeout isolation bit is now part of the handler boundary, it also
needed explicit regression coverage at the registry level so future refactors do not
silently weaken it.

Added registry invariants:

- the attach-timeout pid mark is explicit and queryable
- `BindHostToPid(...)` must not clear it early
- `RegisterAgentSession(...)` is the point that clears it once a new accepted live agent
  exists for that pid

Verification:

- `build/test_session_registry_current.exe`
- `build/test_server_handlers_current.exe`

## Follow-up On 2026-05-19 Host Request Pid Resolution Uses Suspended Spawn Ownership

One more host-side coarse-map leak remained even after request/response routing had
become much more spawn-aware.

Handlers forwarding host-originated requests to the target pid:

- `SCRIPT_POST`
- `SCRIPT_CREATE`
- `SCRIPT_LOAD`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

still started by resolving the pid through:

- `FindPidByHostSession(host_session_id)`

That is too weak for a suspended spawn owner. If the coarse `pid -> host` map is
temporarily unbound or rebound to another host session, the original spawn owner can
still legitimately own the suspended transaction through:

- `SpawnSuspendedEntry.host_session_id`

Without honoring that ownership first, the original host may lose the ability to drive
its own spawned child even though the transaction state still says it owns it.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `FindOwnedSpawnPidByHostSession(uint32_t session_id)` to the registry
- the helper scans suspended spawn entries and returns a still-suspended pid owned by
  that host session even if the coarse `pid -> host` binding is absent
- added `ResolveHostOwnedPidForRequest(...)` in server handlers:
  - first try `FindPidByHostSession(host_session_id)`
  - then fall back to `FindOwnedSpawnPidByHostSession(host_session_id)`
- host request handlers now use that helper instead of relying only on the coarse map

Added regression coverage:

- registry regression proves that when:
  - a host owns a suspended spawn pid
  - no coarse pid binding exists for that host
  - or another host rebinds the same pid in the coarse map
  the original host still resolves its owned suspended pid through suspended spawn
  ownership
- server-handlers regression proves:
  - `SCRIPT_CREATE` from the original suspended spawn owner still reaches the runtime
    child even after the coarse pid binding is rebound to another host

Why this matters:

- host request routing is now aligned with the same transaction-owned spawn boundary
  already used elsewhere
- the coarse global `pid -> host` map is pushed further toward fallback-only semantics
- this removes another case where spawn lifecycle correctness depended on global mutable
  bindings instead of the suspended transaction itself

Verification:

- `build/test_session_registry_current.exe`
- `build/test_server_handlers_current.exe`

## Follow-up On 2026-05-19 DETACH Uses Suspended Spawn Ownership

After moving host-side script and RPC request routing onto spawn-owned pid fallback,
one control-path inconsistency still remained:

- `DETACH_REQUEST`

It still resolved the target pid only through:

- `FindPidByHostSession(request.session_id)`

That creates the same ownership leak as the earlier request path. If the original host
still owns a suspended spawn transaction, but the coarse `pid -> host` map has already
been rebound, `DETACH_REQUEST` should still resolve against the suspended transaction.

Otherwise the old behavior incorrectly downgraded that situation to:

- `session not attached`

even though the real transactional state was:

- host still owns a gate-held suspended spawn pid

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleDetachRequest(...)` now uses `ResolveHostOwnedPidForRequest(...)`
- so detach pid resolution now follows the same rule as other host-originated request
  paths:
  - first coarse `FindPidByHostSession(...)`
  - then suspended-spawn ownership fallback

Added regression coverage:

- if:
  - original host owns suspended spawn pid `200`
  - another host rebinds coarse pid ownership for `200`
- then a detach request for the original host must still:
  - resolve pid `200` through suspended ownership
  - return the correct gate-held error
  - preserve the original suspended ownership record

Why this matters:

- control-path semantics now match the request-path ownership model
- the server no longer misreports "not attached" when the real state is "still owns a
  suspended spawn"
- this reduces another place where coarse mutable host bindings could drift away from
  transaction truth

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Late Control-Ready Rejects Also Roll Back Pre-Registered Current Session

After cleaning up orphan and mismatched-runtime ready residue, the same accept-before-
validate problem still existed on two late control-stage reject paths inside
`HandleAgentReady(...)`.

For a pre-registered control-stage session, the handler could return early when:

- the spawn transaction was already at a runtime boundary, or
- a runtime-ready trace was already authoritative and the incoming control session was not
  the accepted current control-stage peer

Before this fix, those branches only logged and returned. If the control session had
already been pre-registered as the current pid session, it could still remain as
`FindAgentSessionByPid(pid)` even though the control ready itself was rejected.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- both early-return late-control reject branches now also call:
  - `RemoveAgentSessionByPidIfMatches(ready_pid, &session)`
- this rolls back any pre-registered current pid/session mapping for the rejected
  control-stage session before returning

Added regression coverage:

- model a runtime-ready spawn where:
  - the real runtime agent is authoritative
  - a stale control agent is pre-registered as the current pid session
  - a late control-stage `AGENT_READY` arrives for the same pid
- after dispatch:
  - `FindAgentSessionByPid(pid)` remains on the runtime agent
  - runtime-ready state remains intact
  - the rejected late control session does not remain as current pid state

Why this matters:

- all current `AGENT_READY` reject branches now follow the same cleanup rule:
  reject invalid ready and roll back any pre-registered current pid/session residue
- this keeps current-session state aligned with validated lifecycle state instead of raw
  accept order
- it materially reduces another class of retry-poisoning and stale-current-session bugs

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Mismatched Runtime Ready Rolls Back Pre-Registered Current Session

The orphan `spawn_token` cleanup fixed one class of pre-registered pid/session residue,
but another closely related class remained in the runtime identity-mismatch branch.

For spawned pids, `HandleAgentReady(...)` can reject a runtime-stage ready when:

- the incoming runtime identity does not match the suspended spawn target identity

Before this fix, that branch skipped runtime registration, but if the same session had
already been pre-registered as the current pid session by the unix accept path, the wrong
runtime session could still remain as `FindAgentSessionByPid(pid)`.

That is enough to pollute later current-session checks even though the runtime ready itself
was logically rejected.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- in the `runtime_ready && !runtime_spawn_process_name_matches` branch,
  `HandleAgentReady(...)` now also calls:
  - `RemoveAgentSessionByPidIfMatches(ready_pid, &session)`
- this rolls back the pre-registered current pid session for the wrong runtime agent
  before leaving the runtime-ready state unchanged

Added regression coverage:

- model a spawned pid with:
  - a live pinned control-ready agent
  - a wrong runtime agent pre-registered as the current pid session
  - a runtime `AGENT_READY` carrying the wrong process identity
- after dispatch:
  - `FindAgentSessionByPid(pid)` falls back to the control-ready agent
  - control-ready state remains intact
  - no runtime-ready state is recorded for the wrong identity
  - no cached runtime ready frame is recorded for the wrong identity

Why this matters:

- rejected runtime identity mismatches no longer leave behind a poisoned current pid
  session
- this closes another accept-before-validate residue path on the server's real lifecycle
- orphan rejection and mismatched-runtime rejection now both converge on the same
  "rollback pre-registration on invalid ready" rule

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Orphan Spawn-Token Ready Rolls Back Pre-Registered Agent Session

The earlier orphan `spawn_token` protection in `HandleAgentReady(...)` closed the obvious
"accept stale ready and register runtime state" bug, but one more real-world residue still
remained because of server startup ordering.

In the real server:

- the unix agent socket accept path pre-registers `pid -> session` as soon as the peer
  connects
- only afterwards does the agent send `AGENT_READY`

That meant an orphan late `AGENT_READY` with stale `spawn_token` could still leave behind
a pid-level agent session even though the ready itself was dropped. The runtime-ready bits,
process identity, and cached ready frame stayed clean, but the current pid session mapping
could still be polluted by the stale agent connection.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- when `HandleAgentReady(...)` classifies a `spawn_token` ready as orphaned, it now also
  calls:
  - `RemoveAgentSessionByPidIfMatches(ready_pid, &session)`
- this rolls back the pre-registered pid/session mapping for the same agent session before
  dropping the frame

Added regression coverage:

- simulate real server ordering by:
  - pre-registering `pid -> session`
  - then delivering an orphan runtime `AGENT_READY` with stale `spawn_token`
- after the drop:
  - no pid-level agent session remains
  - no process-name identity remains
  - no authoritative/runtime-ready bits remain
  - no cached `AGENT_READY` remains

Why this matters:

- late orphan spawn-child connects can no longer leave behind pid-local residue just
  because socket accept happened before ready validation
- this closes another retry-poisoning seam on the transaction-end side
- it also aligns the handler-level orphan drop with the actual server accept lifecycle

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Spawn Finalize Stops Responding To Closed Host Sessions

One more transaction-end leak remained around blocked finalize:

- the host could disconnect while `FinalizeSpawn()` was still in flight
- `RemoveHostSession(...)` would already clear:
  - host registration
  - pid binding
  - suspended spawn ownership
  - pending spawn ownership
- but `ExecuteSpawnRequest(...)` still held a raw `Session&` and would continue sending:
  - timeout response
  - finalize-failure response
  - success `SpawnResponse`

That meant a spawn transaction could continue talking to a host session that had already
been removed from the registry and whose ownership had already been torn down.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `IsRegisteredHostSession(...)`
- after each long/blocking phase, `ExecuteSpawnRequest(...)` now checks whether the
  initiating host session is still the registered live host:
  - pending-spawn timeout path
  - finalize-failure path
  - finalize-success path before `SpawnResponse`
- if the host session is already gone, the controller drops the response instead of
  reviving a dead transaction endpoint

Added regression coverage:

- if:
  - spawn is blocked inside `FinalizeSpawn()`
  - control-ready already resolved the authoritative child
  - the initiating host is removed from the registry before finalize returns
- then:
  - no `SpawnResponse` is sent to that closed host
  - suspended spawn state is not kept alive afterwards
  - pending spawn state remains cleared

Why this matters:

- transaction teardown is now stronger than the raw `Session&` lifetime
- a host disconnect during finalize no longer allows the transaction to "come back from
  the dead" and emit late responses
- this is another concrete step toward clean transaction-owned spawn termination

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Suspended Spawn Host Routing No Longer Falls Back To Rebound Host

One more subtle host-routing leak remained in the shared helper:

- `ResolveBoundHostSessionForPid(...)`

Its older behavior for spawn-bound pids was:

1. if `spawn_suspended.host_session_id` exists, try `FindHostSession(owner)`
2. if that owner session is currently missing, fall back to `FindHostSessionByPid(pid)`

That fallback is too loose for a still-suspended spawn. Once a suspended transaction has
an authoritative host owner, temporary loss of that owner session must not redirect
agent-originated traffic to some later coarse pid-bound host.

Otherwise a rebound host can incorrectly inherit runtime-ready delivery or other
agent-originated traffic that still belongs to the suspended transaction.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- for pids with a suspended spawn entry and non-zero `host_session_id`:
  - `ResolveBoundHostSessionForPid(...)` now returns only
    `FindHostSession(entry.host_session_id)`
  - if that owner session is absent, the helper returns `nullptr`
  - it no longer falls back to coarse `FindHostSessionByPid(pid)` for still-suspended
    spawn ownership
- non-spawn / no-owner cases still keep the older coarse fallback

Added regression coverage:

- if:
  - a suspended spawn pid is owned by a missing host session id
  - another live host later binds the same pid in the coarse map
- then runtime-stage `AGENT_READY` must not be forwarded to that rebound host

Why this matters:

- still-suspended spawn traffic now follows transaction ownership strictly
- a rebound coarse pid binding can no longer steal agent-originated traffic while the
  spawn transaction is still authoritative
- this removes another ownership leak on the path toward fully transaction-owned spawn
  lifecycle handling

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 RESUME Is Owner-Only For Suspended Spawn

After tightening host-side request routing and detach semantics around suspended spawn
ownership, one more control-path leak remained:

- `RESUME_REQUEST`

The old logic only checked:

- target pid is still suspended
- authoritative agent is ready enough

but it did not verify that the requesting host session was the actual owner recorded in:

- `SpawnSuspendedEntry.host_session_id`

That meant any host session that knew the pid could potentially release the spawn gate,
even if the suspended transaction still belonged to a different host.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleResumeRequest(...)` now rejects resume when:
  - the pid is still held by a suspended spawn
  - and `entry.host_session_id` is non-zero but different from `session.GetId()`
- new error returned:
  - code: `-7`
  - message: `spawned process is owned by another host session`

Added regression coverage:

- if:
  - original host owns suspended spawn pid `200`
  - another host rebinds coarse pid ownership for `200`
  - runtime readiness is already present
- then a resume request from the rebound host must:
  - be rejected
  - not call the gate-release callback
  - preserve suspended ownership on the original host

Why this matters:

- release of a suspended spawn gate is now aligned with transaction ownership, not just
  pid knowledge
- this closes a remaining control-path hole where coarse mutable pid bindings could
  bypass the spawn-owned host boundary
- request routing, detach, and resume now all follow the same ownership model for
  suspended spawns

Verification:

- `build/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-19 Attach REPL Ctrl-C Cleanup Alignment

The attach REPL still had a host-side UX inconsistency:

- `spawn` mode already cleaned up the active script and printed a normal unload line on exit
- `attach` mode could still surface a raw Python `KeyboardInterrupt` traceback if the operator
  pressed `Ctrl+C` while the REPL was blocked in `stdin.readline()`

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- `_run_repl(...)` now treats `KeyboardInterrupt` during prompt input as a normal REPL exit
- reused the existing `_repl_cleanup(...)` path so active scripts are unloaded before returning
- added an attach-specific CLI regression that simulates `Ctrl+C` from `stdin.readline()`

Why this matters:

- attach and spawn REPL sessions now share the same user-visible exit semantics
- `Ctrl+C` no longer leaks a Python traceback for a normal operator action

Verification:

- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_repl_attach_ctrl_c_unloads_active_script`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_repl_attach_bootstraps_loads_script_and_exits host.nook-py.tests.test_cli.CliTests.test_repl_exit_unloads_active_script`

## Follow-up On 2026-05-19 Runtime-Recorded Detection Uses Accepted Spawn Identity

One more part of `HandleAgentReady(...)` was still too coarse even after the earlier
control-stage acceptance cleanup.

The flag:

- `runtime_already_recorded`

was still computed partly from:

- `IsAgentRuntimeReady(pid)`
- `FindAgentSessionByPid(pid) != nullptr`

That still trusted "some runtime-ready session exists on this pid" more than "the spawn
transaction recognizes this runtime child identity".

For suspended spawns, that is the wrong priority. A runtime trace should only count as
"already recorded" if it is a runtime trace the transaction would actually accept.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

- added `HasAcceptedRuntimeTrace(...)`
- when a suspended spawn already has a target/authoritative identity, the server now
  counts runtime-ready as "already recorded" only if:
  - there is a runtime-ready trace for that accepted identity
- it no longer treats any runtime-ready bit plus any current pid session as sufficient
  in that spawn-bound path
- `runtime_already_recorded` now uses:
  - transaction runtime boundary first
  - otherwise the identity-aware accepted runtime trace helper

Why this matters:

- late control-stage suppression is now based on accepted transaction-owned runtime
  identity, not merely on the existence of some runtime-ready residue on the pid
- this removes another coarse pid-global shortcut from the spawn lifecycle
- it makes `HandleAgentReady(...)` more consistent with the identity-aware routing
  already used for:
  - host request forwarding
  - host response forwarding
  - runtime-ready replay/forward decisions

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Spawn Control-Stage Session Acceptance Uses Transaction-Owned Identity

Another remaining spawn-side leak was in the control-stage acceptance path itself.

`HandleAgentReady(...)` still computed:

- `current_agent_session_matches`

through the coarse helper:

- `IsAcceptedCurrentAgentSession(...)`

which only checked the current global:

- `FindAgentSessionByPid(pid)`

That is too loose for suspended spawns. During control-stage and pre-runtime windows,
the child transaction already has more precise ownership information than the coarse
global pid->session pointer alone.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

- added `IsAcceptedSpawnControlStageSession(...)`
- for suspended spawns, control-stage acceptance now prefers transaction-owned identity:
  - if the incoming control-stage ready matches the suspended spawn target identity,
    accept it via spawn-owned lookup instead of only via current global pid session
  - otherwise fall back to the older coarse current-session behavior
- `HandleAgentReady(...)` now uses this helper when evaluating
  `current_agent_session_matches`
- `IsAcceptedAgentSessionForScriptMessage(...)` also uses the same helper while the
  spawn is still blocked in control/runtime-ready wait states

Why this matters:

- control-stage acceptance is now aligned more closely with the same
  transaction-owned identity model already used by:
  - host request routing
  - host response routing
  - runtime-ready forwarding
- this removes another place where the current global pid occupant could dominate a
  still-suspended spawn transaction
- it is another concrete step toward `agent-owned stable spawn`, where transaction
  ownership should decide lifecycle acceptance earlier than coarse pid-global state

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Spawn Target Must Override Mismatched Global Runtime Trace

One more spawn-side leak remained in `HandleAgentReady(...)`.

If a suspended spawn already had a known target identity, for example:

- `target_process_name = com.demo.target`

but the global pid state still contained a mismatched runtime-ready trace for the same
pid, for example:

- current authoritative session = `com.demo.other`
- runtime-ready bit set
- cached runtime `AGENT_READY` frame for the wrong identity

then a later control-stage `AGENT_READY` for the real target could still be treated as
"late control after runtime" and be blocked by the stale global runtime trace.

That is the wrong authority direction. Once the spawn transaction already knows its
target identity, a mismatched global runtime trace must not continue to dominate the
transaction.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- added `HasMismatchedRuntimeTraceForSpawnTarget(...)`
- when a control-stage `AGENT_READY` matches the suspended spawn target identity, but
  the only runtime trace on that pid belongs to a different identity:
  - do not treat that runtime trace as authoritative runtime boundary
  - clear the stale runtime-ready bit and cached runtime-ready frame
  - allow the matching control-stage child to reclaim authoritative control fallback
- added explicit registry helpers:
  - `ClearAgentRuntimeReadyState(int pid)`
  - `ForceAgentReadyStage(int pid, stage)`

Added regression coverage:

- a spawn suspended on `com.demo.target` with a stale global runtime trace from
  `com.demo.other`
- then a matching control-stage `AGENT_READY` for `com.demo.target`
- expected result:
  - stale runtime trace is no longer treated as authoritative
  - control-ready session becomes the authoritative fallback again
  - mismatched runtime-ready lookup and cached ready replay both stay unavailable

Why this matters:

- transaction-owned spawn identity now wins over unrelated global runtime residue
- this removes another way the coarse pid-global state could poison the child lifecycle
- it is a direct prerequisite for a cleaner `agent-owned stable spawn` model, where
  authoritative child identity should come from the transaction boundary, not from any
  stale pid occupant

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Late-Promotion Host Ownership Convergence

The spawn lifecycle still had one remaining coarse host-routing dependency outside the
main server handler paths.

`MaybePromoteLateBoundControlReadyChild(...)` in
[server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
was still gating late promotion on:

- `FindHostSessionByPid(pid)`

That is weaker than the transaction-owned routing now used everywhere else. It means the
late-promotion decision could be affected by unrelated rebinding churn in the global
`pid -> host` map even when the suspended spawn transaction itself still had a valid
owning host session.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_spawn_controller_late_promotion.cpp)

Changes:

- late-promotion host liveness now checks:
  - `FindHostSession(entry.host_session_id)`
- it no longer requires the coarse global `pid -> host` binding for that pid to still be
  present
- added a direct regression covering:
  - the spawn transaction belongs to `original_host`
  - another host temporarily rebinds the same pid
  - that rebound host is removed, so global `FindHostSessionByPid(pid)` becomes null
  - late promotion must still proceed because the suspended spawn still belongs to the
    original live host session

Why this matters:

- it removes one more transaction leak from `spawn_controller`
- late-promotion now follows the suspended spawn's own ownership record instead of the
  mutable global pid binding
- this is the same direction as the earlier message/response/runtime-ready routing
  convergence work

Verification:

- `build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Transaction Runtime Boundary Over Global Runtime Bits

Another `HandleAgentReady(...)` edge was still relying on registry-global runtime state:

- `IsAgentRuntimeReady(pid)`
- `FindAgentSessionByPid(pid)`

That was good enough while the runtime session and global runtime bit stayed intact, but
it was still weaker than the per-spawn transaction state already recorded in
`SpawnSuspendedEntry`.

The concrete failure mode:

- a spawn transaction had already crossed into
  `authoritative_ready_stage = kRuntimeReady`
- a pinned control-ready fallback session still existed
- global runtime state was missing or stale
- a late control-stage `AGENT_READY` from another session could still re-enter the
  control registration path and mutate state that should already have been frozen at the
  runtime boundary

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `HasAuthoritativeRuntimeSpawnBoundary(...)`
- `HandleAgentReady(...)` now checks the suspended spawn transaction first for:
  - `authoritative_ready_stage == kRuntimeReady`
  - matching authoritative process identity
- once that boundary exists, late control-stage `AGENT_READY` is dropped before it can
  mutate control/runtime registration state
- kept the older global runtime-bit path as fallback for non-spawn / non-transaction
  cases

Added regression coverage:

- a new subset regression where:
  - the spawn transaction is already `kRuntimeReady`
  - only the pinned control-ready session remains live
  - global runtime state is absent
  - a late control-stage `AGENT_READY` from another session must be ignored
- mirrored the same case into the main `test_server_handlers` suite

Why this matters:

- another readiness decision moved from global side maps into the transaction-owned
  spawn record
- late control-stage events can no longer regress a spawn that already crossed the
  runtime boundary just because the runtime session/global bit disappeared first
- this is directly aligned with the `agent-owned stable spawn` direction: the spawn
  transaction remains the source of truth for its lifecycle boundary

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 SCRIPT_MESSAGE Uses Stage-Aware Spawn Identity

After tightening request routing, response routing, and runtime-ready forwarding, one
remaining agent->host path still used only the coarse "current pid-bound agent session"
rule:

- `HandleScriptMessage(...)`

That was too weak after runtime-ready, but a naive switch to the stricter runtime-agent
identity rule also breaks the legitimate control-stage caching path before runtime-ready.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `IsAcceptedAgentSessionForScriptMessage(...)`
- for spawn-bound pids:
  - while the transaction is still blocked before runtime-ready, `SCRIPT_MESSAGE`
    acceptance keeps using the current authoritative session rule so early control-side
    messages can still be cached
  - once the spawn is past the runtime boundary, `SCRIPT_MESSAGE` acceptance switches to
    the stricter runtime child identity rule
- `HandleScriptMessage(...)` now uses this stage-aware helper instead of the coarse
  `IsAcceptedCurrentAgentSession(...)`

Added regression coverage:

- a mismatched current runtime-marked session must not be able to forward or cache
  `SCRIPT_MESSAGE` for a runtime-ready spawn
- mirrored in both:
  - the focused spawn-ready subset
  - the full server-handlers regression
- also updated the positive spawn `SCRIPT_MESSAGE` tests to explicitly register the
  correct runtime-ready identity, making the intended contract explicit in tests

Why this matters:

- `SCRIPT_MESSAGE` now follows the same transaction-owned identity boundary as the other
  agent->host paths, but still preserves the legitimate pre-runtime cache path
- this removes another coarse global-session leak without breaking the spawn-stage
  semantics that existing tests rely on

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Runtime AGENT_READY Uses Spawn-Owned Host Routing

After moving `SCRIPT_MESSAGE` and the agent->host response path onto spawn-owned host
routing, one closely related branch still lagged behind:

- runtime-stage `AGENT_READY` forwarding in `HandleAgentReady(...)`

That code still located the host through:

- `FindHostSessionByPid(ready_pid)`

which meant a spawned runtime-ready child could still send its runtime-ready boundary and
cached early messages to the wrong host if the coarse pid->host map had been rebound.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `HandleAgentReady(...)` now resolves the host through
  `ResolveBoundHostSessionForPid(...)`
- this makes runtime-stage `AGENT_READY` forwarding and the associated cached
  `SCRIPT_MESSAGE` replay follow the same spawn-owned host rule as the other
  agent->host paths

Added regression coverage:

- for a spawned pid in `kWaitingRuntimeReady`, if the coarse pid->host map is rebound,
  runtime-stage `AGENT_READY` must still be forwarded to the original suspended host
  owner, not the rebound host
- mirrored in both:
  - the focused spawn-ready subset
  - the full server-handlers regression

Why this matters:

- the spawn-owned host boundary is now consistent across:
  - runtime-ready boundary delivery
  - cached early script-message replay
  - later script/RPC responses
- the coarse global pid->host map is pushed further out of the critical spawn lifecycle
- this keeps the runtime-ready handoff aligned with the same per-transaction ownership
  model used by the rest of the tightened routing

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Spawn-Owned Host Routing

After tightening agent identity on both request and response routing, one more coarse
global dependency remained on the host side.

For spawned pids, agent-originated traffic back to the host was still locating the host
through:

- `FindHostSessionByPid(pid)`

That is weaker than the spawn transaction state itself. Once a spawn is in-flight or
runtime-ready, the authoritative host owner is already recorded in:

- `SpawnSuspendedEntry.host_session_id`

If the coarse pid->host map gets rebound, agent-originated traffic can otherwise drift
to the wrong host even though the spawn transaction still knows the correct owner.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `ResolveBoundHostSessionForPid(...)`
- for spawn-bound pids, host resolution now prefers:
  - `SpawnSuspendedEntry.host_session_id`
  - `FindHostSession(host_session_id)`
- only when no spawn-owned host is present does routing fall back to
  `FindHostSessionByPid(pid)`

This helper is now used by:

- `HandleScriptMessage(...)`
- `HandleScriptCreateResp(...)`
- `HandleScriptLoadResp(...)`
- `HandleScriptUnloadResp(...)`
- `HandleRpcResponse(...)`

Added regression coverage:

- spawned `SCRIPT_CREATE_RESP` must still be delivered to the original suspended host
  owner even if the coarse pid->host map is rebound to another host session
- mirrored in both:
  - the focused spawn-ready subset
  - the full server-handlers regression

Why this matters:

- spawn-owned routing is now consistent in both dimensions:
  - which agent session is authoritative
  - which host session owns the spawn transaction
- pid-level global routing is pushed further into fallback territory
- this is another step toward the desired `agent-owned stable spawn` model where the
  per-spawn transaction is the source of truth instead of the coarse global maps

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Host Response Routing Uses Spawn-Owned Runtime Identity

The previous step tightened host-to-agent request routing for spawned pids, but the
reverse direction still had the same coarse assumption.

Handlers forwarding agent responses back to the host:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_LOAD_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

were still using the generic:

- `IsAcceptedCurrentAgentSession(registry, pid, session)`

That works for coarse pid ownership, but it is too weak once a spawn transaction has
already established a specific runtime child identity. In that state, a mismatched
runtime-marked session could still send responses back to the host if it happened to be
the current pid-bound session.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `IsAcceptedAgentSessionForHostResponse(...)`
- for spawn-bound pids with a recorded runtime child identity, response validation now
  prefers:
  - `FindRuntimeReadyAgentSessionByIdentity(pid, target_process_name)`
- if that identity is expected but no matching runtime-ready session exists, the response
  is rejected instead of falling back to coarse pid/current-session acceptance
- only non-spawn / identity-free cases keep using the older
  `IsAcceptedCurrentAgentSession(...)` rule

This helper is now used by:

- `HandleScriptCreateResp(...)`
- `HandleScriptLoadResp(...)`
- `HandleScriptUnloadResp(...)`
- `HandleRpcResponse(...)`

Added regression coverage:

- spawned `SCRIPT_CREATE_RESP` from a mismatched runtime-marked session must be dropped
  and must not reach the bound host
- mirrored in both:
  - the focused spawn-ready subset
  - the full server-handlers regression

Why this matters:

- request routing and response routing now follow the same spawn-owned runtime identity
  boundary
- once a spawn transaction has identified the real runtime child, both directions of
  script/RPC traffic stop trusting the coarse global pid->session map
- this is another concrete move away from shell/global routing toward
  transaction-owned runtime identity

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

Operational note:

- do not run build and execution of the same regression target in parallel
- this workspace has already exhibited stale-binary false reds when compile/run for the
  same `.exe` were overlapped
- keep compile -> run strictly serial for a given target, and keep package build ->
  push -> restart strictly serial for device validation

## Follow-up On 2026-05-19 Host Request Routing Uses Spawn-Owned Runtime Identity

Another remaining spawn-side leak was not in `AGENT_READY` handling itself, but in the
host-to-agent request path after spawn had already crossed into runtime-ready.

Before this step, handlers like:

- `SCRIPT_POST`
- `SCRIPT_CREATE`
- `SCRIPT_LOAD`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

still resolved the target agent with the global:

- `FindAgentSessionByPid(pid)`

That was too loose for spawned processes. If the global pid-to-session map pointed at a
runtime-looking session with the wrong identity, host requests could be forwarded there
even though the spawn transaction itself had already recorded the intended runtime child
identity.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- added `ResolveBoundAgentSessionForHostRequest(...)`
- for spawn-bound pids, host request routing now prefers:
  - the spawn transaction's `target_process_name`
  - or, when needed, its runtime-authoritative process identity
- this resolution goes through:
  - `FindRuntimeReadyAgentSessionByIdentity(pid, process_name)`
- only non-spawn / identity-free cases fall back to plain `FindAgentSessionByPid(pid)`

This helper is now used by:

- `HandleScriptPost(...)`
- `HandleScriptCreate(...)`
- `HandleScriptLoad(...)`
- `HandleScriptUnload(...)`
- `HandleRpcRequest(...)`

Added regression coverage:

- spawned `SCRIPT_CREATE` must not be forwarded to a runtime-marked session whose
  process identity does not match the spawn transaction's runtime child identity
- mirrored in both:
  - the focused spawn-ready subset
  - the full server-handlers regression

Why this matters:

- once a spawn transaction has established the runtime child identity, later host
  requests no longer trust the coarse global pid-to-session map first
- this moves another operational path from shell/global ownership toward
  transaction-owned runtime identity
- it also matches the practical guarantee users expect after spawn: JS/script traffic
  should follow the actual runtime child, not any session that happens to share the pid

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Regression Baseline Revalidated

After the live-session scaffolding cleanup and the latest owned-state convergence fixes,
the broader regression base is now stronger than the older plan notes suggested.

In particular, the previous note that the full `test_session_registry.cpp` executable
still hit an older control-ready lookup assertion is no longer true in the current tree.

Revalidated:

- full session-registry regression:
  - `build/test-bin/test_session_registry_current.exe`
- full zygote-control regression:
  - `build/test-bin/test_zygote_control_rpc.exe`
- handler stage subset:
  - `build/test-bin/test_server_handlers_stage_subset_current.exe`
- full spawn-ready handler subset:
  - `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- full server-handlers regression:
  - `build/test-bin/test_server_handlers_current.exe`
- source/structure regressions:
  - `build/test-bin/test_server_zygote_control_rpc_regressions_cleanup_current.exe`
  - `build/test-bin/test_strict_zygote_control_uses_helper_current.exe`
  - `build/test-bin/test_zygote_control_regressions_current.exe`
  - `build/test-bin/test_zygote_control_connection_lifetime_regressions_current.exe`
  - `build/test-bin/test_nook_comm_zygote_control_opt_in_regression_current.exe`

Why this matters:

- the current strict/zygote-control cleanup work is no longer leaning on narrow focused
  wrappers alone
- the broader spawn/session/handler regression base is green again before the next
  `agent-owned stable spawn` change
- future work can treat the current tree as a materially better-verified starting point
  than the earlier plan snapshot

## Follow-up On 2026-05-19 Transaction-Owned Spawn-Response Pending State

One remaining spawn-side boundary still depended on a shell/global side channel.

`HandleAgentReady(...)` used:

- `HasPendingSpawnForPid(pid)`

to decide whether a runtime-stage `AGENT_READY` had arrived before the corresponding
`SpawnResponse` had been sent back to the host.

That was too indirect:

- it scanned global pending-spawn state instead of the actual suspended transaction
- if `pending_spawn` was cleared early while finalize was still in flight, runtime-ready
  could be forwarded to the host before the spawn response, breaking ordering

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `SpawnSuspendedEntry` now carries `response_pending`
- `ExecuteSpawnRequest(...)` marks the authoritative suspended spawn as
  `response_pending=true` once the transaction is bound, and clears it only after
  `SpawnResponse` has been sent
- `HandleAgentReady(...)` now holds runtime-ready forwarding/replay based on the
  suspended transaction's own `response_pending` bit instead of global
  `HasPendingSpawnForPid(pid)`
- added a regression where `pending_spawn` is manually cleared during finalize:
  runtime-ready must still remain held until `SpawnResponse` is sent

Why this matters:

- spawn-response ordering is now owned by the spawn transaction itself
- runtime-ready replay no longer depends on a global pending-spawn map staying intact
  until the very end of finalize
- this is another concrete move away from shell-owned inference toward
  `agent-owned stable spawn`

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-19 Main Zygote-Control Regression Coverage

The focused wrappers were green, but the main `test_zygote_control_rpc` runner was not
actually executing the newly added owned-cleanup cases. Reconnecting them exposed one
more issue: several `CallZygoteControlRpc` tests were still using non-started
`StubTransport` sessions even though control-ready lookup now requires sessions to be
alive.

Updated:

- [tests/communication/test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)

Changes:

- added the install/uninstall owned-cleanup regression cases to the main
  `test_zygote_control_rpc` runner
- migrated the `CallZygoteControlRpc` success/rebind/reconnect tests to
  `AliveStubTransport` plus `RegisterLiveControlReadySession(...)`
- kept the negative tests on the older stub/dead-session model where the test intent is
  specifically "not ready" or "closed"

Why this matters:

- the main zygote-control regression binary now covers the cleanup work instead of
  relying only on ad-hoc focus wrappers
- future session-liveness tightening will be caught in the normal regression path

Verification:

- `build/test-bin/test_zygote_control_rpc.exe`

## Follow-up On 2026-05-21 Runtime-Ready Forward Failure State Boundary

One remaining runtime-ready edge case still advanced spawn transaction state too early.

Before this cleanup, `HandleAgentReady(...)` moved a spawn transaction to
`kReadyForScriptLoad` before forwarding runtime-stage `AGENT_READY` to the bound host.
If the host send failed, cached script messages stayed cached, but the transaction state
already looked ready. That created a false-ready window where a later script operation
could be accepted even though the host never observed runtime readiness.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- runtime-stage `AGENT_READY` now advances the spawn transaction to
  `kReadyForScriptLoad` only after the frame is successfully sent to the bound host
- if host forwarding fails, the transaction remains at `kWaitingRuntimeReady`
- cached script messages remain cached, matching the existing replay-prefix cleanup
  semantics
- added a regression to lock the state boundary for failed runtime-ready forwarding

Why this matters:

- runtime readiness is no longer inferred from agent-side state alone when a host is
  bound
- the server-side transaction state now matches what the host has actually observed
- this closes another agent-owned stable-spawn ordering hole without changing the
  injection backend

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`

## Follow-up On 2026-05-21 SpawnResponse Send Boundary Owns Runtime-Ready Release

Another spawn ordering edge existed at the exact `SpawnResponse` send boundary.

Before this cleanup, `ExecuteSpawnRequest(...)` cleared `response_pending` and sometimes
advanced the transaction to `kReadyForScriptLoad` before actually sending
`SpawnResponse` to the host. If runtime-stage `AGENT_READY` arrived in that small
window, `HandleAgentReady(...)` could observe `response_pending=false` and try to
forward runtime ready while the host was still sending the spawn response. In tests this
showed up as a re-entrant send deadlock risk and, after moving the gate, as a missing
cached-ready replay because the post-finalize ready stage had been computed too early.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `response_pending` now remains true until `SpawnResponse` has been successfully sent
- runtime-ready state is only promoted after the host has observed `SpawnResponse`
- after `SpawnResponse` succeeds, the controller re-reads the spawn transaction's
  authoritative ready stage before deciding whether to replay cached runtime
  `AGENT_READY`
- added a regression that injects runtime-ready from inside the host transport's
  `SpawnResponse` send boundary; expected order is always `SpawnResponse` first,
  cached runtime `AGENT_READY` second

Why this matters:

- the spawn response is now the single transaction boundary that releases runtime-ready
  forwarding
- `AGENT_READY` can no longer overtake or re-enter host response sending
- runtime-ready that arrives during the response-send boundary is not lost; it is replayed
  after the response succeeds

Verification:

- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`
- `build/test_session_registry_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`

## Follow-up On 2026-05-21 Strict Zygote-Control Late Promotion Race

A live strict zygote-control smoke exposed two related issues:

- first run timed out waiting for runtime agent ready
- second run reached script load and `installed`, but only partially hooked because the
  late-promotion path injected a full agent after the child had already self-bootstrapped
  to runtime-ready

The log evidence showed the strict path producing a control-stage helper session first,
then the promoted child completing `NookAgentInitializeForSpawnChild` and sending a
runtime-stage `AGENT_READY`. The legacy late-promotion thread was still allowed to run
and could inject another full agent into the same child, causing duplicate agent sessions
and session teardown after script load.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_spawn_controller_late_promotion.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- late promotion now gives the zygote-control child a short self-bootstrap grace window
  before doing legacy full-agent injection
- if a matching runtime-ready session appears during that window, late promotion is
  cancelled instead of injecting a duplicate full agent
- late promotion still falls back to legacy injection when no runtime-ready session
  appears
- runtime-ready session lookup now requires both the runtime-ready bit and the recorded
  `AgentReadyStage::kRuntime`; a stale runtime bit plus a fresh control-stage session no
  longer satisfies runtime-ready waits

Why this matters:

- strict zygote-control no longer races its own promoted-child runtime bootstrap against
  the legacy full-agent promotion fallback
- stale runtime state cannot suppress required fallback injection for a control-ready
  child
- this keeps the current architecture aligned with the agent-owned boundary: control
  helper readiness and runtime agent readiness are distinct observable states

Verification:

- `build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_current.exe`

## Follow-up On 2026-05-22 Late-Promotion Failure Does Not Pre-Advance Runtime State

The next mainline `agent-owned` cleanup stayed on the post-finalize / child-promotion
boundary.

Problem:

- `MaybePromoteLateBoundControlReadyChild(...)` changed the suspended transaction state to
  `kWaitingRuntimeReady` before attempting full-agent late promotion
- if `InjectSpawnChildAgent(...)` then failed, the transaction was left in a false
  post-injection state even though the child was still only control-ready
- that made the server-side lifecycle look more advanced than the actual child-owned
  runtime boundary

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_spawn_controller_late_promotion.cpp)

Added regression:

- `TestLatePromotionDoesNotAdvanceRuntimeStateWhenInjectFails()`

Behavior now guaranteed:

- late promotion keeps the suspended transaction in:
  - `kWaitingAgentReady`
  while child injection is still only being attempted
- the state moves to:
  - `kWaitingRuntimeReady`
  only after `InjectSpawnChildAgent(...)` succeeds

Why this matters:

- the transaction state now tracks the real child-owned lifecycle boundary instead of a
  pre-commit intent
- failed late promotion no longer leaves a false runtime-wait state behind
- this is another concrete step toward cleaner `agent-owned stable spawn` ordering on the
  default/symbi mainline

Verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/handler/message_dispatcher.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_spawn_controller_late_promotion_agent_owned_green.exe`
- `build/test-bin/test_spawn_controller_late_promotion_agent_owned_green.exe`

## Follow-up On 2026-05-21 Strict Zygote-Control Late-Promotion Grace Removed

Real-device strict zygote-control testing still produced a partial Java hook:

- `SCRIPT_CREATE` and `SCRIPT_LOAD` completed
- `MainActivity.get_random()` and `check(int,int)` were installed
- only `lab:frida-0x1:installed` appeared before resume
- the expected first-screen `hit:get_random` / `forced-random=5` messages were missing

Log evidence showed the control helper reported control-ready around `18:47:51.166`,
but the full-agent promotion did not start until after the 750 ms self-bootstrap grace.
The app was already foreground by then, so the later full-agent Java hook install could
only catch later button-triggered calls.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_spawn_controller_late_promotion.cpp)

Behavior now guaranteed:

- control-only late promotion no longer waits for a fixed self-bootstrap grace window
- the thread still performs an immediate runtime-ready check before injection, so it does
  not duplicate a runtime agent that is already present
- if the child is still control-only, full-agent promotion starts immediately

Verification in this workspace:

- `build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test_session_registry_current.exe`
- `build/test-bin/test_server_runtime_current.exe`

## Follow-up On 2026-05-21 Strict Zygote-Control Finalize/Promotion Ordering

Real-device testing after removing the 750 ms grace still showed strict zygote-control
timeouts:

- host failed with `wait runtime agent ready timed out`
- previous logs showed full-agent late promotion starting while zygote-control
  `finalize-clear` was still running
- the promoted child then hung or failed inside remote calls such as env setup / Java
  bootstrap, and Android could kill the app for attach/start timeout

Root cause:

- `ExecuteSpawnRequest()` bound the control-ready child and immediately started
  late-promotion before `FinalizeSpawn()`
- `FinalizeSpawn()` was concurrently tearing down zygote-control hooks/state
- the child full-agent ptrace injection and zygote-control clear path were allowed to
  overlap inside one spawn transaction

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Behavior now guaranteed:

- control-ready child promotion is not started before `FinalizeSpawn()` completes
- full-agent late promotion runs after zygote-control finalize/clear succeeds
- late promotion is synchronous instead of detached, so the server no longer sends the
  spawn response while the fallback full-agent injection is still racing in the
  background
- if runtime-ready has already arrived by the post-finalize boundary, late promotion is
  cancelled instead of injecting a duplicate full agent

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`

## Follow-up On 2026-05-23 Resolved Pending-Spawn Handoff Is Now A Single Registry Boundary

Another real transaction boundary remained in `ExecuteSpawnRequest(...)`.

Problem:

- after `WaitForPendingSpawn(...)` returned, controller code still split one logical handoff
  across multiple registry mutations:
  - `BindHostToResolvedPendingSpawn(...)`
  - `SetSpawnResponsePending(...)`
  - `TakePendingSpawn(...)`
  - or `ConsumeOrClearPendingSpawn(...)` for a non-current/missing host
- that meant the "resolved pending spawn becomes either:
  - a bound host-owned suspended transaction
  - or a consumed orphan with no current host"
  boundary was still controller-assembled instead of registry-owned

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added a single registry handoff helper

New types:

- `ResolvedPendingSpawnHandoffDisposition`
- `ResolvedPendingSpawnHandoffResult`

New helper:

- `PrepareResolvedPendingSpawnHandoff(...)`

It now owns the immediate post-resolution branch:

- current registered host:
  - bind resolved pending spawn to the host-owned suspended transaction
  - consume the pending spawn record
- missing / non-current host:
  - consume or clear the pending spawn record without binding
- registered host but missing resolved pending entry:
  - report explicit handoff failure back to controller

### 2. `ExecuteSpawnRequest(...)` now consumes the handoff as one result

Controller logic now:

- asks the registry for one resolved-pending-spawn handoff result
- reacts only to:
  - bound
  - consumed-without-host
  - missing-for-registered-host

This removes another controller-local bundle of transaction mutation and makes the
post-ready handoff more explicitly registry-owned.

### 3. Added direct regressions for both main handoff outcomes

Coverage now asserts:

- a registered host gets:
  - bound pid ownership
  - suspended spawn context
  - consumed pending-spawn record
- a non-current host pointer causes:
  - pending-spawn consume/clear
  - no host bind
  - no suspended spawn residue

Why this matters:

- this is not just mechanical helper movement; it closes a real transaction boundary that
  was still spread across controller code
- it further narrows the gap between:
  - child-owned readiness resolution
  - host-owned suspended transaction creation
- that is the right direction before the next actual `agent-owned stable spawn` step

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp120.exe`
- `build/test-bin/test_session_registry_tmp120.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp120.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp120.exe`

## Follow-up On 2026-05-23 Spawn-Response Success Commit Is Now Registry-Owned

This step stayed on the real default-spawn mainline instead of more cosmetic cleanup.

Problem:

- after `SpawnResponse` send succeeded, `ExecuteSpawnRequest(...)` still directly owned the
  post-response commit boundary:
  - release spawn-response hold
  - re-resolve post-finalize runtime/control stage
  - decide whether runtime-ready replay should happen
- that meant the transaction's transition from:
  - held-before-response
  to:
  - response-committed child-owned state
  was still controller-assembled

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added explicit response-commit result

New type:

- `SpawnResponseCommitResult`

New helper:

- `CommitSpawnResponseSuccess(...)`

The registry now owns the transaction commit that happens once host-side spawn response
delivery has succeeded.

### 2. The registry helper now defines the committed post-response boundary

`CommitSpawnResponseSuccess(...)` now:

- releases the spawn-response boundary
- resolves authoritative post-finalize stage/identity
- reports whether the committed transaction should replay runtime-ready

That makes the meaningful boundary explicit:

- response sent successfully
- child-owned state committed
- runtime replay eligibility derived from committed registry state

### 3. `spawn_controller.cpp` now consumes that boundary instead of rebuilding it

Controller code now:

- sends `SpawnResponse`
- asks the registry to commit the success boundary
- replays cached runtime/script messages only if the registry says the committed state is
  runtime-ready

So one more piece of spawn lifecycle truth has moved from controller-side re-interpretation
to registry-owned transaction state.

### 4. Added direct regressions for runtime vs control post-response commit

Coverage now asserts:

- runtime-ready committed response:
  - releases response hold
  - promotes to `kReadyForScriptLoad`
  - requests runtime replay
- control-ready committed response:
  - releases response hold
  - stays in `kWaitingRuntimeReady`
  - does not request runtime replay

Why this matters:

- this is a real default-spawn mainline step, not helper churn
- the response-commit boundary is one of the last places where controller code was still
  deciding child-owned lifecycle from partial context
- it moves the default path closer to a true transaction-owned / child-owned spawn model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp121.exe`
- `build/test-bin/test_session_registry_tmp121.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp121.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp121.exe`

## Follow-up On 2026-05-23 Runtime-Ready Visibility Commit Is Now Registry-Owned

The next real default-spawn boundary was the runtime-visible transition after an accepted
runtime-stage `AGENT_READY`.

Problem:

- `HandleAgentReadyForwarding(...)` still directly did:
  - `MarkSpawnRuntimeReadyVisible(pid)`
  - then replay cached script messages
- that meant the "runtime-ready has become host-visible and script-forwardable" boundary
  was still partially assembled in the handler

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added explicit runtime-visible commit helper

New type:

- `RuntimeReadyCommitResult`

New helper:

- `CommitRuntimeReadyVisibility(...)`

The registry now owns the meaning of:

- runtime-ready becomes visible
- cached script-message replay is now allowed

### 2. Handler now consumes the commit result instead of rebuilding it

`MarkRuntimeReadyVisibleAndReplayCachedScriptMessages(...)` now:

- asks the registry to commit runtime visibility
- only replays cached script messages if the registry says that replay is now valid

Why this matters:

- this removes another child-owned lifecycle edge from handler-local mutation
- runtime-visible state and script-forwardability now move together through one registry
  boundary instead of two separate operations

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp122.exe`
- `build/test-bin/test_session_registry_tmp122.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp122.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp122.exe`

## Follow-up On 2026-05-23 Host->Agent Request Target Resolution Is Now Registry-Owned

The last remaining high-value shared-path gap on the default stable-spawn mainline was
host->agent request routing during runtime-owned spawn phases.

Problem:

- handler code still locally assembled the request target from several facts:
  - host-owned pid resolution
  - foreign suspended-owner rejection
  - spawn-blocked vs runtime-ready state
  - agent-session resolution for suspended vs non-suspended cases
- that meant runtime-owned request routing was still partly handler-owned even after the
  rest of the child-owned lifecycle had moved into the registry

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned host-bound request target resolution

New types:

- `HostBoundAgentLookupError`
- `HostBoundAgentRequestTarget`

New helper:

- `ResolveHostBoundAgentRequestTarget(...)`

It now owns:

- owned-spawn pid preference
- fallback bound-pid lookup
- foreign suspended-owner rejection
- spawn-not-ready blocking
- runtime/control/current agent target selection

### 2. Host request forwarding paths now consume that result

Updated request-side paths now use the single registry result for:

- `SCRIPT_CREATE`
- `SCRIPT_LOAD`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`
- `SCRIPT_POST`

### 3. Kept the foreign-owner guard intact

During the migration, one regression surfaced:

- rebound host request routing briefly lost the old
  "foreign suspended spawn owner must be rejected" rule

That was restored inside the new registry helper before final verification.

Why this matters:

- this was the last remaining high-value request-side fallback cluster on the default
  stable-spawn mainline
- runtime-owned host->agent routing is now registry-owned like the other child-owned
  lifecycle boundaries
- after this step, the remaining server-side work is mostly orchestration rather than
  scattered lifecycle truth

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp123.exe`
- `build/test-bin/test_session_registry_tmp123.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp123.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp123.exe`

## 2026-05-23 Handler immediate-error scaffolding convergence

Scope in this step stayed intentionally narrow:

- only [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- only [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- no spawn algorithm change
- no injector / zygote-control / device packaging change

What was added:

- `SendScriptImmediateError(...)`
- `SendScriptCreateImmediateError(...)`
- `SendRpcImmediateError(...)`

What was changed:

- `SCRIPT_CREATE`, `SCRIPT_LOAD`, `SCRIPT_UNLOAD`, and `RPC_REQUEST` now route their
  immediate host-visible error responses through thin shared helpers instead of
  repeating inline response assembly.
- `SCRIPT_LOAD` still keeps its dedicated request forwarding path and spawn-state
  hooks. This step did not touch `MarkSpawnScriptLoadInFlight(...)` /
  `MarkSpawnScriptLoadComplete(...)` behavior.
- `SCRIPT_CREATE` still preserves the existing `script_id = 0` response semantics.

Coverage added:

- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
  - `TestScriptUnloadWithoutRegistryReturnsImmediateHostError()`

Why this matters:

- another block of repeated request-failure scaffolding is now flattened without
  changing wire behavior
- this makes the remaining handler convergence work more mechanical and lowers the
  chance of future per-handler drift
- it is aligned with the current goal of tightening host/agent handler structure
  before touching higher-risk spawn-path behavior again

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp64.exe`
- `.\build\test-bin\test_server_handlers_spawn_ready_subset_tmp64.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp65.exe`
- `.\build\test-bin\test_server_handlers_spawn_ready_subset_tmp65.exe`

## 2026-05-23 Response decode/drop convergence

Scope in this step also stayed local to the handler layer:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

What was added:

- `DecodeAndForwardAgentResponseNoHooks(...)`

What it does:

- wraps the common response path used by:
  - `SCRIPT_CREATE_RESP`
  - `SCRIPT_UNLOAD_RESP`
  - `RPC_RESPONSE`
- keeps the existing sequence:
  - decode and drop on invalid payload
  - resolve current accepted agent session
  - resolve bound host
  - forward to host without any extra spawn-state hooks

What it explicitly does not touch:

- `SCRIPT_LOAD_RESP`
  - that handler still stays separate because it owns
    `MarkSpawnScriptLoadComplete(...)` semantics

Coverage added:

- `TestInvalidScriptUnloadRespIsDropped()`
- `TestInvalidRpcResponseIsDropped()`

Why this matters:

- the response side now mirrors the earlier request-side convergence
- no-hook response handlers no longer duplicate the same decode/forward shell
- this reduces handler drift while preserving the one response path that still has
  spawn-state side effects

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp66.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp66.exe'`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp67.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp67.exe'`

## 2026-05-23 Process/App list handler convergence

Scope:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

What was added:

- `SendProcessListResponse(...)`
- `SendAppListResponse(...)`
- `HandleEnumerationListRequest(...)`
- lightweight `FakeProcessManager` in the subset regression file

What changed:

- `HandleProcessListRequest(...)` and `HandleAppListRequest(...)` now share the same
  decode / unavailable / enumerate / send scaffold through
  `HandleEnumerationListRequest(...)`
- message types, response payloads, and log strings remain unchanged

Coverage added in the focused subset:

- `TestProcessListRequestReturnsProcesses()`
- `TestProcessListRequestWithoutEnumeratorReturnsImmediateError()`
- `TestAppListRequestReturnsApps()`
- `TestAppListRequestWithoutEnumeratorReturnsImmediateError()`

Why this matters:

- another self-contained pair of same-shape handlers is now flattened
- this continues the current strategy of reducing per-handler drift before touching
  any higher-risk spawn-path behavior
- the new coverage also gives the lighter subset suite visibility into list handlers,
  so later refactors do not have to rely only on the larger server handler test file

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp69.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp69.exe'`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp70.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp70.exe'`

## 2026-05-23 Detach/Resume immediate-error convergence

Scope:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

What was added:

- `SendDetachImmediateError(...)`
- `SendResumeImmediateError(...)`

What changed:

- `HandleDetachRequest(...)` now routes repeated error responses through
  `SendDetachImmediateError(...)`
- `HandleResumeRequest(...)` now routes repeated error responses through
  `SendResumeImmediateError(...)`
- decision order, log lines, error codes, and success paths were kept unchanged

Focused coverage added:

- `TestInvalidDetachRequestReturnsImmediateError()`
- `TestDetachRequestWithoutRegistryReturnsImmediateError()`
- `TestInvalidResumeRequestReturnsImmediateError()`
- `TestResumeRequestWithoutRegistryReturnsImmediateError()`

Why this matters:

- `DETACH` and `RESUME` now follow the same immediate-error response shape as the
  already-converged script/rpc handlers
- this reduces another pocket of hand-built response duplication without touching
  the more sensitive spawn ownership / gate-release semantics

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp71.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp71.exe'`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp72.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp72.exe'`

## 2026-05-23 Attach immediate-error convergence

Scope:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

What was added:

- `SendAttachImmediateError(...)`

What changed:

- the immediate attach failure responses now share one helper for:
  - decode failure
  - target resolve failure
  - injector unavailable/failure
  - attach agent-ready timeout/failure outcome
- attach success / reuse / replay behavior was not changed
- attach waiting and late-ready handling were not changed

Focused coverage added:

- `TestAttachRequestWithoutTargetReturnsResolveFailure()`
- `TestAttachRequestResolveFailureReturnsImmediateError()`

Notes:

- this step intentionally did not try to force an "invalid attach request" negative
  decode test into the subset suite because the current `DecodeAttachRequest(...)`
  path is permissive and accepts empty/partial payloads
- the subset coverage instead locks the meaningful immediate failure boundary that
  the current protocol implementation actually exposes: unresolved attach target

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp74.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp74.exe'`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp75.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp75.exe'`

## 2026-05-23 Attach failure-path naming cleanup

Scope:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

What was added:

- `TestAttachRequestWithoutInjectorReturnsImmediateError()`
- `SendAttachAttachFailure(...)`

What changed:

- attach `-3` failure responses that carry resolved target identity now share a named
  helper instead of repeating the same `AttachResponse` assembly
- this currently covers:
  - injector unavailable
  - attach timeout / injector finished but runtime not ready outcome
- no attach state transition, timeout cleanup, or replay behavior changed

Why this step stopped here:

- beyond this point, the remaining attach code is tied much more closely to pending
  attach bookkeeping, timeout markers, and late-ready cleanup semantics
- that is no longer the same low-risk “just flatten repeated response assembly”
  category as the earlier handler convergence steps

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp76.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp76.exe'`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp77.exe`
- `& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test-bin\test_server_handlers_spawn_ready_subset_tmp77.exe'`

## Follow-up On 2026-05-22 Runtime-Ready Visibility Boundary Moves Behind Registry

One more post-response boundary was still split across layers.

After the earlier `response_pending` centralization:

- `SetSpawnResponsePending(pid, false)` already owned the
  `response sent -> runtime-ready may become script-load ready` gate
- but `HandleAgentReady(...)` still directly wrote
  `kReadyForScriptLoad` when a runtime-stage `AGENT_READY` was forwarded to the host,
  or when no host was bound and runtime-ready arrived after the hold was gone

That meant the same lifecycle boundary was still being advanced from multiple places:

- registry on response release
- handler on runtime-ready visibility

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added `MarkSpawnRuntimeReadyVisible(pid)` to the registry

This helper promotes the suspended spawn to `kReadyForScriptLoad` only when all of the
following are true:

- the suspended entry exists
- `response_pending == false`
- authoritative stage is already `kRuntimeReady`
- the transition is monotonic from the current state

If any of those are not true, it returns `false` and leaves the transaction unchanged.

### 2. `HandleAgentReady(...)` now uses the registry helper

The runtime-ready branch no longer directly writes `kReadyForScriptLoad`.

Instead it now calls `MarkSpawnRuntimeReadyVisible(pid)` in the two places where
runtime-ready becomes host-visible:

- runtime-ready arrives after the response hold is already gone and there is no bound host
- runtime-ready is forwarded successfully to the bound host after the response hold is gone

Cached `SCRIPT_MESSAGE` replay behavior is unchanged; this step only narrows who owns the
spawn-state promotion itself.

### 3. Added a focused registry regression

`TestMarkSpawnRuntimeReadyVisiblePromotesOnlyAfterResponseRelease()` covers:

- no promotion before authoritative runtime-ready exists
- no promotion while `response_pending == true`
- promotion succeeds once the authoritative stage is runtime-ready and the response hold
  has been released

Why this matters:

- the runtime-ready visibility boundary is now expressed through a registry helper instead
  of duplicated ad-hoc state writes in the handler
- this reduces one more shared-path seam where host-side code could advance lifecycle
  state independently of the transaction record
- it keeps the `child-owned` spawn progression moving toward single-owner boundaries
  without changing the default device route

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`

## Follow-up On 2026-05-22 Suspended-Spawn Owner Boundary Moved Into Registry

Another small shared-boundary duplication remained around suspended-spawn ownership.

Problem:

- handler-side code still reimplemented suspended-spawn owner lookup using
  `GetSpawnSuspendedEntry(...)`
- the same "who owns this suspended spawn?" and "is this requester foreign?" logic
  was being reconstructed outside the registry
- that increases the chance that future host/owner changes drift between
  registry state and handler-side interpretation

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registry now exposes suspended-spawn owner semantics directly

Added:

- `FindSuspendedSpawnOwnerHostSessionId(int pid)`
- `IsForeignSuspendedSpawnOwner(int pid, uint32_t host_session_id)`

Why this matters:

- suspended-spawn ownership is registry state, so ownership answers should come from
  the registry boundary directly
- callers no longer need to fetch the full suspended entry just to answer a simple
  ownership question

### 2. Handler-side owner checks now delegate to registry

`server_handlers.cpp` now uses the registry helpers for:

- suspended owner host resolution
- foreign-owner rejection

This is intentionally behavior-preserving:

- no route changes
- no spawn strategy changes
- no device-side artifact changes

### 3. Added direct registry coverage for the ownership boundary

New coverage asserts:

- suspended owner host lookup returns `0` when no suspended transaction exists
- suspended owner host lookup returns the recorded owner while held
- foreign-owner detection accepts the owner and rejects other hosts

Why this matters:

- one more shared boundary now lives at the registry layer instead of being rebuilt
  in handlers
- this continues the `child-owned` / `spawn-owned` convergence work without touching
  the stable mainline spawn path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp13.exe`
- `build/test-bin/test_session_registry_tmp13.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp13.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp13.exe`

## Follow-up On 2026-05-22 Spawn-Blocked And Runtime-Visible Gates Moved Into Registry

Another small piece of shared spawn lifecycle logic was still living in handler-local code.

Problem:

- handler-side code still recomputed two core suspended-spawn gates:
  - whether a pid is still blocked for script operations
  - whether runtime-ready may be exposed immediately to the host
- both answers come from registry-owned suspended-spawn state:
  - `state`
  - `response_pending`
  - `authoritative_ready_stage`
- leaving these checks in handler-local helpers keeps lifecycle truth split across layers

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registry now answers the two held-spawn gate questions directly

Added:

- `CanExposeSpawnRuntimeReadyImmediately(int pid)`
- `IsSpawnBlockedForScriptOperations(int pid)`

This moves the gate checks to the layer that owns:

- suspended transaction state
- response boundary state
- authoritative ready stage

### 2. Handler-side checks now delegate to the registry

`server_handlers.cpp` now uses the new registry helpers for:

- runtime-ready immediate visibility decision
- host script-operation blocked checks
- resume blocked-before-runtime checks

This is behavior-preserving:

- no route changes
- no injection changes
- no device artifact changes

### 3. Added direct registry coverage for these gates

New coverage asserts:

- control-ready and held runtime-ready transactions remain script-blocked
- ready-for-script-load no longer reports script-blocked
- runtime-ready cannot become immediately visible while the response boundary is still held
- runtime-ready can become immediately visible once the boundary is released

Why this matters:

- another piece of lifecycle truth is now registry-owned instead of reconstructed in handlers
- this reduces the amount of duplicated suspended-spawn interpretation across shared paths
- it continues the convergence toward a cleaner `child-owned` / transaction-owned spawn model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp14.exe`
- `build/test-bin/test_session_registry_tmp14.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp14.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp14.exe`

## Follow-up On 2026-05-22 Spawn Runtime-Agent Identity Resolution Moved Into Registry

Another repeated suspended-spawn interpretation remained around runtime-agent identity.

Problem:

- handler-side code still owned a helper that reconstructed:
  - which runtime-ready agent session should represent a suspended spawn
- that logic was built from registry-owned data:
  - `authoritative_ready_stage`
  - `authoritative_process_name`
  - `target_process_name`
  - runtime-ready pid/name/session mappings
- leaving this resolution outside the registry meant host-request acceptance,
  host-response acceptance, and script-message acceptance still depended on
  handler-local reconstruction

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registry now resolves the suspended-spawn runtime session directly

Added:

- `FindRuntimeReadyAgentSessionForSpawn(int pid, const SpawnSuspendedEntry& entry)`

It centralizes:

- `ResolveSpawnRuntimeProcessName(entry)`
- runtime-ready stage checks
- authoritative pid/name/session matching

### 2. Handler helper now delegates to the registry

`ResolveRuntimeReadyAgentSessionForSuspendedSpawn(...)` in
`server_handlers.cpp` is now just a thin call into the registry helper.

This is intentionally behavior-preserving:

- no route changes
- no inject changes
- no host protocol changes

### 3. Added direct registry coverage for suspended-spawn runtime resolution

New coverage asserts:

- runtime-ready suspended spawn resolves using authoritative runtime identity
- control-ready suspended spawn does not incorrectly resolve a runtime session
- runtime disconnect causes suspended-spawn runtime resolution to return null

Why this matters:

- another piece of suspended-spawn lifecycle truth is now registry-owned
- the remaining runtime/control acceptance paths can now converge on one shared
  runtime-resolution primitive
- this is a direct step toward reducing handler-local lifecycle reconstruction

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp15.exe`
- `build/test-bin/test_session_registry_tmp15.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp15.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp15.exe`

## Follow-up On 2026-05-22 Suspended-Spawn Control/Runtime Acceptance Helpers Moved Into Registry

This pass intentionally batched a few closely related low-risk convergence steps.

Problem:

- handler-side code was still reconstructing several suspended-spawn acceptance rules:
  - which control-ready session a control-stage spawn should accept
  - whether a host response came from the accepted runtime session
  - whether a script message should accept runtime, reject mismatched runtime, or
    fall back to cached control delivery after runtime disconnect
- all of these depended on registry-owned lifecycle state:
  - control/runtime session pinning
  - authoritative runtime identity
  - current pid-bound agent session
  - suspended-spawn state / authoritative stage / target identity

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registry now owns suspended-spawn control/runtime acceptance helpers

Added:

- `FindControlReadyAgentSessionForSpawn(...)`
- `IsAcceptedHostResponseAgentSessionForSpawn(...)`
- `IsAcceptedScriptMessageAgentSessionForSpawn(...)`

These helpers preserve the current behavior:

- control-stage spawn still falls back to the current pid-bound agent when there is no
  more specific pinned control identity
- host responses for runtime-ready spawn still accept only the resolved runtime agent
- script messages for runtime-ready spawn still reject mismatched runtime sessions, but
  may fall back to the current control session only after runtime disconnect

### 2. Handler-side acceptance logic now delegates instead of reconstructing

`server_handlers.cpp` now uses registry-owned helpers for:

- control-stage accepted session lookup
- runtime-stage host response acceptance
- runtime-stage script message acceptance

This reduces one of the last dense clusters of handler-local suspended-spawn lifecycle
interpretation without changing routes or injection behavior.

### 3. Added direct registry coverage for the acceptance boundaries

New coverage asserts:

- control-stage suspended spawn resolves the expected control session and rejects
  mismatched identities
- runtime-ready suspended spawn host responses accept only the resolved runtime agent
- runtime-ready suspended spawn script messages fall back to control only after runtime
  disconnect, not while mismatched runtime is still present

Why this matters:

- a larger chunk of suspended-spawn truth is now registry-owned
- the remaining handler logic is closer to thin forwarding / routing code
- this is a concrete step toward finishing the `agent-owned stable spawn` state
  convergence without touching the proven device path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp16.exe`
- `build/test-bin/test_session_registry_tmp16.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp16.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp16.exe`

## Follow-up On 2026-05-22 Host-Request Agent Resolution Unified With Spawn Acceptance Helpers

After the previous pass, host-response and script-message acceptance were already mostly
registry-owned, but host-request agent resolution still had its own handler-local
reconstruction path.

Problem:

- `ResolveBoundAgentSessionForHostRequest(...)` was still rebuilding suspended-spawn
  runtime-vs-blocked semantics in `server_handlers.cpp`
- that left host request resolution slightly out-of-line with the new registry-owned
  host-response and script-message acceptance helpers

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registry now resolves the suspended-spawn host-request agent directly

Added:

- `FindHostRequestAgentSessionForSpawn(int pid, const SpawnSuspendedEntry& entry)`

It preserves the current rules:

- use resolved runtime agent when it exists
- block host requests once the transaction is runtime-owned but runtime is no longer
  available
- only fall back to the current pid-bound session before the runtime-owned boundary

### 2. Handler-side host-request resolution now delegates to the registry

`ResolveBoundAgentSessionForHostRequest(...)` is now a thin wrapper around the new
registry helper for suspended-spawn cases.

This makes the three major paths more symmetric:

- host request
- host response
- script message

all now derive their suspended-spawn agent acceptance from registry-owned helpers.

### 3. Added direct registry coverage for host-request suspended-spawn resolution

New coverage asserts:

- runtime-ready suspended spawn resolves the runtime session for host requests
- runtime disconnect on a runtime-owned suspended spawn blocks host-request resolution

Why this matters:

- one more handler-local lifecycle reconstruction path is gone
- the remaining suspended-spawn interpretation in handlers is thinner and easier to audit
- this continues converging the shared path toward a cleaner `agent-owned stable spawn`
  state model without touching device route behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp17.exe`
- `build/test-bin/test_session_registry_tmp17.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp17.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp17.exe`

## Follow-up On 2026-05-22 Host Script/RPC Agent Lookup Converges Behind A Shared Helper

The next remaining duplication was in the host-side request path for:

- `SCRIPT_CREATE`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`
- `SCRIPT_POST`

Each handler still repeated the same three-step boundary logic:

1. resolve the host-owned pid
2. reject if the spawned pid is still in a pre-runtime blocked state
3. resolve the authoritative bound agent session for that pid

The behavior was already correct, but the rule lived in four separate handlers.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `ResolveHostBoundAgentForScriptOperation(...)`

This helper returns:

- resolved pid
- resolved agent session
- a small enum describing the boundary failure:
  - `kHostNotBound`
  - `kSpawnNotReady`
  - `kAgentNotReady`

It centralizes the existing ownership and readiness checks without changing error codes
or user-visible behavior.

### 2. Switched four host-side handlers to the shared helper

The following handlers now all go through the same lookup path:

- `HandleScriptPost(...)`
- `HandleScriptCreate(...)`
- `HandleScriptUnload(...)`
- `HandleRpcRequest(...)`

Each handler still maps the shared helper result to its existing protocol-specific
response shape and message text, so device behavior remains unchanged.

Why this matters:

- one more repeated host-side boundary is now implemented once instead of four times
- future child-owned tightening for host->agent request routing now has a single edit
  point instead of duplicated condition trees
- this reduces regression risk while keeping the default spawn and `--symbi` route stable

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp2.exe`
- `.\build\test-bin\test_server_handlers_spawn_ready_subset_tmp2.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp2.exe`
- `.\build\test-bin\test_session_registry_tmp2.exe`

## Follow-up On 2026-05-22 Agent-Response Host Forwarding Converges Behind A Shared Helper

After host->agent request lookup was centralized, the symmetric duplication still remained
on the agent->host response side for:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

Each handler still repeated the same boundary sequence:

1. verify the sending agent session is the currently accepted source for that pid
2. resolve the bound/suspended owner host for that pid
3. forward the frame, preserving the suspended spawn owner when relevant

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `ResolveHostForwardTargetForAgentResponse(...)`

This helper resolves:

- `pid`
- bound owner `host`
- failure class:
  - `kInvalidSource`
  - `kHostNotFound`

It reuses the existing acceptance and host-resolution rules, so this is a convergence
step, not a semantic change.

### 2. Switched three response handlers to the shared helper

The following handlers now share one forwarding boundary:

- `HandleScriptCreateResp(...)`
- `HandleScriptUnloadResp(...)`
- `HandleRpcResponse(...)`

Each still emits the same handler-specific logs on:

- non-current agent response
- missing host
- host forward send failure

Why this matters:

- both directions of the host/agent request-response boundary are now converging toward
  single edit points
- future child-owned response routing changes no longer need parallel edits across three
  handlers
- this reduces drift risk while keeping the current stable device behavior intact

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp3.exe`
- `& ".\build\test-bin\test_server_handlers_spawn_ready_subset_tmp3.exe"`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp3.exe`
- `.\build\test-bin\test_session_registry_tmp3.exe`

## Follow-up On 2026-05-22 `SCRIPT_LOAD` Now Uses The Shared Host->Agent Lookup Boundary

After converging:

- `SCRIPT_CREATE`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`
- `SCRIPT_POST`

behind `ResolveHostBoundAgentForScriptOperation(...)`, one symmetric path still remained
outside that boundary:

- `SCRIPT_LOAD`

It still manually repeated:

1. host-owned pid resolution
2. suspended-spawn readiness rejection
3. authoritative bound agent lookup

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. `HandleScriptLoad(...)` now uses `ResolveHostBoundAgentForScriptOperation(...)`

The handler now follows the same shared lookup path as the other host->agent script/RPC
operations and preserves the same response mapping:

- `kHostNotBound -> -3`
- `kSpawnNotReady -> -5`
- `kAgentNotReady -> -4`

The existing script-load round-trip state handling is unchanged:

- mark in-flight before send
- restore ready state on send failure

### 2. Added a symmetry regression for rebound host ownership

New regression:

- `TestScriptLoadForReboundHostDoesNotTargetForeignSuspendedSpawn()`

It proves that a rebound coarse pid binding does not let the wrong host send
`SCRIPT_LOAD` into a suspended spawn owned by another host, matching the earlier
`SCRIPT_CREATE` ownership coverage.

Why this matters:

- the host->agent request side is now much closer to single-boundary lookup for all
  script/RPC operations
- future child-owned tightening only needs to touch one lookup helper instead of one
  helper plus a lingering manual `SCRIPT_LOAD` path
- this further reduces drift between the script operation handlers while keeping runtime
  behavior stable

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp4.exe`
- `cmd /c build\test-bin\test_server_handlers_spawn_ready_subset_tmp4.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp4.exe`
- `.\build\test-bin\test_session_registry_tmp4.exe`

## Follow-up On 2026-05-22 Script-Load Round-Trip Uses Registry-Owned State Helpers

The next remaining shared-path seam was the script-load round-trip itself.

Even after the runtime-ready visibility cleanup:

- `HandleScriptLoad(...)` still directly wrote
  `kReadyForScriptLoad -> kScriptLoadDispatched`
- send failure restored state through another direct
  `UpdateSpawnState(..., kReadyForScriptLoad)`
- `HandleScriptLoadResp(...)` completed the round-trip through the same generic write path

That behavior was correct, but the ownership was still too coarse:

- runtime visibility was already being moved behind specific registry helpers
- script-load round-trip was still using the generic transition API directly from handlers

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added explicit registry helpers for the script-load round-trip

New helpers:

- `MarkSpawnScriptLoadInFlight(pid)`
- `MarkSpawnScriptLoadComplete(pid)`

These wrap the only valid round-trip transitions:

- `kReadyForScriptLoad -> kScriptLoadDispatched`
- `kScriptLoadDispatched -> kReadyForScriptLoad`

and still rely on the monotonic transition guard under the registry lock.

### 2. `HandleScriptLoad(...)` / `HandleScriptLoadResp(...)` now use those helpers

Handler-side behavior is unchanged:

- dispatching `SCRIPT_LOAD` marks the transaction in-flight
- agent send failure restores the ready state immediately
- accepted `SCRIPT_LOAD_RESP` restores the ready state before forwarding to the host

The important difference is that the handler no longer chooses raw transaction states
for this round-trip; it asks the registry to advance the transaction through the
script-load lifecycle explicitly.

### 3. Added focused registry coverage

`TestMarkSpawnScriptLoadInFlightAndCompleteFollowRoundTrip()` verifies:

- a runtime-visible spawn can enter `kScriptLoadDispatched`
- the same transaction can then return to `kReadyForScriptLoad`

Why this matters:

- one more spawn lifecycle boundary now has a semantic registry entrypoint instead of
  generic state assignment from handlers
- this keeps the child-owned/script-ready transaction model moving toward
  boundary-specific ownership
- it narrows the set of places that can still bypass intent and write raw spawn states

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`

## Follow-up On 2026-05-21 Versioned Embedded Zygote Helper

Real-device strict zygote-control testing later still showed inconsistent behavior:

- default spawn could hook normally
- `--strict-zygote-control` could spawn and load the script, but sometimes only installed
  late enough to catch button-triggered calls
- repeated rebuild/push/start cycles did not necessarily change the helper code already
  loaded inside `zygote64`

Root cause:

- zygote is long-lived, so a previously injected embedded helper remains mapped as a
  deleted memfd after `nook-server` rebuilds or restarts
- the injector checked for an already loaded `libnook-zygote-helper` module and reused
  that base address
- after code changes, strict zygote-control could therefore reinitialize a stale helper
  in zygote instead of injecting the newly embedded helper blob from the current
  single-file server package

Updated:

- [server/ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)
- [tests/headers/test_ninjector_zygote_remote_scratch_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_ninjector_zygote_remote_scratch_regressions.cpp)
- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp)

Behavior now guaranteed:

- embedded zygote-helper memfd names include the current helper blob identity
  (`sha256` prefix plus size)
- stale `libnook-zygote-helper` mappings in zygote are ignored when their name does not
  match the current embedded helper blob
- strict zygote-control injects the current helper through memfd from `nook-server`;
  it does not require pushing `libnook-zygote-helper.so`, `libnook-agent.so`, or
  `libncore.so` as sidecar files
- device deployment remains single-file: `/data/local/tmp/nook/nook-server`

Evidence from the successful real-device run:

- server started from `/data/local/tmp/nook/nook-server --enable-zygote-control`
- device directory contained only `nook-server`, `server.out`, and `server.err`
- startup logged the embedded helper blob identity:
  `sha256=cdf22e2ae450... source_size=958432`
- zygote injection ignored the old mapping:
  `ignore stale zygote helper base ... current_name=libnook-zygote-helper-cdf22e2ae450-958432`
- the current helper was injected by atomic memfd dlopen:
  `InjectEmbeddedSoByPidAtomic: begin pid=788 name=libnook-zygote-helper-cdf22e2ae450-958432`
- user verification after this package reported Hook normal again

Residual follow-up:

- logs still show repeated `hook_create_native_trampoline failed` retries for
  `android/app/Instrumentation.newApplication` and
  `android/app/Instrumentation.callApplicationOnCreate`
- this did not block the latest user-visible Hook success, but should be cleaned up as a
  separate retry/noise and bootstrap-hook state-machine issue

## Follow-up On 2026-05-21 Spawn-Gate Bootstrap Deferred Cleanup

After strict and default spawn recovered functionally, logcat still showed repeated:

- `InstallStaticReplacementHook: hook_create_native_trampoline failed`
- `InstallStaticReplacementHook failed: android/app/Instrumentation.newApplication`
- `InstallStaticReplacementHook failed: android/app/Instrumentation.callApplicationOnCreate`

Root cause:

- spawn-gate bootstrap hooks can be registered through `NookJavaHookHookDeferred()`
- if those bootstrap hooks never install successfully, they remain in
  `PendingJavaHookRegistry`
- when the spawn gate is later released, `NookComm` previously only reset the local
  `g_spawn_gate_*_hook_id` fields to `-1`
- deferred request ids were therefore left active, and the background deferred retry
  worker kept re-running `ProcessPendingRequests()` every 100 ms against
  `Instrumentation.newApplication` / `callApplicationOnCreate`

Updated:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- [tests/headers/test_spawn_gate_bootstrap_cleanup_regression.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_spawn_gate_bootstrap_cleanup_regression.cpp)
- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp)

Behavior now guaranteed:

- spawn-gate bootstrap hook cleanup is centralized in `ClearSpawnGateBootstrapHooks(...)`
- cleanup runs when the target process receives resume and the authoritative spawn gate is
  released
- cleanup also runs if `NookComm` resets a stale connection before reinitialization
- cleanup uses `NookJavaHookUnhook(...)`, so both installed hook ids and deferred request
  ids are removed through the same path
- `JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(false)` is also cleared at
  the same boundary, so the bootstrap-only lifecycle requirement does not leak past the
  spawn gate window

Verification in this workspace:

- `build/test-bin/test_spawn_gate_bootstrap_cleanup_regression.exe`
- `build/test-bin/test_strict_zygote_control_uses_helper_current.exe`
- `build/test-bin/test_zygote_control_connection_lifetime_regressions_current.exe`

## Follow-up On 2026-05-22 Spawn State Monotonicity Guard

Another `agent-owned` gap remained in the shared spawn transaction state path.

Problem:

- child/runtime-owned readiness can legitimately advance the transaction to:
  - `kReadyForScriptLoad`
  - `kScriptLoadDispatched`
- but `SessionRegistry::UpdateSpawnState(...)` still accepted any direct assignment
- that meant a later stale controller/shared-path write such as:
  - `kWaitingRuntimeReady`
  - or even `kWaitingAgentReady`
  could overwrite the stronger child-owned state

This is exactly the class of regression that previously showed up as:

- hook only partially installing
- spawn appearing successful but script operations seeing the pid as "not ready"
- repeated retries behaving differently depending on timing

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Spawn transaction state transitions are now monotonic

Added `CanTransitionSpawnState(...)` and enforced it in
`SessionRegistry::UpdateSpawnState(...)`.

Allowed progression:

- `kWaitingAgentReady -> kWaitingRuntimeReady`
- `kWaitingAgentReady -> kReadyForScriptLoad`
- `kWaitingRuntimeReady -> kReadyForScriptLoad`
- `kReadyForScriptLoad -> kScriptLoadDispatched`
- `kScriptLoadDispatched -> kReadyForScriptLoad`

Explicitly rejected regressions:

- `kReadyForScriptLoad -> kWaitingRuntimeReady`
- `kReadyForScriptLoad -> kWaitingAgentReady`
- `kScriptLoadDispatched -> kWaitingRuntimeReady`
- `kScriptLoadDispatched -> kWaitingAgentReady`

Why:

- once the child/runtime path has already become authoritative enough for script load,
  later shared-path writes must not drag the transaction back behind that boundary
- this keeps the transaction model aligned with `child-owned` ownership instead of
  "last writer wins"

### 2. Added direct regression coverage at the registry boundary

New regression:

- `TestUpdateSpawnStateDoesNotRegressAfterRuntimeReadyBoundary()`

It verifies:

- a runtime-ready spawn can advance to `kReadyForScriptLoad`
- stale attempts to write `kWaitingRuntimeReady` or `kWaitingAgentReady` are rejected
- the legitimate script-load loop:
  - `kReadyForScriptLoad -> kScriptLoadDispatched -> kReadyForScriptLoad`
  still works

Why this matters for the mainline:

- this is a small but important `agent-owned` tightening
- it protects default spawn and `--symbi` from one more stale-state overwrite seam
- it reduces the chance of the host/controller observing "ready already happened" while
  the shared registry has been regressed back to a waiting state

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`

## Follow-up On 2026-05-23 AGENT_READY Early-Drop Decision Fully Moves Into SessionRegistry

Another remaining `child-owned` gap was that `HandleAgentReadyEarlyDropChecks(...)`
still mixed two different responsibilities:

- deriving the early-drop reason
- executing the pid/session cleanup for that reason

The registry already owned the underlying predicates, but the handler still carried a
large branch ladder and repeated the same cleanup call-site. That kept the `AGENT_READY`
boundary wider than necessary.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. `EvaluateAgentReadyEarlyDrop(...)` now covers all current handler early-drop cases

The registry-owned decision path now returns reasons for:

- orphan spawn-token
- mismatched pending-attach
- stale attach while new attach pending
- foreign attach-like
- mismatched spawn-token
- orphan attach
- invalidated unowned
- mismatched control-stage known spawn identity

To support the last case, `AgentReadyDropContext` now also carries:

- `has_suspended_entry`
- `runtime_ready`
- `suspended_entry`

This keeps the identity mismatch rule inside the same decision boundary instead of
leaking that final special-case branch back into the handler.

### 2. `HandleAgentReadyEarlyDropChecks(...)` now uses the registry decision for all
current early-drop branches

The handler no longer directly calls:

- `ShouldDropStaleAttachAgentReady(...)`
- `ShouldDropForeignAttachLikeAgentReady(...)`
- `ShouldDropMismatchedSpawnTokenAgentReady(...)`
- `ShouldDropOrphanAttachAgentReady(...)`
- `ShouldDropInvalidatedUnownedAgentReady(...)`
- `HasKnownSpawnControlIdentityMismatchForSpawn(...)`

for the actual branch execution path.

It now:

- derives `AgentReadyDropContext`
- asks the registry for `AgentReadyEarlyDropDecision`
- logs the resulting reason
- returns

This makes the handler materially thinner and moves another piece of lifecycle authority
from shell-style branching toward a transaction/registry-owned state boundary.

### 3. Added direct registry regressions for the newly migrated reasons

New coverage now asserts that `EvaluateAgentReadyEarlyDrop(...)` returns the correct
reason for:

- mismatched spawn-token
- orphan attach
- mismatched control-stage known spawn identity

Two test semantics were also tightened while doing this:

- `foreign attach-like` must set `has_bound_host = true` so it does not collapse into the
  earlier orphan spawn-token case
- `invalidated unowned` after `ClearPendingSpawn(...)` correctly reports the reason even
  when the previous pid/session binding has already been cleared, so the expectation is
  `dropped_session == false`

Why this matters:

- all current `AGENT_READY` early-drop reasons now have a single registry-owned decision
  entrypoint
- handler code is closer to a pure route/log shell
- this is the cleanest setup for the next mainline step:
  - moving more pre-registration and registration edge behavior into registry result
    objects instead of handler-local branch execution

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp107.exe`
- `build/test-bin/test_session_registry_tmp107.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp111.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp111.exe`

## Follow-up On 2026-05-23 AGENT_READY Pre-Registration Adjustments Move Into SessionRegistry

After the early-drop migration, one more handler-owned boundary still remained before
registration:

- late control-stage drop at transaction runtime boundary
- late control-stage drop from non-current session
- mismatched runtime-trace reset before accepting control-stage ready

The predicates and actions already existed in `SessionRegistry`, but
`HandleAgentReadyPreRegistrationAdjustments(...)` still executed them inline. That kept
the pre-registration boundary wider than necessary.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned pre-registration decision/result

Introduced:

- `AgentReadyPreRegistrationAction`
- `AgentReadyPreRegistrationDecision`
- `EvaluateAgentReadyPreRegistration(...)`

The registry decision now owns the three current pre-registration outcomes:

- `kDropLateControlAtRuntimeBoundary`
- `kDropLateControlFromNonCurrentSession`
- `kResetMismatchedRuntimeTrace`

This mirrors the earlier early-drop convergence:

- handler derives context
- registry returns the authoritative action/result
- handler logs and returns

### 2. Handler no longer executes these pre-registration branches directly

`HandleAgentReadyPreRegistrationAdjustments(...)` no longer directly calls:

- `ShouldDropLateControlAgentReadyAtRuntimeBoundary(...)`
- `DropAgentReadySessionIfMatches(...)`
- `ResetMismatchedRuntimeTraceForSpawn(...)`

for the main branch execution path.

It now delegates all three decisions to `EvaluateAgentReadyPreRegistration(...)`.

### 3. Added direct registry regressions for the migrated actions

Coverage now asserts that the registry decision:

- drops late control-stage ready at the transaction runtime boundary
- drops late control-stage ready from a non-current session after runtime has already
  been recorded
- resets the mismatched runtime trace by clearing runtime-ready and forcing control-stage

Why this matters:

- both `AGENT_READY` pre-registration and early-drop behavior now have registry-owned
  decision boundaries
- `HandleAgentReady(...)` is closer to a thin shell that:
  - derives context
  - asks the registry what action to take
  - logs/routes based on the returned result
- this is the correct direction for `child-owned / agent-owned stable spawn`, because
  lifecycle authority keeps moving away from handler-local inference and toward explicit
  registry state transitions

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp109.exe`
- `build/test-bin/test_session_registry_tmp109.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp112.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp112.exe`

## Follow-up On 2026-05-23 AGENT_READY Forwarding Decision Moves Into SessionRegistry

With early-drop and pre-registration already migrated, one more meaningful shell layer
remained inside `HandleAgentReady(...)`:

- whether a runtime-stage ready may be exposed immediately
- whether it should be forwarded to the bound host
- whether it must stay held behind the spawn-response boundary
- whether a mismatched runtime ready should be dropped for the bound host

The send/replay mechanics still belong in the handler, but the branch decision itself was
still handler-local.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned forwarding decision

Introduced:

- `AgentReadyForwardAction`
- `AgentReadyForwardDecision`
- `EvaluateAgentReadyForwarding(...)`

The registry now decides between:

- `kExposeRuntimeWithoutHost`
- `kForwardRuntimeToHost`
- `kHoldRuntimeUntilSpawnResponse`
- `kDropMismatchedRuntimeForHost`
- `kHoldControlForHost`
- `kNoBoundHost`

This keeps the state/transaction predicate inside the registry while leaving the actual
host send and cached replay mechanics in the handler.

### 2. Handler forwarding now consumes the registry decision

`HandleAgentReadyForwarding(...)` no longer directly derives:

- whether `CanExposeSpawnRuntimeReadyImmediately(...)` is true
- whether runtime should be forwarded, held, or dropped

It now:

- asks the registry for `AgentReadyForwardDecision`
- performs the existing send / replay / visibility side-effects based on that result

This keeps behavior the same while shrinking another slice of handler-local lifecycle
inference.

### 3. Added direct registry coverage for core forwarding outcomes

Coverage now asserts the registry decision for:

- forward runtime to host once the runtime boundary is releasable
- hold runtime until spawn response while the boundary is still pending
- drop mismatched runtime for a bound host
- expose runtime immediately even when no host is bound

Why this matters:

- `HandleAgentReady(...)` is now thinner at all three major internal boundaries:
  - early-drop
  - pre-registration
  - forwarding
- lifecycle decisions increasingly originate from registry state instead of handler
  branch ladders
- this is directly aligned with the `child-owned / agent-owned stable spawn` goal:
  the child lifecycle becomes an explicit state machine boundary instead of a
  handler-reconstructed one

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp110.exe`
- `build/test-bin/test_session_registry_tmp110.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp113.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp113.exe`

## Follow-up On 2026-05-23 Derived AGENT_READY Context Moves Into SessionRegistry

After early-drop, pre-registration, and forwarding decisions had already moved into the
registry, `HandleAgentReady(...)` still carried one broad handler-owned block:

- assembling `DerivedAgentReadyContext`
- combining multiple registry lookups and predicates
- deciding whether the current session matched the accepted control-stage spawn session
- deriving stale-control-after-runtime from the assembled state

That context builder was no longer just “small glue”; it had become another lifecycle
inference layer inside the handler.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. `DerivedAgentReadyContext` is now a registry-owned type

The shared derived context structure was moved into `session_registry.h` so it can be
returned as a first-class registry result instead of being a handler-local helper type.

### 2. Added `SessionRegistry::DeriveAgentReadyContext(...)`

The registry now derives the full context for an incoming `AGENT_READY`, including:

- spawn context
- expected spawn process name
- bound host
- runtime/control stage
- owned zygote control target
- runtime spawn identity match
- mismatched runtime-trace flag
- runtime-already-recorded flag
- accepted current control-stage session match
- stale-control-after-runtime flag

The key point is that the handler no longer recombines these from many separate registry
queries. That inference now lives at the same boundary as the rest of the spawn/session
state machine.

### 3. Handler now consumes the derived context from the registry

`HandleAgentReady(...)` now directly calls:

- `registry->DeriveAgentReadyContext(...)`

and no longer defines its own:

- `DerivedAgentReadyContext`
- `DeriveAgentReadyContext(...)`

This is another concrete reduction in handler-owned lifecycle interpretation.

### 4. Added direct registry regressions for derived context

Coverage now asserts that the registry-derived context correctly computes:

- runtime-stage fields for a bound control-ready spawn
- stale control-stage arrival after runtime has already been recorded from another session

Why this matters:

- `HandleAgentReady(...)` has now lost another large internal helper block
- more of the spawn/agent lifecycle model is represented as registry-owned derived state
  instead of handler-composed state
- this keeps moving the design toward the intended shape:
  - handler as orchestrator
  - registry as authoritative source of child-owned lifecycle truth

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp111.exe`
- `build/test-bin/test_session_registry_tmp111.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp114.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp114.exe`

## Follow-up On 2026-05-23 Post-Finalize Spawn Context Resolution Moves Into SessionRegistry

After `HandleAgentReady(...)` was thinned substantially, the next higher-value remaining
shared inference was in `spawn_controller.cpp`.

One of the smaller but still duplicated controller-side reads was the
post-finalize context resolver that re-read suspended-spawn truth to decide:

- the effective ready stage after finalize
- the effective runtime process identity after finalize

That logic was pure interpretation of registry-owned suspended transaction state and did
not belong in the controller.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned `PostFinalizeSpawnContext`

Introduced:

- `PostFinalizeSpawnContext`
- `SessionRegistry::ResolvePostFinalizeSpawnContext(...)`

The registry now owns the interpretation of:

- fallback pending-spawn stage/name
- suspended authoritative runtime upgrade
- suspended runtime process-name resolution

### 2. `spawn_controller.cpp` now consumes the registry result

The controller no longer defines its own:

- `PostFinalizeSpawnContext`
- `ResolvePostFinalizeSpawnContext(...)`

Instead it asks the registry directly both:

- after finalize
- and after response-boundary release fallback

This removes another controller-side duplicated read of suspended-spawn lifecycle state.

### 3. Added direct registry regressions for post-finalize context

Coverage now asserts that:

- suspended runtime-ready state overrides the fallback pending-spawn stage/name
- missing suspended state falls back to the pending-spawn snapshot

Why this matters:

- controller-side suspended-spawn interpretation continues shrinking
- more of the `spawn` lifecycle is now expressed as registry-owned derived state
- this is the right direction before tackling the larger remaining controller-side
  inference block: late-promotion eligibility

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp112.exe`
- `build/test-bin/test_session_registry_tmp112.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp115.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp115.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp115.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp115.exe`

## Follow-up On 2026-05-23 Late-Promotion Eligibility And Recheck Move Into SessionRegistry

The larger remaining controller-owned inference block was
`MaybePromoteLateBoundControlReadyChild(...)`.

Before this change, the controller itself decided:

- whether a suspended child was even eligible for late promotion
- whether the transaction was still valid immediately before inject
- whether runtime had already won before inject

That was exactly the kind of shared lifecycle interpretation the project has been moving
out of controllers and handlers.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned late-promotion eligibility decision

Introduced:

- `LatePromotionEligibility`
- `LatePromotionEvaluation`
- `EvaluateLatePromotionEligibility(...)`

The registry now decides whether late promotion should be skipped because of:

- invalid args
- empty agent path
- missing suspended spawn entry
- not being in control-ready/pre-runtime state
- host/state mismatch
- missing owned host session

### 2. Added registry-owned inject-time recheck

Introduced:

- `LatePromotionRecheckResult`
- `RecheckLatePromotionBeforeInject(...)`

The registry now owns the immediate pre-inject recheck for:

- transaction missing before inject
- runtime already present before inject
- transaction changed before inject
- safe to proceed

### 3. `spawn_controller.cpp` now only orchestrates the inject action

`MaybePromoteLateBoundControlReadyChild(...)` no longer directly interprets:

- suspended spawn authoritative stage
- pre-runtime wait-state validity
- host/session ownership consistency
- runtime-ready victory before inject
- transaction drift before inject

It now:

- asks the registry for eligibility
- asks the registry for pre-inject recheck
- performs the actual inject
- updates spawn state on success

This is the biggest controller-side lifecycle inference block removed so far in the
late-promotion path.

### 4. Added direct registry regressions for late-promotion decisions

Coverage now asserts:

- control-ready owned suspended spawn is eligible
- runtime-ready suspended spawn is rejected
- host/state mismatch is rejected
- recheck detects runtime-ready-before-inject
- recheck detects transaction-changed

Why this matters:

- `spawn_controller` is now materially closer to a pure executor of registry-owned spawn
  state transitions
- the late-promotion path follows the same architectural direction already established in
  `HandleAgentReady(...)`
- this is a more meaningful convergence step toward `child-owned / agent-owned stable
  spawn` than further cosmetic cleanup would be

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp113.exe`
- `build/test-bin/test_session_registry_tmp113.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp116.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp116.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp116.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp116.exe`

## Follow-up On 2026-05-23 Spawn-Controller Cleanup Helpers Move Into SessionRegistry

After late-promotion eligibility and pre-inject recheck were moved into the registry, one
more low-risk controller-owned cluster remained: pure registry state cleanup helpers.

These helpers did not perform injector work or response routing. They only mutated
registry state for:

- timeout cleanup
- bound-spawn failure cleanup
- pending-spawn consume-or-clear behavior
- dropped successful response cleanup

That made them a good candidate to move in the same pass.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned spawn cleanup operations

Introduced:

- `CleanupTimedOutSpawnTransaction(...)`
- `CleanupFailedBoundSpawnTransaction(...)`
- `ConsumeOrClearPendingSpawn(...)`
- `CleanupDroppedSuccessfulSpawnResponse(...)`

These were moved because they are fundamentally transaction-state mutations on registry
data, not controller logic.

### 2. `spawn_controller.cpp` now delegates cleanup instead of mutating registry state inline

The controller no longer defines local cleanup helpers for those paths.

It now calls the registry directly on:

- authoritative ready timeout
- pending-spawn bind failure after ready
- finalize failure cleanup
- host-missing success-drop cleanup
- response-send-failed success-drop cleanup
- consume-or-clear branch when the host disappeared before the transaction rebound

### 3. Added direct registry regressions for cleanup behavior

Coverage now asserts that the registry cleanup operations:

- clear timed-out pending spawn and resolved pid state
- clear bound failed-spawn state and host binding
- consume pending spawn when present and clear it otherwise
- clear suspended spawn state after dropped successful response

Why this matters:

- `spawn_controller` now owns less raw registry mutation code
- the controller is closer to the intended role:
  - orchestrate injector calls
  - coordinate host replies
  - delegate transaction-state interpretation and mutation to the registry
- this keeps the architecture moving consistently toward a single source of spawn-state
  truth

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp114.exe`
- `build/test-bin/test_session_registry_tmp114.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp117.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp117.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp117.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp117.exe`

## Follow-up On 2026-05-23 Finalize-Success Bound Spawn Setup And Response-Boundary Release Move Into SessionRegistry

After moving late-promotion and cleanup helpers, one more controller-side cluster still
looked like shared transaction orchestration instead of real controller logic:

- create/update the bound suspended spawn after finalize success
- mark response-pending and enforce held runtime state
- release the spawn-response boundary and then re-resolve post-finalize runtime state

These were multi-step registry operations expressed inline in the controller. They were a
good next candidate to collapse into registry-owned actions.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry-owned finalize-success bind action

Introduced:

- `BindSuspendedSpawnAfterFinalizeResult`
- `BindSuspendedSpawnAfterFinalize(...)`

This registry action now owns the multi-step success-path setup that previously required
the controller to call:

- `MarkSpawnSuspended(...)`
- `SetSpawnResponsePending(...)`
- and sometimes `UpdateSpawnState(..., kWaitingRuntimeReady)`

The result reports whether the newly bound transaction is still waiting for runtime-ready.

### 2. Added registry-owned response-boundary release + post-finalize resolve

Introduced:

- `ReleaseSpawnResponseBoundaryAndResolvePostFinalize(...)`

This action now owns the controller’s former sequence:

- `ReleaseSpawnResponseBoundary(...)`
- if that fails, fall back to `ResolvePostFinalizeSpawnContext(...)`
- otherwise derive the runtime stage/name from the released suspended entry

So the controller no longer reassembles this post-response state itself.

### 3. `spawn_controller.cpp` success path is thinner

After finalize success, the controller now:

- asks the registry to bind/update the suspended spawn transaction
- logs if the result still waits for runtime-ready
- sends the spawn response
- asks the registry to release the response boundary and resolve post-finalize state
- optionally replays cached runtime `AGENT_READY` / `SCRIPT_MESSAGE`

That is materially closer to “controller orchestrates, registry owns transaction state”.

### 4. Added direct registry regressions for the migrated success-path actions

Coverage now asserts that:

- finalize-success binding creates a held suspended spawn transaction and reports waiting
  for runtime-ready
- response-boundary release resolves runtime-ready state to `kReadyForScriptLoad` and
  returns the expected post-finalize context

Why this matters:

- `spawn_controller` now contains less hand-written multi-step registry choreography
- success-path transaction state is increasingly represented as explicit registry-owned
  actions
- this is another concrete convergence step toward a child-owned / transaction-owned spawn
  lifecycle instead of controller-side reconstruction

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp115.exe`
- `build/test-bin/test_session_registry_tmp115.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp118.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp118.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp118.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp118.exe`

## Follow-up On 2026-05-23 Registered Host Predicate Moves Into SessionRegistry

After the larger controller-side state transitions had already moved, one small but still
shared predicate remained in `spawn_controller.cpp`:

- "is this host session still the currently registered host session?"

That check is fundamentally registry-owned state, even though the controller still decides
whether to send or drop a response.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added `IsRegisteredHostSession(...)` to the registry

The registry now owns the predicate:

- `session_id` exists in host registry
- and still maps to the same session pointer

### 2. `spawn_controller.cpp` now calls the registry for that predicate

The controller still decides what response to send or drop, but it no longer re-checks
host registration by directly reading host-session storage itself.

### 3. Added direct registry regression for the predicate

Coverage now asserts:

- current registered host pointer is accepted
- foreign pointer for the same session id is rejected
- removed host session is rejected

Why this matters:

- this is a small step, but it completes another migration of raw registry reads out of
  the controller
- at this point, most remaining controller logic is actual orchestration rather than
  lifecycle inference or direct registry interpretation

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp116.exe`
- `build/test-bin/test_session_registry_tmp116.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp119.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp119.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp119.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp119.exe`

## Follow-up On 2026-05-23 Spawn Failure Reply Skeleton Convergence

There was still one duplicated cleanup/reply pattern left in `spawn_controller`.

Problem:

- three spawn failure branches still open-coded the same broad shape:
  - best-effort `FinalizeSpawn(...)`
  - transaction cleanup
  - "only reply if host session is still registered"
- the branches differed in:
  - cleanup helper
  - error code / message
  - log tag string
- but the reply gate itself was duplicated, which kept the boundary noisy and made
  future cleanup changes easier to drift

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp)

Changes:

### 1. Added a narrow best-effort finalize helper

Introduced:

- `FinalizeSpawnBestEffort(...)`

Used only for the branches that were already intentionally ignoring finalize result:

- authoritative-ready timeout
- resolved-pending bind disappearance

This does not change behavior. It only centralizes the "try finalize and discard error"
intent.

### 2. Added a narrow failure-reply gate helper

Introduced:

- `ReplySpawnFailureIfHostPresent(...)`

This helper only owns:

- checking whether the host session is still registered
- emitting the existing drop log when the host is gone
- sending the exact provided `SpawnResponse` error payload otherwise

It does not own cleanup and does not synthesize new error codes/messages.

### 3. Preserved branch-local semantics while removing duplicated reply gates

The following branches now reuse the same reply gate:

- authoritative-ready timeout
- pending entry disappeared after ready
- finalize failure

Their existing differences remain intact:

- timeout / missing-pending still return `-4`
- finalize failure still returns `-5`
- cleanup helpers and log messages remain branch-local

### 4. Added a focused host-close finalize-failure regression

New protection test:

- `TestSpawnRequestFinalizeFailureAfterHostCloseClearsPendingSpawnState()`

It verifies:

- runtime-ready child arrives
- host session is removed while finalize is blocked
- finalize then fails
- no response is replayed to the closed host
- no suspended spawn / agent session / pending spawn / cached ready or script state remains

Why this matters:

- the controller now has one less repeated lifecycle boundary
- future spawn failure cleanup changes are less likely to drift at the
  "host still alive?" gate
- this is another small `child-owned` convergence step without touching
  device-facing behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp100.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp100.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_finalize_cleanup_focus_tmp101.exe`
- `build/test-bin/test_server_handlers_spawn_finalize_cleanup_focus_tmp101.exe`

## Follow-up On 2026-05-23 `SCRIPT_POST` Front-End Guard Converges With Other Requests

One more tiny handler shell was still sitting outside the shared request front-end path.

Problem:

- `SCRIPT_POST` still open-coded:
  - payload decode
  - null-registry early return
- while neighboring host->agent request handlers had already converged behind
  `DecodeRequestWithRegistryGuard(...)`
- that meant one more request type could drift on:
  - invalid payload handling shape
  - registry-unavailable front-end behavior

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. `SCRIPT_POST` now shares the same decode/registry guard shell

`HandleScriptPost(...)` now goes through:

- `DecodeRequestWithRegistryGuard(...)`

with intentionally no host-visible reply semantics preserved:

- invalid `SCRIPT_POST` still only logs and returns
- missing registry still only logs and returns

So this is a shell convergence only, not a protocol behavior change.

### 2. The lookup/forward tail remains unchanged

After the shared front-end guard:

- host-bound pid/agent lookup still uses `ResolveHostBoundAgentForScriptOperation(...)`
- no-response forwarding still uses `ForwardHostBoundAgentRequestNoResponse(...)`

So runtime/control fallback behavior and no-reply semantics are untouched.

Why this matters:

- one fewer request type is maintaining its own decode/registry shell
- future front-end guard changes for host->agent operations have one less straggler
- this keeps the low-risk child-owned/agent-owned cleanup track moving without touching
  route behavior or real-device semantics

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp101.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp101.exe`

## Follow-up On 2026-05-23 `DETACH` / `RESUME` Front-End Guard Converges

Two more request handlers still had a hand-written decode + registry-unavailable shell.

Problem:

- `HandleDetachRequest(...)`
- `HandleResumeRequest(...)`

were still open-coding:

- request decode
- invalid-payload immediate response
- null-registry immediate response

while nearby handlers had already converged behind:

- `DecodeRequestWithRegistryGuard(...)`

That was not a known behavior bug, but it was still another place where front-end
error handling shape could drift.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_detach_resume_focus.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_detach_resume_focus.cpp)

Changes:

### 1. `DETACH` now uses the shared front-end guard

Only the front-end moved:

- invalid payload still returns detach error `-1`
- missing registry still returns detach error `-2`

The real detach semantics remain unchanged:

- owner resolution
- gate-held rejection
- unbind on success

### 2. `RESUME` now uses the same shared front-end guard

Again, only the front-end changed:

- invalid payload still returns resume error `-1`
- missing registry still returns resume error `-2`

The real resume-state branches are unchanged:

- suspended-entry lookup
- foreign-owner rejection
- authoritative-ready / blocked checks
- release callback failure handling
- success cleanup

### 3. Added a focused detach/resume regression runner

Added:

- `test_server_handlers_detach_resume_focus.cpp`

This gives a fast verification target for:

- detach owner/gate-held behavior
- resume unknown/not-ready/non-owner/release-failure/success behavior

without relying on the full `test_server_handlers.cpp` monolith.

Why this matters:

- two more handlers now share the same request front-end contract
- future request-side guard changes have fewer hand-written stragglers
- the remaining local convergence work is now down to lower-value polish rather than
  meaningful lifecycle drift risks

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_detach_resume_focus.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_detach_resume_focus_tmp3.exe`
- `build/test-bin/test_server_handlers_detach_resume_focus_tmp3.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp102.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp102.exe`

## Follow-up On 2026-05-23 AGENT_READY Registration Plan Now Owns Pending-Spawn Resolve

This pass moved back onto the actual child-owned / agent-owned mainline instead of more
handler shell cleanup.

Problem:

- `PlanAgentReadyRegistration(...)` already decided whether a given `AGENT_READY` was
  eligible to resolve its pending spawn token
- but the actual `ResolvePendingSpawn(...)` call still lived in `HandleAgentReady(...)`
- that left the shared path split awkwardly:
  - registry owned the registration plan
  - handler still owned one more transaction-state mutation after applying that plan

That was exactly the kind of residual split the earlier convergence work was trying to
eliminate.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. `ApplyAgentReadyRegistrationPlan(...)` now resolves pending spawn itself

Added to `AgentReadyRegistrationResult`:

- `resolved_pending_spawn`

And `ApplyAgentReadyRegistrationPlan(...)` now:

- calls `ResolvePendingSpawn(...)` when the plan says it should
- reports whether that resolve actually succeeded

So the registry-owned registration phase now owns:

- global/control registration
- pending-attach clear
- suspended authoritative-ready upgrade
- pending-spawn resolve

### 2. Handler no longer performs a separate resolve step

`HandleAgentReady(...)` no longer calls its former local helper to resolve the token.

Instead it now:

- checks `registration_result.resolved_pending_spawn`
- only performs the existing host-bind/log tail if the registry actually resolved the token

This keeps host-binding/logging in the handler for now, but moves the actual transaction
state mutation into the registry-owned registration boundary.

### 3. Added direct regression coverage for the stronger registration boundary

`TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReady()` now also proves:

- matching runtime-ready registration resolves the pending spawn token
- the pending entry records:
  - `ready = true`
  - authoritative pid
  - runtime-ready stage
  - resolved runtime process name

Why this matters:

- this is a real mainline `child-owned` step, not just shell cleanup
- one more spawn-transaction mutation is now inside the registry-owned registration phase
- the handler is closer to:
  - derive context
  - ask registry for plan
  - apply registry plan
  - do owner-host side effects

which is materially closer to a true agent-owned stable-spawn model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp88.exe`
- `build/test-bin/test_session_registry_tmp88.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp103.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp103.exe`

## Follow-up On 2026-05-23 AGENT_READY Registration Boundary Now Also Owns Resolved-Pending-Spawn Bind

The next mainline step was to remove one more real transaction mutation from the handler.

Problem:

- after the previous step, `ApplyAgentReadyRegistrationPlan(...)` already owned:
  - pending-spawn resolve
- but `HandleAgentReady(...)` still performed the next stateful step itself:
  - `BindHostToResolvedPendingSpawn(...)`

That meant the registration boundary was still split:

- registry resolved the token
- handler rebound host ownership and suspended transaction state

For the child-owned / agent-owned direction, that was still too much transaction mutation
living outside the registry-owned registration phase.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Registration result now reports whether the resolved pending spawn was also bound

Added to `AgentReadyRegistrationResult`:

- `bound_host_to_resolved_pending_spawn`

So the registry registration boundary now reports both:

- token resolved
- host/suspended transaction rebound successfully

### 2. `ApplyAgentReadyRegistrationPlan(...)` now performs the bind itself

When:

- the plan resolves the pending spawn token successfully

it now immediately attempts:

- `BindHostToResolvedPendingSpawn(ready_token, pid, nullptr)`

That moves another real state transition into the registry-owned registration phase:

- host->pid ownership rebound
- suspended spawn transaction reattachment to that host

### 3. Handler now only logs the bind outcome

`HandleAgentReady(...)` no longer performs the bind.

It now only:

- checks `registration_result.bound_host_to_resolved_pending_spawn`
- logs either:
  - resolved + bound
  - resolved but bind skipped because the entry was cleared/mismatched

So the handler has lost one more real transaction mutation and is closer to
registry-plan execution + host-visible side effects only.

### 4. Added stronger registry assertions for the matching runtime-ready path

`TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReady()` now also proves:

- the host is rebound to the resolved pid
- `FindHostSessionByPid(pid)` points at the correct owner host

Why this matters:

- this is another actual mainline `child-owned` step
- one more authoritative spawn-transaction mutation now happens inside the registry-owned
  AGENT_READY registration boundary
- the handler is thinner in exactly the right place: token/host/suspended transaction
  ownership no longer needs a second imperative step there

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp90.exe`
- `build/test-bin/test_session_registry_tmp90.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp104.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp104.exe`

## Follow-up On 2026-05-23 AGENT_READY Registration Boundary Now Also Owns Previous-Pid Cleanup

The next mainline mutation still sitting in the handler was the "same session moved from an
old pid to the authoritative runtime pid" cleanup.

Problem:

- in the matching runtime-ready path, `HandleAgentReady(...)` still did:
  - `RemoveAgentSessionByPidIfMatches(previous_pid, &session)`
  before applying the registration plan
- that was another real state mutation outside the registry-owned registration boundary
- it belonged to the same transition as:
  - runtime registration
  - pending-spawn resolve
  - resolved-pending-spawn bind

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. `ApplyAgentReadyRegistrationPlan(...)` now accepts `previous_pid`

The registration application boundary now receives:

- `previous_pid`
- `pid`
- the existing registration plan and identity inputs

That lets the registry own the "same session moved from old pid to new authoritative pid"
cleanup at the same moment it performs runtime registration.

### 2. Matching runtime-ready registration now clears the superseded pid itself

When:

- `previous_pid > 0`
- `previous_pid != pid`
- and the plan is a matching runtime-ready registration

the registry now performs:

- `RemoveAgentSessionByPidIfMatches(previous_pid, session)`

and reports it through:

- `AgentReadyRegistrationResult.removed_previous_pid_session`

### 3. Handler no longer performs that previous-pid cleanup inline

`HandleAgentReadyPreRegistrationAdjustments(...)` no longer removes the old pid binding for
the matching runtime-ready path.

So the registry-owned AGENT_READY registration boundary now owns all of these linked state
mutations together:

- previous-pid cleanup
- runtime/control registration
- pending-attach clear
- authoritative-ready upgrade
- pending-spawn resolve
- resolved-pending-spawn bind

### 4. Added direct registry regression coverage

New coverage proves:

- a session previously bound at pid `14569`
- moving to matching runtime-ready pid `14570`

results in:

- old pid binding removed
- new pid registered
- process-name binding follows the new pid/session

Why this matters:

- this is another concrete mainline `child-owned` / `agent-owned` step
- the handler lost one more real transaction mutation
- the registry registration boundary is now much closer to the actual ownership transfer
  point for AGENT_READY

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp93.exe`
- `build/test-bin/test_session_registry_tmp93.exe`

## Follow-up On 2026-05-23 AGENT_READY Pre-Registration Runtime-Trace Reset Uses A Registry Action

The next pre-registration mutation still living inline in the handler was the reset of a
stale runtime trace before accepting a control-stage `AGENT_READY`.

Problem:

- when a control-stage `AGENT_READY` arrived after a mismatched runtime trace had been
  recorded, `HandleAgentReadyPreRegistrationAdjustments(...)` still performed two direct
  registry mutations itself:
  - `ClearAgentRuntimeReadyState(pid)`
  - `ForceAgentReadyStage(pid, kControl)`
- the decision was already derived from registry-owned predicates, but the mutation still
  lived as an inline two-step sequence in the handler

That was another small but real gap in the child-owned / agent-owned direction: the
handler still knew too much about how to reset that runtime/control boundary.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added a dedicated registry action for the reset

Added:

- `ResetMismatchedRuntimeTraceForSpawn(int pid)`

It now owns the exact reset sequence:

- clear runtime-ready bit / cached runtime ready frame
- force ready stage back to `kControl`

### 2. Handler now performs the decision, but not the multi-step mutation

`HandleAgentReadyPreRegistrationAdjustments(...)` still decides when the reset should
happen, but it now calls:

- `ResetMismatchedRuntimeTraceForSpawn(pid)`

instead of hand-writing the two underlying registry mutations itself.

This is intentionally another incremental mainline step:

- decision still in handler
- concrete reset mutation now behind a registry-owned action boundary

### 3. Added direct registry regression coverage

New regression proves:

- a pid marked runtime-ready and staged as runtime
- after `ResetMismatchedRuntimeTraceForSpawn(pid)`

ends up with:

- runtime-ready cleared
- authoritative ready stage forced back to control

Why this matters:

- this is another real mainline convergence step, not shell cleanup
- the handler has one less hand-written multi-step registry mutation
- it keeps pushing the runtime/control ownership transition toward explicit registry
  actions, which is exactly the shape needed for a cleaner agent-owned stable-spawn model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp95.exe`
- `build/test-bin/test_session_registry_tmp95.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp106.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp106.exe`

## Follow-up On 2026-05-23 Late-Control AGENT_READY Drop Cleanup Uses A Registry Action

The next remaining real mutation in AGENT_READY pre-registration was the session cleanup on
late control-stage drops.

Problem:

- these two pre-registration branches still did a direct inline cleanup:
  - late control-stage drop at transaction runtime boundary
  - late control-stage drop from a non-current session after runtime was already recorded
- both branches still hand-called:
  - `RemoveAgentSessionByPidIfMatches(pid, session)`

That meant the handler still owned the concrete cleanup action for this late-control drop
class, even though the decision was already registry-derived.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added a dedicated registry action for AGENT_READY drop cleanup

Added:

- `DropAgentReadySessionIfMatches(int pid, Session* session)`

For now this is a thin action wrapper over the existing removal semantics, but it names the
intent explicitly:

- this session is being dropped as part of AGENT_READY handling

### 2. Late-control drop branches now call the registry action

`HandleAgentReadyPreRegistrationAdjustments(...)` now uses:

- `DropAgentReadySessionIfMatches(...)`

for:

- transaction-runtime-boundary late control drops
- non-current-session late control drops

So the handler keeps the decision and logging, but no longer names the low-level cleanup
primitive directly for this drop class.

### 3. Added direct registry regression coverage for the new action

New coverage proves the action matches the prior intended semantics:

- runtime session removed
- control fallback survives
- runtime-ready bit is cleared
- effective stage falls back to control

Why this matters:

- this is another incremental mainline step, not shell cleanup
- the handler now speaks more in registry actions and less in low-level mutation calls
- it keeps pushing AGENT_READY pre-registration toward:
  - registry decides
  - registry mutates
  - handler logs / routes

which is the right direction for a cleaner child-owned / agent-owned stable-spawn model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp97.exe`
- `build/test-bin/test_session_registry_tmp97.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp107.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp107.exe`

## Follow-up On 2026-05-23 AGENT_READY Early-Drop Starts Moving From Handler Branches To Registry Results

The next mainline step was to move beyond action naming and start centralizing actual early-drop
branch execution behind a registry-returned result.

Problem:

- even after cleanup was unified behind `DropAgentReadySessionIfMatches(...)`, the handler
  still open-coded each early-drop branch as:
  - recompute local boolean
  - call registry cleanup action
  - log
  - return
- that still left the early-drop control flow split across many handler branches

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added explicit early-drop reason/result types in the registry

Added:

- `AgentReadyEarlyDropReason`
- `AgentReadyEarlyDropDecision`
- `EvaluateAgentReadyEarlyDrop(...)`

For now this registry entry point only owns the first two clearest cases:

- orphan spawn-token ready
- mismatched pending-attach ready

and it returns:

- which drop reason matched
- whether the agent session cleanup action ran

### 2. Handler now consumes the registry result for those first two branches

`HandleAgentReadyEarlyDropChecks(...)` now:

- builds `AgentReadyDropContext`
- asks the registry for `EvaluateAgentReadyEarlyDrop(...)`
- logs/returns based on the returned reason

for the first two early-drop branches, instead of recomputing them inline.

The remaining early-drop branches still use the prior style for now. This was intentionally
done in a narrow slice first to keep the behavior change surface small.

### 3. Added direct registry regressions for the two moved cases

New coverage proves the registry result/action path for:

- orphan spawn-token ready
- mismatched pending-attach ready

including that the session is actually dropped in both cases.

Why this matters:

- this is the first step from "shared cleanup action" to "registry-owned early-drop result"
- the handler has started losing real early-drop branch execution, not just mutation names
- it establishes the pattern for migrating the remaining early-drop cases in the same way

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp102.exe`
- `build/test-bin/test_session_registry_tmp102.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp109.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp109.exe`

## Follow-up On 2026-05-23 Host Lifecycle Cleanup Skeleton Convergence

This pass stayed strictly local to registry cleanup structure. It did not change:

- spawn algorithm selection
- zygote-control semantics
- injector behavior
- device-facing deployment

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Problem:

- `RemoveHostSession(...)`, `BindHostToPid(...)`, and `UnbindHostSession(...)` each carried their own handwritten cleanup loops for:
  - collecting host-owned pid bindings
  - clearing host-owned pending spawns
  - clearing host-owned pending attaches
  - erasing suspended/script-cache/agent-ready state
  - invalidating detached old agent pids
- the three paths were intentionally similar but not identical, which made future cleanup risky because the real invariants were buried in repetitive loop bodies

Changes:

### 1. Extracted host-bound pid collection helper

Added `CollectHostBoundPidsLocked(...)` to centralize:

- scanning `pid_to_host_session_`
- preserving one pid for rebind paths
- erasing the collected mappings in one place

This is now shared by:

- `RemoveHostSession(...)`
- `BindHostToPid(...)`
- `UnbindHostSession(...)`

### 2. Extracted pending-spawn / pending-attach host cleanup helpers

Added:

- `CollectResolvedPendingSpawnPidsForHostLocked(...)`
- `ClearPendingAttachesForHostLocked(...)`

This preserves the existing semantics:

- resolved pending spawn pids are collected before the spawn entries are erased
- host-owned pending attach entries still clear any associated timeout pid while being removed

### 3. Extracted pid-state cleanup helpers while preserving path differences

Added:

- `ClearOwnedHostPidStateLocked(...)`
- `ClearResolvedPendingSpawnPidStateLocked(...)`

Important preserved distinction:

- `RemoveHostSession(...)` still clears attach-timeout state for directly owned bound pids
- `BindHostToPid(...)` and `UnbindHostSession(...)` still do not clear attach-timeout state for ordinary released/rebound pids
- resolved pending spawn pid cleanup still clears attach-timeout state in all paths

That means this was only a structural convergence, not a semantics change.

### 4. Tightened regression coverage for invalidated pids

Existing host-lifecycle tests already covered most cleanup effects, but two paths were still not explicitly asserting pid invalidation on old suspended owners.

Added assertions to confirm:

- `BindHostToPid(...)` invalidates the old suspended pid it rebinds away from
- `UnbindHostSession(...)` invalidates the old suspended pid it releases

Why this matters:

- it locks down an already-existing lifecycle invariant before more cleanup work happens
- it reduces the chance of a future "looks equivalent" refactor silently dropping invalidation bookkeeping

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp79.exe`
- `build/test-bin/test_session_registry_tmp79.exe`

Note:

- this workspace still shows the same PowerShell/file-visibility nuisance seen earlier: a compile may return `0` before an immediate parallel `Get-ChildItem` sees the fresh exe
- reliable pattern remains:
  - compile to a fresh filename
  - confirm separately
  - run separately

## Follow-up On 2026-05-23 Agent Session Cleanup Skeleton Convergence

This pass also stayed inside local registry structure. No device-facing spawn behavior changed.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Problem:

- `RemoveAgentSessionByPid(...)` still carried a handwritten full-agent cleanup block
- `RemoveAgentSessionByPidIfMatches(...)` also carried a second handwritten variant for the non-rebound terminal cleanup path
- both paths were clearing largely the same pid-scoped structures:
  - agent/control sessions
  - readiness state
  - process-name bindings
  - owned zygote-control pid records
  - optional attach/script/spawn side state

Changes:

### 1. Extracted process-name binding cleanup

Added `ClearAgentProcessNameBindingLocked(...)` to centralize removal of:

- `agent_pid_to_process_name_`
- `agent_process_name_to_pid_`

This removes another repeated block of mirrored map cleanup.

### 2. Extracted agent-session state cleanup skeleton

Added `ClearAgentSessionStateLocked(...)` with two explicit knobs:

- `clear_attach_side_state`
- `clear_script_and_spawn_state`

This helper now owns the shared cleanup for:

- `agent_sessions_`
- `agent_control_sessions_`
- `agent_control_process_names_`
- `agent_session_history_`
- `agent_authoritative_ready_`
- `agent_ready_stages_`
- `agent_runtime_ready_`
- `agent_ready_frames_`
- process-name binding cleanup
- owned zygote-control pid cleanup

The existing callers keep their original differences:

- `RemoveAgentSessionByPid(...)` still clears attach side state and script/spawn cache state
- `RemoveAgentSessionByPidIfMatches(...)` terminal non-rebound cleanup still skips attach-side clearing there, just as before
- rebound handling in `RemoveAgentSessionByPidIfMatches(...)` was left intact and was not folded into the helper

So again this was structural convergence, not a behavior change.

### 3. Added regression for direct agent removal semantics

Added coverage to assert `RemoveAgentSessionByPid(...)`:

- clears suspended spawn state
- clears cached script messages
- does not mark the pid as invalidated

Why this matters:

- host-lifecycle cleanup does invalidate orphaned child pids
- direct agent-session removal does not mean the same thing semantically
- the new test locks that distinction down before further cleanup work

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp81.exe`
- `build/test-bin/test_session_registry_tmp81.exe`

## Follow-up On 2026-05-23 Resolved Pending-Spawn Rebind Cleanup Alignment

This pass started as another structural convergence check in the pending-spawn bind path, but it exposed one real cleanup inconsistency.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Problem:

- `BindHostToResolvedPendingSpawn(...)` rebinds a host from an old suspended pid to the newly resolved pid
- while it already cleared:
  - old suspended spawn state
  - old agent state
  - old cached script messages
- it did **not** mark the old pid in `invalidated_agent_pids_`

That made this path inconsistent with the already-hardened host lifecycle cleanup paths:

- `BindHostToPid(...)`
- `UnbindHostSession(...)`
- `RemoveHostSession(...)`

Those paths already treat "old suspended child ownership was explicitly discarded" as an invalidation event.

Changes:

### 1. Added regression for resolved rebind invalidation

Extended the existing resolved rebind regression to assert:

- old suspended pid is invalidated after `BindHostToResolvedPendingSpawn(...)`

The test failed first, confirming the gap.

### 2. Fixed the cleanup semantics

Updated `BindHostToResolvedPendingSpawn(...)` so that old suspended owned pids are invalidated when they are displaced by the newly resolved pid.

### 3. Folded the path into the shared host-owned cleanup helper

After the semantic fix, the old handwritten cleanup loop in
`BindHostToResolvedPendingSpawn(...)` was replaced with:

- `CollectHostBoundPidsLocked(...)`
- `ClearOwnedHostPidStateLocked(...)`

This removes another cleanup fork and makes future invalidation regressions less likely.

Why this matters:

- it tightens pid-lifecycle coherence across all host-rebind paths
- it reduces one more special-case divergence in registry-owned child cleanup
- it is directly relevant to the longer `child-owned` convergence effort, because stale old child pids are now retired consistently regardless of which host-rebind path displaced them

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp84.exe`
- `build/test-bin/test_session_registry_tmp84.exe`

## Follow-up On 2026-05-23 Clear-Pending-Spawn Orphan Cleanup Convergence

This pass stayed on the same registry-only pending-spawn boundary and did not reveal a new behavior bug.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

What was checked:

- `ClearPendingSpawn(...)` has two intended modes once a pending spawn resolves to a pid:
  - if that pid has **not** been bound back to host-owned suspended context, clear it as an orphan and invalidate it
  - if that pid **has** already been rebound into host-owned suspended context, only drop the pending-spawn record and preserve the child/session state

The second behavior was already implemented, but it was not explicitly locked by a test.

Changes:

### 1. Added a regression that preserves already-bound resolved children

New coverage asserts that after:

- `ResolvePendingSpawn(...)`
- `BindHostToResolvedPendingSpawn(...)`
- `ClearPendingSpawn(...)`

the resolved child remains:

- host-bound
- agent-session-backed
- runtime-ready
- not invalidated
- still represented by its suspended spawn entry

This test passed immediately, confirming the current behavior.

### 2. Folded orphan cleanup into the shared helper

The orphan-cleanup branch in `ClearPendingSpawn(...)` previously had its own handwritten block clearing:

- agent state
- suspended state
- cached script messages
- attach-timeout state
- invalidation

That block now reuses:

- `ClearResolvedPendingSpawnPidStateLocked(...)`

Why this matters:

- resolved orphan cleanup now uses the same pid-retirement skeleton as the other resolved-pending-spawn cleanup sites
- fewer handwritten variants means fewer chances to drift on invalidation or timeout clearing semantics

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp86.exe`
- `build/test-bin/test_session_registry_tmp86.exe`

## Follow-up On 2026-05-23 Spawn Controller Failure-Path Cleanup Convergence

This pass moved one layer outward from `session_registry` into `spawn_controller.cpp`, but still stayed on cleanup structure only. No route selection or device-facing spawn semantics changed.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Problem:

`ExecuteSpawnRequest(...)` still had multiple handwritten cleanup branches for closely related failure paths:

- authoritative-ready timeout
- ready observed but bind-to-host failed
- finalize failure after authoritative pid resolution
- consumed-pending fallback when host registration had already disappeared

Those branches all manipulated the same registry-owned lifecycle pieces:

- pending spawn token state
- bound/suspended spawn transaction state
- authoritative pid-owned agent state
- optional host binding teardown

Changes:

### 1. Added small regression assertions on existing spawn failure tests

Tightened existing coverage to assert:

- timeout path does not leave an agent session for the authoritative pid
- timeout path does not spuriously mark the pid invalidated when no authoritative child state was bound
- replay-send-failure cleanup leaves no residual pending spawn token

These all passed with the current implementation.

### 2. Extracted timeout cleanup helper

Added `CleanupTimedOutSpawnTransaction(...)` to centralize:

- `TakePendingSpawn(...)` vs `ClearPendingSpawn(...)`
- clearing the resolved pid transaction if one had already been recorded
- clearing the separately tracked `authoritative_pid` when present

### 3. Extracted bound-spawn failure cleanup helper

Added `CleanupFailedBoundSpawnTransaction(...)` to centralize the shared bound-child failure skeleton:

- `RemoveAgentSessionByPid(...)`
- optional `UnbindHostSession(...)`
- `ClearSpawnTransactionByPid(...)`
- `ClearPendingSpawn(...)`

This is now used by:

- bind-after-ready failure
- finalize failure after authoritative pid resolution

The only retained difference is whether host unbind is requested.

### 4. Extracted pending-consume fallback helper

Added `ConsumeOrClearPendingSpawn(...)` for the host-missing branch where:

- the code wants to consume the pending spawn if it still exists
- otherwise fall back to `ClearPendingSpawn(...)`

This removes another small handwritten token-cleanup fork.

Why this matters:

- the spawn controller failure paths now describe their intent more clearly instead of restating registry cleanup steps inline
- future changes to token/transaction cleanup are less likely to update one branch and miss another
- this is another prerequisite step before touching higher-risk `child-owned`/agent-owned convergence, because controller failure cleanup is now less fragmented

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp91.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp91.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_finalize_cleanup_focus_tmp91.exe`
- `build/test-bin/test_server_handlers_spawn_finalize_cleanup_focus_tmp91.exe`

## Follow-up On 2026-05-23 Script-Load Completion Hook Convergence

This pass moved into `server_handlers.cpp`, but still stayed on a small structural seam around spawn-state restoration after script-load transitions.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Problem:

- `MarkSpawnScriptLoadComplete(...)` was still being called from multiple script-load completion edges
- one of those edges (`HandleScriptLoadResp(...)`) had duplicated identical lambdas for:
  - invalid source cleanup
  - missing host cleanup

Changes:

### 1. Added a tiny local helper for non-template completion sites

Added `CompleteSpawnScriptLoadIfPresent(...)` to centralize:

- null registry guard
- non-positive pid guard
- `MarkSpawnScriptLoadComplete(pid)`

### 2. Applied it to the duplicated response cleanup hooks

`HandleScriptLoadResp(...)` now uses that helper for both:

- invalid-source cleanup callback
- host-missing cleanup callback

Note:

- I intentionally did **not** force this helper into the earlier templated script-load request forwarding path in the same pass
- that location triggered the usual non-dependent template name lookup nuisance in this file, which is not worth broadening right now for a trivial cleanup
- so the template path was left in its prior inline form and only the non-template duplicated response hooks were converged

This keeps the change low-risk while still reducing one repeated spawn-state restoration branch.

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp92.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp92.exe`

## Follow-up On 2026-05-23 Host-Bound Request Handler Convergence

This pass grouped three neighboring request handlers in `server_handlers.cpp` instead of continuing one-by-one cleanup.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Scope:

- `HandleScriptCreate(...)`
- `HandleScriptUnload(...)`
- `HandleRpcRequest(...)`

Problem:

All three handlers still repeated the same two-stage structure:

1. decode request with registry guard
2. forward host-bound request with lookup-based immediate error mapping

The only real differences were:

- decode function
- operation name
- invalid-request response
- registry-unavailable response
- lookup/send failure response

Changes:

### 1. Added a new protection test for script-create spawn-not-ready behavior

The create/unload/rpc trio already had broad immediate-error coverage, but `script create`
was missing the explicit spawn-not-ready assertion that the other request types already had.

Added:

- `TestScriptCreateSpawnNotReadyReturnsImmediateHostError()`

This locks in the expected immediate error:

- code `-5`
- message `spawned pid is not ready for script create`

The new test passed before the refactor.

### 2. Extracted shared decode+forward wrapper for host-bound request handlers

Added:

- `DecodeAndForwardHostBoundScriptOperationRequest(...)`

It centralizes the common flow:

- `DecodeRequestWithRegistryGuard(...)`
- `ForwardHostBoundScriptOperationRequestWithLookup(...)`

while still taking the per-handler responders as callbacks.

### 3. Switched three handlers onto the shared wrapper

Rewired:

- `HandleScriptCreate(...)`
- `HandleScriptUnload(...)`
- `HandleRpcRequest(...)`

Behavior intentionally preserved:

- same decode functions
- same log labels
- same immediate error codes/messages
- same spawn-not-ready behavior
- same send-failure behavior

Why this matters:

- one more repeated request-handling skeleton is now centralized
- this makes future changes to host-bound immediate-error flow less likely to diverge across script/rpc entry points
- it also clears more local noise before later work on higher-value spawn ownership boundaries

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp94.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp94.exe`

## Follow-up On 2026-05-23 Agent Response Forwarding Wrapper Convergence

This pass continued the same `server_handlers.cpp` cleanup thread and focused on the response side of the same local area.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Generalized the no-hooks response forwarder into a hook-capable wrapper

Added:

- `DecodeAndForwardAgentResponseWithHooks(...)`

and kept:

- `DecodeAndForwardAgentResponseNoHooks(...)`

as the zero-hook convenience wrapper on top.

This means the shared response path now has one common entry point for:

- decode
- invalid-payload drop
- current-session / host-bound forwarding
- optional invalid-source / host-missing / pre-forward side-effects

### 2. Rebased `HandleScriptLoadResp(...)` onto the shared hook-capable wrapper

Before:

- `HandleScriptLoadResp(...)` manually decoded and then called
  `ForwardAgentResponseToBoundHost(...)` directly

After:

- it now goes through `DecodeAndForwardAgentResponseWithHooks(...)`
- while still preserving its special spawn-state restoration hooks through
  `CompleteSpawnScriptLoadIfPresent(...)`

Why this matters:

- `script load resp` was the last nearby response handler still bypassing the shared decode-forward wrapper family
- now the four neighboring response handlers fit a cleaner shape:
  - `script create resp` -> shared wrapper, no hooks
  - `script load resp` -> shared wrapper, hooks
  - `script unload resp` -> shared wrapper, no hooks
  - `rpc response` -> shared wrapper, no hooks

This reduces one more local special case before any higher-risk work on ownership semantics.

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp95.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp95.exe`

## Follow-up On 2026-05-23 Attach Completion Cleanup Convergence

This pass stayed inside `HandleAttachRequest(...)` and only converged the local registry cleanup endings.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added local helpers for attach completion cleanup

Added:

- `CleanupFailedAttachAttempt(...)`
- `FinalizeSuccessfulAttach(...)`

These centralize the shared attach-side registry endings:

- clear pending attach token
- mark/clear attach timeout pid
- optionally unbind the host session

### 2. Switched the authoritative agent-ready timeout failure path to the helper

The attach path that reaches:

- injector launched
- pending attach token registered
- runtime-ready wait failed

now uses `CleanupFailedAttachAttempt(...)` instead of re-stating:

- `ClearPendingAttach(...)`
- `MarkAttachTimeoutPid(...)`
- `UnbindHostSession(...)`

### 3. Switched the success tail to the helper

The attach success path now uses `FinalizeSuccessfulAttach(...)` instead of open-coding:

- `ClearPendingAttach(...)`
- `ClearAttachTimeoutPid(...)`

Intentional non-change:

- the earlier immediate `injector == nullptr` failure path was left alone
- that branch never registers a pending attach token, so forcing it through the same helper would blur a meaningful local distinction

Why this matters:

- attach cleanup now has less handwritten duplication at the two most important completion boundaries
- this preserves the earlier design goal of converging lifecycle cleanup without flattening away real control-flow distinctions

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp96.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp96.exe`

## Follow-up On 2026-05-23 Resume Completion Cleanup Convergence

This pass inspected the neighboring `script post` / `detach` / `resume` cluster.

What stuck:

- a small success-tail helper for `resume`

What was intentionally not forced through:

- a template helper for `script post` lookup/forwarding

Reason:

- that helper immediately ran into the same non-dependent template name lookup nuisance already seen elsewhere in this file
- broadening the template reshuffle for such a small local benefit is not worth the risk right now

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `FinalizeSuccessfulResume(...)`

This helper centralizes the normal success tail for `HandleResumeRequest(...)`:

- `ClearSpawnSuspended(pid)`
- `ClearScriptMessageFrames(pid)`

and keeps the success handler itself shorter and more explicit.

### 2. Left `script post` behavior unchanged after inspection

I attempted to converge its lookup path through a helper, but backed that out after hitting the template visibility trap.

The important point is:

- no behavior change was left behind
- only the safe `resume` tail convergence remains in the tree

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp97.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp97.exe`

## Follow-up On 2026-05-23 Successful Spawn Response Drop Cleanup Convergence

This pass returned to the `spawn_controller` success tail and converged two adjacent drop-cleanup sites.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)

Problem:

After a successful spawn transaction had already reached the response phase, there were still two separate handwritten cleanup edges that both meant:

- "we cannot actually deliver the successful spawn response to the host, so clear the held spawn transaction state"

Those two edges were:

- host session disappeared before sending the success response
- success response send itself failed

Changes:

### 1. Added `CleanupDroppedSuccessfulSpawnResponse(...)`

This helper centralizes the remaining local success-tail cleanup:

- `ClearSpawnTransactionByPid(authoritative_pid)`

with the same guards as the previous inline branches.

### 2. Switched both success-drop edges onto the helper

Rewired:

- `drop spawn success response ... reason=host-missing`
- `drop spawn success replay ... reason=response-send-failed`

Behavior preserved:

- same logs
- same early returns
- same transaction cleanup

Why this matters:

- the spawn controller now has less "same cleanup, different log branch" duplication at the success boundary
- this is small, but it continues the same strategy of shrinking lifecycle forks before touching any higher-risk ownership work

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp98.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp98.exe`

## Follow-up On 2026-05-23 Host<->Agent Script/Rpc Forwarding Boundaries Converge Further

The next low-risk `child-owned` step was to shrink one more chunk of duplicated
host/agent forwarding logic without touching injection routes or device packaging.

Two duplicated areas remained in `server/server_handlers.cpp`:

- `SCRIPT_LOAD` still had its own manual host->agent request forwarding path
  because it also had to:
  - mark the suspended spawn as `kScriptLoadDispatched`
  - restore the suspended state on send failure
- the response side still repeated the same owner-host resolution and forward/drop
  flow for:
  - `SCRIPT_CREATE_RESP`
  - `SCRIPT_LOAD_RESP`
  - `SCRIPT_UNLOAD_RESP`
  - `RPC_RESPONSE`

That duplication was not yet causing a known device regression, but it was still the
kind of split lifecycle logic that tends to drift and reintroduce inconsistent spawn
semantics later.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. `SCRIPT_LOAD` request forwarding now lives behind a dedicated shared helper

Added `ForwardHostBoundScriptLoadRequest(...)`.

It preserves the exact existing `SCRIPT_LOAD` semantics:

- host-bound lookup failure still returns the same immediate host-visible error
- successful dispatch still marks the suspended spawn as `kScriptLoadDispatched`
- send failure still restores the suspended spawn via `MarkSpawnScriptLoadComplete(...)`
  before returning the same `-4` host error

This was intentionally kept separate from the generic request helper because
`SCRIPT_LOAD` still owns the extra state-machine side effects.

### 2. Added request-side regressions for the remaining `SCRIPT_LOAD` boundaries

Coverage now explicitly proves:

- a successful `SCRIPT_LOAD` dispatch advances the suspended spawn into
  `kScriptLoadDispatched`
- a `SCRIPT_LOAD` request sent before any bound runtime agent is ready returns the
  immediate host-visible `-4` error and leaves the suspended spawn in
  `kReadyForScriptLoad`

That closes the main remaining request-side assertion gap before further refactors.

### 3. Agent->host response forwarding now converges behind one shared helper

Added `ForwardAgentResponseToBoundHost(...)`.

`SCRIPT_CREATE_RESP`, `SCRIPT_UNLOAD_RESP`, and `RPC_RESPONSE` now share the same:

- current-agent acceptance check
- bound-host lookup
- invalid-source drop path
- no-host drop path
- forward logging

`SCRIPT_LOAD_RESP` also uses the same helper, but still keeps its state-specific
hook points:

- when there is no host, it still calls `MarkSpawnScriptLoadComplete(...)`
- before forwarding to the host, it still calls `MarkSpawnScriptLoadComplete(...)`

So the behavioral contract stays the same while the owner-resolution boundary becomes
shared.

Why this matters:

- request/response forwarding now has fewer hand-written lifecycle branches
- `SCRIPT_LOAD` remains the only special-case message on purpose, and its special
  behavior is now isolated behind one helper instead of open-coded inline
- this is another concrete reduction in duplicated server-side spawn bookkeeping on
  the path toward a more stable agent-owned/child-owned model

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp47.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp47.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp47.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp47.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp47.exe`
- `build/test-bin/test_session_registry_tmp47.exe`

## Follow-up On 2026-05-23 `SCRIPT_POST` Uses The Shared Host->Agent No-Response Boundary

After the last request/response convergence pass, one more small duplicated request-side
path remained in `server/server_handlers.cpp`:

- `SCRIPT_POST`

Unlike `SCRIPT_CREATE` / `SCRIPT_LOAD` / `SCRIPT_UNLOAD` / `RPC_REQUEST`, this message
does not send any immediate host-visible error response. But it still repeated the same
host-bound lookup cases inline:

- host not bound
- spawn runtime not ready
- agent session not ready
- successful forward

That meant another place where the same script-operation routing policy could drift.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added a shared host->agent no-response forwarding helper

Added `ForwardHostBoundAgentRequestNoResponse(...)`.

It preserves the current no-reply contract:

- lookup failures only log and return
- no host-visible error frame is emitted
- successful requests are forwarded to the resolved agent session
- send failure only logs and returns

This is intentionally separate from `ForwardHostBoundAgentRequest(...)` because the
latter is for request types that must synthesize an immediate response frame on failure.

### 2. `SCRIPT_POST` now uses that shared boundary

`HandleScriptPost(...)` now delegates the host-bound lookup/forward flow to the new
helper instead of open-coding the same branches inline.

Behavior intentionally stays the same:

- ready spawn/runtime agent: forward
- suspended spawn not yet runtime-ready: drop
- no current runtime agent after disconnect/fallback mismatch: drop
- no host-visible response on failure

### 3. Added focused spawn-side `SCRIPT_POST` regressions

Coverage now explicitly proves:

- `SCRIPT_POST` for a ready spawned process forwards to the authoritative runtime agent
- `SCRIPT_POST` while the spawned process is still blocked on runtime-ready does not
  forward to the control-stage fallback session

Why this matters:

- one more host->agent script operation now routes through a shared boundary
- this reduces duplicated request-side lifecycle decisions without touching injection or
  device packaging
- it continues the same agent-owned/child-owned cleanup strategy: fewer hand-written
  side branches, more shared authoritative routing rules

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp49.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp49.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp49.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp49.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp49.exe`
- `build/test-bin/test_session_registry_tmp49.exe`

## Follow-up On 2026-05-23 `SCRIPT_MESSAGE` Tail Routing Isolated Behind A Shared Helper

The next remaining low-risk duplication in `server/server_handlers.cpp` was inside
`HandleScriptMessage(...)`.

Its front half is intentionally special because it decides whether the sending agent
session is acceptable for:

- current attach paths
- spawn control-stage fallback
- spawn runtime-authoritative routing

That acceptance logic is not yet a good target for generic sharing.

But the back half was still a repeated tail-routing block:

- cache before spawn/runtime is visible
- otherwise forward to the bound host
- otherwise drop

That logic is small, but it is another place where the cache/forward/drop boundary
could drift from the rest of the spawn lifecycle.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added an explicit subset regression for the orphan runtime-message drop boundary

Coverage now explicitly proves:

- when a `SCRIPT_MESSAGE` arrives from an agent pid that has:
  - no cache window
  - no bound host
- the message is dropped immediately
- no cached script-message frames are left behind

This boundary was already covered in the broader handler tests, but not in the focused
spawn-ready subset that is now acting as the main safety net for these incremental
refactors.

### 2. Moved the cache/forward/drop tail into `HandleScriptMessageCacheOrForward(...)`

`HandleScriptMessage(...)` now keeps:

- decode
- pid validation
- accepted-agent/session validation

and delegates only the tail behavior to the new helper:

- cache if `ShouldCacheScriptMessageForPid(pid)`
- else forward to `ResolveBoundHostSessionForPid(...)`
- else drop without caching

Behavior stays the same; only the tail routing boundary is isolated.

Why this matters:

- one more repeated spawn message-routing tail is now isolated behind a named helper
- the tricky acceptance logic remains untouched, which keeps this step low-risk
- this continues the same cleanup strategy: move generic lifecycle tails behind shared
  boundaries first, then leave the genuinely stage-specific logic explicit

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp51.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp51.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp51.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp51.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp51.exe`
- `build/test-bin/test_session_registry_tmp51.exe`

## Follow-up On 2026-05-23 Script/Rpc Request Decode And Registry Guards Converge

After the previous request/response forwarding cleanup, another small piece of
duplicated handler boilerplate remained in three request handlers:

- `SCRIPT_CREATE`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

Before any host-bound lookup happened, each handler still repeated the same front-end
shape:

- decode request payload
- log an invalid-message error on decode failure
- synthesize the corresponding immediate host-visible `-1` error response
- reject a missing `SessionRegistry` with the corresponding `-2` response

This duplication was not a functional bug, but it is exactly the kind of repetitive
prelude that makes later behavior changes harder to apply consistently.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `DecodeRequestWithRegistryGuard(...)`

This helper centralizes the shared request prelude:

- decode payload into the caller-provided request struct
- log the provided invalid-message tag on failure
- send the caller-provided invalid-request response
- send the caller-provided registry-unavailable response

The helper intentionally stops there. It does not know anything about:

- host-bound lookup
- spawn blocked vs agent-not-ready mapping
- send-failure responses

Those remain in the existing per-message helpers where the behavior is still
message-specific.

### 2. `SCRIPT_CREATE`, `SCRIPT_UNLOAD`, and `RPC_REQUEST` now use the shared guard

Each handler still preserves its exact response payload contract:

- invalid request: `-1`
- registry unavailable: `-2`
- same message-specific response type and script/session ids as before

So this is a structure-only convergence step, not a behavior change.

Why this matters:

- one more repeated handler boundary is now shared
- the request handlers are getting progressively flatter, leaving only the true
  message-specific behavior in place
- it continues the same low-risk path toward agent-owned/child-owned stability:
  reduce duplicated server-side lifecycle glue without touching injection/runtime
  behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp52.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp52.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp52.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp52.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp52.exe`
- `build/test-bin/test_session_registry_tmp52.exe`

## Follow-up On 2026-05-23 Shared Script/Rpc Operation Request Wrapper Added

After converging the decode/registry-guard prelude, the three request handlers

- `SCRIPT_CREATE`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

still each open-coded the same next step:

- resolve the bound runtime agent for the host
- describe the lookup failure
- forward through the generic host->agent request helper
- synthesize an immediate host-visible response on lookup/send failure

That logic was already using the same lower-level helper, but the callsites still
repeated the same structural shape inline.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added a focused subset regression for `SCRIPT_CREATE` registry-unavailable handling

The focused subset already covered:

- send-failure responses
- spawn-not-ready responses

for several script/rpc operations.

Added one more explicit request-front boundary:

- `SCRIPT_CREATE` without a `SessionRegistry` returns the immediate `-2`
  `SCRIPT_CREATE_RESP`

This keeps the subset safety net aligned with the newer shared prelude/wrapper work.

### 2. Added `ForwardHostBoundScriptOperationRequest(...)`

This wrapper intentionally stays thin:

- it delegates to the existing `ForwardHostBoundAgentRequest(...)`
- it does not change the lookup/send behavior
- it simply gives the common script/rpc request path an explicit shared name

`SCRIPT_CREATE`, `SCRIPT_UNLOAD`, and `RPC_REQUEST` now call this shared wrapper.

Why this matters:

- the three request handlers are converging toward the same shape:
  - decode/registry guard
  - resolve/describe lookup
  - shared script-operation forward
  - message-specific response payload details only
- this keeps narrowing the amount of duplicated handler glue that can drift
- it is still a low-risk refactor because the actual forwarding semantics remain in the
  existing lower-level helper

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp54.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp54.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp54.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp54.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp54.exe`
- `build/test-bin/test_session_registry_tmp54.exe`

## Follow-up On 2026-05-23 Script/Rpc Request Lookup Wrapper Converges Further

The previous pass gave `SCRIPT_CREATE`, `SCRIPT_UNLOAD`, and `RPC_REQUEST` a shared
front-end decode/registry guard and a shared lower-level forwarding wrapper.

One repeated layer still remained in all three handlers:

- resolve host-bound target agent
- describe the lookup failure
- call the shared script-operation request wrapper

This was still structurally duplicated even though the underlying behavior had already
been unified.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added another focused subset regression for request-front behavior

Added explicit subset coverage that:

- `RPC_REQUEST` without a `SessionRegistry` returns the immediate `-2`
  `RPC_RESPONSE`

This complements the earlier `SCRIPT_CREATE` registry-unavailable coverage and keeps
the focused subset aligned with the request-front refactors.

### 2. Added `ForwardHostBoundScriptOperationRequestWithLookup(...)`

This new helper folds together:

- `ResolveHostBoundAgentForScriptOperation(...)`
- `DescribeHostBoundAgentLookupFailure(...)`
- `ForwardHostBoundScriptOperationRequest(...)`

`SCRIPT_CREATE`, `SCRIPT_UNLOAD`, and `RPC_REQUEST` now use it directly.

Their remaining differences are now mostly limited to:

- request decode/registry guard payloads
- response encoding payloads for lookup/send failures

Why this matters:

- another repeated request-side layer is now shared
- the three script/rpc request handlers are getting close to a single structural shape
- this continues the same low-risk convergence strategy: flatten handler glue first,
  keep message-specific payload assembly explicit

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp56.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp56.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp56.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp56.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp56.exe`
- `build/test-bin/test_session_registry_tmp56.exe`

## Follow-up On 2026-05-23 `SCRIPT_LOAD` Front-End Guard Converges With Other Requests

At this point the remaining request handler that still had a hand-written decode/registry
front-end was:

- `SCRIPT_LOAD`

That made sense earlier because `SCRIPT_LOAD` also owns extra spawn state-machine
behavior (`MarkSpawnScriptLoadInFlight(...)` / `MarkSpawnScriptLoadComplete(...)`), but
those behaviors live later in the dispatch path. The decode/registry prelude itself was
still structurally identical to the other script/rpc requests.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added focused subset coverage for `SCRIPT_LOAD` without a registry

Coverage now explicitly proves:

- `SCRIPT_LOAD` without a `SessionRegistry` returns the immediate `-2`
  `SCRIPT_LOAD_RESP`

That closes the same front-end guard boundary already covered for the other request
types.

### 2. `SCRIPT_LOAD` now uses `DecodeRequestWithRegistryGuard(...)`

Only the front-end was converged:

- invalid request: `-1`
- registry unavailable: `-2`

The later `SCRIPT_LOAD`-specific behavior remains untouched:

- host-bound lookup and failure mapping
- `MarkSpawnScriptLoadInFlight(...)`
- `MarkSpawnScriptLoadComplete(...)` on send/response cleanup

Why this matters:

- request-front behavior is now aligned across the script/rpc request family
- the only remaining `SCRIPT_LOAD` special behavior is the real special behavior:
  its spawn state-machine side effects
- this is another small reduction in duplicated handler scaffolding without touching
  runtime injection or device behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp58.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp58.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp58.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp58.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp58.exe`
- `build/test-bin/test_session_registry_tmp58.exe`

## Follow-up On 2026-05-23 Response Decode Shell Starts Converging

After the request-side front-end cleanup, the next remaining low-risk duplication on the
response side was the smallest repeated shell:

- decode response payload
- log invalid payload
- return without any host-visible reply

This pattern was still repeated in:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

`SCRIPT_LOAD_RESP` was intentionally left alone for now because its response path also
owns spawn state restoration.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added focused subset coverage for an invalid response payload

Coverage now explicitly proves:

- an invalid `SCRIPT_CREATE_RESP` payload is dropped
- no host frame is forwarded

This gives the focused subset a direct regression for the no-reply invalid-response
boundary before the response decode shell was shared.

### 2. Added `DecodeResponseNoReply(...)`

This helper centralizes the shared response prelude:

- decode the payload into the caller-provided response struct
- emit the caller-provided invalid log tag on decode failure
- return `false` without sending any response

It is now used by:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

`SCRIPT_LOAD_RESP` remains separate because it still owns stateful cleanup hooks.

Why this matters:

- another repeated shell is now shared
- response-side handlers are being reduced to:
  - decode shell
  - authoritative owner/host resolution
  - message-specific state hooks only where needed
- this keeps the refactor low-risk while continuing the same handler-flattening path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp60.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp60.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp60.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp60.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp60.exe`
- `build/test-bin/test_session_registry_tmp60.exe`

## Follow-up On 2026-05-23 `SCRIPT_LOAD_RESP` Joins The Shared Response Decode Shell

The previous response-side decode cleanup intentionally left one special-case handler
alone:

- `SCRIPT_LOAD_RESP`

The reason was valid: unlike the other responses, it also participates in spawn state
restoration through `MarkSpawnScriptLoadComplete(...)`.

But that special behavior lives *after* payload decode. The decode shell itself was still
the same as the others:

- decode the payload
- log invalid payload
- return without forwarding

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added focused subset coverage that invalid `SCRIPT_LOAD_RESP` must not restore state

Coverage now explicitly proves:

- when a spawn is sitting in `kScriptLoadDispatched`
- and an invalid `SCRIPT_LOAD_RESP` payload arrives
- no host frame is forwarded
- the spawn state remains `kScriptLoadDispatched`

This directly protects against accidentally running the state-recovery path before
successful decode.

### 2. `SCRIPT_LOAD_RESP` now uses `DecodeResponseNoReply(...)`

Only the decode shell was shared.

The special behavior remains unchanged after successful decode:

- invalid source handling
- no-host handling
- `MarkSpawnScriptLoadComplete(...)` before/no-host forward
- normal response forwarding

Why this matters:

- response decode behavior is now aligned across the whole script/rpc response family
- `SCRIPT_LOAD_RESP` remains special only where it truly needs to be special:
  state recovery after decode
- this keeps shaving down repeated handler scaffolding while leaving the sensitive spawn
  state-machine semantics intact

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp62.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp62.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp62.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp62.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp62.exe`
- `build/test-bin/test_session_registry_tmp62.exe`

## Follow-up On 2026-05-23 `SCRIPT_LOAD` Lookup Wrapper Converges With Other Requests

After aligning `SCRIPT_LOAD` with the shared request-front decode/registry guard, one
structural difference still remained between it and the other script/rpc requests:

- `SCRIPT_LOAD` still manually performed:
  - `ResolveHostBoundAgentForScriptOperation(...)`
  - `DescribeHostBoundAgentLookupFailure(...)`
  - `ForwardHostBoundScriptLoadRequest(...)`

while the others had already moved that layer behind a shared `...WithLookup(...)`
wrapper.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `ForwardHostBoundScriptLoadRequestWithLookup(...)`

This helper mirrors the existing script/rpc request pattern, but still routes through
the `SCRIPT_LOAD`-specific forwarding helper so its special semantics remain intact:

- `MarkSpawnScriptLoadInFlight(...)`
- `MarkSpawnScriptLoadComplete(...)` on send failure

### 2. `HandleScriptLoad(...)` now uses the lookup wrapper

So `SCRIPT_LOAD` is now structurally aligned with the others:

- decode/registry guard
- shared lookup wrapper
- message-specific response payload assembly

without losing its state-machine-specific dispatch behavior.

Why this matters:

- the request-side handler family now has a much more uniform shape
- `SCRIPT_LOAD` remains special only where it truly differs
- this is another small reduction in duplicated glue around the spawn/runtime routing
  boundary

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp63.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp63.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp63.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp63.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp63.exe`
- `build/test-bin/test_session_registry_tmp63.exe`

## Follow-up On 2026-05-22 AGENT_READY Registration Actions Use A Shared Registry Executor

Another chunk of `HandleAgentReady()` had already been reduced to:

- derive context
- decide drops
- compute a registration plan

but the actual state writes were still spelled out inline in the handler.

Problem:

- the registry already owned:
  - registration intent
  - stage interpretation
  - suspended spawn authoritative upgrades
- but the handler still directly performed the corresponding mutations:
  - runtime global registration
  - mismatched runtime removal
  - control global registration
  - control identity registration
  - pending-attach clear
  - suspended authoritative-ready upgrade

That meant the most important AGENT_READY state writes were still duplicated at the
handler layer even after the plan had become registry-owned.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added `ApplyAgentReadyRegistrationPlan(...)`

The registry now executes the state writes implied by
`PlanAgentReadyRegistration(...)` through a single entrypoint.

It covers:

- matching runtime-ready global registration
- mismatched runtime session removal
- control-stage global registration when runtime has not yet been recorded
- control identity registration
- pending-attach clear
- suspended authoritative-ready upgrade

### 2. Added direct registry coverage for action execution

New coverage asserts:

- matching runtime-ready execution registers runtime session, frame, stage, and clears
  pending attach
- mismatched runtime-ready execution removes the current runtime session without
  upgrading state
- control-ready execution registers control identity and upgrades suspended spawn
  authoritative state

Why this matters:

- AGENT_READY interpretation and AGENT_READY registration side effects are now both
  primarily registry-owned
- `HandleAgentReady()` no longer reconstructs that write sequence inline
- this is a direct step toward `child-owned` lifecycle truth instead of
  handler-local orchestration

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp30.exe`
- `build/test-bin/test_session_registry_tmp30.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp30.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp30.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp30.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp30.exe`

## Follow-up On 2026-05-22 AGENT_READY Pending-Spawn Resolve And Forward/Hold Tail Use Shared Helpers

After the registration executor moved into the registry, one low-risk inline cluster
still remained in `HandleAgentReady()`:

- resolve pending spawn then bind host
- decide whether runtime-ready may be exposed immediately
- forward, hold, or drop the AGENT_READY at the host boundary

Problem:

- these were still encoded as a long inline tail even though the decisions were already
  derived earlier
- the code path was mechanically stable, but still harder to reason about and easier to
  accidentally drift while continuing the `child-owned` cleanup

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added `ResolvePendingSpawnAfterAgentReady(...)`

This helper now owns:

- `ResolvePendingSpawn(...)`
- `BindHostToResolvedPendingSpawn(...)`
- the existing success / skipped-bind logging

### 2. Added `HandleAgentReadyForwarding(...)`

This helper now owns the host-bound tail for AGENT_READY:

- expose runtime-ready immediately when allowed and no host is currently bound
- forward runtime-ready AGENT_READY to the bound host when the response boundary is open
- replay cached script messages after a successful forward
- hold runtime-ready until the spawn response boundary is released
- drop mismatched runtime-ready notifications
- keep control-stage AGENT_READY in held state

Why this matters:

- the tail end of `HandleAgentReady()` is now shorter and grouped by purpose
- no route behavior changed
- the remaining work toward agent-owned stable spawn can focus more on boundary ownership
  and less on unstructured handler-local sequencing

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp31.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp31.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp31.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp31.exe`

## Follow-up On 2026-05-22 AGENT_READY Pre-Registration Adjustments Use A Shared Helper

After the previous tail cleanup, one more inline cluster still remained before
registration planning in `HandleAgentReady()`:

- drop late control-ready at a runtime boundary
- clear a rebound same-session previous pid when runtime-ready lands on a new pid
- drop stale non-current control-ready after runtime was already recorded
- clear mismatched runtime trace before accepting a control-stage replacement

Problem:

- these steps were conceptually one boundary:
  - "normalize / reject AGENT_READY before registration"
- but they were still encoded as several inline conditionals in the handler
- that made the handler longer than necessary and kept one more lifecycle boundary
  handler-owned

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added `HandleAgentReadyPreRegistrationAdjustments(...)`

This helper now owns the shared pre-registration adjustments:

- late control-stage runtime-boundary drop
- runtime-ready same-session previous-pid cleanup
- stale late-control drop after runtime ownership is known
- mismatched runtime-trace reset before control-stage replacement

### 2. Added direct rebound coverage

New handler coverage asserts:

- when the same live agent session rebounds from an older `peer pid` to the accepted
  runtime-ready `pid`, the older pid binding is cleared and the session remains bound
  only to the new pid

Why this matters:

- another lifecycle boundary is now expressed once instead of inline in the handler
- `HandleAgentReady()` keeps shrinking toward orchestration-only code
- this is the same direction as the larger `child-owned` / agent-owned stable spawn
  cleanup: decisions and boundary actions move into explicit reusable units

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp32.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp32.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp33.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp33.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp33.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp33.exe`

## Follow-up On 2026-05-22 AGENT_READY Early-Drop Checks Use A Shared Helper

The top of `HandleAgentReady()` still contained a dense cluster of early rejection logic.

Problem:

- all of these checks were conceptually the same boundary:
  - reject an AGENT_READY before any registration or stage progression
- but they were still expanded inline in the handler:
  - orphan spawn-token ready
  - mismatched pending-attach ready
  - stale attach while a new attach is pending
  - foreign attach-like ready
  - mismatched existing spawn-token context
  - orphan attach ready
  - invalidated unowned ready
  - mismatched known control identity for spawn

That made the handler front half longer than necessary and kept one more important
policy boundary non-reusable.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added `HandleAgentReadyEarlyDropChecks(...)`

This helper now owns the complete early-rejection cluster and preserves the existing
logging and `RemoveAgentSessionByPidIfMatches(...)` side effects.

### 2. Added direct pending-attach mismatch coverage

New handler regression asserts:

- when a pending-attach token exists but the incoming AGENT_READY identity does not
  match the expected attach target, the AGENT_READY is dropped
- the pending attach remains intact
- no authoritative/runtime agent state is recorded

Why this matters:

- the handler front half is now split into clearer stages:
  - context resolution
  - early drop policy
  - pre-registration normalization
  - registration planning
  - registration execution
  - pending-spawn resolve and host forwarding
- that decomposition makes the remaining convergence work much more mechanical and less
  error-prone

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp34.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp34.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp35.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp35.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp35.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp35.exe`

## Follow-up On 2026-05-22 `HandleAgentReady()` Now Uses A Derived Local Context Object

After the helper extraction work above, `HandleAgentReady()` still had one remaining
source of noise:

- a long inline block unpacking `AgentReadySpawnContext`
- plus several derived booleans recomputed in-place before being fed into the later
  helpers

Problem:

- the helper boundaries were already much cleaner, but the handler still spent a large
  amount of space translating raw context into local booleans
- that made the orchestration layer noisier than necessary and kept more lifecycle
  interpretation than needed inside the handler body

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added `DerivedAgentReadyContext`

`HandleAgentReady()` now computes a single derived local context that carries:

- resolved `AgentReadySpawnContext`
- expected spawn process name
- bound host
- runtime stage bit
- owned zygote-control bit
- runtime process-name match result
- mismatched runtime-trace predicate
- runtime-already-recorded predicate
- current control-stage acceptance result
- stale-control-after-runtime predicate

This lets the handler pass one coherent set of facts into the existing helper stages
instead of re-unpacking and re-deriving them inline.

### 2. Added direct handler coverage for hostless runtime-ready visibility promotion

New regression asserts:

- when a suspended spawn has already crossed the runtime boundary and no host is bound,
  a runtime-stage AGENT_READY still promotes the suspended transaction into
  `kReadyForScriptLoad`

Why this matters:

- the main AGENT_READY path is now much closer to the intended end-state:
  - decode
  - derive context
  - early-drop policy
  - pre-registration normalization
  - registration plan/apply
  - pending-spawn resolve
  - host forwarding / visibility release
- this is the point where the handler becomes mostly orchestration over explicit
  lifecycle boundaries instead of a dense pile of inline lifecycle reconstruction

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp36.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp36.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp37.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp37.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp37.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp37.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp37.exe`
- `build/test-bin/test_session_registry_tmp37.exe`

## Follow-up On 2026-05-22 Host-Bound Request Failure Mapping Starts Converging

After the main AGENT_READY path became mostly orchestration, another smaller but very
repetitive shared boundary stood out:

- host-bound script / rpc requests all performed the same lookup:
  - `ResolveHostBoundAgentForScriptOperation(...)`
- and then expanded the same failure mapping inline:
  - host not bound
  - spawn not ready
  - agent not ready

Problem:

- request handlers were still restating the same lookup failure semantics
- that increases the chance of drift between:
  - `SCRIPT_CREATE`
  - `SCRIPT_LOAD`
  - `SCRIPT_UNLOAD`
  - `RPC_REQUEST`

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added `DescribeHostBoundAgentLookupFailure(...)`

This helper now provides the shared request-side mapping from
`HostBoundAgentLookupError` to:

- user-facing error code
- user-facing error string

It is now used by:

- `HandleScriptCreate(...)`
- `HandleScriptLoad(...)`
- `HandleScriptUnload(...)`
- `HandleRpcRequest(...)`

### 2. Added direct unload-path spawn-not-ready coverage

New regression asserts:

- `SCRIPT_UNLOAD` against a gate-held spawn returns:
  - error code `-5`
  - `"spawned pid is not ready for script unload"`

Why this matters:

- request-side lifecycle error semantics are starting to converge the same way
  AGENT_READY semantics did
- the next step can build on this to centralize more of the host->agent request path
  without changing on-device behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp38.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp38.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp39.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp39.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp40.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp40.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp40.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp40.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp40.exe`
- `build/test-bin/test_session_registry_tmp40.exe`

## Follow-up On 2026-05-23 Host-Bound Request Lookup And Send-Failure Handling Are Further Shared

The previous request-side convergence introduced a shared lookup failure description, but
the request handlers still repeated:

- host-not-bound / agent-not-ready logging
- send-failure logging
- immediate error response plumbing

Problem:

- request handlers still had duplicated control flow around the shared failure mapping
- that was especially visible across:
  - `SCRIPT_CREATE`
  - `SCRIPT_LOAD`
  - `SCRIPT_UNLOAD`
  - `RPC_REQUEST`

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added `HandleHostBoundAgentRequestLookupFailure(...)`

This helper now centralizes:

- request-side host-not-bound logging
- request-side agent-not-ready logging
- dispatch of the already-normalized `HostRequestFailure`

### 2. Added `HandleHostBoundAgentRequestSendFailure(...)`

This helper now centralizes:

- send-failure logging for host->agent request forwarding

while each caller still decides its exact error response payload.

### 3. Applied the helpers incrementally, then across all request types

The shared helpers now cover:

- `SCRIPT_CREATE`
- `SCRIPT_LOAD`
- `SCRIPT_UNLOAD`
- `RPC_REQUEST`

### 4. Added direct rpc spawn-not-ready coverage

New handler regression asserts:

- `RPC_REQUEST` against a gate-held spawn returns:
  - error code `-5`
  - `"spawned pid is not ready for rpc request"`

Why this matters:

- the host->agent request path is now converging the same way the AGENT_READY path did:
  shared lifecycle semantics first, then thinner handlers
- future extraction of a larger request-forward helper can build on these shared pieces
  without changing behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp41.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp41.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp42.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp42.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp43.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp43.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp43.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp43.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp43.exe`
- `build/test-bin/test_session_registry_tmp43.exe`

## Follow-up On 2026-05-22 Remaining Runtime-Boundary / Runtime-Trace Spawn Semantics Moved Into Registry

Another shared-path cleanup point remained in `HandleAgentReady(...)`.

Problem:

- the handler still carried a small cluster of suspended-spawn interpretation helpers:
  - transaction runtime-boundary detection
  - mismatched runtime-trace detection
  - accepted runtime-trace detection
  - known control-identity mismatch detection
- these were no longer route-specific decisions
- they were pure interpretations of registry-owned suspended-spawn and agent-ready state
- leaving them in the handler meant the handler still had to reconstruct child-owned truth
  from registry primitives instead of asking the registry directly

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Runtime-boundary/runtime-trace questions now have explicit registry APIs

Added:

- `HasAuthoritativeRuntimeBoundaryForSpawn(...)`
- `HasMismatchedRuntimeTraceForSpawn(...)`
- `HasAcceptedRuntimeTraceForSpawn(...)`
- `HasKnownSpawnControlIdentityMismatchForSpawn(...)`

These APIs keep the same current semantics, but move the interpretation to the owner of:

- suspended spawn transaction state
- authoritative stage/process identity
- runtime-ready visibility
- current authoritative agent identity

### 2. `HandleAgentReady(...)` now consumes registry-owned answers instead of rebuilding them

The handler no longer carries local helper functions for:

- runtime-boundary detection
- runtime-trace mismatch detection
- accepted runtime-trace detection

It now asks the registry directly using the already-fetched suspended entry context.

This keeps the handler thinner and makes it harder for future shared-path work to fork
slightly different interpretations of the same suspended-spawn state.

### 3. Added direct registry regressions for the moved semantics

New coverage asserts:

- runtime-boundary only holds for suspended entries with authoritative runtime stage
- mismatched runtime traces are detected only when a runtime-ready trace exists but does
  not match the expected/claimed identity
- accepted runtime trace requires runtime-ready identity match
- known control identity mismatch rejects unexpected control-stage names

Why this matters:

- another cluster of child-owned spawn truth has moved out of the handler and into the
  registry
- this reduces handler-local lifecycle reconstruction further
- it is a safer base for the later child-owned/agent-owned spawn convergence because the
  same state questions now have one implementation site

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp18.exe`
- `build/test-bin/test_session_registry_tmp18.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp18.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp18.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp18.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp18.exe`

## Follow-up On 2026-05-22 Current-Agent / Control-Stage Acceptance Helpers Moved Into Registry

One more small handler-local interpretation cluster remained around agent session acceptance.

Problem:

- `server_handlers.cpp` still reconstructed two acceptance rules locally:
  - "is this the current accepted agent session for this pid?"
  - "for a suspended spawn, is this accepted as the control-stage session?"
- these are not transport or handler concerns
- they are registry-owned interpretations of:
  - current pid binding
  - attach-timeout / invalidated state
  - suspended spawn control identity
  - control-session vs current-session fallback

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added direct registry helpers for current/control-stage acceptance

Added:

- `IsAcceptedCurrentAgentSessionForPid(...)`
- `IsAcceptedControlStageAgentSessionForSpawn(...)`

These preserve current semantics, including the existing fallbacks:

- current session accepted when pid is still live
- unbound/non-invalidated/non-timeout pid remains permissive
- control-stage suspended-spawn acceptance still uses the existing
  `FindControlReadyAgentSessionForSpawn(...)` behavior, including its current fallback
  to the pid's current agent when that is how the registry resolves the control owner

### 2. Handler logic now delegates instead of re-deriving those rules

`server_handlers.cpp` no longer duplicates these checks inline.

This further reduces the amount of handler-local reasoning about:

- attach timeout state
- invalidated pid state
- suspended spawn control identity ownership

### 3. Added direct registry regressions for the moved helper semantics

Coverage now asserts:

- current-agent acceptance follows:
  - current bound agent
  - permissive no-current/no-timeout path
  - timeout rejection path
- control-stage acceptance follows the current registry resolution behavior for:
  - matching target identity
  - mismatched identity when current-agent fallback still wins
  - rebound current-agent ownership after control/current split

Why this matters:

- another layer of spawn/attach acceptance semantics is now centralized in the registry
- future child-owned spawn cleanup can ask the registry directly instead of copying
  acceptance rules into more handler branches
- this reduces the chance of subtle divergences between script-message, ready, and host
  request paths

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp22.exe`
- `build/test-bin/test_session_registry_tmp22.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp22.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp22.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp22.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp22.exe`

## Follow-up On 2026-05-22 AGENT_READY Spawn Context Parsing Moved Into Registry

Another handler-local cluster remained in `HandleAgentReady(...)`: gathering suspended
spawn context from three different sources before making any drop/accept decision.

Problem:

- the handler was locally merging:
  - pending-spawn state
  - pending-attach state
  - existing suspended-spawn entry state
- this was still pure registry-owned interpretation, but it lived in the handler
- later drop/accept branches depended on that merged context, so keeping the merge local
  meant future changes risked reintroducing divergent interpretations

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added a dedicated registry result for AGENT_READY spawn context

Added `AgentReadySpawnContext` plus:

- `ResolveAgentReadySpawnContext(...)`

This normalized result now carries:

- `expected_spawn_process_name`
- `has_pending_spawn_context`
- `has_pending_attach_context`
- `pending_attach_matches`
- `has_spawn_suspended_context`
- `spawn_token_mismatches_existing_spawn_context`
- fetched suspended entry / pending attach payloads

### 2. `HandleAgentReady(...)` now consumes one normalized context

The handler no longer manually reconstructs that merge itself.

Importantly, this change does **not** move the drop/accept actions yet.
It only centralizes the data interpretation that those later branches consume.

This keeps the refactor low risk:

- same decisions
- same branch structure
- one source of truth for the merged spawn context

### 3. Added direct registry regressions for the merged context semantics

Coverage now asserts:

- pending-spawn context overrides suspended-entry fallback target resolution
- suspended runtime-ready identity becomes the expected runtime process when no pending
  spawn is present
- pending-attach match state is computed centrally
- suspended spawn-token mismatch is surfaced centrally

Why this matters:

- `HandleAgentReady(...)` is now thinner at the very front of its suspended-spawn path
- the remaining drop/accept decisions can be moved later in smaller steps, each reusing
  the same normalized context
- this is another concrete reduction of handler-local lifecycle reconstruction on the
  child-owned/agent-owned convergence path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp23.exe`
- `build/test-bin/test_session_registry_tmp23.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp23.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp23.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp23.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp23.exe`

## Follow-up On 2026-05-22 Pre-Drop AGENT_READY Filters Moved Into Registry

After centralizing AGENT_READY spawn-context parsing, the next low-risk cluster was the
set of early orphan/foreign/stale drop predicates that still lived inline in the handler.

Problem:

- `HandleAgentReady(...)` still computed several pre-drop conditions locally:
  - stale attach while a new attach is pending
  - foreign attach-like ready with a token but no owned context
  - mismatched spawn-token against an existing suspended context
  - orphan attach timeout ready
  - invalidated unowned ready
- these are still registry-owned state questions
- keeping them local meant the handler still directly combined:
  - pending attach state
  - invalidated/timeout state
  - host-bound ownership state
  - owned zygote-control exceptions

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added explicit registry helpers for the early drop predicates

Added:

- `ShouldDropStaleAttachAgentReady(...)`
- `ShouldDropForeignAttachLikeAgentReady(...)`
- `ShouldDropMismatchedSpawnTokenAgentReady(...)`
- `ShouldDropOrphanAttachAgentReady(...)`
- `ShouldDropInvalidatedUnownedAgentReady(...)`

These consume a small `AgentReadyDropContext` that carries only the already-resolved
front-end facts the handler gathered:

- pending spawn presence
- pending attach match result
- suspended spawn context presence
- spawn-token mismatch flag
- owned zygote-control exception
- bound-host presence

### 2. Handler still performs the action, but the decision now comes from the registry

`HandleAgentReady(...)` still:

- logs the drop reason
- removes the pid/session binding when needed
- returns immediately

But the boolean decision is no longer reconstructed inline for these five cases.

This is intentionally a low-risk midpoint:

- decision centralized
- action unchanged
- control flow unchanged

### 3. Added registry regressions for each moved predicate

Coverage now asserts:

- stale attach requires an unowned conflicting pending attach
- foreign attach-like requires a token with no accepted spawn/attach/suspended context
- mismatched spawn-token reflects the normalized mismatch flag
- orphan attach requires timeout + unbound + non-owned zygote-control state
- invalidated unowned ready requires an actually invalidated pid and no owned
  zygote-control exception

Why this matters:

- another visible slice of AGENT_READY filtering is now registry-owned
- the handler is closer to a pure "act on registry decisions" shape
- this continues the child-owned/agent-owned convergence without touching route behavior

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp24.exe`
- `build/test-bin/test_session_registry_tmp24.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp25.exe`
- `build/test-bin/test_session_registry_tmp25.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp25.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp25.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp25.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp25.exe`

## Follow-up On 2026-05-22 Runtime-Match / Runtime-Recorded / Late-Control Predicates Moved Into Registry

The next remaining cluster in `HandleAgentReady(...)` was closer to behavior boundaries,
but still reducible without moving the actual actions.

Problem:

- the handler still computed several linked booleans inline:
  - whether the runtime process name matched the expected spawn identity
  - whether runtime had already been authoritatively recorded for this spawn
  - whether a late control-stage ready should be dropped at the runtime boundary
  - whether a late control-stage ready from a non-current session should be dropped
- these were built from registry-owned facts that were already partially centralized
- leaving this cluster inline meant the handler still owned a non-trivial chunk of
  "when is this spawn already runtime-owned?" reasoning

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added registry helpers for the linked runtime/late-control predicates

Added:

- `DoesRuntimeSpawnProcessNameMatch(...)`
- `HasRuntimeRecordedForSpawn(...)`
- `ShouldDropLateControlAgentReadyAtRuntimeBoundary(...)`
- `ShouldDropLateControlAgentReadyFromNonCurrentSession(...)`

These helpers preserve the current semantics and intentionally do not change the later
actions or registrations.

### 2. Handler now consumes those registry answers directly

`HandleAgentReady(...)` still:

- removes the old session when needed
- logs the drop reason
- clears runtime-ready state when runtime trace mismatches are detected
- performs the same registration/update actions

But it no longer reconstructs this runtime-owned/late-control decision cluster inline.

### 3. Added registry regressions for the moved semantics

Coverage now asserts:

- runtime process-name matching follows the current permissive control-stage/runtime-stage
  rules
- runtime-recorded uses either the authoritative runtime boundary or accepted runtime
  trace path
- late control drop at runtime boundary only applies to control-stage arrivals
- late control from a non-current session only drops once runtime is already recorded

Why this matters:

- the handler now owns less of the "runtime has already won" reasoning
- this is directly on the path toward true agent-owned stable spawn semantics
- the remaining work is increasingly about moving actions/boundaries, not re-deriving
  spawn truth in multiple places

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp26.exe`
- `build/test-bin/test_session_registry_tmp26.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp27.exe`
- `build/test-bin/test_session_registry_tmp27.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp27.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp27.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp27.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp27.exe`

## Follow-up On 2026-05-22 Post-Decision AGENT_READY Registration Plan Moved Into Registry

To batch more low-risk convergence in one pass, the next step was to pull a larger
post-decision condition cluster out of `HandleAgentReady(...)`.

Problem:

- after all of the early filters and runtime/late-control predicates, the handler still
  decided inline:
  - whether to register runtime globally
  - whether to remove a mismatched runtime session
  - whether to register control globally
  - whether to register control identity only
  - whether to clear pending attach
  - whether to upgrade suspended authoritative ready
  - whether to resolve pending spawn
  - whether the runtime ready was eligible to be exposed
- these were all conditional plans built from already-computed booleans
- the actions themselves could remain in the handler, but the plan calculation did not
  need to stay there

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added `AgentReadyRegistrationPlan`

Added:

- `AgentReadyRegistrationPlan`
- `PlanAgentReadyRegistration(...)`

This centralized the plan for the handler's post-decision work without changing the
actual side effects yet.

The plan now carries:

- runtime registration vs mismatch removal
- control registration vs identity-only registration
- pending-attach clear
- authoritative-ready upgrade stage
- pending-spawn resolution eligibility
- runtime-ready exposure eligibility

### 2. Handler still performs the side effects, but no longer recomputes the plan inline

`HandleAgentReady(...)` still performs:

- session registration/removal
- authoritative/runtime-ready markers
- pending-attach clear
- suspended authoritative-ready upgrade
- pending-spawn resolve/bind
- runtime-ready exposure / forwarding

But it now follows the registry-owned plan instead of recomputing these gates locally.

### 3. Added direct registry regressions for the plan matrix

Coverage now asserts plan behavior for:

- matching runtime-ready
- mismatched runtime-ready
- control-ready before runtime is recorded
- late control after runtime is already recorded

Why this matters:

- a large chunk of the remaining handler logic is now "execute a registry plan"
- this is a bigger step than the earlier predicate-only moves, but still low-risk because
  the action order stayed the same
- it brings the shared-path lifecycle closer to a true child-owned/agent-owned state
  machine with the registry as the source of truth

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp28.exe`
- `build/test-bin/test_session_registry_tmp28.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp29.exe`
- `build/test-bin/test_session_registry_tmp29.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp29.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp29.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_tmp29.exe`
- `build/test-bin/test_spawn_controller_late_promotion_tmp29.exe`

## Follow-up On 2026-05-22 SCRIPT_LOAD_RESP Now Uses The Shared Agent-Response Owner Boundary

Another small lifecycle/ownership split remained on the response side.

Problem:

- `SCRIPT_CREATE_RESP`
- `SCRIPT_UNLOAD_RESP`
- `RPC_RESPONSE`

already used the shared `ResolveHostForwardTargetForAgentResponse(...)` helper.

But `SCRIPT_LOAD_RESP` still used its own inline path:

- validate accepted agent source
- separately resolve bound host
- then forward

Behavior was already correct, but that meant one more response path still duplicated the
same owner-resolution rules that had already been centralized elsewhere.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. `SCRIPT_LOAD_RESP` now resolves its owner host through the shared response helper

`HandleScriptLoadResp(...)` now:

- resolves the accepted source and owner host through
  `ResolveHostForwardTargetForAgentResponse(...)`
- keeps the same invalid-source drop behavior
- keeps the same missing-host behavior
- still restores `kReadyForScriptLoad` through
  `MarkSpawnScriptLoadComplete(pid)` before returning on missing host or before
  forwarding to the owner host

So this is a convergence step, not a behavior change.

### 2. Added response-side ownership symmetry coverage for `SCRIPT_LOAD_RESP`

New regression:

- `TestScriptLoadRespForSpawnUsesSuspendedHostOwnership()`

What it proves:

- a rebound coarse pid binding does not cause `SCRIPT_LOAD_RESP` to be forwarded to the
  wrong host when a suspended spawn still has an authoritative owner host
- the response still goes to the suspended owner host
- the suspended spawn state still returns to `kReadyForScriptLoad`

Why this matters:

- another response path now shares exactly the same owner-resolution boundary as the
  other agent->host response flows
- this reduces the number of places where suspended-spawn ownership could drift apart
  through small handler-local differences
- it is one more incremental step toward a fully `child-owned` / registry-owned spawn
  lifecycle instead of per-handler reconstruction

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp5.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp5.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp5.exe`
- `build/test-bin/test_session_registry_tmp5.exe`

## Follow-up On 2026-05-22 Spawn-Response Boundary Release Now Lives Behind A Registry Helper

Another small piece of shared spawn lifecycle logic was still split across layers.

Problem:

After a successful `SpawnResponse` send, `spawn_controller` still did two separate things:

- clear `response_pending`
- then re-read / re-derive the current authoritative ready stage and runtime identity

That meant the controller still owned part of the "spawn response boundary has been
released; what is now host-visible?" transition.

But this is the same kind of lifecycle truth that has been gradually moving behind
registry-owned semantic helpers.

Updated:

- [server/session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. Added `ReleaseSpawnResponseBoundary(...)`

New registry helper:

- clears `response_pending`
- promotes to `kReadyForScriptLoad` when the authoritative child stage is already
  `kRuntimeReady`
- returns the updated `SpawnSuspendedEntry` snapshot to the caller when requested

So the registry now owns the semantic meaning of:

- "the host has received the spawn response"
- "what state becomes visible after that boundary releases"

### 2. `spawn_controller` now consumes the updated entry instead of reconstructing it

After sending `SpawnResponse`, the controller now:

- calls `ReleaseSpawnResponseBoundary(authoritative_pid, &released_entry)`
- derives post-finalize stage and runtime identity from that returned entry

Fallback resolution remains in place if the entry disappears unexpectedly, but the normal
shared path no longer reconstructs this transition itself.

### 3. Added direct registry coverage for the boundary-release semantics

New coverage asserts:

- runtime-ready suspended spawn:
  - releasing the response boundary clears `response_pending`
  - promotes to `kReadyForScriptLoad`
  - returns the updated authoritative runtime identity
- control-ready suspended spawn:
  - releasing the response boundary clears `response_pending`
  - keeps the state held at `kWaitingRuntimeReady`

Why this matters:

- another piece of spawn lifecycle truth is now registry-owned instead of
  controller-reconstructed
- the boundary between "held behind spawn response" and "host-visible after response"
  is now represented by a single semantic entry point
- this reduces one more class of duplicated stage/identity re-inference in the shared
  spawn path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp6.exe`
- `build/test-bin/test_session_registry_tmp6.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp6.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp6.exe`

## Follow-up On 2026-05-22 Replay Helpers Converge Across Attach Success, Runtime-Ready Forward, And Spawn Finalize

Another low-risk shared-path duplication remained in the replay logic.

Problem:

Three paths each had their own inline replay sequence:

- attach success
- runtime-ready forwarded directly to a host
- spawn finalize replay after the spawn-response boundary

They were all doing variants of the same operations:

- maybe replay cached `AGENT_READY`
- then maybe replay cached `SCRIPT_MESSAGE`
- while preserving the "don't replay script messages if the ready replay itself failed"
  rule where that rule applies

Behavior was already correct, but the shared semantics were still split across multiple
handler/controller blocks.

Updated:

- [server/server_handlers.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.h)
- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

### 1. Added shared replay helper for cached ready + cached script messages

New helper:

- `ReplayCachedAgentReadyThenScriptMessages(...)`

It centralizes:

- cached `AGENT_READY` lookup by pid + identity
- optional runtime-stage requirement
- "skip script-message replay if cached ready send failed"
- "still replay cached script messages when there is no cached ready frame"

This helper is now used by:

- attach success replay
- spawn finalize replay after spawn-response release

### 2. Added shared helper for runtime-visible promotion + cached script-message replay

New helper:

- `MarkRuntimeReadyVisibleAndReplayCachedScriptMessages(...)`

It centralizes:

- `MarkSpawnRuntimeReadyVisible(pid)`
- cached `SCRIPT_MESSAGE` replay immediately after a successfully forwarded
  runtime-stage `AGENT_READY`

This keeps the "runtime is now host-visible" transition paired with the immediate replay
that depends on it.

### 3. Added direct regression for a subtle replay edge

New coverage:

- `TestSpawnFinalizeReplayWithoutCachedAgentReadyStillSendsCachedScriptMessages()`

What it proves:

- when finalize replay is in a runtime-ready state but no cached `AGENT_READY` matches the
  authoritative runtime identity, cached `SCRIPT_MESSAGE` frames must still replay
- only a ready-send failure blocks that message replay, not mere absence of a ready frame

Why this matters:

- replay semantics are now shared across more of the attach/spawn paths instead of being
  hand-coded in each one
- this reduces the chance that future fixes accidentally diverge one replay path from
  another
- it is another low-risk `child-owned` convergence step: one meaning, one helper

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp8.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp8.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp8.exe`
- `build/test-bin/test_session_registry_tmp8.exe`

## Follow-up On 2026-05-22 Runtime-Ready Visibility Gate And Post-Finalize Context Continue To Converge

Two more low-risk shared-path duplications were still worth tightening.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)

Changes:

### 1. Runtime-ready visibility gate now uses a dedicated helper

`HandleAgentReady(...)` previously had an inline block that recomputed:

- runtime-ready eligibility
- whether a suspended spawn entry existed
- whether `response_pending` still held the runtime-ready behind the spawn-response
  boundary

This was behaviorally correct but still left that "can runtime-ready be exposed to the
host immediately?" decision embedded in the handler.

Added:

- `CanExposeRuntimeReadyImmediately(...)`

This helper centralizes the gate:

- runtime-ready must be eligible for the authoritative spawn target
- a suspended entry must exist
- `response_pending` must already be false

`HandleAgentReady(...)` now uses that helper for both:

- host-less visibility promotion
- direct host forwarding of runtime-stage `AGENT_READY`

and retains the existing "hold until spawn response" branch unchanged.

### 2. Post-finalize spawn context is now resolved as one structure instead of two split helpers

`spawn_controller` previously still resolved post-finalize state through two separate
helpers:

- ready stage
- runtime process name

Those helpers both read the same suspended-spawn context, so the controller was still
pulling the same post-finalize truth in two pieces.

Added:

- `PostFinalizeSpawnContext`
- `ResolvePostFinalizeSpawnContext(...)`

This now returns:

- `ready_stage`
- `runtime_process_name`

in a single read path, and the controller uses that one structure through:

- post-finalize pre-bind context resolution
- post-response boundary fallback resolution
- final runtime replay path

Why this matters:

- another handler-local boundary decision now has one shared definition
- another controller-side split read of the same suspended-spawn truth has been collapsed
  into one semantic context
- this continues the same mainline goal: fewer places reconstructing child-owned spawn
  lifecycle facts from scratch

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp10.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp10.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp10.exe`
- `build/test-bin/test_session_registry_tmp10.exe`

## Follow-up On 2026-05-22 Suspended-Spawn Runtime-Agent Identity Resolution Is Now Shared

There was still one more repeated pattern in the shared spawn paths.

Problem:

Three helpers were each re-reading suspended-spawn runtime identity in slightly separate
inline blocks:

- `ResolveBoundAgentSessionForHostRequest(...)`
- `IsAcceptedAgentSessionForHostResponse(...)`
- `IsAcceptedAgentSessionForScriptMessage(...)`

Each one did its own version of:

- read suspended spawn entry
- compute `ResolveSpawnRuntimeProcessName(entry)`
- try `FindRuntimeReadyAgentSessionByIdentity(pid, process_name)`
- decide whether to:
  - accept a runtime-ready agent
  - reject fallback sessions
  - or continue with non-suspended fallback logic

Behavior was already correct, but the same runtime-identity read path still lived in
three places.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added shared suspended-spawn runtime-agent resolution helper

New helper:

- `ResolveRuntimeReadyAgentSessionForSuspendedSpawn(...)`

It centralizes:

- deriving the authoritative runtime process identity from a suspended spawn entry
- resolving the runtime-ready agent session for that identity

### 2. Three shared-path helpers now reuse the same runtime-agent identity resolution

Updated callers:

- `ResolveBoundAgentSessionForHostRequest(...)`
- `IsAcceptedAgentSessionForHostResponse(...)`
- `IsAcceptedAgentSessionForScriptMessage(...)`

This keeps the same externally visible behavior:

- if a suspended spawn has an authoritative runtime identity and a matching runtime-ready
  session exists, that session is the only accepted one
- if a suspended spawn expects a runtime identity but none is currently resolved, these
  paths still reject fallback control/current sessions in the same cases as before

Why this matters:

- one more repeated child-owned identity read path is now shared
- it reduces the chance that future edits accidentally diverge request-side, response-side,
  and script-message-side runtime-session acceptance rules
- this continues the same overall convergence theme: fewer helpers reconstructing the same
  suspended-spawn truth independently

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp11.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp11.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp11.exe`
- `build/test-bin/test_session_registry_tmp11.exe`

## Follow-up On 2026-05-22 Suspended-Spawn Owner-Host Resolution Is Now Shared

Another small owner-resolution pattern was still duplicated.

Problem:

Multiple places were independently re-reading the same suspended-spawn ownership fact:

- `ResolveBoundHostSessionForPid(...)`
- `ResolveHostOwnedPidForRequest(...)`
- `HandleResumeRequest(...)`

Each was separately checking variations of:

- does a suspended spawn exist for this pid?
- does it have a non-zero owner host session id?
- is the current/requesting host that owner, or should it be rejected?

Behavior was correct, but the same owner-host fact still had multiple small inline
read paths.

Updated:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Changes:

### 1. Added shared suspended-spawn owner-host helpers

New helpers:

- `ResolveSuspendedSpawnOwnerHostSessionId(...)`
- `IsForeignSuspendedSpawnOwner(...)`

They centralize:

- reading the authoritative owner host session id for a suspended spawn
- deciding whether a given host session is foreign to that suspended owner

### 2. Existing host/pid owner paths now reuse the same owner-host fact

Updated callers:

- `ResolveBoundHostSessionForPid(...)`
- `ResolveHostOwnedPidForRequest(...)`
- `HandleResumeRequest(...)`

This preserves the same external behavior:

- suspended owner host wins over coarse pid binding
- foreign rebound hosts are still rejected
- resume still refuses a gate-held spawn owned by another host session

Why this matters:

- another child-owned ownership fact now has one shared read path
- it reduces the chance that future edits accidentally drift between:
  - host->pid binding resolution
  - response forwarding owner resolution
  - resume ownership enforcement
- it is one more small convergence step toward single-source suspended-spawn truth

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_tmp12.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_tmp12.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_tmp12.exe`
- `build/test-bin/test_session_registry_tmp12.exe`

## Follow-up On 2026-05-22 Spawn Response Release Is Now A Single Registry Boundary

Another shared-path duplication remained after the earlier stage-aware cleanup.

Problem:

- `spawn_controller` cleared:
  - `response_pending = false`
- and then separately decided whether to push the transaction to:
  - `kReadyForScriptLoad`
- but the real meaning of that transition is "the spawn response hold has been released
  for a runtime-ready child"

That made the response-release boundary split across:

- registry state mutation
- controller-side post-processing

which is the same kind of multi-writer boundary this plan has been systematically
shrinking.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Changes:

### 1. `SetSpawnResponsePending(false)` now owns the response-release promotion

When a suspended spawn entry is:

- no longer `response_pending`
- and its authoritative ready stage is already `kRuntimeReady`

the registry now promotes it directly to:

- `kReadyForScriptLoad`

using the same monotonic state guard already in place.

Why:

- the registry is the right place to express "response hold released"
- once that hold is lifted for a runtime-ready child, the transaction is immediately
  script-load ready
- the controller no longer needs a second parallel "if runtime-ready then advance"
  clause for the same boundary

### 2. `spawn_controller` no longer re-performs that same transition

After `SendSpawnResponse(...)` succeeds, `spawn_controller` now:

- clears `response_pending`
- re-resolves authoritative stage / identity

but does not separately call:

- `UpdateSpawnState(..., kReadyForScriptLoad)`

That release step is now centralized in the registry boundary above.

### 3. Added direct regression coverage

New regression:

- `TestClearingSpawnResponsePendingPromotesRuntimeReadySpawnToScriptLoadReady()`

It verifies:

- runtime-ready suspended spawn starts held in `kWaitingRuntimeReady`
- setting `response_pending = true` then clearing it back to `false`
  promotes the transaction to `kReadyForScriptLoad`

Why this matters:

- one more shared state transition now has a single source of truth
- controller and registry are less likely to drift on "who actually released the hold"
- this continues the broader `child-owned` cleanup: stage facts live in the registry,
  and boundary releases are expressed once instead of reconstructed in multiple places

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`

## Follow-up On 2026-05-22 Direct Spawn-Suspended Creation Uses Stage-Aware State

There was still one more coarse state-entry path after the earlier bind cleanup.

Problem:

- even after `BindHostToResolvedPendingSpawn(...)` became stage-aware,
  `MarkSpawnSuspended(...)` still created fresh suspended spawn transactions with:
  - `kWaitingAgentReady`
  for every case
- that meant any path that directly established a suspended spawn entry with known
  child stage:
  - control-ready
  - runtime-ready but still held behind response boundary
  still reset the transaction to the older generic state

This was the same category of drift as before, just through a different constructor path.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook\kanxue\Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook\kanxue\Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook\kanxue\Nook/tests/communication/test_spawn_controller_late_promotion.cpp)

Changes:

### 1. `MarkSpawnSuspended(...)` now uses the same stage-aware pre-response mapping

Fresh suspended entries now initialize state through the same mapping used by the bind path:

- `kControlReady -> kWaitingRuntimeReady`
- `kRuntimeReady -> kWaitingRuntimeReady`
- `kNone -> kWaitingAgentReady`

Existing suspended entries also opportunistically upgrade their state through the same
mapping when the authoritative ready stage is strengthened.

Why runtime-ready still stays in `kWaitingRuntimeReady` here:

- a runtime-ready child may still be intentionally held behind:
  - pending spawn response delivery
  - or later host-side script-load release
- so this remains a pre-release held state, not immediate script readiness

### 2. Test expectations were aligned to the tighter state model

Updated regressions now expect:

- known control-ready suspended spawn entries to already be in `kWaitingRuntimeReady`
- mismatched runtime/control events to preserve that more accurate held state
- late-promotion paths to start from `kWaitingRuntimeReady` when control-ready was already
  known

### 3. Added direct registry coverage for constructor-path semantics

New coverage asserts:

- `MarkSpawnSuspended(kControlReady)` produces `kWaitingRuntimeReady`
- `MarkSpawnSuspended(kRuntimeReady)` also remains in held `kWaitingRuntimeReady`

Why this matters:

- both major suspended-spawn entry paths now preserve the known child stage
  instead of collapsing it back to "waiting for any agent"
- this further reduces shared-path lifecycle reconstruction
- it is another concrete step toward `child-owned` truth at every registry boundary

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`

## Follow-up On 2026-05-22 Bound Spawn Context Uses Stage-Accurate Pre-Response State

Another `child-owned` mismatch remained at the host-bind boundary.

Problem:

- `BindHostToResolvedPendingSpawn(...)` already knew whether the child had resolved at:
  - control-ready
  - runtime-ready
- but it still initialized the bound suspended transaction state as:
  - `kWaitingAgentReady`
  regardless of the known child stage

That was too coarse:

- a control-ready child should already be past "waiting for any agent"
- a runtime-ready child seen before the spawn response should still be held behind the
  response boundary, but it is also past "waiting for any agent"

This gap mattered because later shared paths then had to re-infer what stage the child
had really reached, instead of the bind boundary carrying that fact forward.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_spawn_controller_late_promotion.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_spawn_controller_late_promotion.cpp)

Changes:

### 1. Bound pending-spawn state now reflects the known pre-response child stage

Added `SpawnStateForPendingReadyStage(...)`.

Binding now maps:

- `kControlReady -> kWaitingRuntimeReady`
- `kRuntimeReady -> kWaitingRuntimeReady`
- `kNone -> kWaitingAgentReady`

Why runtime-ready still maps to `kWaitingRuntimeReady` here:

- before the host receives the `SpawnResponse`, the transaction is still intentionally
  held behind the response boundary
- that boundary controls when script-side operations may proceed
- so this is not "ready for script load" yet, but it is also not "waiting for any agent"

### 2. Late-promotion now accepts the accurate pre-runtime wait state

`MaybePromoteLateBoundControlReadyChild(...)` previously only allowed promotion when the
transaction state was exactly `kWaitingAgentReady`.

After the bind-state tightening above, that became too narrow. Promotion now accepts both:

- `kWaitingAgentReady`
- `kWaitingRuntimeReady`

while still rejecting:

- `kReadyForScriptLoad`
- `kScriptLoadDispatched`
- runtime-ready authoritative children

This keeps the child-owned stage model coherent without losing the existing
late-promotion fallback behavior.

### 3. Added and updated regressions for the new boundary

Coverage now asserts:

- control-ready bind lands in `kWaitingRuntimeReady`
- runtime-ready bind before the response boundary also remains in `kWaitingRuntimeReady`
- runtime-ready held before response does not silently regress to "waiting for agent"
- late-promotion still triggers from the control-ready pre-runtime wait state

Why this matters:

- one more shared boundary now preserves the child's known stage instead of resetting it
  to a generic earlier state
- this reduces the amount of lifecycle reconstruction later in the controller/handler
  paths
- it is another concrete step toward `child-owned` spawn truth instead of
  host-side "best guess" state

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `build/test-bin/test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_spawn_controller_late_promotion.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_spawn_controller_late_promotion_current.exe`
- `build/test-bin/test_spawn_controller_late_promotion_current.exe`
