# Nook NativeCallback Design

**Goal**

Add a first minimal `NativeCallback` API so JavaScript can expose a JS function as a callable native function pointer, forming the foundation needed for later `Interceptor.replace(...)`.

## Scope

In scope:

- add global `NativeCallback`
- support `new NativeCallback(fn, returnType, argTypes)`
- return a pointer-like object that can be passed into `NativeFunction`
- first implementation targets the same minimal synchronous subset already supported by `NativeFunction`
- support a minimal type set:
  - return types: `void`, `int`, `uint32`, `pointer`
  - argument types: `int`, `uint32`, `pointer`
- callbacks are owned by the creating script and are released on script unload

Out of scope:

- `Interceptor.replace`
- explicit `dispose()`
- floating-point types
- `int64` / `uint64`
- variadic callbacks
- ABI selection
- cross-script reuse
- advanced concurrency guarantees beyond the current minimal runtime model

## Recommended API

```javascript
const cb = new NativeCallback(function (left, right) {
  return left + right;
}, 'uint32', ['uint32', 'uint32']);
```

```javascript
const invoke = new NativeFunction(cb, 'uint32', ['uint32', 'uint32']);
const result = invoke(7, 35);
```

The constructor result should behave like a native pointer value for the current runtime surface.

## Behavior

- `fn` must be a function
- `returnType` must be one of the supported strings
- `argTypes` must be an array of supported type strings
- construction returns a pointer-like object referencing a generated trampoline
- callback invocation converts native arguments into JS values using the minimal type set
- JS return values are converted back into native values using the same minimal type set
- `void` returns ignore the JS value and return no value natively
- callbacks are invalid after their owning script unloads

## Architecture

Keep the first slice small and compatible with later `replace` work:

1. add a per-script native callback registry in the runtime state
2. `NativeCallback(...)` allocates:
   - a `callback_id`
   - a tiny trampoline descriptor
   - a registry record containing the JS function and signature metadata
3. construction returns a `NativePointer` wrapping the trampoline address
4. trampoline calls route into one runtime-owned dispatcher
5. dispatcher resolves the `callback_id`, restores the owning script context, invokes JS, marshals the return value, and hands it back to native code

The first version should not expose a separate callback object model if a `NativePointer` return shape is sufficient. That keeps this phase aligned with current APIs and simplifies later use from `NativeFunction` and `Interceptor.replace`.

## Lifecycle

The first version should be conservative:

- callback records are keyed by `script_id`
- `ScriptRegistry::UnloadScript(...)` cleanup should release all callback records for that script
- runtime shutdown should release all remaining callback records
- no user-visible manual disposal API yet

This is enough for the intended first validation path and avoids premature lifecycle complexity.

## Threading

The later `replace` path will eventually need careful threading guarantees. For this phase, keep the callback path intentionally narrow and synchronous.

Use the same runtime ownership discipline already present in the current native-hook bridge:

- recover the owning script context before invoking JS
- never leave callback registry entries detached from their script ownership
- keep the first implementation targeted at the controlled `NativeFunction` -> callback roundtrip path

## Testing Strategy

Add runtime tests for:

1. `typeof NativeCallback === 'function'`
2. constructor rejects non-function `fn`
3. constructor rejects unsupported return types
4. constructor rejects unsupported argument types
5. a `uint32(uint32, uint32)` callback can be called through `NativeFunction`
6. a `pointer(pointer)` callback round-trips a `NativePointer`
7. a `void(uint32)` callback can mutate test state and returns `undefined` to JS when invoked via `NativeFunction`
8. unloading the script clears callback records owned by that script

After local tests pass, extend the device smoke with one self-contained roundtrip:

1. construct `NativeCallback`
2. wrap it with `NativeFunction`
3. call it
4. `send(...)` the result

## Why This Order

`NativeCallback` should land before `Interceptor.replace` because `replace` depends on a solid callback-to-trampoline bridge.

Once this minimal callback base works, the next step is:

1. accept `NativeCallback` in `Interceptor.replace(...)`
2. widen type coverage only after the control path is proven
