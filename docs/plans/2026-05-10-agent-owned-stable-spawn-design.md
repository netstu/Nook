# Nook Agent-Owned Stable Spawn Design

## Context

As of 2026-05-10, Nook has already reached an important milestone on the test device:

- the user-visible deployment can be reduced to one visible `nook-server`
- embedded agent selection is the default runtime path
- embedded `ncore` delivery is working through remote `memfd` + `/proc/self/fd/<n>`
- stable spawn for `com.ad2001.frida0x1` is passing on the Android 11 device

However, that does **not** mean stable spawn is architecturally Frida-like yet.

The current default stable spawn path still depends on the legacy `ncore` logic:

1. `server/ninjector_spawn_injector.cpp` still defaults to the legacy spawn backend
2. `server/ninjector_compat.cpp` still calls `PrepareSpawnInZygote(...)`
3. `PrepareSpawnInZygote(...)` still resolves and invokes `ainject`
4. the actual spawn interception logic still lives in `server/ncore_fallback.cpp`

So the current state is:

- single visible server: yes
- no default sidecar deployment requirement: yes
- stable spawn no longer logically depends on `ncore`: no

That last item is the next real milestone.

## Goal

Move the stable default spawn backend out of `ncore` and into the main agent runtime.

After this change, the default stable spawn model should be:

1. `nook-server` injects `libnook-agent.so` into zygote
2. the zygote-resident agent installs spawn hooks
3. the child process inherits the agent through fork/specialize
4. the child agent activates only when the target package matches
5. the existing `AGENT_READY -> SCRIPT_CREATE -> SCRIPT_LOAD -> RESUME` flow continues unchanged
6. legacy `ncore` remains only as an explicit fallback/debug backend

## Non-Goals

1. Do not redesign the user-facing hook APIs in this phase
2. Do not replace the attach path in the same change
3. Do not chase Android 12+ USAP / repeated-fork compatibility in the first cut
4. Do not remove `ncore` source code immediately
5. Do not change host CLI semantics during the first migration

## Approaches Considered

### Option 1: Keep embedded `ncore` as the stable default and optimize around it

Pros:

- lowest code churn
- current device path already works

Cons:

- does not remove the architectural dependency on `ncore`
- keeps two different spawn systems alive
- does not move Nook materially closer to Frida's model

### Option 2: Merge `ncore` spawn logic into the main agent and make it the default stable backend

Pros:

- closest to the Frida direction
- unifies attach/spawn around one agent runtime
- lets `ncore` become a real fallback instead of the main logic

Cons:

- requires careful refactoring of zygote-side hook lifecycle
- needs a new control protocol between server and zygote agent

### Option 3: Skip the current native hook points and rewrite stable spawn around Java/art specialize hooks immediately

Pros:

- cleaner long term model
- potentially closer to Frida's internal control points

Cons:

- higher risk
- too many variables for the current Android 11 stabilization milestone
- would mix architecture migration with hook-point migration

## Decision

Use Option 2.

First migrate the already-validated native spawn interception logic into the main agent runtime.
Only after that is stable should the hook points themselves be upgraded toward a more Frida-like specialize path.

## Architecture

### Current stable default path

```text
server
  -> inject embedded ncore into zygote
  -> call ainject(package, agent_path)
  -> ncore installs fork/vfork + specialize-related hooks
  -> child matches target package
  -> child loads libnook-agent.so
  -> agent sends AGENT_READY
```

### Target stable default path

```text
server
  -> inject agent into zygote
  -> send SPAWN_INSTALL(target_package, spawn_token)
  -> zygote-agent installs spawn hooks
  -> child matches target package
  -> inherited child-agent activates itself
  -> child-agent sends AGENT_READY
  -> server sends SCRIPT_CREATE / SCRIPT_LOAD / RESUME
```

### Responsibility split

#### `nook-server`

- inject agent into zygote if needed
- arm one stable spawn transaction at a time
- start the target app
- wait for `AGENT_READY` with matching `spawn_token`
- continue the existing script and resume flow
- request zygote-agent uninstall after completion

#### `zygote-agent`

- own spawn hook installation/uninstallation
- store the current armed target package + spawn token
- detect child specialization/fork match
- activate only in the matching child
- stay mostly inert in non-target children

#### `child-agent`

- inherit from zygote through fork/specialize
- activate runtime bridge only when the target package matches
- send `AGENT_READY`
- reuse the current spawn gate/bootstrap path

## Message Flow

The new default stable flow should be message-driven, not file-coordinated and not weakly inferred from pid timing.

### Control-plane messages

New internal control messages between server and zygote-agent:

- `SPAWN_INSTALL`
- `SPAWN_INSTALL_OK`
- `SPAWN_INSTALL_ERROR`
- `SPAWN_UNINSTALL`
- `SPAWN_UNINSTALL_OK`

### End-to-end flow

1. `server` injects agent into zygote
2. `server -> zygote-agent`: `SPAWN_INSTALL(target_package, spawn_token, mode=stable)`
3. `zygote-agent -> server`: `SPAWN_INSTALL_OK`
4. `server` starts target app
5. zygote forks/specializes child
6. `child-agent` checks whether the child package matches the armed target
7. if it matches, `child-agent` activates runtime and sends `AGENT_READY(spawn_token, pid, process_name, arch, version)`
8. `server` binds the ready child to the pending spawn transaction by `spawn_token`
9. `server` continues existing script load and resume flow
10. `server -> zygote-agent`: `SPAWN_UNINSTALL`

## State Machine

The zygote-agent state machine should stay deliberately simple.

### States

- `Idle`
- `Armed`
- `Consumed`

### State meaning

- `Idle`: no spawn hooks installed, no active target package
- `Armed`: spawn hooks installed, one target package/token is being watched
- `Consumed`: a matching child has already activated; waiting for explicit uninstall/cleanup

### Rules

1. only one stable spawn target may be `Armed` at a time
2. a second `SPAWN_INSTALL` while `Armed` must fail explicitly
3. successful child activation transitions `Armed -> Consumed`
4. `SPAWN_UNINSTALL` must always attempt to restore `Idle`
5. `AGENT_READY` must be matched by `spawn_token`, not by timing or pid heuristics

## Hook-Point Strategy

### First migration cut

Do **not** change hook points yet.

Reuse the same native hook points already validated on the Android 11 device through `ncore_fallback.cpp`:

- `fork`
- `vfork`
- `selinux_android_setcontext`
- `android_os_Process_setArgV0`

Reason:

- these points are already proven on the current device
- the current problem is ownership and architecture, not first-principles hook discovery
- moving validated logic is lower risk than inventing a new stable backend and new interception points at the same time

### Later migration

After the stable agent-owned backend is proven:

- evaluate `nativeForkAndSpecialize`
- evaluate `nativeSpecializeAppProcess`
- evaluate Android 12+ `ZygoteCommandBuffer` / USAP-specific paths

That should be a separate phase.

## File Strategy

### New files

- `src/framework/NookZygoteSpawn.h`
- `src/framework/NookZygoteSpawn.cpp`

### Main files to modify

- `src/framework/NookComm.cpp`
- `src/communication/protocol/message_types.h`
- `src/communication/protocol/messages.h`
- `src/communication/protocol/messages.cpp`
- `server/ninjector_spawn_injector.cpp`
- `server/ninjector_spawn_injector.h`
- `server/server_handlers.cpp`
- `server/server_main.cpp`

### Legacy compatibility files retained for fallback

- `server/ncore_fallback.cpp`
- `server/ninjector_compat.cpp`

## Fallback Boundary

The stable default path should become agent-owned.

Legacy `ncore` should remain only when one of these is true:

1. explicit environment switch enables it
2. the new agent-owned zygote path is unavailable on the current build
3. debugging requires side-by-side backend comparison

It must **not** remain the silent default path.

## Validation Matrix

### Host/unit validation

1. `SPAWN_INSTALL` rejects concurrent armed targets
2. `AGENT_READY` is matched by `spawn_token`
3. `SPAWN_UNINSTALL` restores the zygote-agent state to `Idle`
4. default path selection does not call `PrepareSpawnInZygote()` when the agent-owned path is available

### Device validation: first milestone

Primary target:

- `com.ad2001.frida0x1`

Success criteria:

1. only `nook-server` is visible in `/data/local/tmp/nook`
2. spawn succeeds without visible `libncore.so`
3. server log shows zygote-agent install/uninstall, not `ainject`
4. hook script loads and runs
5. no regression in current spawn gate behavior

### Device validation: later

- `com.ad2001.frida0x8` attach regression check
- optional multi-run stability loop on `frida0x1`
- Android 12+ / USAP behavior checks

## Risks

### Risk 1: zygote-agent state leaks across runs

Impact:

- later spawn requests may behave unpredictably

Mitigation:

- enforce `Idle/Armed/Consumed`
- make uninstall idempotent
- add explicit cleanup on failed arm/start paths

### Risk 2: inherited child-agent activates in non-target children

Impact:

- correctness and stability regressions outside the requested package

Mitigation:

- require exact target-package match before activation
- keep non-target child path inert

### Risk 3: migration changes too many variables at once

Impact:

- difficult debugging

Mitigation:

- first migration reuses existing native hook points
- do not combine with attach-path changes
- keep explicit legacy fallback during rollout

## Recommended Execution Order

1. add spawn control messages and zygote-agent state model
2. move the current `ncore` spawn hook logic into `src/framework/NookZygoteSpawn.*`
3. wire server default spawn path to inject zygote agent and use `SPAWN_INSTALL`
4. keep legacy `ncore` behind explicit fallback only
5. validate on `com.ad2001.frida0x1`
6. only then consider replacing hook points with more Frida-like specialize hooks
