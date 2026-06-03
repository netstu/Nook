# Nook Sync Native Hook Callbacks Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Nook execute native `onEnter` / `onLeave` callbacks synchronously on the intercepted thread so Frida-style live argument inspection works.

**Architecture:** Refactor the current queued native callback machinery so the inline hook bridge calls a new synchronous JS runtime hook entrypoint directly. Reuse the existing receiver / mutation helpers where possible and keep hook-status delivery unchanged.

**Tech Stack:** C++17, QuickJS, Android inline hook bridge, existing Nook JS runtime helpers

---

### Task 1: Add a synchronous native hook JS runtime entrypoint

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add or adapt a native hook test so invoking the installed hook directly succeeds without calling `JsRuntime::DispatchPendingNativeHookEvents()`.

**Step 2: Run test to verify it fails**

Use the existing local/native test workflow available for this file, or verify by reasoning if host build is blocked by Android-only dependencies.

**Step 3: Write minimal implementation**

Add a synchronous helper that:

1. locates the hook callback record by `hook_id`
2. creates / reuses the invocation receiver
3. invokes `onEnter` or `onLeave` immediately
4. captures mutations
5. cleans up the active invocation on leave

**Step 4: Verify helper integration**

Confirm the old queued path still compiles and the new helper can be called from the hook bridge.

### Task 2: Route inline hook execution through the synchronous path

**Files:**
- Modify: `src/agent_runtime/nook_native_js_bridge.cpp`
- Modify: `src/agent_runtime/nook_native_js_bridge.h`

**Step 1: Replace enter callback queue/wait flow**

Change `DispatchInlineHookSlot(...)` so enter uses the new runtime helper directly instead of enqueue + wait.

**Step 2: Replace leave callback queue/wait flow**

Change leave handling to call the same runtime helper synchronously and apply return-value mutations immediately.

**Step 3: Keep non-callback queues intact only where still needed**

Preserve hook-status and other unrelated queue users.

### Task 3: Preserve current mutation and shared-`this` semantics

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Test: `tests/communication/test_js_runtime_native_attach.cpp`

**Step 1: Reuse existing receiver/mutation helpers**

Ensure `args[n].replace(...)`, `this.context.x0 = ...`, and `retval.replace(...)` still work.

**Step 2: Verify `this` persists across enter/leave**

Keep `active_native_invocations` or equivalent state so enter/leave share the same receiver object.

### Task 4: Build and deploy Android artifacts

**Files:**
- Use: `obj/local/arm64-v8a/libnook-agent.so`
- Use: `libs/arm64-v8a/nook-server`
- Modify generated asset: `server/generated/nook_embedded_agent_blob.h`

**Step 1: Build `nook_agent`**

Run the static-runtime agent build.

**Step 2: Refresh embedded blob**

Regenerate `server/generated/nook_embedded_agent_blob.h` from the rebuilt agent.

**Step 3: Build `nook_server`**

Rebuild the server so direct launch uses the new embedded agent.

**Step 4: Push both artifacts to `/data/local/tmp/nook-test`**

Push server and standalone agent copy for easier inspection.

### Task 5: Device validation

**Files:**
- Use: `tests/Test_Lab/nook-frida-labs/frida-0x8/script.js`

**Step 1: Start `/data/local/tmp/nook-test/nook-server`**

Ensure the device is running the newly built server.

**Step 2: Re-run the Frida lab**

Run:

```powershell
nook-cli -U com.ad2001.frida0x8 -l .\tests\Test_Lab\nook-frida-labs\frida-0x8\script.js
```

**Step 3: Expected outcome**

Expect:

1. no `readUtf8String unreadable pointer`
2. no crash
3. `lab:frida-0x8:hit:input=Hello:secret=<flag>`
