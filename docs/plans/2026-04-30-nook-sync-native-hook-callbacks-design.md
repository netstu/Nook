# Nook Sync Native Hook Callbacks Design

## Context

Nook's current native hook pipeline does not match Frida's `Interceptor` execution model.

Today the inline hook entrypoint in `src/agent_runtime/nook_native_js_bridge.cpp` does this:

1. capture register values into a `HookEvent`
2. enqueue the event
3. wait for another thread to run `JsRuntime::DispatchPendingNativeHookEvents()`
4. optionally collect argument / retval mutations

This is enough for deferred callback delivery and mutation, but it breaks Frida-style `onEnter` semantics for transient native arguments. In `frida-0x8`, `strcmp` arguments are valid on the hook thread, but may no longer be readable once the JS callback runs later on a different thread.

Frida does not use this model. In Frida Gum, `onEnter` / `onLeave` JS listeners execute synchronously on the intercepted thread. Gum also keeps per-thread invocation state so `this` and leave-time state line up naturally.

## Goal

Make Nook's native hook callbacks execute synchronously on the hook thread so `Interceptor.attach(..., { onEnter() {} })` can inspect live native arguments the same way Frida does.

## Non-Goals

1. Do not redesign Java hook execution in this change.
2. Do not remove the host-facing `send()` / log event path.
3. Do not perfectly clone all of Frida Gum's internal architecture in one pass.
4. Do not add new public scripting API unless needed for compatibility.

## Approaches Considered

### Option 1: Keep async callback delivery and rely on hook-thread snapshots

Pros:

- lowest-risk patch
- enough for `frida-0x8`

Cons:

- still not Frida semantics
- every transient argument shape would need custom snapshot support
- `args[n].readCString()` would remain fundamentally deferred

### Option 2: Move native hook callback execution onto the intercepted thread

Pros:

- matches Frida semantics where it matters
- fixes transient argument reads at the root cause
- simplifies reasoning about `this.context` and `retval.replace()`

Cons:

- requires JS runtime reentrancy / thread ownership discipline
- higher risk than snapshots-only

### Option 3: Run a dedicated hook-thread JS worker with copied invocation payloads

Pros:

- partially isolates the runtime

Cons:

- still not truly synchronous from the target thread's perspective
- complexity without Frida parity

## Decision

Use Option 2.

## Architecture

### Current problem boundary

The broken semantic boundary is between:

- native interception in `DispatchInlineHookSlot(...)`
- JS callback execution in `JsRuntime::DispatchPendingNativeHookEvents(...)`

The callback is logically part of interception, but is physically delayed until another runtime pump processes queued events.

### New execution model

Native hook callback execution moves into a new synchronous JS runtime entrypoint invoked directly from the intercepted thread.

High-level flow:

1. inline hook captures arguments and invocation metadata
2. inline hook calls `JsRuntime::InvokeNativeHookCallbackSync(...)` for `kEnter`
3. JS callback runs immediately on the intercepted thread under runtime lock
4. argument mutations are collected immediately
5. original function executes
6. inline hook calls `JsRuntime::InvokeNativeHookCallbackSync(...)` for `kLeave`
7. return-value mutation is applied immediately

### Invocation state

Nook already has `active_native_invocations` keyed by `script_id` and `invocation_id`. This can remain as the minimal persistence layer for sharing receiver state between enter and leave. We do not need a full Frida-style listener data stack in the first pass as long as:

1. synchronous enter installs the receiver
2. synchronous leave reuses and refreshes that receiver
3. cleanup happens immediately after leave

### Locking model

QuickJS remains single-runtime, single-context. The safe minimal model is:

1. reuse `RuntimeState::runtime_mutex` as the exclusive execution gate
2. intercepted threads acquire that lock before running JS callback code
3. timer / RPC / recv / other runtime entrypoints continue to use the same lock

This serializes all JS execution and preserves correctness. It also matches the practical cost model of Frida native hooks: heavy JS in `onEnter` stalls the intercepted thread.

### Event queue after the change

The hook event queue stops being the primary native callback execution path.

It may still be used for:

1. existing tests that explicitly validate queue helpers
2. hook-status notifications
3. possible future observer / deferred modes

But normal `Interceptor.attach()` and `Nook.Native.attach()` callback delivery should no longer depend on `DispatchPendingNativeHookEvents()`.

## Component Changes

### `src/agent_runtime/js_runtime.cpp`

Add a synchronous runtime entrypoint that:

1. finds the registered callback record for a hook id
2. creates or reuses the invocation receiver
3. builds `args` or `retval`
4. executes the JS callback immediately
5. captures mutations
6. tears down leave-time invocation state

Refactor existing helper logic from `DispatchPendingNativeHookEvents()` so the synchronous path and old queue path reuse the same building blocks.

### `src/agent_runtime/nook_native_js_bridge.cpp`

Replace the async enqueue + wait model inside `DispatchInlineHookSlot(...)` with direct calls into the synchronous JS runtime hook entrypoint.

Keep hook-status notifications and module-load pending hook activation behavior unchanged.

### `src/agent_runtime/nook_native_js_bridge.h`

If needed, reduce the surface area of queue-only helpers and expose only the bridge/runtime calls still used by tests or status delivery.

## Testing Strategy

### Existing behavior to preserve

1. `args[n].replace(...)` still mutates register values
2. `this.context.x0 = ...` still mutates register values
3. `retval.replace(...)` still changes the return value
4. enter/leave share the same `this`

### New behavior to validate

1. `onEnter` executes before original function returns without needing a runtime pump
2. transient string arguments can be read synchronously on the hook thread
3. no extra `DispatchPendingNativeHookEvents()` call is required for installed inline hooks

### Scope of device validation

Primary real-device validation is `tests/Test_Lab/nook-frida-labs/frida-0x8/script.js`.

Expected outcome:

- no `readUtf8String unreadable pointer`
- no crash
- flag-bearing `strcmp` pair printed from `onEnter`

## Risks

1. Deadlock if callback code re-enters a runtime entrypoint that expects a different threading model
   - mitigated by continuing to use the recursive runtime lock already present
2. Throughput regression under hot hooks
   - accepted; same practical tradeoff exists in Frida
3. Mixed semantics between sync and queued paths during transition
   - mitigated by routing normal native hooks to only one path

## Follow-up

After this lands and is stable:

1. remove or narrow obsolete queue-only native callback machinery
2. consider a true per-thread invocation stack abstraction if recursive hook depth becomes a problem
3. re-evaluate whether observer mode should stay queued or also become synchronous
