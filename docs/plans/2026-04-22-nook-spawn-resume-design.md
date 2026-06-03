# Nook Spawn Resume Design

**Goal**

Build a real `spawn -> attach/load -> resume` flow for Nook so that a target app started through `SpawnRequest` can be suspended in its early startup phase and only continue after the host explicitly sends `ResumeRequest`.

**Context**

The current communication stack already has working support for:

- `SpawnRequest / SpawnResponse`
- `AttachRequest / AttachResponse`
- `DetachRequest / DetachResponse`
- `ProcessListReq / ProcessListResp`
- `AppListReq / AppListResp`
- agent readiness and script message forwarding

What is still missing is the actual control-plane semantics of `resume`. Right now `SpawnRequest` only means "start target app and wait for injected agent callback". The process is not intentionally suspended, so the host cannot reliably install hooks before early Java or native code runs.

**Why Resume Exists**

`resume` is not intended to be a generic "send SIGCONT to any pid" command. Its purpose is narrower:

1. Start a target app through `spawn`
2. Inject the agent as early as possible
3. Stop the process before user code continues
4. Let the host attach, load scripts, and install hooks
5. Explicitly continue the process through `resume`

This matches the Frida-style workflow:

```text
pid = spawn(pkg)
attach(pid)
create_script(...)
load()
resume(pid)
```

Without this two-phase control, Nook will race against app startup and miss early lifecycle and native entry points.

## Recommended Architecture

The recommended implementation is to let the injected agent suspend and resume the target process from inside the target process itself, instead of letting the server perform an external `ptrace + PTRACE_CONT`.

### Why This Approach

- It avoids reintroducing server-side privilege coupling for resume.
- It keeps the suspend/resume semantics inside the same runtime that owns the injected state.
- It is closer to the proven idea used by `rustFrida_upstream`, where the early runtime stops itself and waits for host permission to continue.
- It cleanly separates two modes:
  - `attach`: inject into an already running process
  - `spawn`: inject early, suspend, and wait for explicit resume

## Control Flow

### Spawn Phase

1. Host sends `SpawnRequest`.
2. Server uses the existing Ninjector-based spawn path.
3. Target process starts and the agent is injected.
4. Agent initializes communication.
5. Agent marks itself as a spawn-suspended process and sends an internal "spawn suspended" message to the server.
6. Agent raises `SIGSTOP` against its own process.
7. Server records the target `pid` as suspended and associates it with the host session.
8. Host may then attach, create scripts, load scripts, and prepare hooks while the target is stopped.

### Resume Phase

1. Host sends `ResumeRequest(pid)`.
2. Server validates that the `pid` is currently tracked as spawn-suspended.
3. Server forwards the request to the matching agent session.
4. Agent clears its internal suspended state and performs the resume operation.
5. Agent sends `ResumeResponse`.
6. Server clears the suspended entry and returns success to the host.

## State Model

Server-side state must explicitly distinguish normal attached processes from spawn-suspended processes.

Recommended tracked fields per suspended process:

- `pid`
- `host_session_id`
- `agent_session`
- `state`
  - `waiting_agent`
  - `suspended`
  - `resumed`
- optional creation timestamp for timeout cleanup

This state is necessary to prevent:

- resuming a process that was never suspended
- resuming twice
- treating a normal attach target as spawn-resumable
- stale suspended entries after agent disconnect

## Protocol Changes

### Public Messages

Add:

- `ResumeRequest`
  - `pid`
- `ResumeResponse`
  - `pid`
  - `error`

### Internal Messages

Add one agent-to-server internal message, for example:

- `AgentSpawnSuspended`
  - `pid`

This message means: "the spawned target process has reached the controlled suspended state and can later accept `ResumeRequest`".

The existing `AgentReady` message is not enough to represent this state. A process can be ready for communication without yet being intentionally suspended.

## Server Semantics

### `SpawnRequest`

`SpawnRequest` should continue to return the injected target `pid`, but the server must not assume that the process is resumable until the new internal suspended message arrives from the agent.

### `ResumeRequest`

`ResumeRequest(pid)` should behave as follows:

- if `pid` is unknown: error `not found`
- if `pid` is known but not suspended yet: error `not ready`
- if `pid` was already resumed: error `already resumed`
- if the agent session is gone: error `agent unavailable`
- otherwise: forward to agent and await `ResumeResponse`

### Disconnect Cleanup

If the agent disconnects, the server must clear suspended state for that `pid`.

## Agent Semantics

The agent should only enter spawn-suspend flow when it knows it was started through Nook's spawn path.

Important constraints:

- `SIGSTOP` behavior applies only to spawn mode
- normal attach mode must never suspend the target process
- `ResumeRequest` is valid only for processes currently in spawn-suspended state

The agent should not rely on the server to perform `ptrace`-based continuation.

## Why Not Server-Side Resume

An alternative would be to let the server resume the target externally using root privileges and `ptrace`-style primitives. This is not recommended for Nook's current state because:

- it reintroduces privilege sensitivity similar to attach injection
- it forces resume semantics to depend on server execution context
- it duplicates process control responsibilities that the in-process agent can already own

## Reference Value From Other Projects

### GirlHook

GirlHook is useful as a general reference for integrated hook frameworks and workflow shape, but it does not provide a directly reusable spawn-suspend-resume implementation for this design.

### rustFrida_upstream

`rustFrida_upstream` is the stronger reference for this feature because it already uses an early-runtime stop/continue model around spawn, including `SIGSTOP` / `SIGCONT` style process control and host permission to continue.

Nook should borrow the control-flow idea, but keep its existing Ninjector-based spawn entry path and current communication architecture.

## Files Expected To Change

Primary files:

- `src/communication/protocol/messages.h`
- `src/communication/protocol/messages.cpp`
- `server/session_registry.h`
- `server/session_registry.cpp`
- `server/server_handlers.h`
- `server/server_handlers.cpp`
- `src/framework/NookComm.h`
- `src/framework/NookComm.cpp`
- `src/communication/host/host_client.h`
- `src/communication/host/host_client.cpp`
- `tools/nook_spawn_smoke.cpp`
- `tools/nook_resume_smoke.cpp` (new)

Tests:

- `tests/communication/test_protocol.cpp`
- `tests/communication/test_host_client.cpp`
- `tests/communication/test_server_handlers.cpp`

## Testing Strategy

### Local

- protocol round-trip tests for `ResumeRequest / ResumeResponse`
- handler tests for suspended and non-suspended resume paths
- host-client tests for request/response decode

### Device

- start server
- run spawn smoke
- confirm process reaches suspended state
- attach or load a trivial script
- run resume smoke
- verify the app continues only after resume

## Success Criteria

The feature is complete when:

1. `spawn` creates a process that does not freely continue before host permission
2. the server tracks suspended spawned processes explicitly
3. `resume(pid)` succeeds only for tracked suspended spawn targets
4. the host can reliably install hooks before the target continues
5. device smoke confirms the end-to-end flow
