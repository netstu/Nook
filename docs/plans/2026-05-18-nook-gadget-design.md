# Nook Gadget Design

## Goal

Add a `nook-gadget` capability to Nook so a target Android APK can be repackaged with a Nook runtime payload that starts automatically inside the app process and exposes the existing Nook script/control surface without requiring a separate injector at launch time.

The first version should optimize for fast end-to-end validation, not for maximum compatibility with every APK shape on day one.

## Problem Statement

Nook already has most of the runtime pieces needed for an in-process instrumentation flow:

- agent runtime and script registry
- host-side `spawn / attach / resume / post / unload`
- IPC and control-plane logic in `NookComm`
- embedded agent packaging for server-side deployment

What Nook does not yet have is an APK-native deployment path equivalent to a Gadget workflow:

- put a Nook runtime payload inside the target APK
- ensure it is loaded automatically on app startup
- expose a stable control endpoint to the existing host tools

Today, the repository is strongest at:

- injector-driven loading
- server-driven spawn/attach flows
- app-side manual `System.loadLibrary(...)` testing in apps we control

That leaves a gap for:

- non-root repackaged app testing
- "drop into APK and connect later" workflows
- a deployment mode that looks closer to Frida Gadget than to `nook-server`

## External References Reviewed

### Frida Gadget

Relevant ideas:

- the payload is a shared library loaded inside the target process
- the payload self-starts at library load time
- the payload exposes a control interface and waits for or establishes a connection
- configuration is runtime-driven, not hardcoded per target

Relevant boundary:

- Frida Gadget itself is not an APK patcher
- the runtime payload and the APK patch/deployment tooling are separate concerns

### objection

Relevant ideas:

- practical `patchapk` workflow for Android
- modifies the app package to include the native payload
- injects a `System.loadLibrary(...)` bootstrap into app startup code
- handles rebuild/signing as part of the patch flow

Relevant boundary:

- bootstrap approach is pragmatic and fast to validate
- compatibility is good enough for a first version, but it is not the strongest possible lifecycle hook model

### LSPatch

Relevant ideas:

- treats APK patching as a structured packaging problem, not just a smali string edit
- uses a loader/proxy startup model that can survive more app shapes
- separates patch-time metadata, runtime loader behavior, and app bootstrap

Relevant boundary:

- the loader model is more complex than what Nook needs for a first working Gadget path
- copying this whole architecture immediately would slow initial delivery

### LSPlant

Relevant ideas:

- Java/ART hook backend patterns may be useful later if Nook Java Hook evolves

Not directly relevant to `nook-gadget`:

- APK patching
- startup bootstrap
- host connection lifecycle
- script runtime exposure

## Approaches Considered

### Approach A: Minimal bootstrap patcher

Patch the APK, place `libnook-gadget.so` in the native library directory, inject a thin Java/bootstrap hook that calls `System.loadLibrary("nook-gadget")`, and let the native payload self-start.

Pros:

- shortest path to a working Gadget
- reuses existing Nook runtime and host tooling
- easiest way to validate transport, startup timing, and script lifecycle

Cons:

- startup compatibility depends on bootstrap injection quality
- some apps may have unusual initialization paths
- harder to guarantee earliest possible initialization

### Approach B: Proxy loader model

Patch the APK with a dedicated loader/proxy `Application` or equivalent startup delegate that restores the original app lifecycle after Nook initialization.

Pros:

- stronger lifecycle control
- cleaner place for metadata, config, and compatibility logic
- better long-term base for module-style features

Cons:

- significantly more complex first delivery
- requires careful handling of original `Application`, factories, providers, and class loading
- delays proof that the basic Nook Gadget runtime works end-to-end

### Approach C: Layered design, minimal first delivery

Architect the system as if it may later evolve into a loader/proxy model, but implement the first working version using the minimal bootstrap patcher path.

Pros:

- fastest path to a usable result
- avoids premature complexity
- preserves a clean upgrade path to a more robust loader architecture later

Cons:

- first version still inherits bootstrap-style compatibility limits
- requires discipline to keep patch-time and runtime responsibilities separated

## Recommendation

Use **Approach C**.

That means:

- **first version behavior** should follow Approach A
- **internal layering** should preserve the ability to move toward Approach B later

This is the right tradeoff for Nook’s current state because the missing capability is deployment/bootstrap, not runtime instrumentation. Nook already has runtime pieces that can be reused. The first design should therefore minimize new runtime invention and focus on packaging, startup, and connection bring-up.

## First-Version Scope

### In scope

- Android-only `nook-gadget`
- APK patch command that injects Nook into a target APK
- packaged native payload named independently from the current injector-oriented artifacts
- startup bootstrap that loads the gadget automatically
- gadget runtime that self-initializes and opens the existing Nook control surface
- host compatibility with existing `nook-cli` / `nook-py`
- first version configuration for basic connect/listen behavior

### Out of scope

- iOS support
- full LSPatch-style proxy loader in v1
- broad anti-tamper / shell bypass work
- guaranteed compatibility with heavily protected apps
- replacing `nook-server` or existing injector paths
- re-architecting Java Hook around LSPlant-style internals

## Design Principles

1. Keep patch-time logic separate from runtime logic.
2. Reuse the current Nook script/control plane instead of inventing a second protocol.
3. Make the first version observable and debuggable before making it clever.
4. Treat APK bootstrap as replaceable infrastructure.
5. Do not entangle Gadget startup with `nook-server` assumptions.

## Target Architecture

The first version is split into four layers:

1. **Patch tool**
   - rewrites the APK
   - injects bootstrap assets
   - injects `libnook-gadget.so`
   - updates manifest / startup entrypoints
   - rebuilds and signs the APK

2. **Bootstrap layer**
   - smallest possible Java-side startup shim
   - responsible only for ensuring `System.loadLibrary("nook-gadget")`
   - should not contain instrumentation logic

3. **Nook Gadget runtime**
   - native library loaded into the target app process
   - self-initializes once
   - brings up `NookComm`, script runtime bridge, and control endpoint
   - exposes the same conceptual script/session model the host already understands

4. **Host integration**
   - existing `nook-cli` / `nook-py` attach to the gadget endpoint
   - keeps session, script, message, and RPC semantics aligned with current flows where possible

## Component Design

### 1. `nook-gadget` native runtime

This should be a distinct runtime artifact, even if it reuses large parts of the current agent code.

Responsibilities:

- native process-local singleton initialization
- safe repeated-entry guard
- early log initialization
- control transport startup
- script runtime registration
- optional startup mode handling such as wait-before-resume semantics later

Non-responsibilities:

- APK rewriting
- Java-side lifecycle takeover
- rebuild/signing

Preferred implementation direction:

- build `nook-gadget` from current `agent_runtime` + `NookComm` pieces
- extract any server-only assumptions behind small interfaces
- avoid direct dependency on `nook-server` process services

### 2. bootstrap injection

The first version should use a minimal bootstrap similar in spirit to `objection`:

- find a practical app startup location
- inject `System.loadLibrary("nook-gadget")`
- keep bootstrap logic as thin as possible

Preferred strategy order:

1. patch the main application startup path
2. fall back to a generated bootstrap component if the original shape is unsuitable

The bootstrap should not attempt to implement any host protocol. Its only job is to load the native runtime reliably.

### 3. patch metadata

The patcher should write explicit metadata into the APK so the runtime does not depend on hardcoded assumptions.

Expected metadata examples:

- gadget version
- startup mode
- transport mode
- default endpoint settings
- whether the build is debug-friendly

This metadata should be represented as a small config artifact that can later support richer loader behavior without redesigning the patch format.

### 4. patch tool

The patch tool should become a first-class Nook deliverable, conceptually similar to `patchapk`.

Responsibilities:

- unpack APK
- inspect manifest and native ABI layout
- copy `libnook-gadget.so`
- inject bootstrap
- write metadata/config
- rebuild APK
- sign output

The tool should not assume only one ABI forever, but the first version may explicitly target the current Nook strong path:

- `arm64-v8a`

### 5. host-side gadget attach mode

The host should treat a gadgetized app as a first-class attach target, not as a server special case.

Expected behavior:

- list/attach should work with minimal or no new user concepts
- script loading should reuse existing create/load/post/unload flow
- the gadget path should look like an already-live process-local runtime, not a spawned server

If transport details differ internally, that difference should be hidden below the public host API.

## Startup Flow

### First-version boot sequence

1. User patches APK with Nook patch tool.
2. Patched APK is installed and launched.
3. Bootstrap path executes `System.loadLibrary("nook-gadget")`.
4. `libnook-gadget.so` constructor or explicit init entry runs.
5. Gadget initializes runtime guards and logging.
6. Gadget initializes `NookComm` and script runtime bridge.
7. Gadget opens its control endpoint.
8. Host attaches and drives script lifecycle using the current Nook protocol surface.

## Transport Model

The first version should **reuse the current Nook protocol and message model** unless a specific blocker appears.

Recommended v1 behavior:

- reuse current frame/protocol semantics
- support one primary control channel
- prefer a transport choice that matches existing host code with the least change

The exact endpoint type can remain an implementation choice, but the guiding rule is:

- choose the transport that minimizes divergence from current `nook-cli` / `nook-py`

If necessary, introduce a small transport adapter in host and gadget rather than changing higher-level script/session semantics.

## Packaging Layout

The design should distinguish three artifact classes:

1. **server artifacts**
   - current `nook-server`
   - current embedded payloads for server deployment

2. **injector artifacts**
   - current injector-friendly runtime pieces

3. **gadget artifacts**
   - `libnook-gadget.so`
   - bootstrap helper assets
   - patch-time metadata/config assets

This separation matters because the current repository already contains runtime artifacts optimized for other deployment paths. `nook-gadget` should not remain a naming alias over server or injector outputs.

## Configuration Model

The first version should keep configuration deliberately small.

Recommended v1 config fields:

- startup mode: auto-start
- transport mode: default
- optional host endpoint override
- debug logging enabled/disabled

Do not implement Frida Gadget’s full `listen / connect / script / script-directory` surface in v1.

Instead:

- choose one default mode
- keep config schema extensible
- add richer modes only after end-to-end stability exists

## Compatibility Strategy

### First-version compatibility target

- standard Android APKs
- standard `Application` startup path
- `arm64-v8a`
- apps without strong packers or aggressive anti-tamper

### Explicit non-goals for v1 compatibility

- packed/virtualized apps
- apps with heavily rewritten startup graphs
- apps that aggressively validate app signatures or classes.dex layout beyond normal expectations

This should be documented clearly so failures are classified correctly as compatibility limits instead of runtime correctness bugs.

## Error Handling

The design should prefer explicit failure stages over silent partial startup.

Patch-time failures:

- unsupported APK structure
- unsupported ABI layout
- bootstrap injection failure
- signing/rebuild failure

Runtime failures:

- bootstrap reached but native library failed to load
- native library loaded but gadget init failed
- control channel init failed
- script runtime bridge failed to initialize

Host-visible behavior:

- errors should map to observable logs and stable failure messages
- attach timeouts should distinguish “process launched but gadget not reachable” from generic transport failure

## Security And Operational Considerations

`nook-gadget` is an instrumentation payload running inside the target app. That means:

- any control surface must be intentionally exposed
- default networking behavior should be conservative
- local-only or tightly scoped control endpoints are preferable for early versions

The first version should avoid widening exposure beyond what is needed to validate the workflow.

## Testing Strategy

### Patch-time tests

- manifest parsing and rewrite tests
- bootstrap insertion tests
- ABI packaging tests
- metadata emission tests

### Runtime tests

- gadget library loads exactly once
- gadget runtime initializes without `nook-server`
- script create/load/post/unload works in patched app
- host attach works against gadgetized target

### Integration tests

- patch APK -> install -> launch -> attach -> load script -> observe message -> unload
- patch APK -> launch -> attach -> Java Hook script
- patch APK -> launch -> attach -> native hook script

### Regression focus

- startup deadlocks
- double initialization
- process launch crash before `Application.onCreate`
- mismatch between gadget transport and host expectations

## File And Module Direction

Expected new or refactored areas:

- new patch tool area for APK rewriting
- new gadget runtime target
- extracted runtime pieces from current agent/server coupling
- bootstrap asset generation or bootstrap patch helpers
- host-side target discovery or attach adjustments for gadget sessions

Exact file layout should be decided in the implementation plan, but the design constraint is:

- patcher code, gadget runtime code, and host integration code should remain clearly separated

## Migration Strategy

### Stage 1

- create `nook-gadget` runtime artifact
- prove it can self-start in a controlled host app

### Stage 2

- create APK patch tool using minimal bootstrap injection
- validate one end-to-end patched APK workflow

### Stage 3

- align host attach flow with gadget sessions
- document supported and unsupported APK shapes

### Stage 4

- evaluate whether compatibility gaps justify a proxy loader evolution

## Why This Design

This design fits the current Nook codebase because it builds on what already exists:

- the script runtime is already present
- the control plane is already present
- the host tooling is already present

The missing capability is deployment and self-bootstrap. A minimal Gadget-first path solves that directly without prematurely importing the full complexity of a loader-heavy patch framework.

At the same time, keeping patching, bootstrap, runtime, and host integration as distinct layers prevents the first version from becoming a dead end.

## Success Criteria

The design is successful when Nook can support this user story:

1. Patch a standard arm64 Android APK with Nook.
2. Install and launch the patched APK.
3. Attach using existing Nook host tooling.
4. Load a script and receive messages/RPC results.
5. Run at least one Java hook and one native hook workflow without relying on `nook-server` startup inside the target app.

## Next Step

The next document should be an implementation plan for `nook-gadget` that breaks the work into:

- gadget runtime extraction/build target
- patch tool architecture
- bootstrap injection strategy
- host integration changes
- staged verification commands
