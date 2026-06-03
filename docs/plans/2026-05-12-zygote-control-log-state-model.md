# 2026-05-12 Zygote-Control Log State Model

## Goal

Document the current structured log model around `zygote-control`, so real-device logcat can be read against a stable state vocabulary instead of ad-hoc strings.

This step does not change spawn policy. It records the observability surface that now exists in code.

## Scope

The current log model is split into four layers:

1. RPC lifecycle
2. spawn route decision
3. finalize route decision
4. terminal outcome

Together these answer four different questions:

- did the zygote rpc path become ready?
- which backend route did spawn/finalize choose?
- did the flow fallback or abort?
- what was the final failure composition?

## Current Structured Log Surfaces

### 1. RPC lifecycle

Source:

- [zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)

Format:

`zygote-control stage=<stage> event=<event> pid=<pid|unknown> process=<name> method=<rpc> class=<soft|hard> [error=<message>]`

Current stages:

- `ready-wait`
- `install`
- `uninstall`

Typical events:

- `begin`
- `ready`
- `not-ready`
- `rpc-ok`
- `rpc-fail`

Meaning:

- this layer describes zygote agent session readiness and rpc request outcome
- it does not decide whether later fallback is allowed

### 2. Spawn route decision

Source:

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Format:

`zygote-control stage=spawn-route event=<event> package=<pkg> strict=<0|1> fallback=<backend> [detail=<message>]`

Current events:

- `fallback`
  - `zygote-control` failed, but policy allows moving to later backends
- `abort`
  - `zygote-control` failed and policy says stop immediately
- `skip-explicit-symbi`
  - explicit symbi request bypassed zygote-control
- `skip-stable-default`
  - default stable path kept legacy route instead of zygote-control
- `skip-symbi-fallback`
  - symbi fallback path was unavailable or disabled

Meaning:

- this layer explains route choice
- it is the first place to check when asking "why did the server not continue into fallback?"

### 3. Finalize route decision

Source:

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Format:

`zygote-control stage=finalize-route event=<event> package=<pkg> backend=<backend> [detail=<message>]`

Current events:

- `owned-zygote-control`
- `owned-symbi`
- `owned-legacy`
- `fallback-probe`

Meaning:

- this layer explains teardown ownership
- it answers whether finalize is using a remembered successful backend or probing fallback teardown paths because no owned backend was retained

### 4. Terminal outcome

Source:

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Format:

`zygote-control stage=<spawn-result|finalize-result> event=<success|fail> package=<pkg> primary=<backend> secondary=<backend|none> [detail=<message>]`

Meaning:

- `primary` is the route that produced the authoritative result
- `secondary` is the paired fallback/backend context when the final error is composed from two layers
- `detail` keeps the composed human-readable reason

Typical examples:

- `primary=zygote-control secondary=legacy`
  - zygote-control failed and legacy fallback also mattered
- `primary=zygote-control secondary=symbi`
  - zygote-control failed and the symbi stage also contributed to the terminal failure
- `primary=symbi secondary=legacy`
  - explicit or fallback symbi failed and legacy also failed
- `primary=legacy secondary=none`
  - stable path alone determined the outcome

## How To Read Real Logs

Recommended reading order:

1. check rpc lifecycle logs
2. check spawn-route or finalize-route decision
3. check terminal outcome
4. only then read the final CLI error string

This avoids collapsing distinct situations into the same final message.

### Example A: recoverable experimental degradation

If logcat shows:

- `stage=install event=rpc-fail class=soft`
- then `stage=spawn-route event=fallback`
- then `stage=spawn-result event=fail primary=zygote-control secondary=legacy`

that means:

- zygote-control was attempted
- policy allowed fallback
- legacy also failed, so the final error is a composed failure

### Example B: hard lifecycle stop

If logcat shows:

- `stage=spawn-route event=abort`
- then terminal `primary=zygote-control secondary=none`

that means:

- the failure was classified as hard enough that default mode did not permit fallback

### Example C: default stable path never entered zygote-control

If logcat shows:

- `stage=spawn-route event=skip-stable-default`

that means:

- this request stayed on the stable legacy route by policy
- any later failure should be interpreted as legacy-owned, not zygote-control-owned

## Relationship To Existing Policy Docs

This document complements:

- [2026-05-12-zygote-control-fallback-policy.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-zygote-control-fallback-policy.md)
- [2026-05-12-spawn-backend-responsibility-clarification-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-backend-responsibility-clarification-status.md)
- [2026-05-12-spawn-default-stable-path-reanchoring-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-default-stable-path-reanchoring-status.md)

Those documents answer:

- which backend is supposed to own what
- when fallback is legal
- which path is default vs experimental

This document answers:

- how to interpret the logs emitted by that policy

## Files Updated During This Observability Tightening

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [zygote_control_rpc.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.h)
- [zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)
- [test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)
- [test_server_zygote_control_rpc_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_server_zygote_control_rpc_regressions.cpp)
- [test_ninjector_spawn_route_log_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_ninjector_spawn_route_log_regressions.cpp)
- [test_ninjector_finalize_route_log_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_ninjector_finalize_route_log_regressions.cpp)
- [test_ninjector_terminal_outcome_log_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_ninjector_terminal_outcome_log_regressions.cpp)

## Local Verification

Passed during this tightening phase:

- `build/test_zygote_control_rpc_log_green.exe`
- `build/test_ninjector_spawn_injector_route_logs.exe`
- `build/test_ninjector_spawn_injector_route_logs2.exe`
- `build/test_ninjector_spawn_injector_finalize_route_green.exe`
- `build/test_ninjector_terminal_outcome_green.exe`
- `build/test_server_zygote_control_rpc_regressions_log.exe`
- `build/test_ninjector_spawn_route_log_regressions_green.exe`
- `build/test_ninjector_finalize_route_log_regressions_green.exe`
- `build/test_ninjector_terminal_outcome_log_regressions_green.exe`

## Current Position

At this point `zygote-control` observability is layered instead of flat:

- rpc lifecycle explains readiness and rpc transport state
- route logs explain policy choice
- terminal outcome logs explain composed failure ownership
- final CLI errors remain compact user-facing summaries

That is the right stopping point before any deeper runtime/state-machine changes.
