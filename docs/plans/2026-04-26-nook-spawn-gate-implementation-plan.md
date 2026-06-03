# Nook Spawn Gate Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace Nook's current coarse spawn suspension model with a real target-side spawn gate while preserving the existing phase-1 host protocol shape.

**Architecture:** Keep `SpawnRequest -> SpawnResponse -> AgentReady -> ResumeRequest` as the outer host contract in phase 1, but change the underlying semantics from "server-side suspended process" to "server-owned gated child". The injector returns a gated child pid, the agent sends `AgentReady` and waits, and `resume` releases that wait instead of sending a coarse process resume.

**Tech Stack:** C++17, Android runtime bridge, Nook communication protocol/session registry, Python CLI tests

---

### Task 1: Add injector-side gate result abstraction

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/injector.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp`
- Test: `E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp`

**Step 1: Write the failing injector tests**

Extend the spawn injector tests to express the new model:

- successful spawn returns a real child pid that already came from the early gate callback
- the injector transaction clears temporary zygote state before returning
- callback timeout remains a hard failure

Add at least one test asserting the implementation no longer depends on the old callback-file-only wording in its public error/reporting behavior.

**Step 2: Run the failing injector test**

Run:

```powershell
build\test_ninjector_spawn_injector.exe
```

Expected: FAIL in the newly added assertion(s).

**Step 3: Add a minimal gate-oriented return path**

Keep the public `Injector::Spawn(...)` signature for phase 1, but internally make `NinjectorSpawnInjector` treat the callback result as the authoritative gated child pid result.

Implementation notes:

- do not redesign the full server interface yet
- keep the success path returning one `pid`
- ensure the code comments explicitly say this pid is now "gate-held", not "server-side suspended"

**Step 4: Re-run the injector test**

Run:

```powershell
build\test_ninjector_spawn_injector.exe
```

Expected: PASS.

**Step 5: Commit**

```bash
git add server/injector.h server/ninjector_spawn_injector.h server/ninjector_spawn_injector.cpp tests/communication/test_ninjector_spawn_injector.cpp
git commit -m "refactor: treat spawn injector result as gated child"
```

### Task 2: Replace server-side suspended semantics with gate-held semantics

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp`
- Test: `E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp`

**Step 1: Write the failing registry/server tests**

Add or update tests to express:

- `spawn` success marks the pid as gate-held
- `resume` clears the gate-held entry only after a successful release operation
- naming may stay `spawn suspended` in phase 1, but the test comments should describe actual gate semantics

Also add one server test asserting that `resume` now delegates to a gate-release callback instead of assuming a coarse process resume.

**Step 2: Run the failing server handler test**

Run:

```powershell
build\test_server_handlers.exe
```

Expected: FAIL in the new gate-specific assertion(s).

**Step 3: Implement minimal session-registry state upgrade**

In `session_registry.*`:

- extend the tracked spawn entry to distinguish "gate held" from "released"
- keep the existing API names if that avoids broader churn in phase 1

In `server_handlers.*`:

- change spawn success handling to record a gate-held child
- change resume handling to invoke a configurable release callback
- clear the entry only after release succeeds

**Step 4: Re-run the server handler test**

Run:

```powershell
build\test_server_handlers.exe
```

Expected: PASS.

**Step 5: Commit**

```bash
git add server/session_registry.h server/session_registry.cpp server/server_handlers.h server/server_handlers.cpp tests/communication/test_server_handlers.cpp
git commit -m "refactor: model spawn state as gate-held child"
```

### Task 3: Make Android `NookCommWaitForResumeIfSpawned()` real

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp`
- Test: `E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_public_headers.cpp`
- Possibly modify: related runtime glue that tracks gate state for the current target process

**Step 1: Write the failing runtime/header test**

Add a narrow test or compile-level assertion documenting that Android should no longer trivially return `NOOK_STATUS_OK` without consulting gate state.

If a runtime unit test is not practical here, add a header-level guard plus a targeted code comment assertion and validate behavior via follow-up device smoke.

**Step 2: Run the failing local test**

Run:

```powershell
build\test_public_headers.exe
```

Expected: FAIL or incomplete coverage signal from the new assertion/commented expectation.

**Step 3: Implement minimal gate wait**

In `NookComm.cpp`:

- keep `SendAgentReady(...)`
- after control-channel readiness, block in a real wait path when the process was started under spawn gate control
- return only after gate release

Important:

- `AgentReady` must be sent before waiting
- do not deadlock the control channel

**Step 4: Re-run the local test**

Run:

```powershell
build\test_public_headers.exe
```

Expected: PASS.

**Step 5: Commit**

```bash
git add src/framework/NookComm.cpp tests/headers/test_public_headers.cpp
git commit -m "feat: implement target-side spawn gate wait"
```

### Task 4: Keep host spawn client behavior stable while updating semantics

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.cpp`
- Test: `E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_spawn_client.cpp`

**Step 1: Write the failing host test**

Add or adjust a test to document phase-1 expected ordering:

- `SpawnResponse` still arrives first
- `AgentReady` still gates further host actions
- `ResumeRequest` is still the host-side trigger, but should now be treated as "release gate"

The test should focus on semantics, not UI strings.

**Step 2: Run the failing host spawn client test**

Run:

```powershell
build\test_host_spawn_client.exe
```

Expected: FAIL in the new semantic assertion(s).

**Step 3: Implement minimal host adjustments**

Update comments, variable naming, and any state assumptions in `host_spawn_client.cpp` so the client behaves correctly with gate-held children without requiring protocol changes.

Do not add a new message type in this task.

**Step 4: Re-run the host test**

Run:

```powershell
build\test_host_spawn_client.exe
```

Expected: PASS.

**Step 5: Commit**

```bash
git add src/communication/host/host_spawn_client.cpp tests/communication/test_host_spawn_client.cpp
git commit -m "refactor: preserve host spawn flow over gate semantics"
```

### Task 5: Adjust CLI / REPL ordering tests for the real gate

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py`
- Optionally modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/README.md`

**Step 1: Write the failing CLI tests**

Add or update tests to express the desired phase-1 workflow:

- `spawn -l --resume` may now load before release if the runtime gate is real
- `repl spawn -l hook.js` followed by `%resume` should allow script setup before release

Keep the tests narrow:

- assert call ordering
- do not over-couple to exact printed prose unless necessary

**Step 2: Run the failing Python tests**

Run:

```powershell
python -m unittest host/nook-py/tests/test_cli.py
```

Expected: FAIL in the updated ordering assertions.

**Step 3: Implement the minimal CLI behavior change**

Update `cli.py` so the phase-1 host order matches the new gate behavior.

Important:

- only change ordering once the lower layers are in place
- do not reintroduce the old "load before resume while whole process is SIGSTOP'd" bug
- the new ordering is valid only because the gate is now target-side and cooperative

**Step 4: Re-run the Python tests**

Run:

```powershell
python -m unittest host/nook-py/tests/test_cli.py
```

Expected: PASS.

**Step 5: Commit**

```bash
git add host/nook-py/nook/cli.py host/nook-py/tests/test_cli.py host/nook-py/README.md
git commit -m "feat: align cli spawn ordering with target-side gate"
```

### Task 6: Update design-facing docs and review notes

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/code_review.md`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/README.md`
- Reference: `E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-26-nook-spawn-gate-design.md`

**Step 1: Document the semantic shift**

Add a short section that explains:

- old behavior: server-side coarse suspension
- new behavior: injector-delivered gated child + target-side wait

**Step 2: Document CLI expectations**

Describe the expected `spawn` and `repl spawn` ordering after the gate migration.

**Step 3: Verify docs match implementation**

Check that README examples do not still claim the old tested-device limitation once the new gate behavior is in place.

**Step 4: Commit**

```bash
git add docs/code_review.md host/nook-py/README.md docs/plans/2026-04-26-nook-spawn-gate-design.md
git commit -m "docs: describe spawn gate semantics"
```

### Task 7: Final verification

**Files:**
- Verify all touched paths above

**Step 1: Run native/unit test binaries**

Run:

```powershell
build\test_ninjector_spawn_injector.exe
build\test_server_handlers.exe
build\test_host_spawn_client.exe
build\test_public_headers.exe
```

Expected: all PASS.

**Step 2: Run Python CLI tests**

Run:

```powershell
python -m unittest host/nook-py/tests/test_cli.py
```

Expected: PASS.

**Step 3: Real-device smoke validation**

Run the target-side flow and confirm:

- spawn returns a real gated child pid
- `AgentReady` arrives before release
- script load succeeds before resume
- resume releases the gate

**Step 4: Commit**

```bash
git add .
git commit -m "feat: add target-side spawn gate flow"
```
