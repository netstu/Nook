# Nook Inline Deferred Install Design

**Date:** 2026-04-19

**Status:** Approved

## Goal

Replace the current polling-based inline symbol hook install flow with an event-driven design that registers pending inline hook requests and installs them when the target module is loaded.

The first target is the existing arm64 inline hook flow used by:

- `examples/native_hook/nook_native_verify_password_inline_test`

## Current Problem

`NookInlineHookSymbol()` only succeeds when the target module is already loaded.

The current example payload works by:

1. starting a detached thread
2. retrying up to 300 times
3. sleeping 200 ms between attempts
4. stopping only when the target module eventually appears

This is workable for a demo, but it has obvious problems:

- unnecessary wakeups and latency
- payload-side logic is responsible for runtime timing
- duplicate retry code will spread across payloads
- hook install timing is not coupled to actual module load events

## Reference Pattern

Mature Android inline hook frameworks generally do not rely on blind user-side retry loops as the primary mechanism.

The common pattern is:

1. register the desired hook request
2. observe linker load activity
3. when a matching module is loaded, resolve the symbol and install the hook

Some frameworks use linker internals directly. For Nook, the practical first step is to observe:

- `dlopen`
- `android_dlopen_ext`

This keeps the implementation understandable and aligned with the current Nook architecture.

## Approaches Considered

### Approach 1: Keep polling in payloads

Pros:

- smallest code change
- no framework runtime changes

Cons:

- wrong ownership boundary
- repeated wakeups
- every payload must reinvent install timing
- still races with module load timing

### Approach 2: Internal deferred registry plus `dlopen` observer

Pros:

- event-driven for the common case
- payload becomes declarative
- keeps Nook architecture small and first-party
- can be tested in layers

Cons:

- needs internal state management
- needs recursion protection around loader callbacks

### Approach 3: Hook linker internal callbacks directly

Pros:

- closest to what some mature frameworks do
- potentially broader coverage than `dlopen`

Cons:

- higher maintenance cost
- more Android-version-sensitive
- larger jump in complexity for current Nook stage

## Recommendation

Adopt **Approach 2**.

This gives Nook the right architectural boundary now:

- payload registers intent
- framework owns timing
- runtime reacts to load events

while avoiding an early dependency on unstable linker internals.

## Public API

Keep the existing immediate APIs unchanged:

```c
NookStatus NookInlineHookAddress(void* target_address,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle);

NookStatus NookInlineHookSymbol(const char* module_name,
                                const char* symbol_name,
                                void* replacement,
                                void** original,
                                void** hook_handle);
```

Add a new explicit deferred API:

```c
NookStatus NookInlineHookSymbolDeferred(const char* module_name,
                                        const char* symbol_name,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle);
```

Behavior:

1. validate arguments
2. ensure inline runtime is initialized
3. try immediate symbol hook once
4. if the symbol is not currently available, register a pending request
5. return `NOOK_STATUS_OK` once the request is registered successfully

Caller contract:

- `original` and `hook_handle` storage must remain valid until install occurs
- global/static storage is the intended usage pattern

This keeps immediate and deferred semantics explicit and avoids surprising behavior changes to `NookInlineHookSymbol()`.

## Internal Architecture

Split the feature into two layers.

### 1. Pending Inline Hook Registry

New internal component under `src/native_hook/inline_hook/`:

- stores pending symbol hook requests
- matches requests against loaded module paths
- tries install when a matching module appears
- marks entries installed once successful

Record fields:

- module name
- symbol name
- replacement pointer
- caller-provided `original` storage
- caller-provided `hook_handle` storage
- installed flag

Design requirements:

- thread-safe
- idempotent install attempts
- no duplicate installs for the same record
- host-testable through dependency injection

### 2. Android Module Load Observer

New internal Android-specific component:

- resolves `dlopen` and `android_dlopen_ext`
- installs inline hooks on them once per process
- after successful load, notifies the pending registry

Observer callback flow:

1. original loader function is called first
2. if load succeeded and filename is non-null, registry is notified
3. registry tries to install pending hooks matching that module path

Safety requirements:

- one-time install
- thread-local reentrancy guard
- tolerate repeated notifications
- work even if only one of the loader symbols can be hooked

## Fallback Policy

Deferred install should be **event-driven first**.

Finite polling can remain only as an explicit payload-level fallback if a user wants it, but it should no longer be the primary framework story.

The framework deferred path itself should not spin in a background loop.

## Error Handling

Use existing public statuses conservatively:

- invalid input -> `NOOK_STATUS_INVALID_ARGUMENT`
- immediate install success -> `NOOK_STATUS_OK`
- deferred registration success -> `NOOK_STATUS_OK`
- registry/observer setup failure -> `NOOK_STATUS_INTERNAL_ERROR`

The current status enum does not expose a dedicated "pending" state, so successful registration remains `NOOK_STATUS_OK`.

## Testing Strategy

### Host Tests

- pending registry installs only matching entries
- already-installed entries are skipped
- failed install leaves entry pending for later retry
- invalid registration arguments fail cleanly

### Android Verification

Update `nook_native_verify_password_inline_test` to use:

- `NookInlineHookSymbolDeferred(...)`

Expected runtime behavior:

- constructor registers hook request immediately
- no detached retry thread
- hook is installed when `libnative-lib.so` loads
- login path hits replacement and returns `JNI_TRUE`

## Non-Goals

This change does not yet attempt:

- linker internal callback integration
- arm32 support
- automatic retry threads inside the framework
- generalized deferred address hooks

