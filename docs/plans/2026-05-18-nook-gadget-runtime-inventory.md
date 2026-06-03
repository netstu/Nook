# Nook Gadget Runtime Inventory

## Goal

Map the current Nook codebase to the first-version `nook-gadget` runtime so implementation can extract only the pieces needed for an in-process auto-start payload and avoid dragging `nook-server`, injector, or zygote-specific behavior into the gadget path.

## Required Runtime Pieces

### Script runtime core

These files already form the core of the in-process script runtime and should be treated as the main reusable `nook-gadget` payload logic:

- `src/agent_runtime/js_runtime.h`
- `src/agent_runtime/js_runtime.cpp`
- `src/agent_runtime/script_registry.h`
- `src/agent_runtime/script_registry.cpp`
- `src/agent_runtime/nook_script_runtime_bridge.h`
- `src/agent_runtime/nook_script_runtime_bridge.cpp`
- `src/agent_runtime/nook_native_js_bridge.h`
- `src/agent_runtime/nook_native_js_bridge.cpp`
- `src/agent_runtime/nook_java_js_bridge.h`
- `src/agent_runtime/nook_java_js_bridge.cpp`

Why they matter:

- `js_runtime.*` owns script execution, message dispatch, and RPC invocation.
- `script_registry.*` owns script lifecycle tracking.
- `nook_script_runtime_bridge.*` binds the runtime to `NookComm` callbacks and is already close to what a gadget needs.
- `nook_native_js_bridge.*` and `nook_java_js_bridge.*` expose the instrumentation APIs the gadget is supposed to surface to scripts.

### Communication protocol and client-side transport

These components are reusable because `nook-gadget` should speak the existing Nook protocol instead of inventing a new one:

- `src/communication/protocol/*`
- `src/communication/session/*`
- `src/communication/agent/agent_connection.*`
- `src/communication/transport/unix_transport.*`
- `src/communication/transport/tcp_transport.*`

Why they matter:

- protocol/session files define the message format the host already understands
- `AgentConnection` is the nearest current abstraction for an in-process runtime talking to a host-side controller
- transport implementations may be reused directly or wrapped behind a smaller gadget-specific startup adapter

### Nook communication surface

These files are central because they already expose the runtime-facing public API used by the script bridge:

- `include/nook/NookComm.h`
- `src/framework/NookComm.cpp`
- `src/framework/NookCommInternal.h`
- `src/framework/NookCommInternal.cpp`

Why they matter:

- `NookComm.h` is the public API already used by the runtime bridge
- `NookCommInternal.*` contains lower-level helper paths that can stay internal to the gadget runtime
- `NookComm.cpp` currently contains both useful reusable runtime logic and the main unwanted coupling that must be split

### Agent runtime helpers

These framework files are likely reusable with a smaller scope than today:

- `include/nook/NookAgent.h`
- `src/framework/nook_agent_runtime.h`
- `src/framework/nook_agent_runtime.cpp`
- `src/framework/nook_agent_init_policy.h`
- `src/framework/nook_agent_init_policy.cpp`

Why they matter:

- they encode current assumptions around when a process should initialize Nook
- some of this policy should remain reusable
- some of it is currently too tied to spawn/zygote/server paths and must be reduced for gadget use

## Server-Only Pieces

These areas should be treated as out of gadget scope and should not be dragged into `libnook-gadget.so`:

- `server/*`
- embedded payload blob generation and consumption:
  - `server/embedded_*`
  - `server/generated/*`
- `server/server_main.cpp`
- `server/server_handlers.*`
- `server/session_registry.*`
- `server/process_manager.*`
- `server/spawn_controller.*`

Why they are server-only:

- they implement the external controller process, not the in-app runtime
- they track host sessions across processes
- they own spawn/install/finalize orchestration
- they are packaging and deployment infrastructure for `nook-server`, not in-process gadget code

## Injector-Only Pieces

These files and flows are useful references but should not be runtime dependencies of `nook-gadget`:

- `server/injector.*`
- `server/ninjector_*`
- `server/symbi_*`
- `server/ncore_fallback.cpp`
- device-side injected artifact materialization
- memfd/runtime-dir placement logic
- explicit `dlopen` and injected init-symbol orchestration

Why they are injector-only:

- they assume Nook is introduced into a process by an external launcher
- they manage filesystem staging or injected symbol selection
- they route between multiple spawn backends

`nook-gadget` should be loadable by ordinary app startup, not by injector-specific deployment assumptions.

## Zygote- And Spawn-Specific Pieces To Exclude From Gadget v1

The current biggest coupling risk is inside `src/framework/NookComm.cpp`. The gadget runtime must avoid inheriting these responsibilities in v1:

- zygote-control initialization
- spawn gate arm/wait/release state
- inherited connection reset for forked child processes
- early-process detection for zygote/usap
- promoted strict zygote-control child logic
- bootstrap hook installation that only exists to support spawn/child activation timing
- any path whose primary purpose is delayed child activation after specialization

These are valid for current spawn workflows but are not the core of a repackaged gadget model.

## Most Important Extraction Seams

### Seam 1: Split `NookComm.cpp` into reusable gadget runtime startup vs. spawn/zygote policy

Current problem:

- `NookComm.cpp` mixes connection startup, runtime bridge initialization, `AGENT_READY` behavior, auto-init constructor behavior, zygote-control behavior, and spawn-child behavior in one file

Extraction target:

- a smaller gadget runtime path that can do:
  - initialize transport/control channel
  - initialize runtime bridge
  - expose ready state
  - optionally send a gadget-appropriate ready signal

Without:

- spawn gate logic
- zygote-only reinit logic
- injected child restoration logic

### Seam 2: Keep `nook_script_runtime_bridge.cpp` reusable but remove implicit dependency on "NookComm owns eager agent initialization"

Current signal:

- the file already says `NookComm owns eager agent initialization`

Extraction target:

- the bridge should become installable by either:
  - current agent/server path
  - future gadget runtime path

Meaning:

- bridge registration should depend on a ready communication surface, not on the current agent bootstrap model

### Seam 3: Treat current `NookAgentInitialize*` family as multiple deployment policies, not one universal runtime contract

Current public exports:

- `NookAgentInitialize`
- `NookAgentInitializeForZygoteControl`
- `NookAgentInitializeForSpawnChild`

Gadget implication:

- `nook-gadget` likely needs its own smaller startup entry instead of pretending one of these existing policy-heavy exports is already the right long-term contract

The first version can temporarily route through existing initialization code if needed, but the target architecture should move toward a dedicated gadget runtime entry.

## Candidate Reuse Model For v1

### Reuse directly

- script runtime core
- script registry
- native/js/java bridge implementations
- existing protocol/session types
- as much of the client-side connection/transport code as can be used without server assumptions

### Reuse with wrapper or extraction

- `NookComm.cpp`
- `NookCommInternal.*`
- `nook_agent_runtime.*`
- init policy files

### Do not reuse as gadget runtime dependencies

- `server/*`
- `ninjector` paths
- `symbi` paths
- server-side embedded payload orchestration
- spawn/zygote state tracking as part of gadget v1 core

## Recommended Task 2-6 Direction

The first implementation tasks should treat `nook-gadget` as a distinct runtime target with this shape:

1. add a new native target
2. add a small gadget runtime wrapper under a new directory such as `src/gadget/`
3. move or wrap reusable initialization from `NookComm.cpp`
4. keep the public script/runtime surface aligned with current host behavior
5. postpone deep `NookComm` cleanup until a minimal gadget path is working

This keeps the first version focused on proving the deployment model instead of prematurely finishing the full internal architecture.

## Immediate Risks

### Risk 1: Accidentally keeping spawn semantics in gadget startup

Symptom:

- gadget startup waits for spawn-specific conditions or manipulates fork/zygote state

Impact:

- repackaged app startup becomes fragile or blocks at the wrong time

### Risk 2: Accidentally keeping server ownership assumptions in gadget runtime

Symptom:

- runtime assumes a `nook-server` process is the owner of session orchestration

Impact:

- gadget path starts but host attach behavior is inconsistent

### Risk 3: Over-extracting before proving a working path

Symptom:

- broad refactor of `NookComm.cpp` and init policy before a minimal gadget runtime is validated

Impact:

- slower progress and higher regression risk against existing spawn work

## Conclusion

The codebase already contains almost everything needed for `nook-gadget` at the runtime layer. The real work is not inventing a second runtime, but isolating the existing reusable in-process runtime from:

- server ownership
- injector deployment
- zygote/spawn activation policy

The highest-value extraction target is therefore the startup and readiness logic currently concentrated in `src/framework/NookComm.cpp`.
