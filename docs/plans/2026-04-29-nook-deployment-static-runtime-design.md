# Nook Deployment Static Runtime Design

## Context

Nook's current Android deployment shape is less ergonomic than Frida's:

- `nook-server` is typically launched through `LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 ...`
- `libnook-agent.so` is injected from `/data/local/tmp/nook/libnook-agent.so`
- `libnook.so` remains available for example and payload modules

The current workflow works, but it leaks runtime-linker concerns into normal operator usage. Frida's Android deployment model is closer to "push one executable, run it directly" for the server path. Nook should converge toward that operator experience.

## Goals

1. Make `nook-server` runnable through:
   - `adb shell "su -c '/data/local/tmp/nook/nook-server'"`
2. Remove the `libc++_shared.so` runtime dependency from `nook-server`
3. Preserve the current shared-library build for the rest of the tree until server startup is proven stable
4. Prepare a safe next step for making `libnook-agent.so` self-contained without breaking current JS/native runtime symbol lookups

## Non-Goals

1. Do not redesign host CLI behavior in this change
2. Do not delete `libnook.so`
3. Do not embed `libnook-agent.so` into `nook-server`
4. Do not apply aggressive export filtering to `libnook-agent.so` yet

## Constraints

1. The repository already uses `ndk-build` with a global [Application.mk](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/android/Application.mk) configured as `APP_STL := c++_shared`
2. `nook-server` is defined in the shared [Android.mk](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/android/Android.mk)
3. Current runtime code still resolves some symbols through `dlsym(RTLD_DEFAULT, ...)`, including `NookInlineHookAddress` and `NookInlineUnhook`, so Phase B must not hide exports blindly

## Recommended Approach

### Option 1: Split `Android.mk` into dedicated server/agent makefiles

Pros:

- Very explicit per-target build logic

Cons:

- Unnecessary duplication
- Higher maintenance cost
- Easy to drift from the canonical shared source lists

### Option 2: Keep one `Android.mk`, switch STL per invocation through `NDK_APPLICATION_MK`, and limit targets with `APP_MODULES`

Pros:

- Minimal change surface
- Reuses the existing canonical module definitions
- Easy to validate phase-by-phase

Cons:

- Build invocation becomes slightly more explicit

### Option 3: Attempt `$ORIGIN`/`RUNPATH` fixes first

Pros:

- Potentially avoids static runtime size growth

Cons:

- Does not solve the injected-agent self-containment problem cleanly
- Android linker behavior is more version-sensitive
- Still leaves operator-visible runtime-linker complexity

## Decision

Use Option 2.

That means:

1. Keep the existing [Android.mk](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/android/Android.mk)
2. Add a new [Application_static.mk](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/android/Application_static.mk) with `APP_STL := c++_static`
3. Build `nook_server` and later `nook_agent` through `APP_MODULES=...` while using `Application_static.mk`
4. Leave the normal shared-STL application config untouched for the rest of the modules

## Implementation Phases

### Phase A: `nook-server` static runtime

Scope:

- Add static-application config
- Document and validate a dedicated build invocation for `nook_server`
- Update deployment docs to use direct server launch once verified

Success criteria:

- `readelf -d nook-server` no longer lists `libc++_shared.so`
- `adb shell "su -c '/data/local/tmp/nook/nook-server'"` starts successfully

### Phase B1: `libnook-agent.so` static runtime

Scope:

- Build only `nook_agent` with `Application_static.mk`
- Validate no dependency on `libc++_shared.so`

Success criteria:

- `readelf -d libnook-agent.so` no longer lists `libc++_shared.so`
- attach/spawn smoke flows still pass

### Phase B2: export-surface tightening

Scope:

- Audit required exported symbols first
- Only then add `-fvisibility=hidden` and a version script

Success criteria:

- current JS/native runtime symbol lookups still work
- symbol surface is materially reduced

## Risks

1. Static runtime size increase
   - acceptable for now
2. Mixed STL mode confusion during local builds
   - mitigated by explicit `APP_MODULES` and dedicated `Application_static.mk`
3. Over-eager export filtering in Phase B
   - mitigated by postponing export hardening until after self-contained agent validation

## Validation Strategy

### Host-side validation

1. Build `nook_server` with `Application_static.mk`
2. Inspect dynamic dependencies

### Device-side validation

1. Push the rebuilt `nook-server`
2. Run it directly without `LD_LIBRARY_PATH`
3. Confirm host CLI can connect through the normal workflow

## Expected Next Step

Implement Phase A only:

- create `build/android/Application_static.mk`
- document the static build invocation
- verify direct `nook-server` startup
