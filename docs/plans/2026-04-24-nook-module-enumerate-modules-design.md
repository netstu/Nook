# Nook Module EnumerateModules Design

**Goal**

Add `Module.enumerateModules()` so scripts can list loaded modules and inspect minimal Frida-style module metadata before doing address-to-module queries.

This is the missing foundation for later APIs such as `getModuleByAddress(...)`.

## Scope

In scope:

- add `Module.enumerateModules(): Module[]`
- return minimal module objects:
  - `name`
  - `base`
  - `size`
  - `path`
- support current process only
- keep behavior available on both Windows host tests and Android/Linux runtime

Out of scope:

- `getModuleByAddress(...)`
- `ModuleMap`
- export/import/symbol enumeration
- lazy loading or initialization behavior

## Recommended API

Example:

```javascript
const modules = Module.enumerateModules();
modules.forEach(function (m) {
  console.log(m.name + " @ " + m.base + " size=" + m.size);
});
```

For this phase, the object shape stays intentionally small and stable.

## Behavior

- returns an array
- each entry includes:
  - `name: string`
  - `base: NativePointer`
  - `size: number`
  - `path: string`
- array may be empty only if enumeration fails silently at the platform layer, but the binding itself should throw on hard failure

## Architecture

Recommended implementation:

1. add one native helper that collects loaded modules into a small record vector
2. convert records to JS objects inside `js_runtime.cpp`
3. expose one `Module.enumerateModules` binding

Platform strategy:

- Android/Linux: parse `/proc/self/maps`, keep pathname-backed entries only, merge multiple mappings by path, and derive:
  - `base` = lowest mapping start
  - `size` = highest end minus lowest start
  - `name` = basename of path
- Windows: enumerate current-process modules with a snapshot API and use the module name/path directly

## Testing Strategy

Add runtime tests for:

1. binding exists
2. result is an array with at least one module
3. current test executable is present and exposes non-empty `name/path`, non-null `base`, and positive `size`

After local tests pass, extend the device smoke with one message confirming:

- module count
- `libnook-agent.so` is present in the enumeration result
