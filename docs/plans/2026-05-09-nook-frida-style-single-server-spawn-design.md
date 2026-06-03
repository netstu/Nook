# Nook Frida-Style Single-Server Spawn Design

## Context

Nook already has a usable `nook-server`/agent architecture, but the current spawn path still depends on an external injector chain:

- `nook-server` calls into `server/ninjector_spawn_injector.cpp`
- spawn preparation goes through `libncore.so`
- spawn state is coordinated through `spawn_markers/`
- the user still has to think about more than one deployed artifact

Frida does not expose that complexity to the operator. The visible deployment unit is one `frida-server`, while the internal spawn/attach coordination stays inside the daemon.

## Goal

Make the default Nook deployment and runtime model behave like Frida:

- the user pushes only `nook-server`
- `nook-server` owns spawn, attach, resume, and gate release
- `ncore` is no longer a required device-side deployment artifact
- `spawn_markers` is replaced by internal server state or IPC

## Non-Goals

1. Do not redesign Java/Native/PLT hook APIs in this phase
2. Do not switch to a full in-memory agent injection path yet
3. Do not remove standalone builds of `libnook-agent.so`
4. Do not change host CLI semantics in the same change set

## Approaches Considered

### Option 1: Keep the current injector chain and only hide files better

Pros:

- low risk
- fast to ship

Cons:

- still not Frida-like
- `ncore` remains a deployment dependency

### Option 2: Move spawn gating into `nook-server` and retire `ncore` from the device packaging

Pros:

- matches the Frida deployment model
- user-visible artifact count drops to one server
- keeps the current agent/runtime model intact

Cons:

- requires refactoring the current spawn path
- needs careful state handling for early-spawn processes

### Option 3: Rewrite spawn injection as a separate ART/zygote integration layer

Pros:

- maximally clean long term

Cons:

- highest complexity
- too large for the current milestone

## Decision

Use Option 2.

## Architecture

`nook-server` becomes the spawn authority.

1. The host sends `spawn` or `attach` to `nook-server`
2. `nook-server` records spawn state internally instead of writing `spawn_markers`
3. `nook-server` coordinates the early child/agent handshake itself
4. `nook-server` releases the gate when the host asks for `resume`
5. The current `ncore`-backed injector becomes a compatibility path only, not a deployment requirement

This keeps the agent runtime unchanged for now and focuses the refactor on control-plane ownership.

## Implementation Shape

### Server-owned spawn controller

Add a dedicated server-side controller that owns:

- pending spawn requests
- per-pid gate state
- host-session binding
- early agent-ready caching
- resume/release signaling

### Internal spawn state

Replace `spawn_markers/` with internal state in `SessionRegistry` or a dedicated spawn registry:

- `requested`
- `spawned`
- `agent_ready`
- `script_loaded`
- `resumed`

### Compatibility layer

Keep `NinjectorSpawnInjector` only as a temporary adapter while the new server-owned path is introduced.

## File Strategy

Likely touch points:

- `server/server_main.cpp`
- `server/server_handlers.cpp`
- `server/session_registry.h`
- `server/session_registry.cpp`
- `server/injector.h`
- `server/injector.cpp`
- `server/ninjector_spawn_injector.h`
- `server/ninjector_spawn_injector.cpp`
- `src/communication/transport/spawn_marker.*`

## Validation

### Unit-level

1. spawn request creates internal gate state
2. resume clears the gate state
3. agent-ready messages are replayed from server cache
4. no file-based spawn marker is required

### Device-level

1. push only `nook-server`
2. start `./nook-server`
3. attach/spawn still works
4. no user-visible `ncore` deployment step remains

## Risks

1. Early-spawn timing bugs
   - mitigate with cached ready frames and explicit state transitions
2. Regression in attach/spawn flow
   - mitigate with replay tests and device smoke tests
3. Partial migration complexity
   - mitigate by keeping the injector adapter during the transition

## Follow-Up

Once this is stable, the next step is to decide whether the remaining injector compatibility path should:

- be removed entirely, or
- be kept only as a non-default fallback for legacy environments
