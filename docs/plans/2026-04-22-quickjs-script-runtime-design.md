# QuickJS Script Runtime Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a real in-process JavaScript runtime for Nook Agent so `ScriptCreate` compiles and registers a script, `ScriptLoad` executes it, and JS can call `send()`.

**Architecture:** Vendor QuickJS from the local `rustFrida_upstream` source tree into `third_party/quickjs`, add a thin runtime layer under `src/agent_runtime/`, and bridge it to the existing communication callbacks in `NookComm`. The first slice only supports one runtime/context, script registry, `send()`, and create/load/unload lifecycle; no RPC or hook bindings yet.

**Tech Stack:** QuickJS (embedded C engine), existing Nook communication/session stack, Android NDK build via `ndk-build`, Windows smoke host tools.

---

### Task 1: Vendor QuickJS sources into Nook

**Files:**
- Create: `third_party/quickjs/*`
- Modify: `build/android/Android.mk`

**Step 1: Write the failing build expectation**

Expected missing state:
- No `third_party/quickjs`
- No build rules referencing QuickJS

**Step 2: Copy the minimal QuickJS source set**

Source:
- `E:\Learn\my_program\all_my_hook\kanxue\rustFrida_upstream\quickjs-hook\quickjs-src`

Target:
- `third_party/quickjs`

Include only the files required for embedding:
- `quickjs.h`
- `quickjs.c`
- `libregexp.c`
- `libregexp.h`
- `libunicode.c`
- `libunicode.h`
- `libbf.c`
- `libbf.h`
- `cutils.c`
- `cutils.h`
- `list.h`
- `dtoa.c`
- `dtoa.h`
- `quickjs-atom.h`
- `quickjs-opcode.h`
- `LICENSE`

**Step 3: Add QuickJS sources to Android build**

Update `build/android/Android.mk`:
- add `third_party/quickjs` include path
- add QuickJS `.c` files to a reusable variable
- compile them into `nook`, `nook_agent`, and smoke agents that depend on runtime

**Step 4: Verify Android build fails only on missing runtime layer**

Run:
```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk
```

Expected:
- QuickJS compiles
- Remaining failures point to missing runtime glue, not missing third-party files

### Task 2: Add runtime and script registry primitives

**Files:**
- Create: `src/agent_runtime/js_runtime.h`
- Create: `src/agent_runtime/js_runtime.cpp`
- Create: `src/agent_runtime/script_registry.h`
- Create: `src/agent_runtime/script_registry.cpp`
- Test: `tests/headers/test_js_runtime_compile.cpp`

**Step 1: Write the failing compile test**

Test should include the new headers and instantiate the public methods:
- `JsRuntime::Initialize()`
- `JsRuntime::Shutdown()`
- `ScriptRegistry::CreateScript(...)`
- `ScriptRegistry::LoadScript(...)`

**Step 2: Run compile test to verify headers are missing**

Run:
```powershell
g++ -std=c++17 -I . -I include -I src -c tests/headers/test_js_runtime_compile.cpp -o build/test_js_runtime_compile.o
```

Expected:
- FAIL because runtime headers do not exist

**Step 3: Write minimal runtime layer**

Implement:
- singleton `JSRuntime*` and `JSContext*`
- mutex-protected `Initialize/Shutdown`
- `Evaluate(script_source, filename)`
- exception-to-string helper

Implement script registry:
- monotonic `script_id`
- store `{id, name, source, loaded}`
- `CreateScript()` only registers source after syntax/compile validation
- `LoadScript()` executes source once and flips `loaded = true`
- `UnloadScript()` removes script record

**Step 4: Re-run compile test**

Expected:
- PASS

### Task 3: Add JS `send()` binding

**Files:**
- Modify: `src/agent_runtime/js_runtime.h`
- Modify: `src/agent_runtime/js_runtime.cpp`
- Test: `tests/communication/test_js_runtime_send.cpp`

**Step 1: Write the failing runtime test**

Test behavior:
- install a native send sink callback
- evaluate JS calling `send({ type: "send", payload: "hello" })`
- assert sink receives JSON payload

**Step 2: Run test to verify it fails**

Expected:
- FAIL because `send()` is not registered in JS global scope

**Step 3: Implement minimal `send()` binding**

Implementation rules:
- support `send(any)` only in this slice
- stringify JS value to JSON before forwarding
- ignore binary `ArrayBuffer` for now or return unsupported error

**Step 4: Re-run test**

Expected:
- PASS

### Task 4: Wire runtime into NookComm create/load callbacks

**Files:**
- Modify: `src/framework/NookComm.cpp`
- Create: `src/agent_runtime/nook_script_runtime_bridge.h`
- Create: `src/agent_runtime/nook_script_runtime_bridge.cpp`
- Test: `tests/communication/test_agent_connection.cpp`

**Step 1: Write the failing behavior test**

Extend agent-side test so:
- `ScriptCreate` stores and returns a real runtime-backed `script_id`
- `ScriptLoad` executes the stored script
- execution triggers `SCRIPT_MESSAGE` via `send()`

**Step 2: Run test to verify current smoke callback path fails**

Expected:
- FAIL because current implementation only uses manual smoke callbacks

**Step 3: Implement bridge**

Bridge responsibilities:
- register `NookCommSetScriptCreateCallback()` with runtime-backed create
- register `NookCommSetScriptLoadCallback()` with runtime-backed load
- `send()` inside runtime forwards via `NookCommSendMessage()`

**Step 4: Re-run agent communication tests**

Expected:
- PASS

### Task 5: Replace script smoke agent with real runtime smoke

**Files:**
- Modify: `examples/communication/nook_agent_script_smoke.cpp`
- Modify: `tools/nook_script_smoke.cpp`

**Step 1: Update smoke script source expectation**

Use JS source that actually calls:
```javascript
send({ type: "send", payload: "script-loaded" });
```

**Step 2: Remove manual fake script-id assignment logic**

The smoke agent should:
- initialize runtime bridge
- not manually fabricate `script_id` or emit send from load callback

**Step 3: Keep host smoke flow**

Host smoke still does:
- spawn
- create
- load
- wait for `SCRIPT_MESSAGE`

**Step 4: Rebuild tool**

Run:
```powershell
g++ -std=c++17 -I . -I include -I src tools/nook_script_smoke.cpp src/communication/protocol/frame.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/transport/tcp_transport.cpp src/communication/host/host_spawn_client.cpp -lws2_32 -o build/nook_script_smoke.exe
```

Expected:
- PASS

### Task 6: End-to-end Android verification and push

**Files:**
- Modify: `build/android/Android.mk`
- Runtime-related files from previous tasks

**Step 1: Rebuild Android artifacts**

Run:
```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk
```

Expected:
- PASS

**Step 2: Push artifacts**

Push:
- `libs/arm64-v8a/nook-server`
- `libs/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/libnook_agent_script_smoke.so`
- `libs/arm64-v8a/libc++_shared.so`

**Step 3: Device smoke verification**

Server:
```powershell
adb shell "su -c 'NOOK_AGENT_PATH=/data/local/tmp/nook/libnook_agent_script_smoke.so LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

Host:
```powershell
E:\Learn\my_program\all_my_hook\kanxue\Nook\build\nook_script_smoke.exe com.demo.target "send({ type: 'send', payload: 'script-loaded' })" smoke.js 10000 5000
```

Expected:
- `script create ok`
- `script load ok`
- `script message ... script-loaded`
- device log shows runtime-backed execution rather than fake callback response

