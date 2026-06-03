# Nook NativeFunction Design

**Goal**

Add a first minimal `NativeFunction` API so JavaScript can synchronously call native code through a `NativePointer`, moving Nook closer to Frida's core native-invocation workflow.

## Scope

In scope:

- add global `NativeFunction`
- support `new NativeFunction(address, returnType, argTypes)`
- return a callable JS function object
- first implementation targets Android arm64 and other 64-bit integer-pointer calling environments already covered by current tests
- support a minimal type set:
  - return types: `void`, `int`, `uint32`, `pointer`
  - argument types: `int`, `uint32`, `pointer`
- synchronous invocation only

Out of scope:

- `NativeCallback`
- `Interceptor.replace`
- floating-point types
- `int64` / `uint64` arguments and returns
- variadic functions
- explicit ABI selection
- exception recovery for bad target pointers
- cross-architecture portability beyond the current minimal bridge

## Recommended API

```javascript
const add = new NativeFunction(targetPtr, 'int', ['int', 'int']);
const result = add(7, 35);
```

```javascript
const echo = new NativeFunction(targetPtr, 'pointer', ['pointer']);
const value = echo(ptr('0x1234'));
```

The constructor returns a callable object, not a plain metadata record.

## Behavior

- `address` must be a valid pointer-like value accepted by the current runtime
- `returnType` must be one of the supported strings
- `argTypes` must be an array of supported type strings
- calling with the wrong number of arguments throws
- unsupported type strings throw at construction time
- `pointer` arguments accept existing pointer-like JS values
- `pointer` returns become `NativePointer`
- `int` and `uint32` returns become JS numbers
- `void` returns become `undefined`

## Architecture

Keep the first slice intentionally narrow:

1. parse and validate constructor arguments in `js_runtime.cpp`
2. build a callable JS function object whose hidden properties store:
   - target address
   - parsed return type
   - parsed argument types
3. route invocation into one small native call bridge
4. implement the bridge only for the minimal integer/pointer type matrix above

This avoids introducing `libffi` or a larger FFI subsystem before the core calling path is proven on-device.

## Type Model

Use a tiny internal enum:

- `kVoid`
- `kInt`
- `kUInt32`
- `kPointer`

The first version should normalize both `int` and `uint32` arguments into 64-bit call slots internally, then narrow or reinterpret as needed when returning values to JS.

## Error Handling

- constructor validation errors should be `TypeError`
- unsupported return or argument type strings should be `TypeError`
- wrong argument count at call time should be `TypeError`
- bad pointer coercion should reuse the current pointer parsing failure style

The first version should not promise safety if the user calls an invalid executable address. That remains a documented limitation for this phase.

## Testing Strategy

Add runtime tests for:

1. `typeof NativeFunction === 'function'`
2. constructor rejects unsupported return types
3. constructor rejects unsupported argument types
4. constructor rejects non-array `argTypes`
5. a native `int add(int, int)` function can be called from JS
6. a native `pointer echo(pointer)` function round-trips a `NativePointer`
7. a native `void sink(uint32)` function can be called and returns `undefined`
8. wrong argument count throws

After local tests pass, add a device smoke that calls a small exported test function through `NativeFunction` and prints the result through `send(...)`.

## Why This Order

`NativeFunction` is a better next step than more `ModuleMap` polish because it unlocks active native control rather than only better lookup ergonomics.

Once this minimal path works, the natural next stages are:

1. widen type coverage
2. add `NativeCallback`
3. add `Interceptor.replace`
