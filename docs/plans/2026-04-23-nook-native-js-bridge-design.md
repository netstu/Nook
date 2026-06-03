# Nook Native JS Bridge Design

**Goal**

Add a first minimal JavaScript bridge for native hook installation so a QuickJS script can request an inline hook through:

```javascript
Nook.Native.attach({
  type: "inline",
  module: "libtarget.so",
  symbol: "target_func",
  onEnter(args) {},
  onLeave(retval) {}
});
```

**Context**

Nook already has three key building blocks:

1. a working agent-side QuickJS runtime with:
   - `send(...)`
   - `recv(...)`
   - `rpc.exports`
2. a working host/server/agent communication path
3. a working native hook framework with:
   - `NookInlineHook`
   - `NookPltHook`
   - `NookNativeHook`

What is still missing is the bridge between these systems. Right now scripts can send messages and expose RPC methods, but they cannot ask Nook to install real hooks from JavaScript.

If Nook is meant to evolve toward a Frida-like experience, this is the next meaningful step: let scripts drive hook installation directly.

## Scope

This design is intentionally small.

In scope:

- add `Nook.Native.attach(options)` to the QuickJS runtime
- first version supports only `type: "inline"`
- target resolution by `module + symbol`
- register JS `onEnter` and `onLeave` callbacks
- invoke JS callbacks when the native hook fires
- allow callbacks to use existing `send(...)`

Out of scope:

- `type: "plt"` implementation
- direct address hook from JS
- replace semantics
- unhook handle exposed to JS
- argument or return-value modification
- `NativePointer`, `Module`, `Memory` object model
- Frida compatibility wrappers

## Recommended API

First version:

```javascript
Nook.Native.attach({
  type: "inline",
  module: "libtarget.so",
  symbol: "target_func",
  onEnter(args) {
    send({ type: "log", payload: "enter:" + args[0] });
  },
  onLeave(retval) {
    send({ type: "log", payload: "leave:" + retval });
  }
});
```

Return value:

```javascript
{ ok: true, hookId: 1 }
```

Failure mode:

- invalid input or installation failure raises a JS exception

### Callback Data Model

Keep the callback payload minimal:

- `onEnter(args)`
  - `args` is a JS array
  - first version contains up to the first 8 register arguments
  - each value is a stringified pointer/integer like `"0x7f12345678"`
- `onLeave(retval)`
  - `retval` is a single stringified pointer/integer

This is intentionally less ambitious than Frida. The first goal is to prove:

- installation works
- events can safely cross from hook callback to JS
- JS can observe and report them

It is not yet trying to provide a complete typed ABI surface.

## Why A Unified `attach(options)` API

The first version could expose separate APIs such as:

- `inlineHookSymbol(module, symbol, callbacks)`
- `pltHookSymbol(module, symbol, callbacks)`

That would work, but it would hard-code the first public JS shape too early.

Using `attach(options)` instead gives Nook a more future-proof core:

- first version supports only `type: "inline"`
- later versions can add `type: "plt"`
- later versions can add `address`
- convenient aliases can always be added later without breaking the core contract

So `attach(options)` should be the base API, while `inlineHookSymbol(...)` and `pltHookSymbol(...)` can remain future sugar layers.

## Runtime Execution Model

The most important design decision is:

**Do not call QuickJS directly inside the hook trampoline or native hook callback.**

### Why

Native hooks may fire:

- on arbitrary application threads
- at hot call sites
- at times when re-entrant runtime work is dangerous

QuickJS execution is runtime-thread-sensitive. Directly invoking JS from arbitrary hook threads is the fastest route to crashes, deadlocks, and hard-to-debug corruption.

### Recommended Model

Use a two-stage event flow:

1. hook fires in native code
2. native callback creates a compact `HookEvent`
3. event is pushed into an agent-runtime queue
4. runtime-owned dispatcher drains the queue
5. dispatcher invokes the registered JS callback on the runtime thread

This mirrors the same kind of boundary discipline already used elsewhere in the communication/runtime stack and is the only sane first version.

## Architecture

The bridge should stay agent-local and not require protocol changes.

### Layer 1: QuickJS Binding

Add `Nook.Native.attach` in the JS runtime bootstrap.

Responsibilities:

- create global `Nook` object if missing
- create `Nook.Native` object
- parse `attach(options)`
- validate:
  - `type`
  - `module`
  - `symbol`
  - callback presence and function type
- call into a C++ bridge helper
- store JS callback references keyed by `hookId`

### Layer 2: Native Hook JS Bridge

Add a small bridge module in `src/agent_runtime/`, for example:

- `nook_native_js_bridge.h`
- `nook_native_js_bridge.cpp`

Responsibilities:

- convert validated JS request into native hook install request
- call `NookInlineHookSymbol(...)`
- allocate internal `hookId`
- maintain a registry:
  - `hookId`
  - hook type
  - target module
  - target symbol
  - native hook handle
  - original function pointer if relevant

The bridge layer should not know about host communication. It only mediates between runtime and hook framework.

### Layer 3: Hook Event Queue

Add an internal queue that stores compact events:

- `hookId`
- `phase` (`enter` / `leave`)
- `threadId`
- `args[0..7]` or `retval`

This queue must be thread-safe because hook callbacks can occur on arbitrary threads.

### Layer 4: Runtime Dispatcher

Add a dispatcher in the runtime layer that:

- drains queued `HookEvent`s
- looks up JS callbacks by `hookId`
- converts native values to JS strings
- invokes `onEnter(args)` or `onLeave(retval)`

Errors in JS callbacks:

- should be logged
- may optionally emit `send({ type: "error", ... })`
- must not break the target process execution path

## Why Inline First

First version should support only:

- `type: "inline"`

Reasons:

- it is closer to Frida's `Interceptor.attach` mental model
- it covers more useful cases than PLT-only interception
- Nook already treats inline hook as a major capability
- it keeps the first bridge implementation focused

If users pass:

```javascript
{ type: "plt", ... }
```

the bridge should clearly raise:

- `not implemented yet`

That keeps the API shape stable while avoiding fake support.

## Data Representation Details

First version should not try to infer argument types.

Recommended representation:

- native `uint64_t` / pointer values are converted to hex strings
- JS receives strings only

This avoids:

- `double` precision loss for 64-bit values
- premature pointer object design
- ABI/type-system complexity

Later, when a `NativePointer` abstraction exists, the dispatcher can return objects instead of strings without changing the fundamental bridge architecture too much.

## Lifecycle And State

Each script runtime instance should own its JS callback table.

Important cleanup rules:

- unloading a script must release all JS callback references associated with its hooks
- shutting down the runtime must clear the callback registry
- hook installation state must not leave dangling JS values after unload

The first version may keep native hooks installed for process lifetime if clean uninstall is not yet ready, but the JS-side references must still be cleaned correctly. Ideally the bridge should also keep enough bookkeeping to support future unhook.

## Error Handling

Validation errors should become JS exceptions such as:

- `attach options must be an object`
- `attach type must be 'inline'`
- `attach module is required`
- `attach symbol is required`
- `attach onEnter must be a function`

Installation errors should include hook-layer failure context such as:

- `inline hook install failed: symbol not found`
- `inline hook install failed: internal error`

Callback execution errors:

- log stack/error text
- do not crash target
- do not automatically uninstall the hook

## Testing Strategy

### C++ Unit Tests

Add tests for:

- JS binding input validation
- `attach(options)` success path with fake installer
- callback registry creation
- hook event queue push/pop
- dispatcher calling the correct JS callback
- callback exception path

Use dependency injection or test-only bridge seams wherever possible. Do not require a real native hook install in pure unit tests.

### JS Runtime Tests

Add QuickJS-focused tests for:

- `Nook.Native.attach` exists
- invalid options raise exceptions
- successful attach returns `{ ok: true, hookId }`
- runtime dispatcher invokes `onEnter` / `onLeave`

### Device Smoke

Use a known native test target already present in the project.

Smoke script shape:

```javascript
Nook.Native.attach({
  type: "inline",
  module: "libtarget.so",
  symbol: "target_func",
  onEnter(args) {
    send({ type: "send", payload: "enter:" + args[0] });
  },
  onLeave(retval) {
    send({ type: "send", payload: "leave:" + retval });
  }
});
```

Validate:

- script loads successfully
- hook installs successfully
- function hit produces `send(...)` traffic to host
- both enter and leave callbacks are observed

## Files Expected To Change

Primary files:

- `src/agent_runtime/js_runtime.cpp`
- `src/agent_runtime/js_runtime.h` if new public runtime helpers are needed
- `src/agent_runtime/nook_script_runtime_bridge.cpp` if runtime bootstrap wiring belongs there

New bridge files:

- `src/agent_runtime/nook_native_js_bridge.h`
- `src/agent_runtime/nook_native_js_bridge.cpp`

Likely touched native hook headers only if minimal bridge-facing glue is needed:

- `include/nook/NookInlineHook.h`
- `src/framework/NookInlineHook.cpp`

Tests:

- `tests/communication/test_js_runtime_rpc.cpp` only if reused structure makes sense
- new runtime/bridge tests such as:
  - `tests/communication/test_js_runtime_native_attach.cpp`
  - `tests/communication/test_native_js_bridge.cpp`

## Success Criteria

The feature is complete when:

1. QuickJS exposes `Nook.Native.attach(options)`
2. `type: "inline"` install requests succeed for valid `module + symbol`
3. JS receives `onEnter` and `onLeave` callbacks for hook hits
4. callbacks can call existing `send(...)`
5. callback exceptions do not crash the target
6. unit tests cover validation, registration, dispatch, and error paths
7. a device smoke test confirms end-to-end native-hook-to-JS flow
