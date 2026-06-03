# Nook Interceptor.replace Design

**Goal**

Add a first minimal `Interceptor.replace(target, replacement)` and `Interceptor.revert(target)` pair so scripts can replace one native function with a `NativeCallback` using a Frida-like external API shape.

## Scope

In scope:

- add `Interceptor.replace(target, replacement)`
- add `Interceptor.revert(target)`
- first version supports:
  - `target`: `NativePointer` or pointer string
  - `replacement`: `NativeCallback`
- reuse the existing inline hook implementation and cleanup paths
- track replace state by `target_address`

Out of scope:

- auto-wrapping plain JS functions into `NativeCallback`
- `{ module, symbol }` target form for `replace`
- exposing `original` to the replacement callback
- `Interceptor.flush()`
- `Interceptor.revertAll()`
- argument mutation / retval mutation helpers beyond what the replacement callback already does by returning a native value

## Recommended API

```javascript
const target = Module.getExportByName('libnook-agent.so', 'NookNativeFunctionSmokeAdd');
const replacement = new NativeCallback(function (left, right) {
  return left + right + 1;
}, 'uint32', ['uint32', 'uint32']);

Interceptor.replace(target, replacement);
Interceptor.revert(target);
```

The external shape should already match Frida's mental model even if the first implementation remains narrow internally.

## Behavior

- `replace(target, replacement)` installs one inline hook for `target`
- repeated `replace` for the same `target` throws
- `replacement` must be a `NativeCallback` trampoline pointer created by the current script
- `revert(target)` uninstalls the replacement for that exact target
- `revert(target)` throws if no replacement is installed for that target
- script unload automatically reverts replacements owned by that script

## Architecture

Do not build a second hook engine.

Instead:

1. parse `target` into a concrete runtime address
2. validate `replacement` as a `NativeCallback` trampoline
3. install the replacement through the existing inline hook path
4. record the replace mapping inside the JS runtime by `target_address`
5. on `revert(target)`, look up the mapping and reuse the current unhook path

Internally the implementation may still keep `hook_id` / `hook_handle`, but these stay hidden from the replace API.

## Internal State

Add one per-script replace registry keyed by `target_address`, storing:

- `target_address`
- `replacement_address`
- `hook_id`

This keeps the public API target-centric while still reusing the current hook-centric internals.

## Why This Shape

The user explicitly wants the final experience to be Frida-like.

That means:

- the public API should already be `replace(target, replacement)`
- `revert(target)` should use the same target-centric lookup
- internal hook ids remain implementation detail

This lets future iterations widen capability without changing the user mental model again.

## Testing Strategy

Add runtime tests for:

1. `typeof Interceptor.replace === 'function'`
2. `typeof Interceptor.revert === 'function'`
3. `replace` rejects non-pointer target
4. `replace` rejects non-`NativeCallback` replacement
5. replacing a native test function changes its behavior
6. `revert(target)` restores original behavior
7. replacing the same target twice throws
8. reverting a non-replaced target throws
9. script unload removes replace records and uninstalls the hook

After local tests pass, extend the device smoke with one minimal proof:

1. resolve one stable export
2. replace it with `NativeCallback`
3. call it through `NativeFunction`
4. verify replaced result
5. revert it
6. verify original result

## Relationship To Existing APIs

`Interceptor.attach(...)`, `detach(...)`, and `detachAll()` remain intact.

The first version of `replace/revert` layers on top of the same native hook backend and script-owned cleanup model rather than replacing them.
