# Nook Spawn Gate Design

## Goal

Replace Nook's current coarse spawn suspension model with a Frida-style early-process gate so scripts can be created and loaded before the spawned app is allowed to continue.

## Problem

Nook's current `spawn` flow is built around a host/server-side suspended-process model:

1. host sends `SpawnRequest`
2. server launches the target process
3. server/host treat the target as "spawn suspended"
4. host later sends `ResumeRequest`

This model was good enough for the first working prototype, but it has a hard limitation on real devices:

- when the process is suspended too early or too coarsely, the target-side agent cannot reliably process `script.create()` / `script.load()` before resume

This is why current CLI behavior had to settle on:

1. `spawn`
2. wait for `AgentReady`
3. `resume`
4. create/load script

That ordering is the opposite of Frida's useful spawn semantics.

## Design Summary

The new design keeps the outer request/response shell mostly stable in the first phase, but replaces the meaning of "spawn suspended" with a real target-side gate:

1. injector installs a zymbiote-style early hook
2. target child reaches the early hook and reports back
3. injector restores zygote patching immediately
4. agent starts inside the child and connects to server
5. child enters a target-side gate wait
6. host gets `AgentReady`, creates/loads scripts
7. host sends `ResumeRequest`
8. server releases the target-side gate
9. child continues normal execution

This gives Nook the important Frida-like property:

- the app can remain blocked at a safe early gate while the script is already alive

## Scope

First phase only changes the suspension model. It does not yet redesign the full host protocol or CLI surface.

The first phase explicitly aims to preserve:

- `SpawnRequest`
- `SpawnResponse`
- `AgentReady`
- `ResumeRequest`
- `ResumeResponse`
- most current `host_spawn_client` behavior
- most current `nook-cli spawn` / `repl spawn` shell

## Architecture

### 1. Injector Layer

The injector's job is only to deliver a gated child process to the server.

Responsibilities:

- patch zygote with an early-process hook
- wait for child callback
- identify the real child pid / metadata
- restore zygote patching immediately
- return the child pid and gate ownership result

Non-responsibilities:

- script loading
- RPC routing
- host-side session decisions

This layer should evolve from the already-validated `Ninjector --spawn-symbi` transaction:

- callback-based child identification
- automatic zygote restore
- child-local one-shot unhooking

### 2. Server Layer

The server becomes the owner of spawn gate state.

Responsibilities:

- handle `SpawnRequest`
- call the injector and receive a gated child pid
- bind the host session to this child pid
- track gate state in the session registry
- forward `AgentReady`
- translate `ResumeRequest` into "release gate"

The server should stop thinking in terms of:

- "OS-level suspended process"

and instead track:

- "gate-held child"

### 3. Agent / Runtime Layer

The agent must implement the real gate wait inside the target process.

Responsibilities:

- initialize communication
- send `AgentReady`
- block on a real target-side gate
- continue only after the server releases it

This is the missing piece in current Android behavior. Today:

- `NookCommWaitForResumeIfSpawned()` on Android returns success immediately

It must become a real synchronization point.

### 4. Host Layer

The host layer is intentionally kept as stable as possible in phase 1.

Responsibilities stay the same:

- call `spawn`
- wait for `AgentReady`
- create/load scripts
- call `resume`

What changes is only the backend meaning of `resume`:

- old meaning: send `SIGCONT` or equivalent process resume
- new meaning: release the target-side early gate

## State Machine

Phase 1 introduces a more precise internal state model:

- `spawn_requested`
- `gate_held`
- `agent_ready`
- `released`

### `spawn_requested`

- server has accepted `SpawnRequest`
- injector transaction is running

### `gate_held`

- injector has returned a valid child pid
- zygote patch is already restored
- child is expected to enter or already be in target-side gate wait

### `agent_ready`

- agent session exists
- `AgentReady` has been sent to the host
- gate has not yet been released
- this is the intended script installation window

### `released`

- host has sent `ResumeRequest`
- server has released the gate
- target process continues

## Compatibility Strategy

Phase 1 should preserve existing public semantics where possible, even if names are temporarily imprecise.

### Preserve existing protocol

Keep:

- `SpawnResponse(pid)`
- `AgentReady`
- `ResumeRequest(pid)`

Do not add a new message type in phase 1 unless necessary.

### Preserve existing registry API shape first

The current registry and tests already talk about:

- `MarkSpawnSuspended(...)`
- `IsSpawnSuspended(...)`
- `SpawnSuspendedEntry`

In phase 1, these may continue to exist as API names, but their meaning changes from:

- "server-side suspended process"

to:

- "server-owned gate-held child"

This keeps the migration smaller.

### Rename later

Once the new behavior is stable, the next cleanup pass should rename:

- `SpawnSuspendedEntry` -> `SpawnGateEntry`
- `MarkSpawnSuspended` -> `MarkSpawnGateHeld`
- related tests and log text

That rename should be a second step, not bundled into first implementation.

## Command Semantics After Phase 1

### `nook-cli spawn ... -l script.js --resume`

Target behavior:

1. `spawn`
2. wait for `AgentReady`
3. create/load script
4. send `resume`
5. child continues

This is the first genuinely useful Frida-style path.

### `nook-cli repl spawn ... -l script.js`

Target behavior:

1. `spawn`
2. wait for `AgentReady`
3. do not auto-release
4. allow deferred or explicit script load while gate is still held
5. `%resume` releases gate

This directly fixes the current architectural limitation where loading before resume was impossible under coarse process suspension.

### `%resume`

New meaning:

- release the target-side gate

Not:

- resume a previously SIGSTOP'd process

## Files Expected To Change

### Injector / Server Integration

- `server/ninjector_spawn_injector.*`
- `server/injector.h`

### Server State / Dispatch

- `server/server_handlers.*`
- `server/session_registry.*`
- `tests/communication/test_server_handlers.cpp`
- `tests/communication/test_ninjector_spawn_injector.cpp`

### Agent / Runtime Gate

- `src/framework/NookComm.cpp`
- possibly related runtime state used to decide whether the process is gated
- `tests/headers/test_public_headers.cpp`

### Host / Client

- `src/communication/host/host_spawn_client.cpp`
- `host/nook-py/nook/cli.py`
- `host/nook-py/tests/test_cli.py`

Phase 1 should minimize host changes, but the test expectations around ordering will need to change after the target-side gate becomes real.

## Risks

### 1. Deadlock between `AgentReady` and gate wait

If the agent blocks too early, `AgentReady` will never reach the host.

Mitigation:

- send `AgentReady` before entering the gate wait
- gate only after control channel is known-good

### 2. Gate release races

If the host releases too early, script installation may still miss startup windows.

Mitigation:

- keep current host ordering shape
- only allow `resume` after the usual script setup path is complete

### 3. Mixed semantics during migration

During phase 1, code and tests may still use "spawn suspended" naming while behavior is already gate-based.

Mitigation:

- document the semantic change clearly
- plan a dedicated rename cleanup after behavior stabilizes

## Recommendation

Implement the migration in two stages:

### Stage 1

- keep protocol stable
- replace server-side coarse suspension with target-side gate
- make `NookCommWaitForResumeIfSpawned()` real on Android
- keep old naming where needed

### Stage 2

- clean up naming
- revisit whether a dedicated `SpawnGateReady` protocol event is worth adding
- simplify CLI/REPL ordering now that the real gate exists

## Expected Outcome

After stage 1, Nook should support the workflow that matters most:

- spawn target
- let the target reach a safe early gate
- connect agent
- load scripts before app execution continues
- release the gate only when the host is ready

That is the minimum viable foundation needed to make Nook's spawn semantics feel meaningfully Frida-like.
