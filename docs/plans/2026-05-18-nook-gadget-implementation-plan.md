# Nook Gadget Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build the first working `nook-gadget` path so an Android APK can be patched, auto-load a Nook native runtime on startup, and be controlled through the existing Nook host script/session flow.

**Architecture:** Implement the first version as a minimal `patchapk + bootstrap + nook-gadget` flow. Reuse the current Nook agent runtime and communication protocol, but extract a dedicated gadget runtime target that can self-start without depending on `nook-server`. Keep patch-time logic, bootstrap injection, native gadget startup, and host integration in separate layers so the design can later evolve toward a loader-style model if needed.

**Tech Stack:** C++17, Android NDK, existing Nook communication/session/runtime code, Python host CLI/SDK, Java/Kotlin bootstrap assets, APK rewriting/signing toolchain, Android manifest parsing.

---

### Task 1: Add a design-anchored runtime inventory for gadget extraction

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-design.md`
- Create: `docs/plans/2026-05-18-nook-gadget-runtime-inventory.md`

**Step 1: Write the missing inventory document**

List the current code that `nook-gadget` is expected to reuse:

- `src/agent_runtime/*`
- `src/framework/NookComm.cpp`
- any current agent init/bootstrap code
- any server-only coupling points that must be removed or abstracted

The document should identify:

- required runtime pieces
- server-only pieces
- injector-only pieces
- likely seams for extraction

**Step 2: Review the inventory against the design**

Check that every required gadget responsibility from the design is mapped to current code or explicitly marked "new work required".

**Step 3: Update the design if the inventory exposes a missing boundary**

Only add minimal clarifications. Do not expand scope.

**Step 4: Save the inventory**

Expected result:

- one concise runtime inventory document exists
- the design and inventory agree on where `nook-gadget` will come from

**Step 5: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-design.md docs/plans/2026-05-18-nook-gadget-runtime-inventory.md
git commit -m "docs: add nook gadget runtime inventory"
```

### Task 2: Add failing build-level proof for a standalone gadget target

**Files:**
- Modify: `build/android/Android.mk`
- Modify: `build/android/Application.mk`
- Modify: any existing NDK build glue currently used for agent/server targets
- Create or modify: a tiny build verification source under `tests/headers/`

**Step 1: Write the failing build verification**

Add a header/build regression that assumes a dedicated `nook_gadget` module exists and exports a minimal public startup surface.

Examples of what to assert:

- the build defines a distinct gadget module
- gadget build does not directly require `nook-server`
- gadget startup symbol/header is available

**Step 2: Run the smallest build check to verify it fails**

Run the narrowest compile or source-string regression command that proves the gadget target does not exist yet.

Expected:

- missing build rule
- missing header
- missing symbol declaration

**Step 3: Add the minimal gadget target scaffolding**

Create a new native target concept for `nook-gadget`:

- separate name from `nook_agent`
- link only the pieces needed for gadget startup
- keep the first target as thin as possible

Do not yet solve full runtime startup. Only make the target exist cleanly.

**Step 4: Re-run the build verification**

Expected:

- the narrow gadget-target proof now passes

**Step 5: Commit**

```bash
git add build/android/Android.mk build/android/Application.mk tests/headers
git commit -m "build: add nook gadget target scaffold"
```

### Task 3: Add failing runtime tests for gadget self-initialization guards

**Files:**
- Create: `src/gadget/nook_gadget_runtime.h`
- Create: `src/gadget/nook_gadget_runtime.cpp`
- Create or modify: `tests/headers/test_nook_gadget_runtime_init.cpp`

**Step 1: Write the failing runtime test**

Add tests for a small gadget runtime API, for example:

- `Initialize(...)` succeeds once
- second `Initialize(...)` is idempotent
- `IsInitialized()` reflects runtime state
- `ShutdownForTesting()` or equivalent test-only reset path works in host tests

Keep the initial API deliberately tiny.

**Step 2: Run the test to verify it fails**

Build and run a narrow host-side test binary for the new runtime unit.

Expected:

- missing files or missing symbols

**Step 3: Implement the minimal runtime guard**

Add:

- one-time initialization guard
- thread-safe state tracking
- test-only reset hook if needed

Do not pull in transport or full `NookComm` yet.

**Step 4: Re-run the test to verify it passes**

Expected:

- runtime init test exits cleanly

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_runtime.h src/gadget/nook_gadget_runtime.cpp tests/headers/test_nook_gadget_runtime_init.cpp
git commit -m "feat: add gadget runtime init guard"
```

### Task 4: Add failing tests for gadget-owned script/runtime bridge startup

**Files:**
- Modify: `src/gadget/nook_gadget_runtime.cpp`
- Modify: `src/agent_runtime/nook_script_runtime_bridge.cpp`
- Create or modify: `tests/headers/test_nook_gadget_runtime_bridge.cpp`

**Step 1: Write the failing bridge test**

Add a test that expects gadget initialization to:

- register the script runtime callbacks
- not depend on `nook-server`
- allow a minimal create/load script path in-process

If a fully executable end-to-end host test is too heavy here, add a focused unit seam proving the bridge registration path can be called from gadget runtime startup.

**Step 2: Run the test to verify it fails**

Expected:

- runtime startup does not yet wire the bridge

**Step 3: Implement the minimal bridge bring-up**

Make `nook-gadget` startup responsible for:

- initializing the script runtime bridge
- registering script callbacks
- avoiding duplicate bridge registration on repeated startup calls

Keep any server-specific or injector-specific code out of the gadget path.

**Step 4: Re-run the test to verify it passes**

Expected:

- the runtime bridge test passes

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_runtime.cpp src/agent_runtime/nook_script_runtime_bridge.cpp tests/headers/test_nook_gadget_runtime_bridge.cpp
git commit -m "feat: wire script bridge into gadget runtime"
```

### Task 5: Add failing tests for gadget-owned control channel startup

**Files:**
- Modify: `src/gadget/nook_gadget_runtime.h`
- Modify: `src/gadget/nook_gadget_runtime.cpp`
- Modify: `src/framework/NookComm.cpp`
- Create or modify: `tests/headers/test_nook_gadget_control_channel.cpp`

**Step 1: Write the failing control-channel test**

Add a narrow seam around gadget startup that expects:

- gadget runtime can initialize a control path
- startup does not assume `nook-server`
- failure from control-channel bring-up is surfaced as a runtime error

Prefer dependency injection for the transport/connection factory if necessary so the test can stay host-side.

**Step 2: Run the test to verify it fails**

Expected:

- gadget runtime has no dedicated control-channel startup path yet

**Step 3: Implement minimal gadget control-channel initialization**

Extract or wrap the current `NookComm` bring-up so gadget runtime can:

- initialize the control path
- report startup failure precisely
- avoid server-only setup

Do not redesign the full protocol. Reuse the current message/session model.

**Step 4: Re-run the test to verify it passes**

Expected:

- control-channel startup test passes

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_runtime.h src/gadget/nook_gadget_runtime.cpp src/framework/NookComm.cpp tests/headers/test_nook_gadget_control_channel.cpp
git commit -m "feat: add gadget control channel startup"
```

### Task 6: Add a real native entrypoint for `libnook-gadget.so`

**Files:**
- Create: `src/gadget/nook_gadget_entry.cpp`
- Modify: build files for the gadget target
- Create or modify: `tests/headers/test_nook_gadget_entry_surface.cpp`

**Step 1: Write the failing entry-surface test**

Add a regression that expects `nook-gadget` to expose one explicit startup entry strategy, for example:

- constructor-backed startup
- plus an explicit init symbol for controlled app-side testing

The test should only prove the surface exists and is internally routed through the gadget runtime guard.

**Step 2: Run the test to verify it fails**

Expected:

- entry source or exported startup path missing

**Step 3: Implement the minimal entrypoint**

Add:

- constructor or equivalent Android-compatible auto-start path
- explicit init function for test apps if useful
- one-time routing into `nook_gadget_runtime`

**Step 4: Re-run the test to verify it passes**

Expected:

- entry-surface regression passes

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_entry.cpp tests/headers/test_nook_gadget_entry_surface.cpp build/android/Android.mk
git commit -m "feat: add nook gadget native entrypoint"
```

### Task 7: Add a controlled app-side smoke example for gadget startup

**Files:**
- Create or modify: a focused example under `examples/` or `tests/examples/`
- Modify: any sample app-side runtime loader glue needed for the gadget target
- Create: `tools/nook_gadget_smoke.cpp` or equivalent if a host-side helper is more appropriate

**Step 1: Write the failing smoke scenario**

Define a controlled scenario where an app or sample loader does:

- `System.loadLibrary("nook-gadget")`
- waits for startup
- verifies the process does not crash
- verifies at least one script/runtime callback path is reachable

**Step 2: Run the smoke scenario to verify it fails**

Expected:

- gadget startup path is incomplete or not yet packaged

**Step 3: Implement the minimal sample integration**

Update example/runtime glue only as much as needed to:

- load the gadget
- surface startup logs
- prove gadget-owned startup is separate from injector/server paths

**Step 4: Re-run the smoke scenario to verify it passes**

Expected:

- gadget loads in a controlled app-side environment

**Step 5: Commit**

```bash
git add examples tests/examples tools
git commit -m "test: add controlled nook gadget smoke flow"
```

### Task 8: Add failing patch-tool shape tests and command scaffold

**Files:**
- Create: `tools/nook_patchapk.py` or `host/nook-py/nook/patchapk.py`
- Create: `tests/headers/test_nook_patchapk_surface.cpp` or a Python test under `host/nook-py/tests/`
- Modify: host CLI integration if the tool should be surfaced there

**Step 1: Write the failing patch-tool test**

Add a test asserting a first-class patch command exists with at least:

- source APK input
- output APK path
- target ABI selection or default
- signing control

If implemented in Python, prefer a Python CLI/unit test.

**Step 2: Run the test to verify it fails**

Expected:

- command/module missing

**Step 3: Add the minimal patch-tool scaffold**

Create the command/module with:

- argument parsing
- structured stage logging
- placeholders for unpack, inject, rebuild, sign

Do not implement the full patch flow yet.

**Step 4: Re-run the test to verify it passes**

Expected:

- patch command exists and parses expected flags

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py tests
git commit -m "feat: add nook patchapk command scaffold"
```

### Task 9: Add failing tests for patch-time gadget library placement

**Files:**
- Modify: patch tool module
- Create or modify: patch-tool tests under `host/nook-py/tests/` or a dedicated patch tool test area

**Step 1: Write the failing placement test**

Add tests expecting the patcher to:

- detect APK ABI directories
- place `libnook-gadget.so` in the correct location
- reject unsupported ABI shapes for v1

For v1, the expected supported path is:

- `arm64-v8a`

**Step 2: Run the tests to verify they fail**

Expected:

- placement logic missing

**Step 3: Implement minimal library placement**

Add patch logic to:

- inspect package native lib layout
- copy `libnook-gadget.so`
- error clearly on unsupported ABI structures

**Step 4: Re-run the tests to verify they pass**

Expected:

- ABI placement tests pass

**Step 5: Commit**

```bash
git add host/nook-py/tests tools/nook_patchapk.py
git commit -m "feat: place nook gadget library in patched apk"
```

### Task 10: Add failing tests for patch-time metadata/config emission

**Files:**
- Modify: patch tool module
- Create: a small config schema file or config writer helper
- Create or modify: patch-tool tests

**Step 1: Write the failing config test**

Add tests expecting patch output to include minimal metadata/config such as:

- gadget version
- startup mode
- debug logging flag

The test should verify shape and location, not future rich semantics.

**Step 2: Run the tests to verify they fail**

Expected:

- metadata/config file not emitted

**Step 3: Implement minimal metadata emission**

Write a small config artifact into the patched APK assets or another stable package location.

Keep the schema small and extensible.

**Step 4: Re-run the tests to verify they pass**

Expected:

- config emission tests pass

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests
git commit -m "feat: emit nook gadget patch metadata"
```

### Task 11: Add failing tests for bootstrap injection

**Files:**
- Modify: patch tool module
- Create: bootstrap injection helper module
- Create or modify: patch-tool tests with fixture APK/smali samples

**Step 1: Write the failing bootstrap test**

Add tests expecting patcher output to inject a startup bootstrap that results in:

- `System.loadLibrary("nook-gadget")`
- no instrumentation logic embedded in Java
- one clear startup hook site for v1

The test can work on decoded fixture files first if full APK fixtures are too heavy.

**Step 2: Run the tests to verify they fail**

Expected:

- bootstrap injection helper missing

**Step 3: Implement minimal bootstrap injection**

Add logic that:

- locates a practical startup class/path
- injects or generates minimal bootstrap code
- keeps the injected code as thin as possible

Do not yet attempt a full loader/proxy application model.

**Step 4: Re-run the tests to verify they pass**

Expected:

- bootstrap injection tests pass

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests
git commit -m "feat: inject nook gadget bootstrap"
```

### Task 12: Add failing tests for manifest rewrite and rebuild/sign pipeline

**Files:**
- Modify: patch tool module
- Modify: any signing/build helper scripts
- Create or modify: patch-tool integration tests

**Step 1: Write the failing integration test**

Add a patch integration test that expects the tool to:

- open an APK
- rewrite required manifest pieces
- rebuild the output package
- produce a signed APK or explicitly skip signing with a controlled flag

The first test may use a fixture APK or a small synthetic package.

**Step 2: Run the tests to verify they fail**

Expected:

- rebuild/sign path incomplete

**Step 3: Implement minimal rebuild/sign flow**

Add the smallest reliable pipeline for:

- unpack
- modify
- rebuild
- sign

Prefer a structure that can be swapped later, but do not overabstract.

**Step 4: Re-run the tests to verify they pass**

Expected:

- patch integration test passes

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests tools
git commit -m "feat: rebuild and sign patched nook apk"
```

### Task 13: Add failing host attach tests for gadget sessions

**Files:**
- Modify: `host/nook-py/nook/` session/device logic
- Modify: any host target discovery layer needed
- Create or modify: `host/nook-py/tests/test_client.py`
- Create or modify: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing host tests**

Add tests asserting host behavior can treat a gadgetized target as a normal attachable runtime:

- attach path does not assume `nook-server`
- script create/load/post/unload uses the same public surface
- transport differences remain hidden below the user-facing API

**Step 2: Run the tests to verify they fail**

Expected:

- host code still assumes server-centric runtime shape in at least one place

**Step 3: Implement the minimal host alignment**

Update host code so gadget sessions:

- fit existing session abstractions
- reuse script lifecycle APIs
- report precise errors when a gadget endpoint is present but not ready

**Step 4: Re-run the tests to verify they pass**

Expected:

- host attach tests pass

**Step 5: Commit**

```bash
git add host/nook-py/nook host/nook-py/tests
git commit -m "feat: support gadget attach sessions in host runtime"
```

### Task 14: Add a patched-APK end-to-end validation recipe and smoke harness

**Files:**
- Create: `tools/nook_gadget_patch_smoke.ps1`
- Create or modify: `tools/` helper scripts for install/launch/attach
- Modify: `host/nook-py/README.md`
- Modify: `README.md`

**Step 1: Write the failing validation checklist**

Document the exact expected v1 workflow:

1. build `libnook-gadget.so`
2. patch APK
3. install patched APK
4. launch app
5. attach with `nook-cli`
6. load script
7. observe message or RPC result

If a smoke harness is added, make the script fail early until all commands are wired.

**Step 2: Run the harness/checklist to verify gaps**

Expected:

- at least one missing command or path still blocks the flow

**Step 3: Implement the minimal smoke harness**

Add a helper script that:

- validates artifacts exist
- runs patch/install/launch/attach steps
- prints exact user actions and expected outputs

Keep it explicit rather than magical.

**Step 4: Re-run the smoke harness**

Expected:

- the end-to-end recipe is executable at least for the controlled v1 target

**Step 5: Commit**

```bash
git add tools/nook_gadget_patch_smoke.ps1 README.md host/nook-py/README.md
git commit -m "docs: add nook gadget patch smoke workflow"
```

### Task 15: Run full staged verification and capture residual gaps

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-design.md`
- Create: `docs/plans/2026-05-18-nook-gadget-validation-status.md`

**Step 1: Run the narrow test suites from previous tasks**

Run the exact host/unit/integration commands added along the way.

Expected:

- all committed stage-local tests pass

**Step 2: Run the controlled gadget smoke path**

Run the app-side and patched-APK smoke validations.

Expected:

- the supported v1 target works end-to-end

**Step 3: Capture residual limitations**

Document:

- unsupported APK shapes
- known bootstrap limitations
- any host/runtime rough edges
- whether the next step should be compatibility hardening or loader evolution

**Step 4: Save the validation status**

Expected:

- a short status document exists with real commands and outcomes

**Step 5: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-design.md docs/plans/2026-05-18-nook-gadget-validation-status.md
git commit -m "docs: record nook gadget validation status"
```

## Execution Order

Follow the tasks in order. The dependency chain is intentional:

1. inventory and build target
2. runtime startup
3. bridge and control channel
4. native entrypoint
5. controlled smoke
6. patch tool scaffold
7. library placement and metadata
8. bootstrap injection
9. rebuild/sign pipeline
10. host attach alignment
11. end-to-end smoke and validation

## Success Criteria

This implementation plan is complete when:

- `libnook-gadget.so` exists as a distinct artifact
- a controlled app can auto-load the gadget and bring up runtime state
- a patch tool can inject the gadget into a standard arm64 APK
- the patched APK can be installed and launched
- the existing host tooling can attach and drive script lifecycle
- the supported scope and compatibility limits are documented clearly

## Notes

- Keep v1 Android-only and `arm64-v8a`-first.
- Reuse existing host/runtime protocol semantics whenever possible.
- Resist importing a full proxy-loader design into the first delivery.
- If bootstrap injection proves too brittle for target fixtures, capture that as a compatibility limit first, not as a reason to derail the whole runtime path.
