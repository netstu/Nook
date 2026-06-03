# Java Env monitor postmortem

Date: 2026-04-29

## Summary

`Env.monitorEnter(obj)` and `Env.monitorExit(obj)` were implemented, passed desktop tests, but failed on Android device validation.

Final decision:

- do not ship public monitor enter/exit in the current `Env` architecture
- rollback the runtime surface
- document the failure as an architecture boundary, not as an unfinished feature

## What happened

Desktop validation initially looked correct:

- monitor methods existed
- host-side forwarding tests passed
- Android build succeeded

Device validation disproved the design assumption:

- `monitorEnter(...)` succeeded
- `monitorExit(...)` failed on the same object
- failure reproduced for:
  - a fresh `java.lang.Object.$new()`
  - `Java.choose(...)` matches
  - retained wrappers

This ruled out "bad wrapper source" as the cause.

## Root cause

Nook's current `Java.vm.getEnv()` public surface does not expose a live long-lived `JNIEnv*`.

Instead:

- `env.handle` is diagnostic only
- every `env.xxx()` helper call independently re-enters the native bridge
- on Android, that path currently uses temporary `JavaEnv`
- `JavaEnv` attaches in the constructor when needed
- `JavaEnv` detaches in the destructor

This means a cross-call pair such as:

```javascript
env.monitorEnter(obj);
env.monitorExit(obj);
```

does not reliably execute inside one stable attached-thread lifetime.

That makes the pair unsafe in the same architectural sense as:

- local refs
- local frames

## Why Frida differs

Frida-style JNI helper usage assumes access to a stable live thread-local `JNIEnv*` context.

That is why low-level pairs like:

- local ref create/delete
- monitor enter/exit

can be exposed more directly there.

Nook currently exposes a higher-level runtime-managed `Env` facade instead of that lower-level live JNI object model.

## Verification trail

Red step used for rollback:

- changed the desktop regression to require `typeof env.monitorEnter === "undefined"`
- ran:
  - `cmd /c build\\test_js_runtime_native_attach.exe`
- observed failure at the new absence assertion

Green step:

- removed runtime exposure
- removed monitor test hooks
- replaced success tests with absence regression
- rebuilt and ran:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`

## Resulting boundary

Safe under the current architecture:

- exception helpers
- string helpers
- global refs
- weak global refs

Not safe to expose directly under the current architecture:

- local refs
- local frames
- `monitorEnter/monitorExit`

## Recommendation

Near-term Frida alignment should continue only with helpers that remain valid across independent JNI re-entry.

`monitorEnter/monitorExit` should only be revisited after an `Env` execution-model redesign.
