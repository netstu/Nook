# Spawn / Zygote / Ready Stability Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Tighten Nook's current spawn gate model so `spawn / zygote / ready` behavior becomes deterministic, diagnosable, and safe to build on before later Frida-style pre-resume activation work.

**Architecture:** Keep the current Ninjector-based spawn path and existing child-side gate model, but formalize state transitions across injector/server, agent/NookComm, and Java-ready bootstrap. The main work is explicit transaction state, authoritative child/session ownership, clearer timeout/error staging, and stricter `spawn -> load -> resume` ordering.

**Tech Stack:** C++17 server/runtime code, host Python CLI, existing communication protocol/session framework, Android device validation.

---

### Task 1: Document and test the current authoritative spawn-child ownership rules

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_server_handlers.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server/session_registry.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server/session_registry.cpp`

**Step 1: Write the failing test**

Add tests that model:

- one spawn transaction awaiting a child
- an early non-authoritative `zygote64`-like ready event
- a later authoritative child ready event
- host-facing binding must remain attached only to the authoritative child

Also add a negative test:

- second authoritative bind attempt for the same transaction must be rejected or ignored deterministically

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe
.\build\test_server_handlers.exe
```

Expected:

- FAIL because current registry/handler logic does not yet enforce authoritative child ownership as an explicit rule

**Step 3: Write minimal implementation**

Add or refine registry state so one spawn transaction explicitly tracks:

- expected/authoritative pid
- host session id
- current state
- whether authoritative child binding is already sealed

Do not mix this loosely into generic pid/session maps.

**Step 4: Run test to verify it passes**

Run the same command again.

Expected:

- PASS

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 2: Add failing tests for stage-specific spawn timeout and error reporting

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_host_spawn_client.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\communication\host\host_spawn_client.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\communication\host\host_spawn_client.h`

**Step 1: Write the failing test**

Add tests expecting stage-aware error messages for at least:

- spawn result timeout
- agent-ready timeout
- gate-held timeout / not-ready timeout

The tests should assert that returned errors are no longer a generic:

- `operation timed out`

and instead contain stage-specific wording.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_spawn_client.exe
.\build\test_host_spawn_client.exe
```

Expected:

- FAIL because current host-side spawn errors are still too coarse

**Step 3: Write minimal implementation**

Refine host spawn client error handling so timeout/failure messages include stage context.

Do not redesign the protocol yet. This is only about reporting the existing staged lifecycle more truthfully.

**Step 4: Run test to verify it passes**

Run the same command again.

Expected:

- PASS

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 3: Add failing tests for strict `spawn -> load -> resume` state transitions

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_server_handlers.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server/server_handlers.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server/session_registry.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server/session_registry.cpp`

**Step 1: Write the failing test**

Add tests covering:

- script load is allowed only after authoritative child bind / gate-held state
- resume is rejected before gate-held state
- resume is accepted only once
- post-load resume transitions the transaction out of suspended state

**Step 2: Run test to verify it fails**

Run:

```powershell
.\build\test_server_handlers.exe
```

Expected:

- FAIL because current transaction state is not yet strict enough

**Step 3: Write minimal implementation**

Implement explicit transaction states, for example:

- `waiting_spawn_result`
- `waiting_agent_ready`
- `ready_for_script_load`
- `waiting_resume_release`
- `resumed`
- `failed`

Enforce them in handler logic. Keep the change minimal and scoped to spawn-related flows.

**Step 4: Run test to verify it passes**

Run:

```powershell
.\build\test_server_handlers.exe
```

Expected:

- PASS

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 4: Add failing tests for REPL / CLI suspended-session semantics

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\nook\cli.py`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\tests\` if existing CLI tests exist, otherwise create a focused host-side test file

**Step 1: Write the failing test**

Add tests expecting:

- suspended spawn sessions report suspended state clearly
- `%resume` against already resumed session reports an explicit state message
- startup script load during suspended spawn is allowed
- error text reflects whether failure happened before or after gate-held state

**Step 2: Run test to verify it fails**

Use the narrowest existing CLI/host test command available in this repo for the affected file.

Expected:

- FAIL because current CLI state messaging is not yet explicit enough

**Step 3: Write minimal implementation**

Refine CLI/repl state reporting only. Do not redesign the command surface.

Expected improvements:

- clearer suspended/resumed state output
- clearer resume refusal messages
- clearer script-load-vs-resume sequencing output

**Step 4: Run test to verify it passes**

Run the same host-side test command again.

Expected:

- PASS

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 5: Add targeted cold-spawn Java-ready regression coverage

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_ready_smoke.js`
- Possibly create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_ready_spawn_smoke.js`
- Modify: relevant host/device regression notes in docs later

**Step 1: Write the failing smoke or regression script**

Create a smoke that validates cold-spawn behavior more explicitly:

- script loads while spawn gate is held
- `Java.ready(...)` eventually fires without extra page switching
- output makes it obvious whether the failure is:
  - no authoritative child bind
  - no gate-held state
  - no Java class-loader-ready transition

**Step 2: Run the smoke to verify the current gap**

Run on device after rebuild/push if needed, using the same spawn flow already used in previous investigations.

Expected:

- first run reveals whether any cold-spawn `Java.ready(...)` instability still exists

**Step 3: Write minimal implementation**

Fix only the specific lifecycle/state issue identified by the smoke.

Do not broaden scope into full Frida-style pre-resume activation yet.

**Step 4: Run smoke to verify it passes**

Run the same device smoke again.

Expected:

- reliable cold-spawn `Java.ready(...)` callback without needing page changes

**Step 5: Commit**

Skip commit in-session unless explicitly requested.

### Task 6: Run device validation and update regression docs

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\code_review.md`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\step6.md` if wording should change

**Step 1: Build Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4
```

Expected:

- relevant server / agent artifacts rebuild successfully

**Step 2: Push updated binaries**

Push every changed runtime artifact required by the spawn path, for example:

```powershell
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so
```

Also restore execute bits where required.

**Step 3: Run focused device validation**

Validate at least:

- repeated cold `spawn --resume --wait`
- `repl spawn -l ...`
- cold Java-ready smoke

Capture the exact output/log sequence for:

- spawn response
- authoritative child ready
- script load
- resume release
- Java-ready callback

**Step 4: Update docs**

Record:

- what state-machine tightening was added
- what timeout/error messages changed
- whether `zygote64/usap64` misbinding is now fully closed
- what still remains before a Frida-like pre-resume activation pass

**Step 5: Final verification**

Re-run all touched local tests and record the commands in the doc update.

Expected:

- touched host/server tests pass
- device validation is stable across repeated runs

**Step 6: Commit**

Skip commit in-session unless explicitly requested.
