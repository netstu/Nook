# Nook Gadget V2 Design

## Goal

Define the next-stage evolution of `nook-gadget` after the current v1 packaging and startup path:

1. make the runtime behave more like Frida Gadget at the control/configuration layer
2. then improve APK patch compatibility toward a stronger loader/proxy model inspired by LSPatch

The priority order is explicit:

- **v2.1 first:** Gadget-style runtime semantics
- **v2.2 second:** stronger repackaging compatibility

## Current Baseline

The current validated `nook-gadget` v1 path already provides:

- APK patching with `libnook-gadget.so`
- packaged `assets/nook-gadget/config.json`
- optional packaged `assets/nook-gadget/startup.js`
- `auto-start` and `manual` startup modes
- host attach after normal app launch
- authoritative device validation on `TargetDemo`
- additional compatibility evidence on launcher-fallback samples

The current patch strategy is intentionally minimal:

- inject `System.loadLibrary("nook-gadget")`
- prefer `Application.onCreate()`
- fall back to launcher `Activity.onCreate(Bundle)`

That means the current implementation is **closer to objection-style bootstrap patching** than to a full LSPatch loader/proxy architecture.

## Problem Statement

Two important gaps remain:

### Gap A: Gadget semantics are still too thin

The runtime works, but its configuration and connection behavior are not yet a close Frida Gadget analogue.

Examples:

- transport behavior is still mostly a single default path
- the runtime does not yet expose a clean `listen` vs `connect` model
- packaged startup behavior exists, but config shape is still deliberately narrow
- host-side gadget interaction semantics are not yet a first-class productized surface

### Gap B: patch compatibility still depends on minimal bootstrap injection

The current patcher is effective, but it still inherits the known limits of direct startup-method injection:

- unusual startup chains are harder to support
- lifecycle ownership is weak compared with a proxy loader model
- compatibility improvements will eventually require something stronger than "edit an existing `onCreate`"

## External Alignment

### Frida Gadget alignment target for v2.1

The v2.1 target is not "copy Frida exactly". The target is:

- app-bundled runtime
- runtime-owned configuration
- clearer startup behavior
- explicit passive vs active connection model
- host attach that feels natural for a gadgetized target

### LSPatch alignment target for v2.2

The v2.2 target is not "port LSPatch wholesale". The target is:

- stronger startup ownership
- less fragile bootstrap strategy
- clearer separation of original app lifecycle and Nook bootstrap
- better support for more APK shapes without entangling v2.1 runtime work

## Approaches Considered

### Approach A: Do compatibility first

Implement the loader/proxy path before touching the gadget runtime model.

Pros:

- directly attacks the biggest long-term compatibility limit
- could reduce future bootstrap rework

Cons:

- makes the system more complex before the gadget runtime contract is stable
- increases moving parts across patch tool, runtime, and host at once
- likely causes rework if `listen/connect/config` semantics change later

### Approach B: Do Gadget semantics first, compatibility second

First stabilize the runtime/control/config behavior, then improve patch compatibility with a stronger bootstrap architecture.

Pros:

- isolates the runtime product model from patcher complexity
- makes validation more precise
- gives v2.2 a stable gadget contract to preserve

Cons:

- compatibility limits remain during the first half of v2
- some runtime choices must still anticipate a future loader model

### Approach C: Do both in parallel

Advance config/transport semantics and loader/proxy compatibility together.

Pros:

- shortest path to a theoretically complete v2

Cons:

- highest execution risk
- too many simultaneous seams
- harder to debug regressions and validate progress

## Recommendation

Use **Approach B**.

That means:

- **v2.1** defines the gadget contract
- **v2.2** upgrades the packaging/bootstrap model while preserving that contract

This keeps the system disciplined:

- runtime semantics first
- compatibility architecture second

## V2 Structure

### V2.1: Frida-Gadget-style runtime semantics

Primary objective:

- make `nook-gadget` behave more like a real gadget at runtime, not just like an embedded payload that happens to auto-load

#### In scope

- richer `config.json` model
- explicit `listen` and `connect` interaction modes
- connection endpoint settings
- clearer startup/load policy semantics
- host CLI / SDK behavior that treats gadget targets as first-class
- better runtime failure classification and logs
- stronger validation for control-plane startup modes

#### Out of scope

- loader/proxy `Application` takeover
- broad anti-tamper bypass
- generalized APK compatibility rewrite
- non-Android support

### V2.2: Stronger patch compatibility model

Primary objective:

- reduce dependence on direct `onCreate` injection by moving toward a proxy/loader startup architecture

#### In scope

- loader/proxy bootstrap design
- original `Application` lifecycle restoration
- more resilient startup ownership
- compatibility work for more APK shapes
- stronger classification of patch-time failure reasons

#### Out of scope

- inventing a separate gadget protocol
- replacing the v2.1 config/transport model

## V2.1 Design

### Runtime model

The runtime should move from a narrow default transport concept to an explicit interaction model.

Recommended configuration shape:

```json
{
  "gadget_version": "0.2",
  "startup_mode": "auto-start",
  "debug_logging": false,
  "interaction": {
    "type": "listen",
    "transport": "tcp",
    "address": "127.0.0.1",
    "port": 27042
  },
  "startup_script": {
    "mode": "asset",
    "path": "assets/nook-gadget/startup.js",
    "required": false,
    "on_load": "auto"
  }
}
```

The exact field names may change during implementation, but the semantic model should be:

- **interaction.type**
  - `listen`
  - `connect`
- **interaction.transport**
  - first version may keep this narrow
  - but config should no longer hardcode the old implicit default behavior
- **startup_script.on_load**
  - `auto`
  - `manual`

### Listen mode

In `listen` mode:

- the patched app starts normally
- the gadget opens a control endpoint and waits
- the host connects later

This is the closest continuation of the current validated path, but it should become configuration-driven rather than merely inherited behavior.

### Connect mode

In `connect` mode:

- the gadget starts in-process
- it actively connects back to a configured host endpoint
- host tooling should accept and manage that session without pretending it was a normal local attach

This is the most important new v2.1 behavior.

### Host behavior

The host should preserve one conceptual model:

- sessions
- scripts
- RPC
- messages

But it should stop exposing gadget-specific awkwardness where avoidable.

Desired user-facing direction:

- `nook-cli` can interact with gadgetized targets without requiring the user to reason about server assumptions
- gadget sessions are visible and stable enough for `attach`, `call`, `post`, and observation flows
- connection-mode differences stay below the public API where possible

### Failure handling

V2.1 should classify failures more precisely:

- config parse failure
- unsupported interaction type
- endpoint bind failure
- outbound connect failure
- startup-script failure with runtime still alive
- startup-script failure with `required=true`

This matters because `listen/connect` failures otherwise look like generic "attach failed" noise.

### Validation strategy

V2.1 validation should add:

- `listen` mode real-device validation
- `connect` mode real-device validation
- config-surface tests
- host CLI behavior tests
- failure-mode tests for invalid config and connection failure

## V2.2 Design

### Motivation

The current minimal bootstrap path is fast and useful, but it remains structurally weaker than a loader/proxy path.

V2.2 should improve patch compatibility without breaking the v2.1 gadget contract.

### Loader/proxy direction

The preferred evolution is:

- patch metadata remains app-bundled
- gadget runtime remains the same conceptual runtime from v2.1
- bootstrap becomes more structured
- original app lifecycle is restored after Nook initialization rather than merely sharing an edited existing `onCreate`

### Suggested startup ownership order

1. proxy/custom `Application` path
2. original `Application` restoration
3. optional earlier bootstrap points only if justified by real samples

The first goal is not "earliest possible execution at all costs". The first goal is "more reliable ownership than direct smali injection."

### Compatibility targets

V2.2 should explicitly target:

- non-synthesized custom `Application` samples
- more complex manifest/app startup shapes
- additional multi-sample APK validation breadth

### Failure model

Patch-time failure reporting should become more specific:

- no viable startup hook point
- proxy loader insertion failed
- original `Application` recovery metadata incomplete
- rebuild/signing mismatch

## Cross-Version Constraints

The following constraints should hold across v2.1 and v2.2:

1. do not fork the script/session protocol
2. do not create a second host control model just for gadget
3. keep patch-time logic and runtime logic separated
4. keep the v2.1 runtime contract stable while evolving v2.2 bootstrap
5. keep validation authoritative through device-backed evidence, not just unit coverage

## Success Criteria

### V2.1 success

- gadget supports explicit `listen` mode via config
- gadget supports explicit `connect` mode via config
- host tools can interact with both modes without ad hoc manual workarounds
- startup-script policy is configuration-driven and stable
- real-device validation proves both startup/control paths

### V2.2 success

- patch flow no longer relies only on direct `onCreate` injection
- at least one real non-synthesized custom `Application` sample passes
- compatibility evidence improves beyond the current launcher-fallback-heavy matrix
- v2.1 gadget behavior remains intact after bootstrap architecture changes

## Recommended Next Step

Write an implementation plan that splits work into:

1. v2.1 config/runtime/host tasks
2. v2.1 validation tasks
3. v2.2 loader/proxy tasks
4. v2.2 compatibility validation tasks
