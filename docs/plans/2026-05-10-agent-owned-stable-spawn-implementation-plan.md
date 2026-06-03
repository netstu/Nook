# Agent-Owned Stable Spawn Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the default stable spawn backend agent-owned so `nook-server` no longer depends on legacy `ainject/ncore` logic for the main Android 11 device path.

**Architecture:** Keep the existing `AGENT_READY -> SCRIPT_CREATE -> SCRIPT_LOAD -> RESUME` child flow intact, but move zygote-side spawn interception ownership from `server/ncore_fallback.cpp` into the main agent runtime. The server should inject the zygote agent, arm one spawn transaction through explicit IPC, and only use legacy `ncore` through an explicit fallback path.

**Tech Stack:** C++17, Android NDK, existing Nook communication protocol/session runtime, current agent runtime in `src/framework`, current server spawn injector in `server/`

---

### Task 1: Add protocol messages for zygote-agent spawn control

**Files:**
- Modify: `src/communication/protocol/message_types.h`
- Modify: `src/communication/protocol/messages.h`
- Modify: `src/communication/protocol/messages.cpp`
- Test: `tests/communication/test_messages.cpp` or the closest existing protocol encode/decode test

**Step 1: Write the failing test**

Add encode/decode coverage for:

- `SPAWN_INSTALL`
- `SPAWN_INSTALL_OK`
- `SPAWN_INSTALL_ERROR`
- `SPAWN_UNINSTALL`
- `SPAWN_UNINSTALL_OK`

Test the fields that matter for stable matching:

- `target_package`
- `spawn_token`
- `mode`
- `error`

**Step 2: Run test to verify it fails**

Run the protocol test target.
Expected: message type values or payload codecs do not exist yet.

**Step 3: Write minimal implementation**

Add the new message types and codecs without changing unrelated protocol behavior.

**Step 4: Run test to verify it passes**

Run the same protocol test target again.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/communication/protocol/message_types.h src/communication/protocol/messages.h src/communication/protocol/messages.cpp tests/communication/test_messages.cpp
git commit -m "feat: add zygote spawn control protocol"
```

### Task 2: Create an agent-side zygote spawn controller module

**Files:**
- Create: `src/framework/NookZygoteSpawn.h`
- Create: `src/framework/NookZygoteSpawn.cpp`
- Test: `tests/headers/` or the closest focused unit-style test target available for pure helper/state code

**Step 1: Write the failing test**

Add focused tests for the pure state model:

- initial state is `Idle`
- `Install(target, token)` transitions `Idle -> Armed`
- second install while `Armed` fails
- `Consume(token)` transitions `Armed -> Consumed`
- `Uninstall()` restores `Idle`

Keep this first test file pure state logic only. Do not mix it with live hook installation yet.

**Step 2: Run test to verify it fails**

Run the focused test target.
Expected: module does not exist yet.

**Step 3: Write minimal implementation**

Implement:

- state enum
- armed target package
- armed spawn token
- install/uninstall/consume helpers
- explicit reset behavior

Do not add actual hook code in this task.

**Step 4: Run test to verify it passes**

Run the same test target again.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/framework/NookZygoteSpawn.h src/framework/NookZygoteSpawn.cpp tests/headers/*
git commit -m "feat: add zygote spawn state controller"
```

### Task 3: Port the current stable native spawn hook logic out of `ncore_fallback.cpp`

**Files:**
- Modify: `server/ncore_fallback.cpp`
- Modify: `src/framework/NookZygoteSpawn.cpp`
- Modify: `src/framework/NookZygoteSpawn.h`
- Test: add focused helper coverage where possible

**Step 1: Write the failing test**

Add helper-level tests for the package match and activation decision logic using the existing semantics from the current stable backend.

Do not attempt to unit-test real Android hook installation here; test only the decision helpers and state transitions.

**Step 2: Run test to verify it fails**

Expected: helper functions do not exist in the new module yet.

**Step 3: Write minimal implementation**

Port, with minimal semantic change, the logic that is currently responsible for:

- tracking target package / so path
- intercepting fork/vfork path
- detecting child package match through the existing specialize-related points
- deciding whether the inherited child should activate
- uninstalling hook state after consumption

During this task:

- the new module may temporarily share small helper logic with `ncore_fallback.cpp`
- do not delete `ncore_fallback.cpp`
- keep behavior as close as possible to the already working Android 11 path

**Step 4: Run test to verify it passes**

Run the focused helper tests again.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/framework/NookZygoteSpawn.cpp src/framework/NookZygoteSpawn.h server/ncore_fallback.cpp tests/headers/*
git commit -m "feat: move stable spawn logic into agent module"
```

### Task 4: Wire `NookComm` to accept spawn-control messages in zygote mode

**Files:**
- Modify: `src/framework/NookComm.cpp`
- Modify: `src/framework/NookZygoteSpawn.cpp`
- Modify: `src/framework/NookZygoteSpawn.h`
- Test: the closest existing agent/runtime protocol handling tests

**Step 1: Write the failing test**

Add a test that feeds a `SPAWN_INSTALL` message into the agent-side handler and verifies:

- zygote spawn state becomes `Armed`
- install success response is generated

Add another test for `SPAWN_UNINSTALL` restoring `Idle`.

**Step 2: Run test to verify it fails**

Expected: `NookComm` does not dispatch these control messages yet.

**Step 3: Write minimal implementation**

Teach the agent runtime to:

- recognize zygote spawn control messages
- dispatch them into `NookZygoteSpawn`
- return explicit success/error responses

Do not touch attach/runtime bridge behavior outside the necessary dispatch path.

**Step 4: Run test to verify it passes**

Run the same agent/runtime protocol tests again.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/framework/NookComm.cpp src/framework/NookZygoteSpawn.cpp src/framework/NookZygoteSpawn.h tests/*
git commit -m "feat: handle zygote spawn control in agent runtime"
```

### Task 5: Change the server default spawn path to use zygote-agent control

**Files:**
- Modify: `server/ninjector_spawn_injector.cpp`
- Modify: `server/ninjector_spawn_injector.h`
- Modify: `server/server_handlers.cpp`
- Modify: `server/server_main.cpp`
- Test: `tests/communication/test_ninjector_spawn_injector.cpp`

**Step 1: Write the failing test**

Add a regression test that asserts:

- when the agent-owned path is available, default spawn does not call `PrepareSpawnInZygote()`
- the server instead injects the zygote agent and sends `SPAWN_INSTALL`
- `AGENT_READY` is matched by `spawn_token`

**Step 2: Run test to verify it fails**

Run the spawn injector test target.
Expected: current default path still enters legacy `PrepareSpawnInZygote()`.

**Step 3: Write minimal implementation**

Change the default flow in `NinjectorSpawnInjector::Spawn()` to:

1. ensure zygote agent session exists
2. send `SPAWN_INSTALL`
3. start target app
4. wait for `AGENT_READY` with matching token
5. send `SPAWN_UNINSTALL`

Keep the legacy `ncore` path behind explicit fallback only.

**Step 4: Run test to verify it passes**

Run the same spawn injector test target again.
Expected: PASS.

**Step 5: Commit**

```bash
git add server/ninjector_spawn_injector.cpp server/ninjector_spawn_injector.h server/server_handlers.cpp server/server_main.cpp tests/communication/test_ninjector_spawn_injector.cpp
git commit -m "feat: default spawn to zygote agent control"
```

### Task 6: Tighten legacy `ncore` into explicit fallback only

**Files:**
- Modify: `server/ninjector_spawn_injector.cpp`
- Modify: `server/ninjector_compat.cpp`
- Modify: `server/ncore_fallback.cpp`
- Test: `tests/communication/test_ninjector_spawn_injector.cpp`

**Step 1: Write the failing test**

Add coverage asserting that legacy `ncore` runs only when:

- an explicit environment switch enables it, or
- the agent-owned path is unavailable

Also add a regression assertion that the default path never silently drops into `ainject`.

**Step 2: Run test to verify it fails**

Expected: current fallback conditions are still too permissive for the new design.

**Step 3: Write minimal implementation**

Gate all legacy `ncore` usage behind explicit policy.
Keep the old backend functional for debugging, but remove it as a silent default.

**Step 4: Run test to verify it passes**

Run the same spawn injector test target.
Expected: PASS.

**Step 5: Commit**

```bash
git add server/ninjector_spawn_injector.cpp server/ninjector_compat.cpp server/ncore_fallback.cpp tests/communication/test_ninjector_spawn_injector.cpp
git commit -m "feat: demote legacy ncore to explicit fallback"
```

### Task 7: Device smoke-test the first agent-owned stable path on Android 11

**Files:**
- Use: built `nook-server`
- Use: existing `frida-0x1` test scripts

**Step 1: Push clean runtime**

Use a clean device runtime directory containing only `nook-server`.

**Step 2: Start server**

Start `nook-server` under `linker64` as currently used for device testing.

**Step 3: Run stable spawn smoke**

Use the existing `com.ad2001.frida0x1` spawn smoke script that is already known to verify hook success.

**Step 4: Verify logs**

Confirm:

- spawn succeeds
- `AGENT_READY` arrives with the expected token
- zygote-agent install/uninstall messages are present
- no default `ainject` path is used
- hook output is correct

**Step 5: Commit**

```bash
git add .
git commit -m "test: validate agent-owned stable spawn on device"
```

### Task 8: Update design/status docs to reflect the new backend split

**Files:**
- Modify: `docs/step11.md`
- Modify: `docs/plans/2026-05-10-attach-explicit-init-and-single-server-status.md`
- Modify: `docs/plans/2026-05-10-agent-owned-stable-spawn-design.md`

**Step 1: Write the documentation update**

Document:

- the old stable backend
- the new default backend
- the explicit fallback boundary for `ncore`
- the exact first validated device target

**Step 2: Verify the docs update**

Search the updated docs for `ainject`, `ncore`, `stable spawn`, and `fallback` to confirm the wording matches the final architecture.

**Step 3: Commit**

```bash
git add docs/step11.md docs/plans/2026-05-10-attach-explicit-init-and-single-server-status.md docs/plans/2026-05-10-agent-owned-stable-spawn-design.md
git commit -m "docs: record agent-owned stable spawn architecture"
```
