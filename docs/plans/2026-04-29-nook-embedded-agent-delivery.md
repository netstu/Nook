# Nook Embedded Agent Delivery Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Let operators deploy only `nook-server`, with the server automatically materializing `libnook-agent.so` beside itself before attach/spawn uses it.

**Architecture:** Preserve the existing path-based injector contract and solve the UX problem through packaging. Build `libnook-agent.so` first, generate an embedded byte-array header, then build `nook-server` with runtime helper logic that writes or reuses a sibling `libnook-agent.so`.

**Tech Stack:** Android NDK `ndk-build`, PowerShell asset-generation script, C++17 server runtime helpers

---

### Task 1: Add failing runtime tests for embedded-agent materialization behavior

**Files:**
- Modify: `tests/communication/test_server_runtime.cpp`
- Modify: `server/server_runtime.h`

**Step 1: Write the failing test**

Add tests covering:

1. environment override still wins
2. executable-directory fallback still resolves to sibling `libnook-agent.so`
3. missing file is written
4. stale file is replaced
5. matching file is reused

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_runtime.cpp server/server_runtime.cpp src/communication/io/io_loop.cpp src/communication/transport/transport.cpp -o build/test_server_runtime.exe
build/test_server_runtime.exe
```

Expected:

- compile or runtime failure because the new helper API does not exist yet

**Step 3: Write minimal implementation declarations**

Add declarations for materialization helpers in `server_runtime.h`.

**Step 4: Re-run test to keep it red for the intended reason**

Expected:

- failure now points to missing implementation logic instead of missing declarations

### Task 2: Implement runtime helper logic for sibling-agent materialization

**Files:**
- Modify: `server/server_runtime.cpp`
- Modify: `server/server_runtime.h`

**Step 1: Write minimal implementation**

Implement helpers for:

1. detecting executable path
2. resolving sibling agent path
3. writing embedded bytes through temp-file + rename
4. reusing matching files
5. replacing stale files

Keep the logic generic so tests can use tiny dummy byte arrays.

**Step 2: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_runtime.cpp server/server_runtime.cpp src/communication/io/io_loop.cpp src/communication/transport/transport.cpp -o build/test_server_runtime.exe
build/test_server_runtime.exe
```

Expected:

- PASS

### Task 3: Add embedded-agent blob generation

**Files:**
- Create: `tools/build_embedded_agent_blob.ps1`
- Create: `server/generated/nook_embedded_agent_blob.h`

**Step 1: Write the failing check**

Run:

```powershell
Get-Content server/generated/nook_embedded_agent_blob.h
```

Expected:

- file missing or placeholder only

**Step 2: Write minimal generation script**

Script responsibilities:

1. read `libs/arm64-v8a/libnook-agent.so`
2. emit a header with:
   - `kNookEmbeddedAgentBlob[]`
   - `kNookEmbeddedAgentBlobSize`

Use a deterministic header comment saying it is generated.

**Step 3: Run generator**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_embedded_agent_blob.ps1
```

Expected:

- header generated successfully

### Task 4: Wire the generated blob into `nook-server`

**Files:**
- Modify: `server/server_main.cpp`
- Modify: `server/server_runtime.cpp`
- Modify: `build/android/Android.mk` if include visibility requires it

**Step 1: Write the failing test**

Treat this as an integration-level red state:

- `nook-server` currently still expects an external `libnook-agent.so`

**Step 2: Implement minimal integration**

At startup:

1. if `NOOK_AGENT_PATH` is set, use it unchanged
2. otherwise, resolve sibling `libnook-agent.so`
3. ensure the embedded blob is materialized there
4. pass that path into `ServerHandlerConfig.agent_path`

**Step 3: Rebuild static server**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_MODULES=nook_server -j4
```

Expected:

- build succeeds

### Task 5: Add a packaging command sequence for agent-then-server

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Write the failing check**

Run:

```powershell
rg -n "embedded|only `nook-server`|build_embedded_agent_blob" host/nook-py/README.md
```

Expected:

- no embedded-agent packaging workflow documented

**Step 2: Write minimal documentation**

Document the exact sequence:

1. build static `nook_agent`
2. run `tools/build_embedded_agent_blob.ps1`
3. build static `nook_server`
4. push only `nook-server`

**Step 3: Verify docs**

Run the same `rg` command and confirm the new workflow appears.

### Task 6: Device verification

**Files:**
- Use: `build/android/libs/arm64-v8a/nook-server`

**Step 1: Push only `nook-server`**

Run:

```powershell
adb shell su -c 'rm -f /data/local/tmp/nook-test/libnook-agent.so'
adb push build/android/libs/arm64-v8a/nook-server /data/local/tmp/nook-test/nook-server
adb shell 'chmod 755 /data/local/tmp/nook-test/nook-server'
```

Expected:

- no agent file present before startup

**Step 2: Start server**

Run in interactive shell:

```powershell
adb shell
su
cd /data/local/tmp/nook-test
./nook-server
```

Expected:

- server starts
- sibling `libnook-agent.so` is materialized automatically

**Step 3: Verify attach works**

Run:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_enumerate_class_loaders_smoke.js --wait --usb
```

Expected:

- attach succeeds
- script create/load succeeds

