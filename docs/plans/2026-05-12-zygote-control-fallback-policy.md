# 2026-05-12 Zygote-Control Fallback Policy

## Goal

Tighten `zygote-control` spawn fallback semantics so that only recoverable experimental-path failures degrade to fallback backends, while lifecycle-hard failures stop immediately.

## Problem

Earlier behavior was too broad:

- any `zygote-control` failure could flow into later fallback stages
- only strict mode guaranteed immediate abort

That mixed together two very different classes of failure:

1. **soft failures**
   - indicate the experimental `zygote-control` path is currently unavailable
   - fallback is reasonable

2. **hard failures**
   - indicate `zygote-control` already entered a meaningful lifecycle stage and encountered a state/ownership problem
   - fallback hides the real failure and can produce confusing follow-on behavior

## Change Applied

Updated:

- `server/ninjector_spawn_injector.cpp`
- `tests/communication/test_ninjector_spawn_injector.cpp`

Added:

- `ShouldAllowFallbackAfterZygoteControlFailure(...)`

## Policy

### Soft failures: allow fallback

These are treated as experimental-path degradation:

- zygote agent session not found
- zygote control ready wait timed out
- ready wait timed out
- zygote control rpc timeout
- rpc timeout
- `remote_dlopen_failed:dlopen failed: library "/proc/self/fd/..."`

These correspond to:

- monitor/session/rpc availability issues
- memfd linker acceptance issues
- zygote-control hook-install path not becoming usable

### Hard failures: do not fallback

These now stop immediately even in default mode:

- `set zygote spawn control failed`
- `start_target_app failed`
- `no zygote-monitor targets armed`

Reason:

- `set zygote spawn control failed` means the server could not arm the zygote control state for the required target
- `start_target_app failed` means the control path was already armed and launch itself failed; continuing into fallback is no longer a “different backend,” it is a second action after lifecycle failure
- `no zygote-monitor targets armed` means no valid zygote-control route survived target selection / installation

## Interaction With Strict Mode

Strict mode remains stronger than the default policy.

If strict mode is enabled through either:

- `NOOK_STRICT_ZYGOTE_CONTROL=1`
- request marker `--nook-strict-zygote-control`

then **all** `zygote-control` failures abort immediately, regardless of whether they are soft or hard.

## Tests Added / Updated

In `tests/communication/test_ninjector_spawn_injector.cpp`:

- install-time `rpc timeout` still falls back by default
- `start_target_app failed` now aborts by default
- `set zygote spawn control failed` now aborts by default
- strict env mode still aborts
- strict request-argv mode still aborts

## Practical Reading Of Device Failures

When real-device logs show:

- `zygote control rpc timeout`
- `zygote agent session not found`
- `/proc/self/fd/... not found`

the server may still legally degrade to fallback in default mode.

When logs show:

- `set zygote spawn control failed`
- `start_target_app failed`

the server should now stop and surface the `zygote-control` failure directly.

That keeps experimental-path instability visible instead of silently converting it into legacy behavior.

## Local Verification

Passed:

- `build/test_ninjector_spawn_injector_policy_green.exe`
- `build/test_zygote_control_rpc_policy_regress.exe`
- `build/test_session_registry_policy_regress.exe`
- `build/test_server_zygote_control_rpc_regressions_policy.exe`

## Result

`zygote-control` fallback is now policy-driven instead of “any failure may fall through.”

This is a prerequisite for the next phase:

- further lifecycle tightening inside `zygote-control`
- then eventual agent-owned stable spawn work once failure boundaries are clean
