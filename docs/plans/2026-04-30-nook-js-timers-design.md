# Nook JS Timers Design

## Goal

Add Frida-style global timer APIs to the QuickJS runtime used by Nook scripts:

- `setImmediate(fn, ...args)`
- `setTimeout(fn, delay, ...args)`
- `setInterval(fn, delay, ...args)`
- `clearTimeout(id)`
- `clearInterval(id)`

The target is behavioral compatibility for normal Frida scripts, not a full browser or Node.js event loop.

## Scope

This design only covers runtime-local timers inside `src/agent_runtime/js_runtime.cpp`.

Included:

- Function-only timer callbacks
- Numeric timer handles
- One-shot and repeating timers
- Auto-cleanup on script unload and runtime shutdown
- Integration with existing QuickJS pending-job draining

Excluded:

- String source evaluation timers
- `queueMicrotask`
- Promise-specific scheduling changes
- Cross-thread JS execution
- High-precision wakeup guarantees

## Current State

The runtime already exposes globals such as `send`, `recv`, `hexdump`, `console`, `Java`, and native helpers, but does not register any timer APIs. The runtime also already drains QuickJS pending jobs through `ExecutePendingJobsLocked()` and `DrainWeakBindingMaintenanceLocked()`.

This means the current architecture can execute queued JavaScript work safely when the host enters the runtime, but it cannot schedule future work on its own.

## Recommended Approach

Implement a lightweight timer scheduler inside `RuntimeState` and run it at existing safe points:

- after script evaluation
- after RPC calls
- after inbound message dispatch
- during explicit waits introduced for timer progress in tests and later host integration

The timer scheduler should be script-aware so timers are automatically removed when the owning script unloads.

## Runtime Model

Each timer record should contain:

- timer id
- owning script id
- callback `JSValue`
- captured argument list
- due time based on `std::chrono::steady_clock`
- interval duration in milliseconds
- repeating flag
- canceled flag

The runtime should maintain:

- a timer map keyed by timer id
- a monotonic counter for next timer id

Sorting can be kept simple at first. A linear scan is acceptable for the current scale because scripts normally schedule only a small number of timers.

## Semantics

### `setImmediate`

- Reject non-function callback with `TypeError`
- Schedule asynchronous one-shot execution
- Equivalent to zero-delay one-shot timer, but still not inline

### `setTimeout`

- Reject non-function callback with `TypeError`
- Coerce delay to integer milliseconds
- Negative delays are clamped to zero
- Return numeric handle

### `setInterval`

- Same validation and delay coercion as `setTimeout`
- Re-schedule after each successful callback execution until canceled

### `clearTimeout` / `clearInterval`

- Accept numeric handle
- Missing or unknown handle is a no-op
- Either clear function may cancel either timer type

## Execution and Error Handling

Timer callbacks should execute under the owning script id using the existing `ScopedCurrentScriptId`.

If a callback throws:

- surface the exception through the existing error path
- stop the failing timer from repeating again
- avoid crashing the native runtime

After each callback, the runtime should run `ExecutePendingJobsLocked()` so Promise jobs and finalizers can continue to drain in the same way as existing runtime work.

## Cleanup

When unloading a script:

- remove all timer records owned by that script
- free callback and argument `JSValue`s

When shutting down the runtime:

- free every remaining timer record before destroying the QuickJS context

## Testing Strategy

Add focused tests in `tests/communication/test_js_runtime_native_attach.cpp` for:

1. binding existence
2. non-function rejection
3. `setImmediate` executes asynchronously
4. `setTimeout(0)` executes asynchronously
5. `clearTimeout` prevents execution
6. `setInterval` repeats
7. `clearInterval` stops repeating
8. script unload clears pending timers

Tests should use existing RPC-driven inspection patterns and avoid device dependencies.

## Tradeoffs

Why not use QuickJS libc timers:

- current runtime does not use the libc event loop
- mapping that in would still require native scheduling glue
- it would add another scheduling model instead of integrating with the one Nook already controls

Why not rely on the host:

- timer semantics should remain local to the script runtime
- host-driven scheduling would make behavior depend on external polling traffic
- this would diverge from Frida's script model

## Next Step

Implement via TDD:

1. add failing tests
2. add timer records and cleanup
3. register global timer bindings
4. add timer draining
5. verify with targeted tests
