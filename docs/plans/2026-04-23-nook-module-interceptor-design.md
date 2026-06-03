# Nook Module + Interceptor Design

**Goal**

Add the first Frida-style low-level JS API pair:

```javascript
var target = Module.findExportByName("libdemo.so", "target_func");
var hook = Interceptor.attach(target, {
  onEnter(args) {},
  onLeave(retval) {},
});
```

This should coexist with the current declarative API:

```javascript
Nook.Native.attach({ type: "inline", module, symbol, onEnter, onLeave });
```

## Why This Next

The current Native JS bridge already proves three hard parts:

- JS can request a native inline hook
- deferred install works when the module is not loaded yet
- hook callbacks can cross back into QuickJS safely

What is still missing is the more Frida-like workflow:

1. find one runtime address from JS
2. attach by address

That is the smallest next capability that makes the scripting model feel less custom and more reusable.

## Approaches

### Approach 1: Keep only `Nook.Native.attach({ module, symbol })`

Pros:

- no new global API surface
- minimal new code

Cons:

- still forces one Nook-specific declarative shape
- does not unlock address-based composition
- not a good base for later `replace`, `detachAll`, or `NativeFunction`

### Approach 2: Add `Module.findExportByName()` + `Interceptor.attach(address, callbacks)`

Pros:

- closest to the Frida mental model without overbuilding
- cleanly separates address discovery from hook install
- reuses the current native-js callback and event-dispatch path
- later features can layer on top naturally

Cons:

- adds two new JS globals
- requires one new address-based install path in the bridge

### Approach 3: Jump straight to `NativePointer` / `Memory` / full Frida object model

Pros:

- most familiar end state

Cons:

- much larger surface
- forces pointer representation decisions too early
- slows current progress for little immediate value

## Recommendation

Choose **Approach 2**.

Keep the first version intentionally narrow:

- `Module.findExportByName(moduleName, symbolName)`
- `Interceptor.attach(address, { onEnter, onLeave })`

Do not add:

- `replace`
- `detachAll`
- `NativePointer`
- `Memory`
- argument mutation
- return-value replacement

## First-Version Semantics

### `Module.findExportByName(moduleName, symbolName)`

Input:

- `moduleName`: string
- `symbolName`: string

Output:

- returns a hex string like `"0x7f12345678"` when found
- returns `null` when not found

First version keeps `moduleName` required. It does not yet support the Frida-style `null` global search form.

### `Interceptor.attach(address, callbacks)`

Input:

- `address`: hex string returned by `Module.findExportByName(...)`
- `callbacks.onEnter(args)`
- `callbacks.onLeave(retval)`

Output:

```javascript
{ ok: true, hookId: 1, deferred: false }
```

Notes:

- address-based attach is always immediate in this first version
- no deferred behavior for raw address targets
- callback payload format stays identical to the current native-js bridge

## Bridge Design

The existing `src/agent_runtime/nook_native_js_bridge.*` stays the core.

Extend it so one hook request may be expressed in either of two ways:

- `module_name + symbol_name`
- `target_address`

The current slot/event/callback system should remain shared.

That means:

- `Nook.Native.attach(...)` continues using module + symbol
- `Interceptor.attach(...)` uses address
- both end up producing the same `hookId`
- both use the same enter/leave dispatch path

## JS Runtime Design

Add two new global objects:

- `Module`
- `Interceptor`

Bindings:

- `Module.findExportByName`
- `Interceptor.attach`

Implementation strategy:

- keep all binding work inside `src/agent_runtime/js_runtime.cpp`
- do not introduce a new host/device protocol dependency
- keep return values simple JSON-like JS objects and strings

## Testing Strategy

### JS Runtime Tests

Add tests for:

- `typeof Module.findExportByName === "function"`
- `typeof Interceptor.attach === "function"`
- export lookup success returns one hex string
- export lookup miss returns `null`
- `Interceptor.attach` validates target and callbacks
- address attach success returns `{ hookId, deferred:false }`

### Bridge Tests

Add tests for:

- installing one address-based native-js hook
- unhook on script unload still works for address-installed hooks

## Success Criteria

The feature is complete when:

1. JS can resolve an exported symbol into one address string
2. JS can attach one inline hook by that address
3. enter/leave callbacks behave the same as the current native bridge
4. unload still cleans the installed address-based hook
