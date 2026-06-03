# Nook Frida-Style Single-Server Spawn Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `nook-server` own spawn, attach, resume, and gate release so the default device deployment is just one server.

**Architecture:** Keep the current agent/runtime model intact, but move spawn control-plane ownership into `nook-server`. Replace file-based spawn markers with internal registry state and cached frames, and keep the current injector path only as a temporary compatibility adapter while the server-owned flow is introduced.

**Tech Stack:** C++17, Android NDK, current Nook communication/session runtime, existing server and injector abstractions

---

### Task 1: Add a server-owned spawn state model on top of the existing registry

**Files:**
- Modify: `server/session_registry.h`
- Modify: `server/session_registry.cpp`
- Test: `tests/communication/test_session_registry.cpp` (or the closest existing session-registry test file)

**Step 1: Write the failing test**

Add tests that assert the registry can represent a spawn transaction without relying on `spawn_markers/`:

```cpp
TEST(SessionRegistry, TracksSpawnTransactionState) {
    SessionRegistry registry;
    registry.MarkSpawnSuspended(1234, 7);
    SpawnSuspendedEntry entry{};
    ASSERT_TRUE(registry.GetSpawnSuspendedEntry(1234, &entry));
    EXPECT_EQ(entry.pid, 1234);
    EXPECT_EQ(entry.host_session_id, 7u);
    EXPECT_TRUE(entry.suspended);
    EXPECT_EQ(entry.state, SpawnTransactionState::kWaitingAgentReady);
}
```

Add a second test that verifies `UpdateSpawnState(...)` and `ClearSpawnSuspended(...)` behave as the server-owned gate state needs.

**Step 2: Run test to verify it fails**

Run the session-registry test target already used in this repo.
Expected: either missing coverage or failing assertions because the new state transitions are not yet exercised the way the design needs.

**Step 3: Write minimal implementation**

Keep the current maps, but make the state transitions explicit and predictable for the new server-owned flow.

**Step 4: Run test to verify it passes**

Run the same test target again.
Expected: PASS.

**Step 5: Commit**

```bash
git add server/session_registry.h server/session_registry.cpp tests/communication/test_session_registry.cpp
git commit -m "feat: model server-owned spawn state"
```

### Task 2: Move spawn handling to a server-owned controller path

**Files:**
- Modify: `server/server_handlers.cpp`
- Modify: `server/server_main.cpp`
- Modify: `server/injector.h`
- Modify: `server/injector.cpp`
- Modify: `server/ninjector_spawn_injector.h`
- Modify: `server/ninjector_spawn_injector.cpp`

**Step 1: Write the failing test**

Add a server-handler test that verifies a spawn request now flows through server-owned state updates, not file marker creation.

The test should assert that a successful spawn request records a suspended transaction and binds the host session to the pid.

**Step 2: Run test to verify it fails**

Run the server-handler test target.
Expected: the current path still depends on the injector/marker flow.

**Step 3: Write minimal implementation**

Introduce a server-owned spawn controller abstraction inside `server/` and route `HandleSpawnRequest(...)` through it.
Keep `NinjectorSpawnInjector` behind an interface adapter during migration.

**Step 4: Run test to verify it passes**

Run the same server-handler test target.
Expected: PASS.

**Step 5: Commit**

```bash
git add server/server_handlers.cpp server/server_main.cpp server/injector.h server/injector.cpp server/ninjector_spawn_injector.h server/ninjector_spawn_injector.cpp
git commit -m "feat: route spawn through server-owned control"
```

### Task 3: Remove file-based spawn marker dependency from the default flow

**Files:**
- Modify: `src/communication/transport/spawn_marker.cpp`
- Modify: `server/server_main.cpp`
- Modify: `server/server_handlers.cpp`
- Modify: `docs/step10.md` or the current deployment doc

**Step 1: Write the failing test**

Add a test or integration assertion that the default spawn path does not need `spawn_markers/` to succeed.

**Step 2: Run test to verify it fails**

Expected: current flow still touches the filesystem marker path.

**Step 3: Write minimal implementation**

Stop creating or consuming markers in the default path and rely on the in-memory spawn transaction state instead.

**Step 4: Run test to verify it passes**

Expected: PASS.

**Step 5: Commit**

```bash
git add src/communication/transport/spawn_marker.cpp server/server_main.cpp server/server_handlers.cpp docs/step10.md
git commit -m "feat: remove marker dependency from default spawn flow"
```

### Task 4: Keep early agent-ready replay working after the control-plane move

**Files:**
- Modify: `server/server_handlers.cpp`
- Modify: `server/session_registry.cpp`
- Test: existing spawn/attach replay test or a new focused replay test

**Step 1: Write the failing test**

Add a test that verifies cached `AGENT_READY` and cached script-message frames are replayed to a later host session after spawn.

**Step 2: Run test to verify it fails**

Expected: replay still works only through the old path or is not sufficiently covered.

**Step 3: Write minimal implementation**

Make the server-owned flow preserve the existing replay behavior.

**Step 4: Run test to verify it passes**

Expected: PASS.

**Step 5: Commit**

```bash
git add server/server_handlers.cpp server/session_registry.cpp tests/communication/*
git commit -m "feat: preserve spawn replay across server-owned flow"
```

### Task 5: Update deployment documentation to reflect single-server usage

**Files:**
- Modify: `docs/plans/2026-05-09-nook-frida-style-single-server-spawn-design.md`
- Modify: `host/nook-py/README.md`
- Modify: `docs/step10.md`

**Step 1: Write the failing documentation check**

Search for deployment text that still tells the user to manage `ncore` as a required deploy artifact.

**Step 2: Write minimal documentation**

Document the new default:

1. build/push `nook-server`
2. start `./nook-server`
3. use `spawn` / `attach` as before
4. do not mention `ncore` as a user deployment step

**Step 3: Verify the docs update**

Run a search for `ncore` and `spawn_markers` in the deployment docs and confirm the user-facing workflow no longer depends on them.

**Step 4: Commit**

```bash
git add docs/plans/2026-05-09-nook-frida-style-single-server-spawn-design.md host/nook-py/README.md docs/step10.md
git commit -m "docs: describe single-server deployment"
```

### Task 6: Device smoke test the new single-server flow

**Files:**
- Use: built `nook-server` artifact

**Step 1: Push only `nook-server`**

Run:

```powershell
adb shell su -c 'rm -f /data/local/tmp/nook-test/libnook-agent.so /data/local/tmp/nook-test/libncore.so'
adb push build/android/libs/arm64-v8a/nook-server /data/local/tmp/nook-test/nook-server
adb shell 'chmod 755 /data/local/tmp/nook-test/nook-server'
```

**Step 2: Start the server**

Run it in an interactive shell.
Expected: server starts without the user manually staging `ncore`.

**Step 3: Run spawn and attach smoke cases**

Use the existing smoke scripts that already passed on-device.
Expected: spawn/attach still works and the agent responds normally.

**Step 4: Verify no extra deployment artifact is needed**

Expected: the only required server-side device artifact is `nook-server`.

**Step 5: Commit**

```bash
git add .
git commit -m "test: verify single-server deployment flow"
```
