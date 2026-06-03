# Strict Zygote-Control Java Ready Bootstrap Cleanup

## Goal

Record the latest strict spawn stabilization work around `Java.ready` bootstrap
ownership, and the follow-up cleanup that removed temporary debug output from the
user-visible script message surface.

This round did not change the intended spawn architecture. It tightened the current
baseline after a real regression where:

- strict spawn intermittently timed out after `SCRIPT_LOAD`
- a crash was observed inside the embedded agent runtime
- temporary `java-ready-debug:*` messages leaked into normal CLI output

## Real failure seen on device

One strict-path failure was not just a slow ready gate. A tombstone showed:

- `SIGSEGV`
- after `SCRIPT_LOAD`
- before `SCRIPT_LOAD_RESP`
- inside `/memfd:libnook-agent (deleted)`
- with ART `ResumeAll()` on the stack

Separately, a later JS bootstrap regression exposed:

- `java-ready-debug:queued:1`
- `java-ready-debug:poll-drain`
- `java-ready-callback-error:ReferenceError: 'defaultLoaderHandle' is not defined`

That second regression proved the current `Java.ready` bootstrap string had been edited
in a way that removed required class-loader helper state.

## Root cause

The current strict path already had native-side spawn-gate ownership for the early
bootstrap window.

At the same time, the JS-side `Java.ready` implementation still carried behavior that
had been shaped around its own readiness-hook ownership model. During stabilization
work, it also temporarily emitted direct debug `send(...)` messages into the normal
script channel.

The important conclusions from this round were:

1. strict spawn should not reintroduce a second JS-owned `Instrumentation` readiness
   hook path on top of the native bootstrap owner
2. class-loader aware helpers such as:
   - `defaultLoaderHandle`
   - `isLoaderWrapper(...)`
   - `getLoaderHandle(...)`
   are still required by the JS bootstrap surface
3. temporary readiness debug messages should not remain on the normal user-visible
   script message path once the issue is understood

## Fixes applied

### 1. Keep readiness on the polling / native-state model

Updated:

- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)

Change:

- `Java.ready(...)` continues to gate on:
  - `Java._isClassLoaderReady()`
  - `Java._isLifecycleReady()`
- readiness drains callbacks through the polling path instead of reviving the older
  JS-side `Instrumentation` install path

Why:

- the strict path already has a native bootstrap owner during the sensitive spawn
  window
- duplicating readiness ownership in JS increases race risk and was the most coherent
  explanation for the observed post-`SCRIPT_LOAD` instability

### 2. Restore loader-helper definitions required by the JS bridge

Updated:

- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)

Change:

- restored:
  - `defaultLoaderHandle`
  - `isLoaderWrapper(...)`
  - `getLoaderHandle(...)`

Why:

- `Java.use`
- `Java.choose`
- `Java.cast`
- `Java.retain`
- `Java.registerClass`

all still rely on the default loader handle model in the current bridge.

Without these helpers, strict and default spawn could both load a script and then fail
inside the queued Java callback path.

### 3. Remove temporary `java-ready-debug:*` script-message leakage

Updated:

- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- [tests/headers/test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)

Change:

- removed temporary debug sends:
  - `java-ready-debug:queued:*`
  - `java-ready-debug:poll-drain`
  - `java-ready-debug:immediate`
- kept the actual readiness logic unchanged
- updated the source-string regression so these debug messages are now forbidden

Why:

- those messages were useful during root-cause isolation
- they should not remain on the normal CLI/script surface once strict spawn is back to a
  usable baseline

## Regression coverage

Updated:

- [tests/headers/test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)

Coverage now asserts:

- `Java.ready(...)` is still present
- lifecycle / class-loader readiness helpers are still wired
- old JS-side `Instrumentation` hook strings are still absent
- temporary `java-ready-debug:*` strings are absent from the bootstrap source

## Local verification

Verified in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/headers/test_java_ready_object_bridge.cpp -o build/test-bin/test_java_ready_object_bridge_current.exe`
- `.\build\test-bin\test_java_ready_object_bridge_current.exe`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner host.nook-py.tests.test_cli.CliTests.test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`

Observed:

- header/source regression passed
- CLI banner regressions passed

## Follow-up On 2026-05-20 Host Spawn Ready Identity Matching

Another strict-path risk remained on the host side even after the server/runtime cleanup.

`HostSpawnClient::SpawnAndWait(...)` previously accepted the first runtime-stage
`AGENT_READY` that matched:

- `pid`
- `stage == runtime`

That was still too loose for repeated strict spawn attempts. If a stale or foreign
runtime-ready event arrived for the same pid but the wrong process identity, the host
could incorrectly treat that as success and move on to script operations.

Updated:

- [src/communication/host/host_spawn_client.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.h)
- [src/communication/host/host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.cpp)
- [tests/communication/test_host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_spawn_client.cpp)

Changes:

- `TryTakeMatchingAgentReadyLocked(...)` now matches runtime-ready events by:
  - `pid`
  - runtime stage
  - requested spawn `identifier` / runtime `process_name` when both are present
- added a regression where:
  - the host first receives a runtime-ready event for the correct pid but wrong process
    name
  - then receives the correct runtime-ready event
  - only the correct event may satisfy `SpawnAndWait(...)`

Why this matters:

- it closes another stale-event seam that can show up during strict retries
- it keeps host-side spawn completion aligned with the same identity boundary already
  being enforced on the server side
- it reduces the chance of "spawn looks ready but only part of the later hook/script
  path works" caused by a mismatched runtime-ready event being consumed too early

Additional verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_host_spawn_client_current.exe`
- `.\build\test-bin\test_host_spawn_client_current.exe`

## Follow-up On 2026-05-20 Host Spawn Ready Sequence Boundary

There was still one more host-side stale-event seam after identity matching.

Even after requiring:

- matching `pid`
- runtime stage
- matching spawn `identifier` / runtime `process_name`

`HostSpawnClient::SpawnAndWait(...)` could still incorrectly consume a runtime-ready
event that had arrived **before** the corresponding `SpawnResponse`.

That is not a safe completion signal for the current architecture. The authoritative
server-side ordering is:

1. host receives `SpawnResponse`
2. host then waits for the runtime-stage `AGENT_READY` belonging to that transaction

If the host accepts a pre-response runtime-ready event, repeated strict retries can
still be poisoned by stale earlier traffic.

Updated:

- [src/communication/session/session.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/session/session.h)
- [src/communication/session/session.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/session/session.cpp)
- [src/communication/host/host_spawn_client.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.h)
- [src/communication/host/host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.cpp)
- [tests/communication/test_host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_spawn_client.cpp)

Changes:

- `Session` now tracks a monotonically increasing received-frame sequence
- `SendRequest(...)` can return the receive-sequence of the matched response frame
- host-side queued `AGENT_READY` events now carry that receive sequence
- `SpawnAndWait(...)` only accepts runtime-ready events whose sequence is strictly after
  the received `SpawnResponse`

Added regression:

- deliver runtime `AGENT_READY`
- then deliver `SpawnResponse`
- then deliver the real current runtime `AGENT_READY`
- `SpawnAndWait(...)` must ignore the pre-response event and only complete on the
  post-response runtime-ready event

Why this matters:

- it closes another stale retry-poisoning seam in the strict path
- it aligns host-side acceptance with the server-side transaction boundary
- it reduces the chance of a late/foreign earlier event making the host believe the
  spawned child is ready before the current transaction has actually crossed the normal
  response boundary

## Follow-up On 2026-05-20 Pending Spawn Token Must Not Rebind To Wrong Pid

Another server-side transaction-ownership gap remained around `pending_spawn`.

Before this fix, `SessionRegistry::ResolvePendingSpawn(...)` trusted:

- `spawn_token`
- process-name checks for runtime stage

but it did not reject a ready event that carried the **correct token** while arriving
from the **wrong pid** after that host session had already been rebound to a different
pid.

That meant an older or foreign child could still try to satisfy the current pending
spawn just by reusing the expected token.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `ResolvePendingSpawn(...)` now checks whether the pending spawn's owning host session
  is already bound to a specific pid
- if that host is already bound and the incoming ready event uses a different pid, the
  pending spawn resolution is rejected

Added regression:

- register pending spawn token for `com.demo.target`
- bind the same host session to pid `60002`
- deliver runtime `AGENT_READY` with the correct token but pid `60077`
- pending spawn must remain unresolved
- no suspended spawn entry may be created for the foreign pid

Why this matters:

- it closes another "old event poisons new spawn" seam on the server side
- it keeps token ownership aligned with the already-bound host/pid transaction
- this is especially relevant for repeated strict retries, where stale zygote-side
  events can otherwise drift into the next logical spawn transaction

### Important nuance discovered during verification

The first version of this guard was too broad.

It blocked **all** token resolutions whenever the owning host session already had any
other pid binding, but one existing and correct flow still needs to work:

- a host already owns an older suspended spawn pid
- a new resolved pending spawn for the same host is promoted onto a new pid
- the registry must allow that ownership to rebind from the old suspended pid to the
  new resolved pid

So the final rule is narrower:

- reject token resolution when the owning host is bound to a different pid **unless**
  that other pid is already an owned suspended spawn for the same host

This keeps legitimate same-host suspended-spawn rebinding intact while still rejecting
foreign pid attempts to satisfy the current token.

Additional verification after narrowing:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_session_registry_current.exe`
- `.\build\test-bin\test_session_registry_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `.\build\test-bin\test_server_handlers_spawn_ready_subset_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_current.exe`
- `.\build\test-bin\test_server_handlers_current.exe`

## Follow-up On 2026-05-20 Resolved Spawn Token Pid Must Not Drift Across Stages

One more transaction-identity gap remained even after guarding token resolution against
obviously foreign pids.

Before this fix, a pending spawn could still behave like this:

1. control-stage `AGENT_READY` resolves `spawn_token` onto pid `A`
2. later runtime-stage `AGENT_READY` arrives with the **same token** but pid `B`
3. the pending spawn upgrades to runtime-ready on pid `B`

That is not a valid transaction upgrade. Once a token has already resolved onto a
specific pid, later stage upgrades must stay on that pid.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

Changes:

- `ResolvePendingSpawn(...)` now rejects any later resolution for a token if:
  - the entry is already ready
  - it already owns pid `A`
  - the incoming ready event tries to upgrade it on pid `B`

Added regressions:

- registry-level:
  - control-ready resolves token on pid `4321`
  - runtime-ready on pid `4322` must be rejected
- handler-level:
  - control `AGENT_READY` resolves token on pid `60009`
  - runtime `AGENT_READY` from pid `60010` must not steal that transaction

Why this matters:

- it removes another stale/foreign event path that could mutate an already-owned spawn
  transaction
- it keeps stage upgrades bound to a single child identity
- this directly matches the strict-path requirement that the promoted child identity
  remain stable across control-ready -> runtime-ready transition

## Follow-up On 2026-05-20 Finalize Success Must Not Leave Orphan Suspended Spawn State

Another strict-path transaction leak remained on the server finalize-success path.

The sequence was:

1. host starts spawn
2. control-ready resolves and finalize proceeds
3. host disconnects before `SpawnResponse` is sent
4. finalize still succeeds
5. server returns early because the host session is gone

Before this fix, that branch could still leave behind:

- a suspended spawn entry
- host/pid transaction ownership
- cached script-message state for that pid

That is exactly the kind of ghost state that can poison the next strict or default
spawn attempt.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)

Change:

- when finalize succeeds but the host session is already gone, the server now calls
  `ClearSpawnTransactionByPid(authoritative_pid)` before dropping the response

Why this matters:

- it keeps finalize-success and finalize-timeout cleanup behavior aligned
- it prevents orphan suspended-spawn ownership from surviving a dead host transaction
- it removes one more stale-state seam that can show up later as:
  - strict spawn timeout
  - partial hook behavior
  - wrong cached runtime/script replay

Additional verification:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test-bin/test_server_handlers_current.exe`
- `.\build\test-bin\test_server_handlers_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_current.exe`
- `.\build\test-bin\test_server_handlers_spawn_ready_subset_current.exe`
- `.\build\test-bin\test_session_registry_current.exe`
- `.\build\test-bin\test_host_spawn_client_current.exe`

## Follow-up On 2026-05-20 Finalize Failure Cleanup Must Use Full Transaction Cleanup

The finalize-success orphan cleanup exposed one more asymmetry nearby.

When `FinalizeSpawn(...)` failed, the server previously did:

- `RemoveAgentSessionByPid(authoritative_pid)`
- `UnbindHostSession(...)`
- `ClearSpawnSuspended(authoritative_pid)`
- `ClearPendingSpawn(spawn_token)`

That was close, but still weaker than the timeout and host-missing success cleanup
paths because `ClearSpawnSuspended(...)` only removes the suspended entry. It does not
act as the single "drop the whole spawn transaction" primitive.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Change:

- finalize-failure cleanup now uses:
  - `ClearSpawnTransactionByPid(authoritative_pid)`
  instead of:
  - `ClearSpawnSuspended(authoritative_pid)`

Regression tightened:

- the existing finalize-failure test now also asserts:
  - `TakeScriptMessageFrames(pid).empty()`

Why this matters:

- it makes success / timeout / finalize-failure cleanup semantics converge on the same
  transaction-level primitive
- it reduces the chance of script-cache or pid-ownership residue surviving a failed
  strict spawn attempt
- this is exactly the kind of leftover state that can later show up as inconsistent
  strict retry behavior on device

## Follow-up On 2026-05-20 Response-Pending Must Begin At Transaction Bind Time

One more strict-path timing seam remained around when a resolved spawn transaction
became "response pending".

Before this change, `BindHostToResolvedPendingSpawn(...)` created or rebound the
spawn-owned transaction, but the `response_pending` bit was only set later by
`spawn_controller.cpp`.

That left a narrow ordering gap:

1. control-ready resolves pending spawn
2. host is rebound to the child pid
3. runtime-ready arrives very quickly
4. server-side `HandleAgentReady(...)` may observe:
   - bound host exists
   - no `response_pending` bit yet
5. runtime-ready can be treated as forwardable too early

The host-side receive-sequence guard can still reject that event, but the server-side
ordering should not depend on the host cleaning it up later.

Updated:

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [tests/communication/test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)

Change:

- `BindHostToResolvedPendingSpawn(...)` now sets `response_pending = true` immediately
  when it creates or rebinds the suspended spawn transaction

Regression tightened:

- resolved-pending-spawn rebind coverage now asserts the new suspended entry is:
  - `suspended == true`
  - `response_pending == true`

Why this matters:

- it makes the transaction boundary explicit at the exact point ownership is bound
- it reduces reliance on later outer-layer sequencing to suppress early runtime-ready
  forwarding
- it aligns better with the intended strict-path model:
  - bind transaction
  - hold runtime-ready
  - send spawn response
  - replay runtime-ready only after response boundary

Additional regression coverage added after this change:

- [tests/communication/test_server_handlers_spawn_ready_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers_spawn_ready_subset.cpp)

New coverage now explicitly proves:

- once a pending spawn is rebound onto the host-owned transaction
- runtime-ready may arrive immediately
- the server still must:
  - cache/update runtime-ready state
  - keep `response_pending == true`
  - avoid forwarding `AGENT_READY` to the host before `SpawnResponse`

## Follow-up On 2026-05-20 Python Host Spawn Path Must Clear Stale Ready And Script Queues

The real user-facing `nook-cli` spawn path goes through the Python host device layer,
not the C++ `HostSpawnClient`.

That layer already waited for the correct runtime-stage `AGENT_READY`, but it did not
clear old queued events before starting a new spawn transaction.

So after repeated runs, two stale-event classes could survive into the next spawn:

- old `_agent_ready_events`
- old `_script_messages`

This is the Python equivalent of the transaction-boundary leaks already being fixed on
the server and C++ host helper side.

Updated:

- [host/nook-py/nook/device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)
- [host/nook-py/tests/test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)

Change:

- `Device.spawn(...)` now clears:
  - `_agent_ready_events`
  - `_script_messages`
  under the state lock before issuing a new `SPAWN_REQUEST`

Why this matters:

- a stale runtime-ready must not satisfy the next spawn transaction
- a stale script message must not be delivered as if it belongs to the newly spawned
  process
- this directly targets repeated-run flakiness that otherwise appears as:
  - partial hook behavior
  - out-of-order script output
  - confusing "current run" vs "previous run" message mixing

Additional verification:

- `python -m unittest host.nook-py.tests.test_client.DeviceTests.test_spawn_clears_stale_agent_ready_events_before_new_transaction host.nook-py.tests.test_client.DeviceTests.test_spawn_clears_stale_script_messages_before_new_transaction`
- `python -m unittest host.nook-py.tests.test_client`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner host.nook-py.tests.test_cli.CliTests.test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`

## Follow-up On 2026-05-20 Python Spawn Runtime-Ready Matching Must Respect Process Identity And Response Order

After queue cleanup, one more Python host-side gap still remained.

`Device.spawn(...)` previously waited for runtime-ready using only:

- `pid`
- `stage == runtime`

That was too loose for repeated strict/default spawn attempts, because Python could
still accept:

- a runtime-ready for the correct pid but wrong process name
- a runtime-ready delivered before the matching `SPAWN_RESPONSE`

The C++ `HostSpawnClient` had already been tightened against both cases. The real
`nook-cli` path needed the same protection.

Updated:

- [host/nook-py/nook/device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)
- [host/nook-py/tests/test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)

Changes:

- Python `Device` now tracks a monotonically increasing receive sequence for incoming
  frames
- `SPAWN_RESPONSE` retrieval can return the response frame plus its receive sequence
- spawn runtime-ready wait now matches by:
  - `pid`
  - runtime stage
  - requested process identity when present
  - receive sequence strictly after `SPAWN_RESPONSE`

Why this matters:

- it closes the same stale-event seam in the actual Python CLI path
- it keeps Python spawn completion aligned with the current transaction boundary
- it reduces repeated-run failures where the host thinks the new child is ready based
  on older traffic

Additional verification:

- `python -m unittest host.nook-py.tests.test_client.DeviceTests.test_spawn_ignores_mismatched_runtime_agent_ready_for_same_pid host.nook-py.tests.test_client.DeviceTests.test_spawn_ignores_runtime_agent_ready_delivered_before_spawn_response`
- `python -m unittest host.nook-py.tests.test_client`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner host.nook-py.tests.test_cli.CliTests.test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`

## Follow-up On 2026-05-20 Python Script Unload Must Revoke Old Script Identity

After the Python spawn boundary work, one more user-visible lifecycle leak remained in
the script object itself.

Before this change, `Script.unload()` only sent the unload request. It did **not**:

- revoke message callbacks bound to the current `script_id`
- clear the script object's own `script_id`

That meant an unloaded script object could still behave like an active one:

- `post(...)`
- `call(...)`
- `wait_for_message(...)`

and old script-id callbacks could remain registered on the device.

Updated:

- [host/nook-py/nook/script.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/script.py)
- [host/nook-py/tests/test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)

Change:

- `Script.unload()` now:
  - removes registered message callbacks for the current `script_id`
  - clears `self.script_id` in a `finally` block

Why this matters:

- a script object should not keep a stale live identity after unload
- old callbacks should not remain attached to a dead script id
- this reduces repeated-run leakage where output from an unloaded script appears to
  belong to the current active session

Additional verification:

- `python -m unittest host.nook-py.tests.test_client.DeviceTests.test_unload_clears_script_id_and_prevents_further_use host.nook-py.tests.test_client.DeviceTests.test_unload_removes_registered_message_callbacks_for_old_script_id`
- `python -m unittest host.nook-py.tests.test_client`
- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_repl_attach_ctrl_c_unloads_active_script host.nook-py.tests.test_cli.CliTests.test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner host.nook-py.tests.test_cli.CliTests.test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`

## Follow-up On 2026-05-20 CLI Wait Paths Should Prefer Current Script Identity

After tightening the Python device and script layers, the CLI still had one broad
message-consumption surface.

Several command paths knew the active `script_id`, but still consumed messages using:

- `wait_for_script_message(..., script_id=None)`

That meant the CLI could still accept unrelated messages from the same device/session
when it should have preferred the active script boundary.

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- added a helper that prefers:
  - current `script_id`
  and falls back to:
  - `script_id == 0` broadcast messages
- applied that helper to:
  - REPL message loop
  - wait-mode script streaming when a concrete script is loaded
  - unload cleanup drain
  - `post` command response wait

Why this matters:

- it narrows the CLI message surface to the active script when possible
- it still preserves compatibility with script-id `0` broadcast messages
- it reduces another class of "message belongs to some other script/run" confusion

Verification completed:

- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_spawn_command_waits_and_prints_messages_until_interrupted host.nook-py.tests.test_cli.CliTests.test_attach_command_waits_and_emits_json_lines host.nook-py.tests.test_cli.CliTests.test_attach_command_wait_cleanup_drains_unload_messages host.nook-py.tests.test_cli.CliTests.test_post_command_spawns_loads_script_and_posts_message`

Note:

- full `python -m unittest host.nook-py.tests.test_cli` did not complete within the
  current timeout window, but the targeted wait/drain/post regressions affected by this
  change were verified green.

## Follow-up On 2026-05-20 CLI Wait Loops Must Not Hot-Spin And No-Script Frida Spawn Must Enter REPL

The narrowed CLI message-routing work exposed one more host-side issue during full
Python CLI verification.

Two separate problems were involved:

1. several CLI wait loops treated immediate `TimeoutError` as a tight `continue`
2. top-level Frida-style spawn without `-l` still normalized to `command="spawn"` with
   `wait=True`, even though there was no script-bound message stream to wait on

In the fake-device test harness, the first issue could starve other threads and make
the suite appear to hang. The second issue could leave:

- `nook-cli -U -f com.demo.target --strict-zygote-control`

stuck in a message wait loop instead of entering the expected interactive spawn session.

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- added `time.sleep(0)` on `TimeoutError` in:
  - `_repl_message_loop(...)`
  - `_wait_for_messages(...)`
  - `_wait_for_messages_for_script(...)`
- imported `time` explicitly for that yield path
- changed Frida-style argument normalization so:
  - `-U -f <package>` without `-l`
  - now maps to `command="repl", repl_mode="spawn"`
  - instead of a plain wait-mode spawn command
- aligned CLI regression expectations with the current targeted-script wait and unload
  cleanup behavior

Why this matters:

- it removes a test-harness-visible hot-spin that was masking the real remaining CLI
  failures
- it keeps no-script Frida-style spawn aligned with the intended interactive usage
  model instead of waiting forever on a non-existent script stream
- it preserves the stricter current-script message boundary while making the full CLI
  regression set runnable again

Verification completed:

- `python -m unittest -v host.nook-py.tests.test_cli.CliTests.test_frida_style_spawn_with_strict_zygote_control_passes_internal_marker`
- `python -m unittest -v host.nook-py.tests.test_cli.CliTests.test_frida_style_top_level_spawn_waits_for_messages_without_auto_unload_on_stdin_eof`
- `python -m unittest host.nook-py.tests.test_cli`
- `python -m unittest host.nook-py.tests.test_client`

Observed:

- full `host.nook-py.tests.test_cli` now completes green
- full `host.nook-py.tests.test_client` remains green
- the current host-side strict/default spawn boundary cleanup is now backed by both the
  targeted regressions and the full Python CLI/client suites

## Follow-up On 2026-05-20 Spawn Success Response Send Failure Must Clear Transaction State

After the Python host/CLI cleanup, one more server-side transaction residue seam was
identified around the spawn success boundary.

Before this fix, the success path did:

1. finalize spawn
2. mark the bound spawn entry ready / waiting
3. attempt to send `SpawnResponse` to the host
4. continue with cached ready replay logic

If step 3 failed because the host-side send path was already broken, the server could
still leave behind:

- `pid -> host_session` binding
- suspended spawn transaction state
- response-pending / ready-stage state usable by later retries

That is exactly the kind of half-open residue that can poison the next strict or default
spawn attempt.

Updated:

- [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Changes:

- `SendSpawnResponse(...)` now returns `bool`
- on spawn success, if sending the `SpawnResponse` fails:
  - server immediately calls `ClearSpawnTransactionByPid(authoritative_pid)`
  - server skips cached `AGENT_READY` / script-message replay
  - server logs the response-send failure as a dropped spawn success replay path

Why this matters:

- a failed success response is operationally equivalent to a dead host boundary for that
  transaction
- replaying ready/messages after that point cannot help the current request
- leaving the transaction alive creates exactly the stale ownership and cache pollution
  that later shows up as repeated strict-path flakiness

Regression added:

- spawn success response send fails on the host transport
- no spawn response bytes are delivered
- the host/pid binding must be cleared
- no suspended spawn entry may remain for that pid

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/handler/message_dispatcher.cpp -o build/test-bin/test_server_handlers_current12.exe`
- `build/test-bin/test_server_handlers_current12.exe`
- `python -m unittest host.nook-py.tests.test_cli host.nook-py.tests.test_client`

## Current status

After this cleanup, the current baseline is:

- default spawn remains the practical stable path
- strict `zygote-control` remains an experimental but now much cleaner validation path
- `Java.ready` no longer leaks temporary debug markers into normal script output

This does **not** mean strict `zygote-control` is fully finished. It means the current
bootstrap ownership is cleaner, the visible debug residue is gone, and the tree is in a
better state for the next strict-path device revalidation step.

## Follow-up On 2026-05-20 Java.ready Polling Must Not Run Inside Script Load

One more strict/default spawn timing bug was isolated after the earlier bootstrap
cleanup.

The previous `Java.ready(...)` bootstrap still used:

- `setImmediate(pollReadyState)`

to keep polling for:

- `Java._isClassLoaderReady()`
- `Java._isLifecycleReady()`

At the same time, `JsRuntime::Evaluate(...)` finished script load by draining pending
jobs/timers.

That combination was wrong for spawn:

1. script calls `Java.perform(...)`
2. `Java.ready(...)` queues callback and schedules `setImmediate(...)`
3. script load completion drains pending timers/jobs
4. the ready poll spins inside the script-load boundary instead of waiting for native
   spawn-gate readiness

This coherently explained two real device symptoms:

- `[*] Loading 'script.js'...` becomes slow or times out
- first-launch Java hooks can still install too late or inconsistently depending on when
  readiness finally flips

### Fixes applied

Updated:

- [src/agent_runtime/js_runtime.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.h)
- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- [tests/communication/test_js_runtime_pump.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_pump.cpp)
- [tests/headers/test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)

Changes:

- removed JS-side `Java.ready(...)` self-polling via `setImmediate(pollReadyState)`
- added `Java.__nookDispatchReady()` as an explicit runtime-side ready-dispatch entry
- added `JsRuntime::DispatchJavaReadyCallbacks(...)`
- changed spawn-gate readiness hook points to:
  - dispatch queued Java-ready callbacks explicitly
  - then drain pending JS tasks
- changed `JsRuntime::Evaluate(...)` to stop draining timers as part of script-load
  completion; it now only executes immediate pending jobs needed for evaluation/export
  capture

Why this matters:

- script load should only finish evaluation, not impersonate the later app-readiness
  transition
- native spawn-gate ownership now explicitly controls when queued Java-ready callbacks
  are released
- this matches the intended strict-path boundary much more closely:
  - load script
  - wait for class-loader / lifecycle edge in native hooks
  - dispatch Java-ready callbacks
  - resume app

### Local verification

Verified in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_pump.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_pump.exe`
- `build\\test_js_runtime_pump.exe`

Observed:

- script load no longer blocks on a self-polling `Java.ready(...)` loop in the focused
  harness
- queued `Java.ready(...)` callback runs only after the explicit
  `DispatchJavaReadyCallbacks(...)` boundary

## Next

1. Re-run repeated real-device strict spawn validation from this cleaned baseline.
2. If flakiness remains, focus on strict-path timing/order around:
   - runtime ready
   - script load
   - resume
3. Keep default spawn untouched while continuing strict-path convergence work.

## Follow-up On 2026-05-21 Attach And Spawn Must Share Host-Side Runtime-Ready Semantics

After the `Java.ready(...)` bootstrap cleanup, the next remaining semantic split was not
inside the runtime anymore. It was at the host boundary.

Before this change:

- spawn host path returned success only after:
  - `SPAWN_RESPONSE`
  - then a host-visible runtime-stage `AGENT_READY`
- attach host path returned success after:
  - `ATTACH_RESPONSE`
  - while the server internally hid the runtime-ready wait and replayed cached
    `AGENT_READY` afterward

That meant attach and spawn both often worked, but the contract was inconsistent:

- spawn: host explicitly observes runtime readiness before script operations
- attach: host implicitly trusts the server response and only incidentally sees replayed
  runtime readiness later

### Fixes applied

Updated:

- [src/communication/host/host_client.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_client.h)
- [src/communication/host/host_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_client.cpp)
- [host/nook-py/nook/device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)
- [tests/communication/test_host_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_client.cpp)
- [host/nook-py/tests/test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)

Changes:

- `HostClient` now mirrors `HostSpawnClient` and records incoming `AGENT_READY` frames
  through a session message callback
- C++ attach now succeeds only after observing a matching runtime-stage `AGENT_READY`
  after the `ATTACH_RESPONSE` sequence boundary
- Python `Device.attach()` now does the same by reusing the existing `_wait_for_agent_ready(...)`
  path already used by spawn
- control-stage `AGENT_READY` is ignored for attach success, same as spawn success

Why this matters:

- attach and spawn now converge on the same host-side rule:
  success means the host has actually observed runtime readiness
- later script-create/load failures are less likely to come from attach-vs-spawn semantic
  skew
- future work on ready/resume ordering can now target one host contract instead of two

### Regression coverage

Added focused tests proving:

- attach success waits for runtime-stage `AGENT_READY`, not just `ATTACH_RESPONSE`
- attach times out if runtime-stage `AGENT_READY` never arrives
- control-stage `AGENT_READY` alone is not enough to satisfy attach success

### Pitfall caught during implementation

While adding the C++ host wait path, a local deadlock was introduced by accidentally
nesting a `lock_guard` and a `unique_lock` on the same mutex in `HostClient::Attach(...)`.

This never reached device code, but it did hang the focused C++ host regression binary.
It was fixed immediately by switching attach waiting to a single `unique_lock`, matching
the existing spawn host pattern.

### Local verification

Verified in this workspace:

- `python -m unittest host.nook-py.tests.test_client`
- `python -m unittest host.nook-py.tests.test_cli`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_host_client.cpp src/communication/host/host_client.cpp src/communication/session/session.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/transport/transport.cpp -o build/test_host_client_ready.exe`
- `build\\test_host_client_ready.exe`

### Status after this step

- default spawn stays on the current stable path
- strict spawn stays on the current working path
- attach no longer has a weaker success contract than spawn at the host boundary

This still does not mean the entire strict/zygote-control story is finished. It means the
host-visible ready semantics are now materially closer across attach and spawn, which
reduces one more source of non-Frida-like behavior before the next runtime/order cleanup.

## Follow-up On 2026-05-21 Strict Spawn Must Not Be Preempted By Symbi Or Legacy Fallback

The next regression we isolated was not in the native ready flow itself, but in the
strict route policy boundary. A strict request could still be influenced by explicit
`--nook-spawn-backend=symbi` or by legacy fallback flags, which made the experimental
zygote-control route less isolated than intended.

### Fixes applied

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Changes:

- strict requests now keep the strict route boundary even when `--nook-spawn-backend=symbi` is present
- strict requests no longer advertise symbi-first or legacy fallback availability
- added a focused regression to lock the policy boundary

### Verification

Verified in this workspace:

- `build\test_ninjector_spawn_injector_policy_focus.exe`
- `build\test_ninjector_spawn_injector.exe`

### Status

The default stable spawn path remains untouched. The strict route is now narrower and
cleaner, which makes later zygote-control and agent-owned spawn work easier to reason
about without hidden route preemption.

## Follow-up On 2026-05-21 Strict Abort Must Keep Rollback Residue Scoped To The Failed Attempt

One more strict-path cleanup boundary was worth locking explicitly before returning to
the larger `agent-owned stable spawn` work.

The issue was not a new device crash. It was a missing white-box regression around this
failure shape:

- strict `zygote-control`
- zygote route aborts
- launch-stage failure already includes rollback detail
  - `start_target_app failed; rollback failed: clear zygote spawn control failed`

That path already carried a structured failed transaction snapshot, but it did not yet
have a focused regression proving the rollback residue stayed scoped to the failed
attempt instead of degrading into an unstructured abort surface.

Updated:

- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added regression:

- `TestApplyZygoteControlRouteAbortKeepsResidualTransactionScopedToAttempt()`

Coverage now proves:

- strict route abort still reports the structured final error
  - `zygote-control stage=spawn class=hard state=launch-app detail=...`
- the failed transaction snapshot retains:
  - request identifier
  - spawn token
  - armed zygote targets
- this is validated without changing the default `symbi-first` path

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\test-bin\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\test-bin\test_zygote_control_rpc_strict_cleanup_current.exe`

Why this matters:

- strict-path rollback semantics now have one more concrete guardrail before the next
  agent-owned refactor
- the current cleanup work remains isolated from the stable default spawn route

## Follow-up On 2026-05-21 CLI Attach Prompt Should Match Spawn Readiness

One small UX mismatch remained after the host-side attach convergence:

- spawn already printed `Waiting for agent runtime ready...`
- attach previously jumped straight to `Attaching to ...` and then `Attached ...`

That was misleading now that attach also waits for the same runtime-ready boundary.

### Fixes applied

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- attach now prints `Waiting for agent runtime ready...` before calling into the
  host/device attach path
- this keeps the user-visible flow aligned with spawn and avoids implying that attach
  is "ready" earlier than it really is

### Verification

Verified in this workspace:

- `python -m unittest host.nook-py.tests.test_cli`
- `python -m unittest host.nook-py.tests.test_client`

### Status

Attach/spawn are now closer both internally and in the CLI surface. The remaining work
is no longer about simple ready signaling mismatch; it should move back to the remaining
strict-path / zygote-control edge cases rather than host UX drift.

## Follow-up On 2026-05-21 Strict Recorder Cleanup After Failure Snapshot

The next strict cleanup seam was global recorder residue after `FailZygoteControlSpawn()`.

Problem:

- `FailZygoteControlSpawn()` already snapshots the current zygote-control state into the
  per-attempt transaction
- but it left `last_zygote_control_failure_state_` and
  `current_zygote_control_lifecycle_stage_` dirty afterwards
- that made later strict diagnostics vulnerable to stale process-global recorder state
  instead of the current attempt detail or transaction snapshot

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector_route_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector_route_subset.cpp)

Added regression:

- `TestFailZygoteControlSpawnClearsGlobalRecorderAfterSnapshot()`

Behavior now guaranteed:

- strict failure still snapshots recorder state into the failed transaction
- once that snapshot is taken, the process-global strict recorder is cleared immediately
- later requests must resolve from their own transaction/detail path instead of stale
  recorder residue

Implementation detail:

- recorder helpers are now `const`-safe because they only mutate guarded diagnostic state
- this keeps the cleanup inside the existing `const` failure path without widening behavior
  elsewhere

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-22 Symbi Child-Owned Commit Strips Legacy Ncore Residue

This round stayed on the mainline `agent-owned` track and did not expand strict
`zygote-control`.

Problem:

- child-owned `symbi` commits already stored authoritative ownership in `spawn_state`
- but if mixed legacy fields leaked into the same owner record, the symbi commit path
  still preserved:
  - `ncore_path`
  - `materialized_ncore`
- those fields belong to legacy shell-owned fallback semantics, not to child-owned
  symbi ownership

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added regression:

- `TestBuildPendingSpawnCommitStripsLegacyNcoreResidueFromSymbiOwner()`

Behavior now guaranteed:

- symbi child-owned commits still preserve:
  - `identifier`
  - `spawn_token`
  - `agent_path`
  - `materialized_agent`
- but they now strip legacy-only residue:
  - `ncore_path`
  - `materialized_ncore`

Why this matters:

- it sharpens the owner split on the mainline path:
  - legacy shell-owned state stays on the legacy route
  - symbi child-owned state carries only symbi-relevant ownership payload
- this is a direct cleanup step toward cleaner `agent-owned stable spawn`

Additional test-alignment note:

- this round also aligned several older injector tests with the current routing model:
  - route-specific tests that manually seed policy now also set authoritative
    `primary_route`
  - tests expecting default symbi behavior now set `NOOK_PREFER_SYMBI_BACKEND=1`
- those updates do not change runtime behavior; they bring the regression suite in line
  with the current route-selection semantics

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_agent_owned_green.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_agent_owned_green.exe`

## Follow-up On 2026-05-22 Strict Activity-Stage Gate White-Screen Fix

The next strict-path regression turned out to be neither a Java hook install failure nor
an Activity-stage callback miss. The app was white-screening after:

- `[+] Process resumed`
- `lab:frida-0x1:installed`

with the target stuck on the splash/start page.

Root cause found from device logs:

- strict spawn had already moved its blocking point from:
  - `Instrumentation.newApplication(...)`
  - `Instrumentation.callApplicationOnCreate(...)`
  to:
  - `Instrumentation.callActivityOnCreate(...)`
- this was the correct direction because startup-time `MainActivity.get_random()` must be
  hooked before the activity's real `onCreate` path runs
- however, the first Activity-stage implementation still deadlocked:
  - `SpawnGateCallActivityOnCreateHookCallback` entered
  - it waited on `blocking strict activity bootstrap on resume request pid=%u`
  - host later sent resume
  - resume handler set `g_strict_spawn_resume_requested = true`
  - but did not `notify_all()` the waiting condition variable
- result:
  - the strict activity gate never woke up
  - pending Java hooks were never processed for `MainActivity`
  - the gate never released
  - app stayed white-screened after `Process resumed`

Updated:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp)

Implementation:

- kept the strict Activity-stage ownership model:
  - do not block at `newApplication`
  - do not block at `callApplicationOnCreate`
  - wait at `callActivityOnCreate`
- on deferred strict resume:
  - resume handler still records `g_strict_spawn_resume_requested = true`
  - but now also calls `g_spawn_gate_cv.notify_all()`
- once awakened, the Activity-stage callback:
  - synchronously processes pending Java hooks for the concrete activity class
  - pumps runtime Java-ready work
  - releases the strict gate

Behavior now guaranteed:

- strict spawn no longer white-screens simply because host resume arrived without waking
  the Activity-stage gate
- strict spawn keeps the later Activity-stage release point required for startup-time
  Java hook coverage
- `lab:frida-0x1:installed` is no longer a false-positive state where the script loaded
  but the app remained blocked forever

Added regression:

- `test_zygote_control_regressions.cpp` now asserts that the deferred strict resume path
  not only logs:
  - `spawn gate resume handler defer strict release request_pid=%u`
  but also immediately wakes the gate through:
  - `g_spawn_gate_cv.notify_all()`

Real-device outcome after push:

- strict spawn resumed without the previous white-screen deadlock
- app entered normally
- the user confirmed strict path became usable again on-device

## Follow-up On 2026-05-21 Java.ready Queue Is Script-Unload Aware

The next lifecycle cleanup tightened the script unload boundary for deferred
`Java.ready(...)` callbacks.

Problem:

- `Java.ready(...)` callbacks queued before Java/class-loader readiness were now
  tagged with the original `script_id`
- but `ScriptRegistry::UnloadScript()` only removed native/C++ owned resources
  and did not remove JS-side pending ready callbacks
- if readiness fired after unload, the stale callback could still run under the
  old script id and install hooks after the host had already unloaded the script

Updated:

- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- [tests/communication/test_js_runtime_pump.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_pump.cpp)

Behavior now guaranteed:

- queued `Java.ready(...)` entries remain associated with their original script id
- `JsRuntime::RemoveMessageHandler(script_id)` calls the JS bootstrap cleanup
  hook before freeing the rest of the script-owned runtime resources
- callbacks queued by an unloaded script are dropped and will not run when
  `JsRuntime::DispatchJavaReadyCallbacks()` later fires
- callbacks from other still-loaded scripts are preserved

Implementation detail:

- added `Java.__nookDropReadyCallbacksForScript(scriptId)` inside the bootstrap
  closure so it can filter the private `readyCallbacks` array
- added `DropJavaReadyCallbacksForScriptLocked(...)` on the C++ side and invoked
  it from `RemoveMessageHandler(...)` while the QuickJS context is still alive

Verification in this workspace:

- `E:\\MinGW\\ucrt64\\bin\\g++.exe -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_pump.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_pump_current.exe`
- `build\\test_js_runtime_pump_current.exe`
- `E:\\MinGW\\ucrt64\\bin\\g++.exe -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `build\\test_java_ready_object_bridge.exe`

Follow-up verification after device testing:

- default spawn, explicit `--symbi`, and strict zygote-control were all reported
  hook-effective on the Android 11 device
- logcat showed the new single-file server path still using embedded blobs:
  `agent=__embedded_agent__`, `SpawnViaSymbiEmbedded`, and
  `InjectEmbeddedSoByPidAtomic`
- logcat showed Java hooks installed before app resume-side callbacks:
  `install hook request ... get_random`, `install hook request ... check`
- logcat showed runtime-side messages:
  `lab:frida-0x1:installed`, `lab:frida-0x1:hit:get_random`, and
  `lab:frida-0x1:result:forced-random=5:expected-input=14`
- added an extra host regression, `TestUnloadScriptKeepsOtherQueuedJavaReadyCallbacks`,
  proving unload cleanup is scoped to the target script and preserves other scripts'
  queued ready callbacks

## Follow-up On 2026-05-21 Deferred Java.ready Callback Preserves Script Context

Default and symbi spawn both reproduced this runtime error after script load:

- `java-ready-callback-error:InternalError: Java implementation install must run while loading a script`

Root cause:

- `Java.ready()` stored only callback functions in its pending queue
- when spawn-gate later dispatched queued ready callbacks, `state.current_script_id`
  was already back to `0`
- Java method `implementation = fn` correctly rejected the install because it could
  no longer associate the hook callback with the script that created it

Updated:

- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- [tests/communication/test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)

Implementation:

- added `__nookGetCurrentScriptId()` so bootstrap code can capture the script that
  queued a `Java.ready()` callback
- added `__nookRunInScript(scriptId, callback)` so delayed ready dispatch runs
  the callback under the original script context
- changed `readyCallbacks` entries from bare functions to `{ scriptId, callback }`
- added `TestJavaReadyDeferredCallbackPreservesScriptContextForImplementationInstall()`
  covering a delayed `Java.ready()` callback that installs a Java implementation

Verification in this workspace:

- Android build compiled `src/agent_runtime/js_runtime.cpp` into `libnook-agent.so`
- regenerated embedded agent and ncore blobs
- rebuilt `libs/arm64-v8a/nook-server`, size `8188632`
- `build\\test-bin\\test_java_ready_object_bridge_current.exe`
- device default spawn log showed:
  - no `java-ready-callback-error`
  - `script load ok script_id=1`
  - Java hooks installed for `MainActivity.get_random` and `MainActivity.check`
  - `SendJsonToHost: script_id=1 ... lab:frida-0x1:installed`
  - `lab:frida-0x1:hit:get_random`
  - `lab:frida-0x1:hit:check:left=5:right=14`
- user retested after push and confirmed hook behavior is normal

## Follow-up On 2026-05-21 Agent-Owned Default Owner Split

The next agent-owned cleanup clarified the default finalize owner boundary without changing the
stable default spawn backend choice.

Goal:

- keep `shell_owner_state` authoritative for legacy shell-owned routes
- keep `spawn_state` authoritative only for child-owned routes like symbi
- keep `zygote_control_transaction` authoritative for zygote-controlled routes
- stop letting compatibility-only `spawn_state` act like a legacy owner during finalize/retry

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Behavior now guaranteed:

- default symbi ownership still resolves from `spawn_state`
- legacy shell ownership still resolves from `shell_owner_state`
- compatibility-only `spawn_state` no longer wins owner extraction by itself

Verification:

- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Agent-Owned Retry Restore Boundary

The next owner-boundary cleanup tightened retry restoration after finalize failure.

Goal:

- legacy retry restore must repopulate `shell_owner_state`
- symbi retry restore may still use `spawn_state`
- retry recovery must not reintroduce legacy owner residue into `spawn_state`

Updated:

- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added regression:

- `TestRestoreOwnedSpawnStateForRetryKeepsLegacyOwnerOutOfSpawnState()`

Behavior now guaranteed:

- legacy owner retry restore lands in `shell_owner_state`
- `spawn_state` stays empty for legacy retry recovery
- this matches the current split used by finalize/admission/deferred-release paths

## Follow-up On 2026-05-21 Strict Finalize Retry Filters Compatibility Payload

The next cleanup closed a real retry pollution bug in the strict finalize rollback path.

Problem:

- when `TakeActiveOwnerForFinalize()` resolves a matching zygote-control transaction over a
  matching legacy shell owner, it intentionally normalizes the returned `owned_spawn_state`
  backend to `kNone`
- however, `FinalizeSpawn()` rollback previously restored any non-empty `owned_spawn_state`
  after finalize failure
- that meant a normalized legacy record carrying only `identifier/spawn_token` could be
  restored into `spawn_state` as if it were active retry ownership
- this reintroduced compatibility payload as active state and blurred the owner split that
  the previous cleanups established

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added regression:

- `TestFinalizeSpawnFailureDoesNotRestoreNormalizedLegacyCompatIntoSpawnState()`

Behavior now guaranteed:

- finalize failure still restores the matching zygote-control transaction for retry
- retry restore only accepts authoritative owner records
- normalized compatibility payload with `backend == kNone` is no longer restored into
  `spawn_state`
- this keeps strict rollback aligned with the owner split:
  - legacy owner -> `shell_owner_state`
  - symbi owner -> `spawn_state`
  - zygote-control owner -> `zygote_control_transaction`
  - compatibility token payload -> not a retry owner

Implementation detail:

- `HasOwnedSpawnStateForRetry()` now defers to `HasAuthoritativeSpawnOwner()`
  instead of treating any non-empty token-bearing payload as retry-owning state

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Zygote-Control Commit Strips Temporary Artifact Fields

The next agent-owned cleanup tightened what survives into active state after a
zygote-control route commit.

Problem:

- zygote-control is supposed to keep only:
  - authoritative ownership in `zygote_control_transaction`
  - request-token compatibility payload in `spawn_state`
- but the current commit path still allowed temporary artifact fields from
  `owned_state` to survive in `spawn_state`, including:
  - `ncore_path`
  - `agent_path`
  - `materialized_ncore`
  - `materialized_agent`
- those fields are not part of active ownership for zygote-control and should not
  remain live after commit

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added/expanded regression coverage:

- `TestBuildPendingSpawnCommitSeedsZygoteControlOwnedRecord()`
- `TestCommitPendingSpawnNormalizesZygoteControlShellOwnerState()`

Behavior now guaranteed:

- zygote-control pending commit keeps only request-token compatibility payload in
  `spawn_state`
- temporary artifact fields are stripped before/when the commit becomes active
- active zygote-control state now stays aligned with the intended split:
  - transaction owns lifecycle
  - compat token supports request correlation only

Implementation detail:

- `BuildPendingSpawnCommit()` now clears non-token artifact fields for
  non-child-owned backends
- `CommitPendingSpawn()` also normalizes active zygote-control compat state to
  remove any temporary artifact residue

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Legacy Commit Keeps Only Token-Level Compatibility Residue

The next lifecycle cleanup applied the same residue rule to legacy shell-owned routes,
but with a narrower condition after validating the existing compatibility semantics.

Problem:

- legacy commit already stores the authoritative owner in `shell_owner_state`
- but the session-local `spawn_state` could still retain duplicate artifact fields from
  the same request, such as:
  - `ncore_path`
  - `agent_path`
  - `materialized_ncore`
  - `materialized_agent`
- those fields are not needed once legacy ownership has moved to `shell_owner_state`
- however, existing tests also proved that `spawn_state` may intentionally carry a
  foreign compatibility payload alongside an active shell owner, so a broad cleanup
  would be wrong

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Expanded regression coverage:

- `TestBuildPendingSpawnCommitKeepsNonZygoteOwnersSessionLocal()`
- `TestApplySuccessfulRouteCommitSeedsLegacyFallbackAndOwner()`
- `TestCommitPendingSpawnSeparatesAuthoritativeAndCompatibilityShellState()`

Behavior now guaranteed:

- when legacy ownership is committed for the same request token:
  - `shell_owner_state` keeps the authoritative lifecycle fields
  - `spawn_state` keeps only token-level compatibility payload
  - duplicate artifact residue is removed
- when `spawn_state` carries foreign compatibility payload next to an active shell owner,
  that payload is preserved

Implementation detail:

- `BuildPendingSpawnCommit()` now strips artifact fields when moving legacy ownership into
  `shell_owner_state`
- `CommitPendingSpawn()` performs the same cleanup only when the compatibility payload
  matches the active shell-owner token, avoiding accidental removal of foreign compat
  state

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Shared Cleanup For Same-Request Compatibility Residue

The next lifecycle pass moved the same-request compatibility cleanup into a shared
helper so deferred-release/finalize paths behave like commit paths.

Problem:

- earlier cleanups removed duplicate artifact residue during commit
- but deferred-release and finalize extraction still had a separate seam:
  after clearing a legacy shell owner for the same request, `spawn_state` could still
  retain duplicate artifact fields
- this created path-dependent behavior where commit-time state was clean, but later
  owner-release paths could still leave residue behind

Updated:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Expanded regression coverage:

- `TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingOwner()`
- `TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingSpawnOwnerOnly()`
- `TestTakeActiveOwnerForFinalizePreservesForeignResidualTransaction()`
- `TestTakeActiveOwnerForFinalizePromotesResidualTransactionToOwnedOwner()`

Behavior now guaranteed:

- for legacy shell-owned requests, duplicate compatibility residue is stripped not only
  during commit, but also when the authoritative owner is later released/extracted
- same-request compat payload still keeps token-level correlation
- foreign compatibility payload is still preserved

Implementation detail:

- introduced `StripCompatibilityArtifactResidueForMatchingShellOwnerLocked()`
- `CommitPendingSpawn()` and `ClearAuthoritativeSpawnOwnerSlot()` now share the same
  cleanup rule
- the helper is applied before clearing the shell owner slot so the matching token
  is still available for comparison

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Zygote-Owned Finalize Failure Stops Restoring Compat Spawn State

The next strict-path seam turned out to be in finalize failure rollback, not in the
owner extraction step itself.

Problem:

- `TakeActiveOwnerForFinalize()` correctly cleared token-only compat payload when a
  matching zygote-control transaction owned the request
- but `FinalizeSpawn()` failure rollback still had a generic restore path:
  when no authoritative owner remained, it would restore `finalize_session.owned_spawn_state`
  if it looked retry-worthy
- for zygote-owned finalize failures this was wrong:
  retry should restore only the transaction record
  compat spawn state must stay cleared

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Added regression:

- `TestTakeActiveOwnerForFinalizeClearsTokenOnlyCompatWhenTransactionOwnsRequest()`

Behavior now guaranteed:

- direct owner extraction clears token-only compat payload when the request is
  zygote-transaction-owned
- if finalize later fails on the zygote-owned path, rollback restores only
  `zygote_control_transaction`
- `spawn_state` remains empty instead of being repopulated by compat retry residue

Implementation detail:

- `FinalizeSpawn()` rollback now skips `RestoreOwnedSpawnStateForRetry()` when
  `finalize_owner == SpawnOwnershipState::kZygoteControlOwned`

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`

## Follow-up On 2026-05-21 Zygote-Control Success Route Carries Request Token

The next cleanup aligned the two zygote-control success entrypoints.

Problem:

- `ApplySuccessfulZygoteControlAttemptResult()` preserved `owned_state.spawn_token`
  when the caller supplied one
- `ApplyZygoteControlRouteAttempt()` created an empty `SpawnOwnedState` on success,
  so the zygote-control success route could commit without the request token in
  `pending_commit.spawn_state`
- this made request-token compatibility depend on which helper path committed the
  successful zygote-control result

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

Expanded regression coverage:

- `TestApplyZygoteControlRouteSuccessCommitsOutcome()`

Behavior now guaranteed:

- zygote-control success commits preserve the request spawn token regardless of whether
  the commit enters through `ApplySuccessfulZygoteControlAttemptResult()` directly or
  through `ApplyZygoteControlRouteAttempt()`
- the token remains compatibility payload only:
  `spawn_state.backend == kNone` and `spawn_state.identifier` stays empty

Verification in this workspace:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `build\\test-bin\\test_ninjector_spawn_injector_strict_cleanup_current.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_route_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_ninjector_spawn_injector_policy_focus.exe`
- `build\\test_ninjector_spawn_injector_policy_focus.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_zygote_control_rpc_strict_cleanup_current.exe`
- `build\\test-bin\\test_zygote_control_rpc_strict_cleanup_current.exe`
