# Nook Architecture

## Positioning

Nook is an Android hook framework that keeps three hook capabilities in one codebase:

- Java Hook
- PLT Hook
- Inline Hook

The current implementation target is `arm64-v8a`, with Android NDK builds as the main supported workflow.

## Major Layers

- `include/nook/`
  - public headers and stable framework-facing API surface
- `src/framework/`
  - exported entrypoints such as `NookInlineHook*`, `NookPltHook*`, `NookJavaHook*`, `NookComm*`
- `src/native_hook/core/`
  - native hook shared helpers such as module enumeration, symbol resolution, and runtime patch support
- `src/native_hook/plt_hook/`
  - PLT/GOT relocation patch path
- `src/native_hook/inline_hook/`
  - arm64 inline patching, trampoline allocation, pending inline registry, and Android module-load observer
- `src/java_hook/`
  - Java Hook runtime plus deferred Java hook observer path
- `src/agent_runtime/`
  - QuickJS runtime, script registry, script runtime bridge, and the Native JS bridge
- `src/communication/`
  - host-agent protocol, transports, sessions, and message dispatch
- `server/`
  - `nook-server` process, host request handling, spawn/attach helpers

## Current API Strategy

Public entrypoints are split by capability instead of forcing one opaque native hook facade:

- `NookJavaHook*`
- `NookPltHook*`
- `NookInlineHook*`
- `NookNativeHook*`

Important:

- `NookInlineHook*` is the real inline hook surface
- `NookNativeHook*` currently routes to the PLT hook path only
- if the caller wants inline behavior today, it should call `NookInlineHook*` directly

## Inline Hook Architecture

The current inline hook path has two modes:

- immediate address or symbol install
- deferred symbol install

Immediate path:

1. resolve the target address
2. relocate overwritten instructions
3. allocate trampoline
4. patch target entry
5. keep one hook record for unhook support

Deferred path:

1. store one pending hook request
2. start the Android linker observer
3. detect target module load through `soinfo::call_constructors()`
4. resolve the symbol inside the now-loaded module
5. install the real inline hook

`libnook_inline_observer_probe.so` is part of this design. It is used only by the deferred observer to safely derive `soinfo` field offsets on the running device.

## Native JS Bridge

The agent runtime exposes a first minimal JS-side native hook API:

```javascript
Nook.Native.attach({
  type: "inline",
  module: "libnative-lib.so",
  symbol: "Java_com_demo_target_LoginFragment_verifyPasswordNative",
  onEnter: function (args) {},
  onLeave: function (retval) {},
});

var address = Module.findExportByName("libnative-lib.so", "Java_com_demo_target_LoginFragment_verifyPasswordNative");
var next = address.add(4);
var listener = Interceptor.attach(address, {
  onEnter: function (args) {
    args[0].add(4);
  },
  onLeave: function (retval) {
    retval.isNull();
  },
});
listener.detach();
```

Current behavior:

- only `type: "inline"` is implemented
- hook events are queued on the native side and later dispatched on the JS runtime thread
- `Nook.Native.attach(...)` supports `module + symbol`, including deferred install
- `ModuleMap` is available as a snapshot helper with `has/find/get/values/update`
- `Module.enumerateModules()` returns minimal loaded-module objects `{ name, base, size, path }[]`
- `Module.load(name)` performs a basic `dlopen/LoadLibrary` and returns a loaded module object
- `Module.ensureInitialized(name)` currently acts as a conservative loaded-module readiness check and returns `undefined` for already loaded modules
- `Module.enumerateImports(name)` returns the current loaded import/relocation subset as `{ type, name, module, slot, address }[]`
- `Module.findImportByName(name, symbol)` and `Module.getImportByName(name, symbol)` return the current resolved import target as `NativePointer`
- `Module.findGlobalExportByName(symbol)` and `Module.getGlobalExportByName(symbol)` search across already loaded modules and return `NativePointer`
- `DebugSymbol.fromAddress(address)` returns a minimal debug-symbol object `{ address, name, moduleName, toString() }` for loaded modules by resolving the nearest visible export/symbol at or below the supplied address
- `DebugSymbol.fromAddress(address)` now keeps a runtime-local cache of loaded modules and per-module exports; the cache is invalidated on `Module.load(...)`, runtime init, and runtime shutdown so repeated symbolization in observer hooks does not rescan every module on every frame
- global `Thread.backtrace(context?, backtracer?)` returns `NativePointer[]`; `Thread.backtrace(Backtracer.ACCURATE)` walks the current JS runtime thread, while `Thread.backtrace(this.context, Backtracer.ACCURATE)` reconstructs the hooked native thread through `pc/lr/fp`
- global `Backtracer.ACCURATE` and `Backtracer.FUZZY` are available as the current mode selectors; `ACCURATE` uses unwind / frame reconstruction, while `FUZZY` scans stack memory for executable-pointer candidates starting from the current stack pointer or `context.sp`
- native hook callbacks support `blocking: false` as an observer mode: JS callbacks still run, but the hooked native thread does not wait for callback completion and therefore cannot consume argument/retval mutations from that invocation
- if a script still attempts `args[n].replace(...)` or `retval.replace(...)` in observer mode, the runtime emits a warning log and ignores that mutation for the live native call
- for heavy observer scripts such as `Thread.backtrace(...)` + `DebugSymbol.fromAddress(...)`, the recommended combination is `blocking: false` plus the cached symbolizer path above; otherwise the target thread may still visibly stall on repeated per-frame symbol resolution
- `Module.enumerateExports(name)` returns minimal export objects `{ type, name, address }[]`
- `Module.enumerateSymbols(name)` currently reuses the same loaded dynamic/export symbol subset and returns `{ type, name, address }[]`
- `Module.findSymbolByName(name, symbol)` and `Module.getSymbolByName(name, symbol)` return loaded-module symbol addresses as `NativePointer`
- `Module.findExportByName(...)` resolves symbols from already loaded modules and returns `NativePointer | null`
- `Module.getExportByName(...)` resolves symbols from already loaded modules and throws when resolution fails
- `Module.findBaseAddress(name)` returns the base `NativePointer | null` for a loaded module
- `Module.getBaseAddress(name)` returns the base `NativePointer` and throws when the module is missing
- `Module.findRangeByAddress(address)` returns `{ base, size, protection } | null`
- `Module.attachExport(moduleName, symbolName, callbacks)` installs an inline hook by module+symbol and supports deferred install when the module is not loaded yet
- deferred native-js installs now also emit host-visible hook-status events with `pending`, `installed`, or `failed` states so the CLI / REPL can observe delayed install progress
- the native inline-hook bridge now resolves `{ module, symbol }` targets through a two-step chain:
  1. loaded-module resolver (`ResolveSymbolAddressInLoadedModule`)
  2. general symbol fallback (`ResolveSymbolAddress`)
- this closes a real mismatch where JS/runtime module visibility and native hook bridge resolution previously diverged, causing some already-reachable targets to be misclassified as deferred/pending forever
- `ptr(...)`, `NULL`, and a minimal `NativePointer` object model are available
- global `uint64(...)` and `int64(...)` constructors are available for precise 64-bit values
- global `hexdump(target, options?)` is available with `offset`, `length`, `header`, and `ansi`
- global `Process.enumerateRanges(protection)` is available and currently returns `{ base, size, protection }[]`
- global `Process.findRangeByAddress(address)` is available and currently returns `{ base, size, protection } | null`
- global `Process.getModuleByAddress(address)` is available and currently returns `{ name, base, size, path } | null`
- global `Process.pointerSize`, `Process.pageSize`, `Process.arch`, and `Process.platform` are available as basic process metadata
- global `Process.id` and `Process.isDebuggerAttached()` are available for process identity / debugger-state queries
- global `Process.getCurrentThreadId()` and `Process.enumerateThreads()` are available for a first minimal process-thread view and currently expose `{ id, name, state }`
- global `Thread.id` is available as the current JS runtime thread ID
- global `Thread.sleep(seconds)` is available and currently blocks the JS runtime thread for the requested duration
- global `Process.enumerateModules()`, `Process.findModuleByName(name)`, `Process.getModuleByName(name)`, and `Process.mainModule` are available for process module queries
- current `NativePointer` methods: `toString()`, `toInt32()`, `toUInt32()`, `add(...)`, `sub(...)`, `and(...)`, `or(...)`, `xor(...)`, `isNull()`, `equals(other)`, `compare(other)`, `readByteArray(length)`, `readCString(maxLength?)`, `readAnsiString(maxLength?)`, `readUtf8String(maxLength?)`, `readUtf16String(maxLength?)`, `readPointer()`, `readU8()`, `readU16()`, `readU32()`, `readU64()`, `readS8()`, `readS16()`, `readS32()`, `readS64()`, `writeByteArray(value)`, `writeAnsiString(text)`, `writeUtf8String(text)`, `writeUtf16String(text)`, `writePointer(...)`, `writeU8(...)`, `writeU16(...)`, `writeU32(...)`, `writeU64(...)`, `writeS8(...)`, `writeS16(...)`, `writeS32(...)`, `writeS64(...)`
- current `NativePointer` methods: `toString()`, `toInt32()`, `toUInt32()`, `add(...)`, `sub(...)`, `and(...)`, `or(...)`, `xor(...)`, `isNull()`, `equals(other)`, `compare(other)`, `readByteArray(length)`, `readCString(maxLength?)`, `readAnsiString(maxLength?)`, `readUtf8String(maxLength?)`, `readUtf16String(maxLength?)`, `readPointer()`, `readU8()`, `readU16()`, `readU32()`, `readU64()`, `readS8()`, `readS16()`, `readS32()`, `readS64()`, `readFloat()`, `readDouble()`, `writeByteArray(value)`, `writeAnsiString(text)`, `writeUtf8String(text)`, `writeUtf16String(text)`, `writePointer(...)`, `writeU8(...)`, `writeU16(...)`, `writeU32(...)`, `writeU64(...)`, `writeS8(...)`, `writeS16(...)`, `writeS32(...)`, `writeS64(...)`, `writeFloat(...)`, `writeDouble(...)`
- `Memory.alloc(size)`, `Memory.allocAnsiString(text)`, `Memory.allocUtf8String(text)`, `Memory.allocUtf16String(text)`, `Memory.copy(dst, src, size)`, `Memory.dup(address, size)`, `Memory.protect(address, size, protection)`, `Memory.patchCode(address, size, apply)`, `Memory.scanSync(address, size, pattern)`, and `Memory.scan(address, size, pattern, callbacks)` are available
- `Memory.protect(...)` accepts Frida-style three-character protection strings such as `---`, `r--`, `rw-`, `r-x`, and `rwx`; invalid arguments throw, and syscall failure returns `false`
- `Memory.patchCode(...)` currently provides a first minimal commit flow:
  it snapshots the target bytes into a temporary writable buffer, invokes `apply(code, size)` with that scratch pointer, then copies the modified bytes back to the target range, flushes the instruction cache, and restores the original page protection
- `Memory.scanSync(...)` currently accepts whitespace-separated two-digit hex bytes plus `??` wildcards and returns `{ address, size }[]`
- `Memory.scan(...)` currently runs synchronously under the hood, supports `onMatch`, `onError`, and `onComplete`, and honors `'stop'` from `onMatch`
- `Interceptor.attach(...)` supports immediate address-based inline install with `NativePointer` or pointer string input, and also accepts `{ module, symbol }` targets for deferred-capable module+symbol install
- `Interceptor.replace(target, replacement)` supports the first minimal pointer-target replacement flow backed by the same inline hook backend
- `Interceptor.replace(target, replacement)` also accepts a `NativeFunction` target; when `replacement` is a plain JS function, the runtime infers the callback signature from that `NativeFunction`, auto-registers a matching `NativeCallback`, and exposes `target.original(...)` as a trampoline to the pre-replacement implementation
- `Interceptor.revert(target)` reverts a previously replaced target, even if the revert call comes from a later script
- `NativeFunction` now accepts:
  `void | bool | int8 | uint8 | int16 | uint16 | int32 | int | uint32 | int64 | uint64 | float | double | pointer`
- `NativeCallback` exposes the same JS-facing type set
- `onEnter(args)` exposes native arguments as `NativePointer` objects
- `onLeave(retval)` exposes the return value as a `NativePointer`
- native hook callbacks now receive a minimal Frida-style invocation receiver as `this`
- `this.threadId` and `this.returnAddress` are available in both `onEnter` and `onLeave`
- `this.context` currently exposes `x0..x7`, `sp`, `fp`, `lr`, and `pc` as `NativePointer` values
- properties written to `this` in `onEnter` survive until the matching `onLeave`
- native hook dispatch is now synchronous for active inline-hook invocations:
  the hook thread waits for JS callback completion before continuing
- `onEnter` may rewrite `this.context.x0..x7`, and the updated register values are passed into the original native call
- `onEnter` may also rewrite arguments directly through `args[n].replace(...)`
- `args[n].replace(...)` updates both the real native call argument and the current JS-side `args[n]` pointer view
- `onLeave` may override the native return value through `retval.replace(...)`, including plain integers such as `1`
- `retval.replace(...)` updates both the final native return value and the current JS-side `retval` / `this.context.x0` view
- `this.context.x0` is refreshed to the live return register value before `onLeave` runs
- missing `onEnter` or `onLeave` callbacks are skipped cleanly; an `onLeave`-only hook no longer emits a spurious `not a function` runtime error
- attach results include `hookId`, `deferred`, and `detach()`
- `Interceptor.detach(hookId)` and `Interceptor.detachAll()` clean up hooks owned by the active script; `detachAll()` also reverts active replacements owned by that script
- replacement hooks are also reverted automatically on script unload and runtime shutdown

`NativePointer.readUtf8String(maxLength?)` has one explicit safety rule in the current runtime:

- before dereferencing, the runtime checks whether the requested memory range is readable
- unreadable pointers throw `TypeError: readUtf8String unreadable pointer`
- this prevents simple script mistakes such as `ptr("0x1").readUtf8String(16)` from crashing the target process

`NativePointer.readUtf16String(maxLength?)` follows the same rule for UTF-16LE buffers:

- `maxLength` is interpreted as a maximum UTF-16 code-unit count
- unreadable pointers throw `TypeError: readUtf16String unreadable pointer`
- writable checks also apply to `writeUtf16String(...)`

General memory reads and writes follow the same defensive rule:

- before dereferencing, the runtime checks whether the requested range is readable or writable
- unreadable or unwritable ranges raise a JS `TypeError` instead of crashing the process
- on Android arm64, range checks normalize tagged userspace pointers before matching `/proc/self/maps`
- this matters for heap allocations returned by `Memory.alloc(...)`, because the raw pointer may carry a top-byte tag even though the underlying mapping is valid

Current 64-bit integer behavior:

- `NativePointer.readU64()` returns a minimal `UInt64`-style JS object instead of a lossy JS number
- `NativePointer.writeU64(...)` accepts `number`, `string`, `uint64(...)`, and `int64(...)`
- `NativePointer.readS64()` returns the same minimal object model in signed mode
- `NativePointer.writeS64(...)` accepts `number`, `string`, `int64(...)`, and `uint64(...)`
- current `UInt64` / `Int64` objects expose `toString()` and `valueOf()`
- `valueOf()` is still best-effort and follows JS number semantics, so string conversion remains the precise path for large values

Immediate native-js install:

- if the module is already loaded, the bridge resolves the runtime address and installs with `NookInlineHookAddress`

Deferred native-js install:

- if the module is not loaded yet, the bridge stores a pending native-js hook record in `src/agent_runtime/`
- it then starts the same inline module observer used by the framework deferred path
- when the module loads, the observer calls back into the native-js bridge and completes install
- `Module.attachExport(...)` is the Frida-style JS entrypoint that uses this deferred path without requiring script-side fallback logic

This separation is intentional:

- `src/native_hook/inline_hook/` owns module-load observation
- `src/agent_runtime/` owns JS-facing pending state and callback bookkeeping
- runtime callbacks temporarily restore the owning script context, so `listener.detach()` works from `rpc.exports`, `recv()`, `onEnter`, and `onLeave`

## Java Hook Architecture

Java Hook is already integrated through `src/framework/` and `src/java_hook/`.

The current deferred Java Hook direction matches the inline path conceptually:

- payloads declare hook intent once
- the framework owns pending registration
- runtime/class-load observation decides when install can really happen

The current minimal JS-facing Java path now also has a separate bridge layer in
`src/agent_runtime/nook_java_js_bridge.*`:

- `Java.perform(fn)` is exposed in QuickJS and currently runs synchronously inside script load
- `Java.use(className)` returns a lazy class wrapper and lazy method wrapper objects
- `method.implementation = fn` currently registers hook intent through the Java JS bridge and stores the JS callback in runtime-owned state
- unit tests already cover minimal `callOriginal(...)` plumbing through the bridge

Current limit:

- the device runtime now exposes the API surface and install path
- full end-to-end device dispatch from a real Java hook callback back into JS is still being integrated
- until that lands, the new host smoke should be treated as an API-surface smoke, not a full Java hook smoke

## Communication Architecture

The host/device control plane is split like this:

- host CLI / Python SDK
  - creates sessions, loads scripts, posts messages, calls RPC exports
- `nook-server`
  - accepts host connections
  - coordinates spawn, attach, detach, resume
  - forwards script and agent messages
- agent runtime
  - runs inside target process
  - hosts QuickJS
  - bridges native hook, script, and RPC events

This is the base that later Frida-like workflows build on.

## Build Outputs

The main Android build currently produces:

- `libnook.so`
- `libnook-agent.so`
- `nook-server`
- `libnook_inline_observer_probe.so`

Plus several smoke/example libraries under `libs/arm64-v8a/`.

## Current Limits

- arm32 inline hook is not implemented yet
- `NookNativeHook*` is not yet a unified PLT + Inline facade
- Native JS bridge currently supports inline hooks only
- `NativeFunction` currently supports only the minimal synchronous subset:
  - integer / pointer path: `void`, `bool`, `int8`, `uint8`, `int16`, `uint16`, `int32` / `int`, `uint32`, `int64`, `uint64`, `pointer`
  - floating-point path: `float`, `double`
- `NativeCallback` exposes the same JS-facing type set and remains the current replacement callback foundation for `Interceptor.replace`
- `NativeFunction` raw native calls are currently validated for:
  - homogeneous `float` and `double` argument signatures
  - mixed 0-2 argument signatures composed from `raw` / `float` / `double`
- larger mixed floating-point signatures are not implemented yet
- `Interceptor.replace/revert` currently supports only:
  - pointer-like targets, plus `NativeFunction` targets carrying runtime metadata
  - `NativeCallback` replacements, plus plain JS function replacements when the target is a `NativeFunction`
  - no `original` callable
  - no `flush()` / transaction model
- `UInt64` / `Int64` currently provide only the minimal object model needed by the runtime (`toString()` / `valueOf()`); arithmetic helpers are not implemented yet
- `hexdump(...)` now emits address, hex, and ASCII columns, and supports optional `header` and `ansi` output
- `readUtf8String()` is for real `char*` memory, not JNI `jstring`
- `readUtf8String()` is intentionally conservative today: unreadable ranges fail with a JS exception instead of probing memory unsafely
- `Nook.Jni.readJStringUtf8(env, jstring)` is currently a guarded placeholder:
  - it exists at the JS API layer
  - it intentionally throws in the current async native-hook runtime instead of touching
    cross-thread `JNIEnv*` / local `jstring` references
  - safe Java-string decoding requires capturing or globalizing the value on the original
    hook thread before JS dispatch
- the first safe bridge for this pattern now exists in the native-hook event path:
  - hook requests may declare snapshot descriptors such as
    `snapshot: [{ index: 2, type: "jstringUtf8" }]`
  - the native hook captures selected `jstring` arguments on the hook thread
  - the async JS callback then receives copied UTF-8 text as read-only metadata such as
    `args[2].$jniUtf8`
  - `envIndex` is optional and defaults to argument `0`
  - hook requests may also declare `snapshot: [{ index: 0, type: "cstringUtf8" }]`
  - transient C strings are exposed as read-only metadata such as `args[0].$utf8`
  - this is still a narrow snapshot bridge, not a full generic Java bridge
