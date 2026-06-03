# Zygote-Control Strict Request Leak And CLI Banner

## Goal

Record the latest real-device fixes that moved `zygote-control` from "intermittent"
to "usable on device" for both:

- default `spawn`
- `--strict-zygote-control`

Also record the host-side CLI regression where the Frida-style one-shot flow stopped
printing the banner.

## Real device failures observed

Before this round of fixes, two classes of failures were still active:

1. strict request state leaked across spawn attempts
2. the host Frida-style CLI flow no longer printed the banner even when the rest of
   the command worked

The most important real-device symptom for the first bug was:

- strict spawn could succeed once
- the next default spawn still logged:
  - `strict=0`
- but the child still executed:
  - `synchronous child bootstrap prime strict zygote-control fast path process=com.ad2001.frida0x1`
- then the child could crash or the host could time out waiting for runtime ready

That meant the default path was still being polluted by residual strict-only state.

## Root cause

The strict request split was only half-finished.

We had already introduced:

- `NOOK_STRICT_ZYGOTE_REQUEST`

and updated promoted-child detection in:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)

to prefer the explicit strict request signal.

However, cleanup in:

- [server/ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)

still only removed `NOOK_STRICT_ZYGOTE_REQUEST` when `strict_request == true`.

That created a real leak:

1. one strict spawn wrote `NOOK_STRICT_ZYGOTE_REQUEST=1` into zygote
2. a later default spawn called cleanup with `strict_request=false`
3. cleanup skipped unsetting `NOOK_STRICT_ZYGOTE_REQUEST`
4. the next default child still looked strict to the promoted-child logic

So the default route could:

- commit with `strict=0`
- but still take the strict child fast path in-process

## Fixes applied

### 1. Promoted strict-child detection no longer keys off helper-local control mode

Updated:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)

Change:

- `IsPromotedStrictZygoteControlSpawnChild(...)` now checks:
  - `NOOK_STRICT_ZYGOTE_REQUEST`
- it no longer checks:
  - `NOOK_STRICT_ZYGOTE_CONTROL`

Why:

- `NOOK_STRICT_ZYGOTE_CONTROL` is the helper-local control-mode signal
- it is not a valid user-visible strict spawn request bit
- reusing it polluted the default path

### 2. Zygote spawn-control cleanup now always clears strict request state

Updated:

- [server/ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)

Change:

- `ClearZygoteSpawnControl(...)` now always unsets:
  - `NOOK_STRICT_ZYGOTE_REQUEST`

It no longer conditions that unset on the caller's `strict_request` boolean.

Why:

- cleanup must remove residual state from the remote zygote
- cleanup cannot trust the current host-side route mode to describe prior zygote state

### 3. Frida-style CLI banner restored for non-REPL human flows

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)

Change:

- added `_should_print_banner_for_command(args)`
- the main command path now prints the banner for human-readable:
  - `spawn`
  - `attach`
  - `call`
  - `post`
  - `unload`
- REPL keeps using its existing banner path
- JSON mode still skips the banner

Why:

- the banner had only been printed from `_create_repl_context(...)`
- Frida-style one-shot `nook-cli -U -f ... -l ...` does not go through REPL setup
- that made the host output look regressed even when the device flow was correct

## Regression coverage

Updated:

- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Coverage added:

- strict promoted-child detection must use `NOOK_STRICT_ZYGOTE_REQUEST`
- strict promoted-child detection must not directly read `NOOK_STRICT_ZYGOTE_CONTROL`
- zygote cleanup must always unset `NOOK_STRICT_ZYGOTE_REQUEST`
- human-readable `spawn` CLI output must include the banner

## Real-device verification

Packaged server used for this round:

- [nook-server](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/single-server-package/arm64-v8a/nook-server)
- sha256:
  - `f1ca6988c7f74ab0b9aea5bf4bfc79104064dd5b3f7a7b622f8499dbc2a767a7`

Observed on device:

- `nook-server` remained alive as root
- default `spawn` completed and hook hit:
  - `lab:frida-0x1:hit:get_random`
  - `lab:frida-0x1:hit:check:left=5:right=14`
- `--strict-zygote-control` also completed on device
- the previous `wait runtime agent ready timed out` regression was no longer reproduced

One preserved default-path device trace showed:

- `zygote-control stage=spawn-route event=commit package=com.ad2001.frida0x1 strict=0`
- later:
  - `forward runtime-stage AGENT_READY`
  - `script create ok`
  - `script load ok`
  - hook-hit logs

That confirms the split between:

- spawn response / control-ready ownership
- runtime-ready / script operations

is functioning again on the current real device.

## Current status

`zygote-control` is now:

- usable on real device
- verified for both default spawn and strict spawn on the current Android 11 test phone

But it is still not considered fully "done" because:

- default-path and strict-path log semantics still need cleanup
- some child-fast-path wording is still overly strict-specific
- the work to converge this into true `agent-owned stable spawn` is still ahead

## Next

1. Continue cleaning the state-machine and logging boundary between:
   - helper-only local zygote control
   - explicit strict request
   - default stable spawn
2. Reduce residual strict-specific wording in default-path logs.
3. Use the now-stable current route as the base for the next `agent-owned stable spawn`
   convergence step.

## Follow-up On 2026-05-17 CLI Surface Cleanup

Host CLI surface was further tightened to match the now-stable route model.

Updated:

- [host/nook-py/nook/cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
- [host/nook-py/tests/test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Changes:

- Frida-style top-level help now keeps the stable default examples first
- experimental spawn routing examples are listed separately:
  - `--strict-zygote-control`
  - `--spawn-symbi`
- spawn backend flags now share a single helper and are labeled `experimental` in argparse help
- stale CLI tests that still expected one-shot human flows to suppress the banner were updated to match the documented banner policy

Verification:

- targeted host regressions passed:
  - `test_top_level_help_mentions_frida_style_invocations`
  - `test_main_help_prints_frida_style_invocations`
  - `test_frida_style_help_marks_experimental_spawn_flags`
  - `test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`
  - `test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner`

## Follow-up On 2026-05-17 Default-Path Log Wording Cleanup

Updated:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp)

Changes:

- reduced residual strict-specific wording in promoted-child logs
- log strings now say:
  - `promoted zygote-control child`
  - `zygote-control promoted-child fast path`
- this keeps the log surface closer to the actual control/lifecycle model instead of implying every promoted-child path is user-visible `strict`

Verification note:

- the updated source-string regression advanced past the wording checks and then stopped on a later pre-existing source assertion unrelated to this wording cleanup

## Follow-up On 2026-05-17 Server Strict-Env Route Isolation

Another strict/default boundary leak was still present on the host injector side.

Root cause:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp) still treated the server process environment variable:
  - `NOOK_STRICT_ZYGOTE_CONTROL`
- as a valid input for public spawn route selection

That meant a server started under a local strict helper env could still promote an otherwise default:

- `nook-cli -U -f ...`

request onto the strict zygote-control route even when the request itself did not opt in.

Fix:

## Follow-up On 2026-05-21 Spawn Gate Resume Timeout Root Cause

Current real-device failure that was re-observed on the Android 11 phone:

- default `spawn` reached:
  - `SCRIPT_CREATE_RESP`
  - `SCRIPT_LOAD_RESP`
- but then failed at:
  - `release gate failed pid=... role=identity-aware error=request timeout`
  - `resume failed pid=... error=spawn gate release failed`
- app stayed blocked on:
  - `blocking app bootstrap on spawn gate`

This confirmed the failure was no longer in:

- spawn response
- runtime-ready
- script create
- script load

and was specifically in the final:

- server -> agent `ResumeRequest`

leg of spawn-gate release.

### Root cause

The agent-side receive loop in:

- [src/communication/agent/agent_connection.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/agent/agent_connection.cpp)

used:

- `transport_->RecvAll(..., 100)`

for both frame header and payload reads.

But the transport layer semantics were:

- `Recv()` returns `0` on timeout / temporary no-data
- `Transport::RecvAll()` treated any `<= 0` as hard failure

So a perfectly valid fragmented or slightly delayed frame could cause the agent recv loop to
drop out between messages.

That matches the device symptom exactly:

1. `SCRIPT_CREATE` succeeded
2. `SCRIPT_LOAD` succeeded
3. agent sent `SCRIPT_LOAD_RESP`
4. agent recv loop could fall out during the next idle/fragmented read window
5. server sent `ResumeRequest`
6. no agent-side `spawn gate resume handler begin` log ever appeared
7. server timed out waiting for `ResumeResponse`

### Fix

Updated:

- [src/communication/agent/agent_connection.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/agent/agent_connection.cpp)

Change:

- replaced the `RecvAll(..., 100)` usage in `AgentConnection::RecvLoop()` with explicit
  incremental header/payload reads
- temporary `Recv()==0` now means:
  - retry if transport is still connected and healthy
- only real disconnect/error exits the recv loop

This makes the agent control channel robust against:

- header/payload fragmentation
- short inter-frame idle windows
- timing gaps between `SCRIPT_LOAD_RESP` and `ResumeRequest`

### Regression coverage

Updated:

- [tests/communication/test_agent_connection.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_agent_connection.cpp)

Added:

- fragmented `ResumeRequest` delivery test

Behavior:

1. push frame header first
2. wait long enough to cross the old 100 ms idle window
3. push payload bytes later
4. verify `ResumeResponse` is still produced

This test failed before the fix and passed after it.

- host/server route policy now only treats:
  - `--nook-strict-zygote-control`
- as the strict route selector
- `NOOK_STRICT_ZYGOTE_CONTROL` remains helper-local child/bootstrap state only
- it is no longer accepted as a server-side public route input

Updated:

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)
- [tests/communication/test_ninjector_spawn_injector_route_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector_route_subset.cpp)

Regression coverage added/updated:

- server local strict env must not promote the default stable spawn route
- helper-local embedded zygote helper routing still works when the request explicitly opts into strict mode
- failed zygote-control transaction snapshots now seed off explicit strict requests instead of ambient env state

Verification:

- rebuilt:
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
- passed:
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
  - `build/test_zygote_control_regressions.exe`

Additional regression hardening applied immediately after:

- [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp) now also asserts that:
  - server spawn routing must not read `IsEnvEnabled("NOOK_STRICT_ZYGOTE_CONTROL")`
  - transaction strict arming must derive from `IsStrictZygoteControlRequested(request)`
  - public spawn execution policy must derive strict mode only from the explicit request

Verification:

- rebuilt:
  - `build/test_zygote_control_regressions.exe`
- passed:
  - `build/test_zygote_control_regressions.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
  - `build/test_ninjector_spawn_injector.exe`

## Follow-up On 2026-05-17 AGENT_READY Stage Monotonicity

The next child-promotion boundary tightened in this round was inside:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)

Problem:

- `SendAgentReadyLocked(...)` previously only suppressed duplicate runtime-stage sends
- control-stage sends had no explicit stage memory
- promotion/control paths could therefore re-enter `NotifyZygoteControlReadyToServer()` or other control-ready helpers without a single monotonic stage model

Fix:

- replaced the old boolean:
  - `g_agent_ready_sent`
- with an explicit highest-stage tracker:
  - `g_highest_agent_ready_stage_sent`
- `SendAgentReadyLocked(...)` now maps protocol stages onto an internal monotonic order:
  - control -> `0`
  - runtime -> `1`
- duplicate or regressive stage sends are now suppressed uniformly

Why the explicit mapping matters:

- protocol enum values are currently:
  - `kRuntime = 0`
  - `kControl = 1`
- so raw `static_cast<int>(stage)` is not a valid monotonic lifecycle ordering
- the guard must therefore use an internal semantic rank instead of the wire enum value

Regression coverage added:

- [tests/headers/test_zygote_control_connection_lifetime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_connection_lifetime_regressions.cpp)

Verification:

- rebuilt:
  - `build/test_zygote_control_connection_lifetime_regressions_stage_guard.exe`
- passed:
  - `build/test_zygote_control_connection_lifetime_regressions_stage_guard.exe`
  - `build/test_zygote_control_regressions.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
  - `build/test_ninjector_spawn_injector.exe`

## Follow-up On 2026-05-17 Late Control-Ready Must Not Regress Runtime-Ready

The next boundary bug was on the server receive side, after the agent-side stage
guard had already been tightened.

Problem:

- a runtime-stage `AGENT_READY` could arrive first and correctly become the
  authoritative runtime session
- a later control-stage `AGENT_READY` for the same pid could still mutate the same
  pid slot
- that allowed two bad regressions:
  - `agent_sessions_[pid]` could be overwritten by the later control session
  - `agent_ready_stages_[pid]` could be written back from runtime to control

That is exactly the kind of split-state leak that later shows up on device as:

- runtime ready observed once
- then script/load waits or control-side lookups behave as if the child never
  fully promoted

Fixes:

- [server/server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
  now treats a late control-stage ready for an already runtime-ready pid as
  control-side metadata only:
  - keep the existing authoritative runtime session
  - keep the existing runtime process-name binding
  - still register the control-ready session separately
- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
  now stores `AgentReadyStage` monotonically using an internal lifecycle rank:
  - control -> `0`
  - runtime -> `1`

Why:

- protocol enum order is still:
  - `kRuntime = 0`
  - `kControl = 1`
- so stage persistence cannot use raw enum ordering
- and once runtime ownership is established for a pid, a later control callback
  must never demote that pid's authoritative state

Regression coverage added:

- [tests/communication/test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
  now includes:
  - `TestLateControlAgentReadyDoesNotRegressRuntimeStage`

Verification:

- rebuilt:
  - `build/test_server_handlers_stage_monotonic.exe`
- passed:
  - `build/test_server_handlers_stage_monotonic.exe`
  - `build/test_zygote_control_regressions.exe`
  - `build/test_zygote_control_connection_lifetime_regressions_stage_guard.exe`
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`

Note:

- a full rebuild of `tests/communication/test_session_registry.cpp` still hits an
  older control-ready lookup failure around
  `TestFindControlReadyAgentSessionByIdentityRejectsSupersededSameNamePid`
- that failure was not introduced by this change, and this round did not expand
  into the broader control-ready lookup semantics

## Follow-up On 2026-05-17 Authoritative Lookup Preference Alignment

Another server-side lifecycle mismatch was still present in

- [server/session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)

Problem:

- `FindAuthoritativeAgentSessionByPid(...)` already behaved like the runtime-owned
  authoritative view by reading `agent_sessions_[pid]`
- but `WaitForAuthoritativeAgentSessionByIdentity(...)` still used the
  control-preferring resolver
- so if both sessions were alive for the same pid:
  - immediate authoritative lookup preferred runtime
  - waiting authoritative lookup preferred control

That mismatch is exactly the kind of semantic drift that later reintroduces
"wrong session selected" regressions once more code starts using the wait-based API.

Fix:

- introduced an authoritative-specific resolver that prefers:
  - runtime/agent session first
  - control-ready session only as fallback
- `WaitForAuthoritativeAgentSessionByIdentity(...)` now uses that resolver

Regression coverage added:

- [tests/communication/test_session_registry_authoritative_preference.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry_authoritative_preference.cpp)

Verification:

- rebuilt:
  - `build/test_session_registry_authoritative_preference.exe`
  - `build/test_server_handlers_stage_monotonic.exe`
  - `build/test_host_spawn_client.exe`
- passed:
  - `build/test_session_registry_authoritative_preference.exe`
  - `build/test_server_handlers_stage_monotonic.exe`
  - `build/test_host_spawn_client.exe`
  - `build/test_ninjector_spawn_injector.exe`

Real-device note:

- this API-alignment fix is not yet on the active default spawn/script path
- the earlier same-session regression was verified on device before this follow-up:
  - default `spawn + script.js` reached
    `runtime AGENT_READY -> SCRIPT_CREATE -> SCRIPT_LOAD -> resume -> app displayed`

## Follow-up On 2026-05-20 Promoted Strict Child Full-Agent Semantics

Another real-device regression was reproduced on the current Android 11 test phone
while revisiting the promoted strict child path.

Problem:

- strict `zygote-control` child bootstrap was already working through:
  - control-stage `AGENT_READY`
  - late promotion
  - full-agent `NookAgentInitializeForSpawnChild`
- but an attempted cleanup changed the promoted-child full-agent path to behave more
  like ordinary child-owned spawn:
  - re-enable in-process spawn-gate arming
  - install bootstrap hooks during full-agent init

That change was wrong for the current strict promoted-child lifecycle.

Observed regression:

- host failed with:
  - `wait runtime agent ready timed out`
- device log showed:
  - control-stage ready arrived
  - late promotion started
  - promoted child full-agent init armed spawn gate again
  - server stayed at `spawn waiting runtime-ready`
  - no host-visible runtime-ready arrived

Concrete log shape from the broken build:

- `promoted zygote-control child full-agent spawn gate evaluated ... armed=1`
- `spawn success ... ready_stage=control`
- `spawn waiting runtime-ready ...`
- then host timeout:
  - `wait runtime agent ready timed out`

Root cause:

- strict promoted-child bootstrap and strict promoted-child full-agent promotion are
  not the same as ordinary spawned-child initialization
- re-arming the spawn gate in the promoted full-agent phase reintroduced a dependency
  cycle:
  - child already used the strict fast-path control-ready bootstrap
  - late promotion then tried to bring up the full agent
  - full-agent init reintroduced gate/bootstrap semantics that prevented runtime-ready
    from being forwarded in time

Fix:

- [src/framework/NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
  was restored to the previously working promoted strict-child behavior:
  - promoted strict child still skips spawn-gate re-arming in full-agent init
  - promoted strict child still skips bootstrap-hook installation in
    `NookAgentInitializeForSpawnChild(...)`

Important note:

- this follow-up does **not** mean the long-term architecture is ideal
- it only records the currently validated device invariant:
  - for strict promoted children, full-agent promotion must preserve the existing
    working semantics until a new lifecycle design is proven end-to-end on device

Real-device verification after rollback:

- strict command again reached:
  - `Spawned (pid: 21611)`
  - `Script loaded`
  - `Process resumed`
  - `lab:frida-0x1:installed`
- device log again showed the healthy path:
  - runtime-stage `AGENT_READY`
  - `SCRIPT_CREATE`
  - `SCRIPT_LOAD_RESP`
  - `resume success`

Pitfall to preserve:

- do not casually "normalize" promoted strict-child full-agent init toward the default
  spawn path without a full device repro
- this exact mistake can regress strict mode from:
  - `script load/resume works`
  back to
  - `wait runtime agent ready timed out`
