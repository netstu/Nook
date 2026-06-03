# Agent-Owned Stable Spawn Owned Session Boundary And Unload Fix

## Date

2026-05-15

## Scope

This step continues the `agent-owned stable spawn` convergence work without
switching the default stable backend.

The default real-device path remains:

- stable spawn default -> `legacy-ncore`
- `zygote-control` remains opt-in / experimental
- single-file visible deployment remains `nook-server`

## What Changed

### 1. Zygote-control ownership is now marked only after ready succeeds

`server/zygote_control_rpc.cpp` was already updated so
`MarkOwnedZygoteControlProcess(...)` happens only after the ready wait actually
succeeds.

That means:

- a merely observed control-ready session is not enough
- failed or timed-out install attempts do not become cleanup-owned targets

### 2. Soft-skip uninstall paths now clear ownership

The `usap` soft-skip uninstall paths now also clear explicit ownership in
`SessionRegistry`.

That closes a stale-ownership leak where cleanup state could survive even when
the corresponding agent/session path was already gone.

### 3. Server-side reusable zygote session probe now uses ownership, not control-ready presence

`server/server_main.cpp` previously passed a callback into
`NinjectorSpawnInjector` that treated either of these as a reusable zygote
monitor:

- `FindControlReadyAgentSessionByPid(...)`
- `FindControlReadyAgentSessionByProcessName(...)`

That is the wrong boundary for the upcoming agent-owned model because:

- `control-ready session exists`
- `this zygote is explicitly owned by the current zygote-control lifecycle`

are not the same fact.

After this step, `server_main.cpp` only reports a reusable authoritative
zygote-control monitor when:

- `HasOwnedZygoteControlTarget(...)` is true

This keeps the experimental route aligned with the explicit ownership model
already used for shutdown cleanup.

### 4. Script unload no longer unhooks Java hooks while holding the runtime lock

`src/agent_runtime/js_runtime.cpp` now removes JS callback ownership under the
runtime mutex, releases that lock, and only then calls
`UninstallJavaJsHook(...)`.

That fixes the unload timeout / deadlock shape caused by inverse lock ordering:

- Java hook callback path: `hook mutex -> runtime mutex`
- old unload path: `runtime mutex -> hook mutex`

## Why This Matters

The target `agent-owned stable spawn` architecture needs a clean distinction
between:

- an agent connection that is technically alive and control-ready
- a zygote process that the current server lifecycle explicitly owns for spawn
  control

If those two concepts stay merged, the experimental path keeps inheriting
hidden state from unrelated ready sessions, which is exactly the kind of
ambiguity that made earlier `zygote-control` cleanup and fallback behavior hard
to reason about.

The unload fix matters for the same reason at a different boundary: it removes
one more host/runtime lifecycle edge where implicit lock coupling could pollute
real-device spawn validation.

## Validation

Passed locally:

- `build/test_zygote_control_rpc.exe`
- `build/test_session_registry.exe`
- `build/test_server_components.exe`
- `build/test_java_js_bridge.exe`
- `build/test_script_registry_unload.exe`
- `build/test_ninjector_spawn_injector.exe`
- `build/test_server_zygote_control_rpc_regressions.exe`
- `tools/build_single_server_package.ps1 -ForceRebuild`

Real-device validation observed:

- default stable spawn still routes through `legacy-ncore`
- shutdown cleanup now soft-skips inactive zygote/usap processes instead of
  trying to uninstall non-owned targets
- script unload again completes and returns `SCRIPT_UNLOAD_RESP`
- rebuilt `nook-server` starts cleanly from `/data/local/tmp/nook/nook-server`

## Current Runtime Impact

No intended user-visible default backend switch.

What changes is the ownership model around experimental `zygote-control`:

- reusable monitor authority now follows explicit owned-target state
- not generic control-ready session presence

And on the runtime side:

- script unload no longer times out due to the Java-hook/runtime lock inversion

## Next Step

Continue removing remaining places where experimental `zygote-control` still
derives routing/authority from generic ready-session presence instead of
explicit owned transaction state.

Only after those boundaries are consistently explicit should the project move
from state-prep and compatibility cleanup into the first real
`agent-owned stable spawn` default-route replacement work.
