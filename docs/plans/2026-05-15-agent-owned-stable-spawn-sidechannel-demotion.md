# Agent-Owned Stable Spawn Side-Channel Demotion

## Date

2026-05-15

## Scope

This step removes the old zygote environment side-channel from the main
`zygote-control` spawn path when an agent-owned install/uninstall control path
is available.

It does not switch the default stable backend.

The default stable device path remains:

- stable spawn default -> `legacy-ncore`
- `zygote-control` remains non-default / experimental
- single-file `nook-server` packaging remains authoritative

## What Changed

### 1. Main zygote-control path no longer depends on `SetZygoteSpawnControl()`

Before this step, `TrySpawnViaZygoteControl()` did two different control-plane
things for the same transaction:

1. write `NOOK_TARGET_PACKAGE` / `NOOK_SPAWN_TOKEN` into zygote through
   `SetZygoteSpawnControl()`
2. call `install_zygote_hook_()` to arm the agent-owned controller

That duplicated control ownership.

After this step:

- if `install_zygote_hook_` exists:
  - the main path uses agent-owned install/uninstall only
  - the old env side-channel is skipped
- if `install_zygote_hook_` does not exist:
  - the old side-channel remains available as compatibility behavior

### 2. Rollback/finalize follow the same rule

The same demotion now applies to:

- install failure rollback
- launch failure rollback
- owned zygote-control finalize

Meaning:

- uninstall callback remains the primary owned teardown
- `ClearZygoteSpawnControl()` is only used on the compatibility side-channel
  path

### 3. Spawn injector tests now encode the new boundary explicitly

`tests/communication/test_ninjector_spawn_injector.cpp` now expects that
agent-owned zygote-control traces do **not** contain:

- `set-control:*`
- `clear-control:*`

when install/uninstall callbacks are present.

Compatibility tests that intentionally exercise the old side-channel still keep
their `clear-control-*` expectations.

## Why This Matters

The target `agent-owned stable spawn` model cannot have two equally real arming
sources for the same transaction.

If the server both:

- arms zygote env variables directly
- and asks the zygote agent to arm its controller

then state ownership remains ambiguous and teardown semantics stay harder to
reason about.

This step makes the boundary cleaner:

- agent-owned install/uninstall is the primary control source
- old env mutation becomes compatibility-only

That is closer to the intended Frida-like direction where the agent owns the
spawn transaction and the server is a control-plane client, not a second hidden
state writer.

## Validation

Passed:

- `build/test_ninjector_spawn_injector.exe`
- `build/test_zygote_control_rpc.exe`
- `tests/communication/test_agent_connection.exe`
- `tests/headers/test_zygote_spawn_state.exe`

## Current Runtime Impact

No intended default stable backend change.

User-visible effect for the experimental zygote-control path:

- when install/uninstall callbacks exist, zygote-control no longer relies on
  `NOOK_TARGET_PACKAGE` / `NOOK_SPAWN_TOKEN` env writes as the primary control
  path

This reduces control duplication without changing the current stable default
spawn route.

## Next Step

Keep shrinking the remaining fallback-shaped dependencies inside
`NinjectorSpawnInjector` and `zygote-control`, then move default routing only
after the agent-owned path is complete enough to replace the current stable
`legacy-ncore` ownership.
