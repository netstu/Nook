# Native Hook `readUtf8String()` Hotpath Optimization

Date: 2026-05-03

## Background

During validation of `tests/Test_Lab/nook-frida-labs/frida-0x8/script2.js`, Nook could install and execute the `strcmp` hook, but the target app still showed obvious startup lag or temporary white-screen behavior before the UI became responsive.

The target script is Frida-style and intentionally simple:

```javascript
Interceptor.attach(Module.findExportByName("libc.so", "strcmp"), {
    onEnter: function (args) {
        var arg0 = Memory.readUtf8String(args[0]);
        var flag = Memory.readUtf8String(args[1]);
        console.log("Input", arg0);
        console.log("The flag is", flag);
    }
});
```

The goal was to keep this script unchanged and make Nook's runtime behave more like Frida on this hot path.

## Root Cause

The earlier `ignore-level` optimization removed self-recursive hook amplification, but it did not address the dominant steady-state cost.

The main remaining overhead came from `Memory.readUtf8String(args[i])` inside high-frequency blocking native hook callbacks:

1. `Memory.readUtf8String(pointer)` rebuilt a fresh `NativePointer` wrapper even when `pointer` was already the original hook argument object.
2. `NativePointer.readUtf8String()` always fell back to the generic readable-memory path unless an explicit snapshot had already been attached.
3. On Android/Linux, readable-range lookup could still end up rescanning `/proc/self/maps` on misses, which is too expensive for extremely hot functions like `strcmp`.

## Implemented Optimization

### 1. Preserve hook argument object metadata in `Memory.readUtf8String()`

When `Memory.readUtf8String()` receives an existing `NativePointer` object, it now reuses that object instead of always constructing a new bare pointer wrapper.

Effect:

- preserves hook-argument metadata
- preserves any lazily cached `$utf8` value
- aligns better with Frida-style usage where `args[0]` and `args[1]` are already rich pointer objects

## 2. Add lazy `$utf8` caching on `NativePointer.readUtf8String()`

`NativePointer.readUtf8String()` now:

- checks for an existing `"$utf8"` property first
- returns that cached string directly when available
- stores the decoded UTF-8 string back onto the pointer object after the first successful read

Effect:

- repeated reads of the same hook argument avoid repeating the full memory-read path
- hook argument objects now behave as reusable hot-path carriers for decoded string data

## 3. Add process-wide readable mapping snapshot cache on Android/Linux

Readable mapping lookup was upgraded from a pure thread-local one-range cache to a two-level strategy:

1. thread-local last-hit cache
2. process-wide readable mapping snapshot cache
3. fallback refresh from `/proc/self/maps` only on snapshot miss

Effect:

- avoids repeated full `/proc/self/maps` scans on hot reads
- reduces mapping lookup cost for repeated string reads in blocking hooks
- benefits all native string reads, not only `strcmp`

## Files Changed

- `src/agent_runtime/js_runtime.cpp`
- `tests/communication/test_js_runtime_native_attach.cpp`
- `tests/headers/test_java_hook_runtime_regressions.cpp`

## Validation

### Source-level regression

Verified by structure regression:

- `tests/headers/test_java_hook_runtime_regressions.cpp`

Added assertions to ensure:

- readable mapping snapshot cache exists
- `Memory.readUtf8String()` preserves pointer-object path
- `NativePointer.readUtf8String()` checks `"$utf8"` cache

### Runtime behavior

Validated on-device through the `frida-0x8/script2.js` flow:

- hook install succeeds
- hook output remains correct
- startup and post-entry interaction become smooth enough for practical use

### Build

Confirmed with Android NDK build of `nook_agent` and redeployed to:

- `/data/local/tmp/nook-test/libnook-agent.so`

## Notes

This optimization improves the hot path substantially, but it does not fully eliminate the architectural difference between Nook and Frida:

- Nook still uses blocking synchronous native hook callback dispatch for mutation-capable hooks
- Frida's native instrumentation pipeline is more specialized for very high-frequency hooks

If future hot functions still exhibit lag, the next place to optimize is:

- `JsRuntime::InvokeNativeHookCallbackSync()`
- blocking callback dispatch cost
- potential fast-path specialization for observer-only high-frequency hooks
