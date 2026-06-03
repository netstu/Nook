# Nook Spawn Resume Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a true spawn-suspend-resume control flow so a process started through `SpawnRequest` is stopped early and only continues after an explicit `ResumeRequest`.

**Architecture:** Keep the current Ninjector-based spawn path, but add an internal suspended-state handshake between agent and server. The server tracks spawn-suspended pids and forwards `ResumeRequest` to the correct agent session. The agent is responsible for entering the suspended state and later resuming itself.

**Tech Stack:** C++17, Android NDK, existing Nook communication protocol/session framework, Unix socket agent channel, TCP host channel, Android signals.

---

### Task 1: Add failing protocol tests for resume messages

**Files:**
- Modify: `tests/communication/test_protocol.cpp`
- Modify: `src/communication/protocol/messages.h`
- Modify: `src/communication/protocol/messages.cpp`

**Step 1: Write the failing test**

Add tests covering:

- `ResumeRequest { pid = 2100 }`
- `ResumeResponse { pid = 2100, error = {} }`
- `ResumeResponse { pid = 2100, error = { code = -3, message = "not suspended" } }`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_protocol.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_protocol.exe
build\test_protocol.exe
```

Expected: link or compile failure for missing `ResumeRequest` / `ResumeResponse` symbols.

**Step 3: Write minimal implementation**

Add to `messages.h`:

- `struct ResumeRequest { uint32_t pid = 0; };`
- `struct ResumeResponse { uint32_t pid = 0; ErrorInfo error; };`

Add encoder/decoder declarations and definitions in `messages.cpp`.

Use compact fields:

- request pid: field `1`
- response pid: field `1`
- response error: field `15`

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_protocol.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_protocol.exe
build\test_protocol.exe
```

Expected: `Protocol tests passed!`

**Step 5: Commit**

```bash
git add tests/communication/test_protocol.cpp src/communication/protocol/messages.h src/communication/protocol/messages.cpp
git commit -m "feat: add resume protocol messages"
```

### Task 2: Add failing host-client tests for resume

**Files:**
- Modify: `tests/communication/test_host_client.cpp`
- Modify: `src/communication/host/host_client.h`
- Modify: `src/communication/host/host_client.cpp`

**Step 1: Write the failing test**

Add a test that:

- sends `ResumeRequest(pid=2100)`
- fake transport replies with `ResumeResponse(pid=2100)`
- asserts `HostClient::Resume(...)` returns success

Add one negative test:

- fake transport replies with `ResumeResponse(error.code != 0)`
- assert returned error message propagates

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_host_client.cpp src/communication/host/host_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_client.exe
build\test_host_client.exe
```

Expected: missing `HostClient::Resume(...)`.

**Step 3: Write minimal implementation**

Add:

```cpp
bool Resume(int timeout_ms,
            const ResumeRequest& request,
            ResumeResponse* response,
            std::string* error_message = nullptr);
```

Use the same pattern already used by:

- `HostClient::Attach(...)`
- `HostClient::Detach(...)`
- `HostClient::EnumerateProcesses(...)`

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_host_client.cpp src/communication/host/host_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_client.exe
build\test_host_client.exe
```

Expected: success with exit code `0`.

**Step 5: Commit**

```bash
git add tests/communication/test_host_client.cpp src/communication/host/host_client.h src/communication/host/host_client.cpp
git commit -m "feat: add host resume client"
```

### Task 3: Add internal spawn-suspended state to server registry

**Files:**
- Modify: `server/session_registry.h`
- Modify: `server/session_registry.cpp`
- Test: `tests/communication/test_server_handlers.cpp`

**Step 1: Write the failing test**

Add tests expecting:

- `MarkSpawnSuspended(pid, host_session_id)` stores state
- `IsSpawnSuspended(pid)` returns true
- `ClearSpawnSuspended(pid)` removes state
- agent disconnect cleanup removes suspended state

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe
build\test_server_handlers.exe
```

Expected: compile failure for missing registry APIs.

**Step 3: Write minimal implementation**

Add a small struct in `session_registry.h`:

```cpp
struct SpawnSuspendedEntry {
    int pid = -1;
    uint32_t host_session_id = 0;
    bool suspended = false;
};
```

Add APIs:

- `void MarkSpawnSuspended(int pid, uint32_t host_session_id);`
- `bool IsSpawnSuspended(int pid) const;`
- `bool GetSpawnSuspendedEntry(int pid, SpawnSuspendedEntry* out) const;`
- `void ClearSpawnSuspended(int pid);`

Store them in a dedicated map, not mixed into existing `pid_to_host_session_`.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: binary exits with code `0`.

**Step 5: Commit**

```bash
git add server/session_registry.h server/session_registry.cpp tests/communication/test_server_handlers.cpp
git commit -m "feat: track spawn suspended processes"
```

### Task 4: Add failing server-handler tests for resume request semantics

**Files:**
- Modify: `tests/communication/test_server_handlers.cpp`
- Modify: `server/server_handlers.h`
- Modify: `server/server_handlers.cpp`

**Step 1: Write the failing test**

Add tests for:

- resume on unknown pid -> error
- resume on known pid but not suspended -> error
- resume on suspended pid with no agent -> error
- resume on suspended pid with agent -> forwarded request and success
- second resume on same pid -> error

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe
build\test_server_handlers.exe
```

Expected: failure due to missing `HandleResumeRequest` path.

**Step 3: Write minimal implementation**

In `server/server_handlers.cpp`:

- add `SendResumeResponse(...)`
- register `MessageType::kResumeRequest`
- implement `HandleResumeRequest(...)`

Minimal logic:

- decode request
- require `registry != nullptr`
- require suspended entry for pid
- require `agent != nullptr`
- forward `ResumeRequest` to agent
- on agent `ResumeResponse`, clear suspended entry and return to host

If a direct synchronous forward is awkward, use the same routing pattern already used by:

- `SCRIPT_CREATE`
- `SCRIPT_LOAD`
- `SCRIPT_UNLOAD`

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: binary exits with code `0`.

**Step 5: Commit**

```bash
git add tests/communication/test_server_handlers.cpp server/server_handlers.h server/server_handlers.cpp
git commit -m "feat: add server resume routing"
```

### Task 5: Add internal agent-to-server suspended notification message

**Files:**
- Modify: `src/communication/protocol/message_types.h`
- Modify: `src/communication/protocol/messages.h`
- Modify: `src/communication/protocol/messages.cpp`
- Modify: `tests/communication/test_protocol.cpp`

**Step 1: Write the failing test**

Add round-trip coverage for a new internal message, for example:

```cpp
struct AgentSpawnSuspended {
    uint32_t pid = 0;
};
```

**Step 2: Run test to verify it fails**

Run the protocol test command from Task 1.

Expected: missing message symbol or decoder.

**Step 3: Write minimal implementation**

Add:

- a new internal `MessageType`, preferably under `0xFFxx`
- matching struct and encode/decode helpers

Keep payload minimal: just `pid`.

**Step 4: Run test to verify it passes**

Run the protocol test command again.

Expected: `Protocol tests passed!`

**Step 5: Commit**

```bash
git add src/communication/protocol/message_types.h src/communication/protocol/messages.h src/communication/protocol/messages.cpp tests/communication/test_protocol.cpp
git commit -m "feat: add agent spawn suspended message"
```

### Task 6: Add server handling for agent suspended notification

**Files:**
- Modify: `server/server_handlers.cpp`
- Modify: `tests/communication/test_server_handlers.cpp`
- Modify: `server/session_registry.cpp`

**Step 1: Write the failing test**

Add a test that:

- host performs `SpawnRequest`
- agent later sends `AgentSpawnSuspended { pid = X }`
- registry marks `X` as suspended for the correct host session

**Step 2: Run test to verify it fails**

Run the server handler test command from Task 4.

Expected: no handler or missing state update.

**Step 3: Write minimal implementation**

In `server/server_handlers.cpp`:

- register handler for `AgentSpawnSuspended`
- locate the bound host session using existing `pid -> host` binding from spawn
- call `registry->MarkSpawnSuspended(pid, host_session_id)`
- log a clear line like:

```cpp
NOOK_SERVER_LOGI("spawn suspended pid=%u host_session=%u", pid, host_id);
```

**Step 4: Run test to verify it passes**

Run the server handler test command again.

Expected: binary exits with code `0`.

**Step 5: Commit**

```bash
git add server/server_handlers.cpp server/session_registry.cpp tests/communication/test_server_handlers.cpp
git commit -m "feat: track suspended state from agent notifications"
```

### Task 7: Add agent-side resume request callback plumbing

**Files:**
- Modify: `include/nook/NookComm.h`
- Modify: `src/framework/NookComm.cpp`
- Modify: any existing agent connection helper used for message dispatch

**Step 1: Write the failing test**

If a direct unit test is not practical, add a targeted smoke-oriented helper test or at minimum a temporary local assertion path in agent smoke code that exercises:

- receive `ResumeRequest`
- invoke registered callback
- send `ResumeResponse`

**Step 2: Run verification to confirm missing behavior**

Use whichever existing smoke is closest to agent message callbacks and confirm there is no resume handling path yet.

**Step 3: Write minimal implementation**

Add agent-side callback plumbing similar in style to:

- `NookCommSetMessageCallback(...)`
- script callback handling paths already present in `NookComm.cpp`

Expose a narrow callback like:

```cpp
typedef NookStatus (*NookCommResumeCallback)(uint32_t pid, char** error_message);
NookStatus NookCommSetResumeCallback(NookCommResumeCallback callback);
```

This callback should:

- only run for spawn-suspended state
- return success/failure
- produce `ResumeResponse`

**Step 4: Run verification**

Build the relevant local smoke or agent library and confirm the callback path executes without crash.

**Step 5: Commit**

```bash
git add include/nook/NookComm.h src/framework/NookComm.cpp
git commit -m "feat: add agent resume callback plumbing"
```

### Task 8: Implement agent spawn-suspend state and early stop behavior

**Files:**
- Modify: `src/framework/NookComm.cpp`
- Modify: agent initialization path used after injection
- Reference: `server/ninjector_spawn_injector.cpp`

**Step 1: Write the failing behavior check**

Create or adapt a smoke scenario where:

- a target is spawned
- agent connects
- before host resume, app should not proceed normally

Even if this is a manual device smoke first, document the exact expected log sequence.

**Step 2: Verify it currently fails**

Run the existing spawn flow and observe that the app continues immediately.

**Step 3: Write minimal implementation**

Add an internal flag so the agent knows it was loaded via spawn mode.

Implementation outline:

- determine spawn-mode marker
- after communication bootstrap, send `AgentSpawnSuspended`
- then call `kill(getpid(), SIGSTOP)`

Do not apply this path for normal attach-mode injection.

**Step 4: Run device verification**

Expected order:

- agent connects
- server logs suspended state
- app does not continue until explicit resume

**Step 5: Commit**

```bash
git add src/framework/NookComm.cpp
git commit -m "feat: suspend spawned target until host resume"
```

### Task 9: Add host resume smoke tool

**Files:**
- Create: `tools/nook_resume_smoke.cpp`
- Modify: build command notes if needed

**Step 1: Write the failing smoke command**

Define the tool to accept:

```text
nook_resume_smoke.exe <pid> [timeout_ms] [host] [port]
```

**Step 2: Build before implementation to confirm file is missing**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tools/nook_resume_smoke.cpp src/communication/host/host_client.cpp src/communication/protocol/frame.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/transport/tcp_transport.cpp -lws2_32 -o build/nook_resume_smoke.exe
```

Expected: file-not-found or compile failure.

**Step 3: Write minimal implementation**

Follow the same pattern as:

- `tools/nook_attach_smoke.cpp`
- `tools/nook_detach_smoke.cpp`
- `tools/nook_process_list_smoke.cpp`

Print:

```text
resume ok: pid=...
```

or:

```text
resume failed: ...
```

**Step 4: Build and verify**

Run the build command again.

Expected: successful build.

**Step 5: Commit**

```bash
git add tools/nook_resume_smoke.cpp
git commit -m "feat: add resume smoke tool"
```

### Task 10: Extend spawn smoke to validate suspended-then-resume flow

**Files:**
- Modify: `tools/nook_spawn_smoke.cpp`
- Possibly create: `tools/nook_spawn_resume_smoke.cpp`

**Step 1: Write the failing scenario**

Define an end-to-end smoke that:

1. sends `SpawnRequest`
2. waits for `AGENT_READY`
3. optionally attaches or loads a trivial script
4. sends `ResumeRequest`
5. verifies resume success

**Step 2: Verify current smoke does not cover it**

Run the existing spawn smoke and confirm it stops after `AGENT_READY`.

**Step 3: Write minimal implementation**

Either:

- extend `nook_spawn_smoke.cpp`, or
- keep it clean and create a separate `nook_spawn_resume_smoke.cpp`

Preferred if scope grows: create a separate tool.

**Step 4: Run local or device verification**

Use exact commands and save sample expected output:

```text
spawn response ok: pid=...
agent ready: pid=...
resume ok: pid=...
```

**Step 5: Commit**

```bash
git add tools/nook_spawn_smoke.cpp
git commit -m "feat: validate spawn to resume control flow"
```

### Task 11: Build Android artifacts and run full device validation

**Files:**
- Modify: none unless fixes are required
- Validate: `libs/arm64-v8a/nook-server`
- Validate: agent `.so` files involved in spawn path

**Step 1: Build Android artifacts**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

Expected: `nook-server` and the relevant agent library build successfully.

**Step 2: Push updated binaries**

Run:

```powershell
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb shell chmod 755 /data/local/tmp/nook/nook-server
```

Push any updated agent `.so` used by the spawn flow as well.

**Step 3: Restart server**

Use the same launch mode already proven to work in this environment.

**Step 4: Run device smoke**

Expected command sequence:

```powershell
build\nook_spawn_smoke.exe com.demo.target 10000 5000
build\nook_resume_smoke.exe <pid>
```

Collect:

```powershell
adb logcat -d -v threadtime -s NookServer NookComm NookCommApi NookNinjector
```

Expected logs include:

- spawn success
- agent ready
- spawn suspended
- resume success

**Step 5: Commit**

```bash
git add docs/plans/2026-04-22-nook-spawn-resume-design.md docs/plans/2026-04-22-nook-spawn-resume-implementation-plan.md
git commit -m "docs: add spawn resume design and implementation plan"
```

### Task 12: Final regression sweep

**Files:**
- Validate existing tests and smoke binaries

**Step 1: Re-run communication regression tests**

Run:

```powershell
build\test_protocol.exe
build\test_host_client.exe
build\test_server_handlers.exe
```

Expected: all exit with code `0`.

**Step 2: Re-run previously working device paths**

Validate:

- `nook_process_list_smoke.exe`
- `nook_app_list_smoke.exe`
- `nook_attach_smoke.exe`
- `nook_detach_smoke.exe`

Expected: no regression.

**Step 3: Capture residual risks**

Document anything still manual, especially:

- how agent knows it is in spawn mode
- exact suspend point timing
- what happens if host never resumes

**Step 4: Commit**

```bash
git add .
git commit -m "test: verify resume flow and regressions"
```
