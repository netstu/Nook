# Nook Spawn / Zygote / Ready Stability Design

## Goal

Stabilize Nook's current `spawn / zygote / ready` pipeline before attempting a more Frida-like pre-resume activation model.

This pass does not try to redesign the full spawn architecture. It tightens the existing one so that:

- `spawn --resume --wait` no longer times out intermittently
- host sessions do not bind to `zygote64` / `usap64`
- `AgentReady`, spawn callback, script load, and resume release have a deterministic order
- `Java.ready(...)` behaves more consistently during cold spawn

## Why This Pass Comes First

Nook is already much closer to Frida at the API layer:

- `Java.openClassFile(...)`
- `Java.registerClass(...)`
- `Java.performNow(...)`
- `Java.ClassFactory.get(loader).openClassFile(...)`

The next bottleneck is no longer API shape. It is lifecycle reliability.

If the current spawn gate remains timing-sensitive, any attempt to push directly toward Frida-style "script fully active before app user code continues" semantics will amplify instability instead of reducing it.

So the correct order is:

1. stabilize the current spawn gate and ready model
2. then evolve that stable model toward Frida-like pre-resume activation

## Current Observed Problems

Recent logs and device tests show the remaining instability is concentrated in four places:

1. `spawn` can still fail with coarse `operation timed out`
2. host/session binding can still be polluted by early `zygote64` / `usap64` agent presence
3. the ordering between:
   - authoritative spawned child pid
   - `AgentReady`
   - script load
   - resume release
   is still distributed across multiple components instead of enforced by one state machine
4. `Java.ready(...)` during cold spawn still depends on lifecycle timing in ways that are not surfaced clearly to the host

## Design Principles

This pass should follow three constraints:

### 1. Stabilize Without Replacing

Do not replace the current spawn-gate model yet.

Keep:

- the current Ninjector-based spawn entry
- the current child-side gate model
- the current host `spawn -> load -> resume` shape

Only tighten the transitions and ownership.

### 2. Make State Explicit

Today too much meaning is implied by timing and side effects.

This pass should make the following states explicit:

- injector has produced a candidate child pid
- target child has connected
- target child is the authoritative spawn child
- target child is gate-held
- host may load scripts
- host has released resume
- target child has left gate-held state

### 3. Separate Spawn State From Java Ready State

`spawn gate released` and `Java.ready(...) fired` are not the same event.

They may be correlated in some runs, but they belong to different layers:

- spawn gate is process control
- `Java.ready(...)` is class-loader / Android lifecycle readiness

This pass should keep them separate and observable.

## Chosen Design

### High-Level Model

Keep the current spawn gate architecture, but formalize it as a three-layer state machine:

- injector/server layer
- agent/process-control layer
- Java-ready/bootstrap layer

Each layer gets a narrow responsibility.

### Layer 1: Injector / Server

Responsibilities:

- identify the authoritative target child pid
- reject non-authoritative `zygote64` / `usap64` agent-ready noise
- keep per-spawn gate state with explicit lifecycle:
  - `waiting_child`
  - `child_connected`
  - `gate_held`
  - `scripts_loaded`
  - `resume_released`
  - `completed`
  - `failed`
- report stage-specific timeout/failure reasons instead of generic `operation timed out`

Important rule:

- only one pid is authoritative for a given spawn transaction
- any earlier zygote-side connection must never claim the host-facing spawned session

### Layer 2: Agent / NookComm

Responsibilities:

- connect as the child-side agent
- report spawn callback readiness after communication bootstrap is truly usable
- wait for resume only at a lifecycle-safe blocking point
- release the gate exactly once

Important rule:

- "ready to bind to host" and "safe to wait for resume" must happen in a defined order
- the gate wait should live at a lifecycle point late enough to avoid startup crashes but early enough to precede target application code of interest

### Layer 3: Java Ready / Bootstrap

Responsibilities:

- track app class-loader readiness
- run `Java.ready(...)` callbacks once the application loader is usable
- stay orthogonal to spawn gate state

Important rule:

- `Java.ready(...)` should not be used as an implicit transport signal for spawn control
- it should remain a Java-layer readiness primitive only

## Recommended State Machine

For each spawned target, the server should track a dedicated spawn transaction object.

Minimum fields:

- `requested_package`
- `expected_pid`
- `host_session_id`
- `agent_session_id` if connected
- `state`
- `created_at`
- `last_transition_at`
- `failure_reason`

Recommended server states:

- `waiting_spawn_result`
- `waiting_agent_ready`
- `waiting_gate_hold`
- `ready_for_script_load`
- `waiting_resume_release`
- `resumed`
- `failed`

Recommended invariants:

- `script load` is allowed only in `ready_for_script_load`
- `resume` is allowed only in `waiting_resume_release`
- timeout handling is state-specific

## Host / CLI Behavior

This pass should make host semantics clearer without changing the outer CLI shape.

Desired flow:

1. `spawn`
2. wait for authoritative child bind
3. if `-l` is present, load script while gate is held
4. if `--resume` is present, release resume after script load success
5. if `--wait` is present, continue streaming messages

Important host behavior improvements:

- distinguish timeout stages in error messages
- distinguish:
  - spawn result timeout
  - agent-ready timeout
  - gate-held timeout
  - script-load timeout
- make suspended vs resumed state visible in REPL / CLI status

## Relationship To Frida

This pass is not the final Frida-equivalent spawn model.

Frida's ideal user-facing experience is closer to:

- spawn target
- script active before meaningful target execution proceeds
- resume once instrumentation is fully in place

This pass is a prerequisite for that. It gives Nook:

- one authoritative child binding
- one stable gate lifecycle
- one observable separation between spawn control and Java readiness

Only after that is stable should Nook move toward a stronger Frida-style pre-resume activation design.

## Alternatives Considered

### Option 1: Small tactical fixes only

Pros:

- fastest short-term

Cons:

- keeps state split across components
- likely to reintroduce timing bugs later
- weak foundation for Frida-style evolution

Rejected.

### Option 2: Stability tightening of the existing model

Pros:

- preserves what already works
- contains risk
- creates a clean base for later Frida-like evolution

Cons:

- not yet the final desired user experience

Chosen.

### Option 3: Immediate move to Frida-like pre-resume activation

Pros:

- closest to end-state

Cons:

- too much risk while current gate semantics still have timing gaps
- likely to mix lifecycle bugs with API evolution

Rejected for now.

## Validation Plan

### Host / Local

Add or extend tests for:

- authoritative child/session binding rules
- zygote/usap rejection from host-facing spawn ownership
- state-specific timeout/error propagation
- `spawn -> load -> resume` ordering
- idempotent / one-shot resume release

### Device

Validate:

- repeated cold `spawn --resume --wait`
- `repl spawn -l ...`
- early Java scripts using `Java.ready(...)`
- no more accidental `zygote64` host binding
- no generic timeout where a stage-specific failure should surface

## Out of Scope

Not part of this pass:

- new public Java API
- full Frida-equivalent pre-resume activation semantics
- replacing the current Ninjector spawn entry path
- redesigning `Java.ready(...)` into a broader VM lifecycle API

## Expected Outcome

After this pass, Nook should have:

- a deterministic spawn transaction lifecycle
- stable child pid ownership
- clearer CLI / REPL behavior around suspended spawn sessions
- more predictable `Java.ready(...)` behavior under cold spawn

And, most importantly, it will be safe to start the next phase:

- evolving from "stable gate-held spawn" toward a more Frida-like pre-resume activation model
