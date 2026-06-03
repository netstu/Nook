# Stable Spawn IPC-Only Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove `spawn_markers/` and `spawn_result.json` from the default stable legacy spawn path so stable spawn coordination is pure IPC.

**Architecture:** Keep the current stable backend on legacy `ncore`, but make child activation and server-side spawn completion depend only on `spawn_token`, `AGENT_READY`, and `SessionRegistry` pending-spawn state. Do not expand scope into zygote-control stabilization; only preserve its opt-in compatibility.

**Tech Stack:** C++17, Android native runtime/server code, existing session/protocol stack, host-side regression executables.

---

### Task 1: Lock the New Stable-Path Boundary in Tests

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\framework\test_nook_agent_init_policy.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_ninjector_spawn_injector.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_server_runtime.cpp`

**Step 1: Write the failing tests**

- Change agent-init-policy expectations so inherited child activation no longer requires a spawn-marker boolean for normal app processes.
- Add or tighten spawn-injector assertions so stable legacy spawn behavior does not depend on `callback_file` or `spawn_marker_dir`.
- Change runtime expectations so `spawn_markers` is no longer treated as a steady-state required runtime artifact for the stable path.

**Step 2: Run targeted tests to verify they fail**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/framework/test_nook_agent_init_policy.cpp src/framework/nook_agent_init_policy.cpp -o build/test_nook_agent_init_policy.exe
build/test_nook_agent_init_policy.exe
```

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_runtime.cpp server/server_runtime.cpp src/communication/io/io_loop.cpp -o build/test_server_runtime.exe
build/test_server_runtime.exe
```

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build/test_ninjector_spawn_injector.exe
```

Expected: at least one test fails because the current implementation still models file-based spawn coordination in the stable path.

**Step 3: Write minimal implementation to satisfy the new tests**

- Remove stable-path dependency on spawn-marker gating.
- Remove stable-path config dependency on callback/spawn-marker fields.
- Keep experimental zygote-control compatibility out of the stable assertions.

**Step 4: Run the targeted tests to verify they pass**

Run the same three commands above.

Expected: PASS.

**Step 5: Commit**

```powershell
git add tests/framework/test_nook_agent_init_policy.cpp tests/communication/test_ninjector_spawn_injector.cpp tests/communication/test_server_runtime.cpp
git commit -m "test: lock stable spawn ipc-only boundary"
```

### Task 2: Remove Stable-Path File Coordination from Agent Activation

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\nook_agent_init_policy.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\nook_agent_init_policy.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookComm.cpp`

**Step 1: Write/adjust the failing test**

- Ensure inherited agent activation for normal app child paths is based on process class plus spawn token semantics, not marker-file presence.

**Step 2: Run the specific test and verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/framework/test_nook_agent_init_policy.cpp src/framework/nook_agent_init_policy.cpp -o build/test_nook_agent_init_policy.exe
build/test_nook_agent_init_policy.exe
```

Expected: FAIL against the old `has_spawn_marker` behavior.

**Step 3: Write minimal implementation**

- Simplify `ShouldActivateInheritedNookAgent(...)` to reflect token-driven inherited-child activation semantics for the stable path.
- Update `NookComm.cpp` to keep current `NOOK_SPAWN_TOKEN` / override-based activation logic coherent with the new helper semantics.

**Step 4: Re-run the test**

Run the same command above.

Expected: PASS.

**Step 5: Commit**

```powershell
git add src/framework/nook_agent_init_policy.h src/framework/nook_agent_init_policy.cpp src/framework/NookComm.cpp tests/framework/test_nook_agent_init_policy.cpp
git commit -m "refactor: remove stable spawn marker dependency from agent activation"
```

### Task 3: Remove Stable-Path Callback/Marker Config Drift

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\ninjector_spawn_injector.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\ninjector_spawn_injector.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\server_runtime.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_ninjector_spawn_injector.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_server_runtime.cpp`

**Step 1: Write/adjust the failing tests**

- Update spawn-injector tests so stable legacy spawn config no longer expects `callback_file` / `spawn_marker_dir`.
- Update runtime tests so `spawn_markers` is no longer part of the normal stable runtime surface.

**Step 2: Run targeted tests and verify they fail**

Run the same targeted test commands from Task 1.

Expected: FAIL because config/runtime still model obsolete file coordination.

**Step 3: Write minimal implementation**

- Remove `callback_file` and `spawn_marker_dir` from stable-path config/state where they are no longer real dependencies.
- Keep any experimental-path compatibility out of stable-path structures unless strictly required.
- Keep `ncore` prepare/clear and `spawn_token` flow unchanged.

**Step 4: Re-run targeted tests**

Run the same commands.

Expected: PASS.

**Step 5: Commit**

```powershell
git add server/ninjector_spawn_injector.h server/ninjector_spawn_injector.cpp server/server_runtime.cpp tests/communication/test_ninjector_spawn_injector.cpp tests/communication/test_server_runtime.cpp
git commit -m "refactor: remove stable spawn callback and marker config drift"
```

### Task 4: Update Step10 State and Run Full Regression Slice

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\step10.md`

**Step 1: Add a minimal documentation update**

- Update only the current-state section to say the default stable legacy spawn path no longer depends on `spawn_result.json` / `spawn_markers`.
- Keep the zygote-control caveats intact.

**Step 2: Run full regression slice**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/framework/test_nook_agent_init_policy.cpp src/framework/nook_agent_init_policy.cpp -o build/test_nook_agent_init_policy.exe
build/test_nook_agent_init_policy.exe
```

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp -o build/test_session_registry.exe
build/test_session_registry.exe
```

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_runtime.cpp server/server_runtime.cpp src/communication/io/io_loop.cpp -o build/test_server_runtime.exe
build/test_server_runtime.exe
```

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector.exe
build/test_ninjector_spawn_injector.exe
```

Expected: all pass.

**Step 3: Commit**

```powershell
git add docs/step10.md
git commit -m "docs: record stable spawn ipc-only coordination"
```
