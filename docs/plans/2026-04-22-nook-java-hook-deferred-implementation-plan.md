# Nook Java Hook Deferred Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first-stage deferred Java hook flow to `Nook` that replaces payload-side retry polling with a framework-owned pending registry and Java runtime observer, while keeping room for a second-stage class-initialization delay hook.

**Architecture:** The implementation keeps the current `ArtMethod` patch core intact and adds a new deferred layer around it. Stage one introduces a pending request registry plus a Java observer that reacts to `Application.attach(...)` and `ClassLoader.loadClass(...)`. Stage two is left as a follow-up for true static-method class-initialization delay hook.

**Tech Stack:** C++17, JNI, Android ART internals already used by `JavaHook.cpp`, existing Nook framework/public headers, Android NDK build.

---

### Task 1: Add the public deferred Java hook API surface

**Files:**
- Modify: `include/nook/NookJavaHook.h`
- Modify: `src/framework/NookJavaHook.cpp`
- Test: `examples/java_hook/nook_java_hook_example.cpp`

**Step 1: Write the failing compile-level usage**

Add a call site in `examples/java_hook/nook_java_hook_example.cpp` that uses:

```cpp
int hook_id = NookJavaHookHookDeferred(
        "com.demo.target.SomeClass",
        "someMethod",
        "()I",
        0,
        callback);
```

Expected result before implementation:

- compile failure because `NookJavaHookHookDeferred` is undeclared

**Step 2: Run build to verify it fails**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- compiler error for missing `NookJavaHookHookDeferred`

**Step 3: Add the public API declaration and stub**

In `include/nook/NookJavaHook.h`, add:

```cpp
int NookJavaHookHookDeferred(const char* class_name,
                             const char* method_name,
                             const char* signature,
                             int is_static,
                             NookJavaHookCallback callback);
```

In `src/framework/NookJavaHook.cpp`, add a temporary stub:

```cpp
int NookJavaHookHookDeferred(const char* class_name,
                             const char* method_name,
                             const char* signature,
                             int is_static,
                             NookJavaHookCallback callback) {
    return NookJavaHookHook(class_name, method_name, signature, is_static, callback);
}
```

**Step 4: Run build to verify it passes**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build succeeds

**Step 5: Commit**

```bash
git add include/nook/NookJavaHook.h src/framework/NookJavaHook.cpp examples/java_hook/nook_java_hook_example.cpp
git commit -m "feat: add deferred java hook api surface"
```

### Task 2: Create the pending Java hook registry

**Files:**
- Create: `src/java_hook/deferred/pending_java_hook_registry.h`
- Create: `src/java_hook/deferred/pending_java_hook_registry.cpp`
- Modify: `src/framework/NookJavaHook.cpp`
- Test: `src/java_hook/deferred/pending_java_hook_registry.cpp`

**Step 1: Write the failing registry test helpers**

Add test-only helper functions to the registry implementation:

```cpp
size_t GetPendingJavaHookCountForTesting(void);
size_t GetInstalledPendingJavaHookCountForTesting(void);
void ResetPendingJavaHookRegistryForTesting(void);
```

Then temporarily call them from a small internal compile check in `src/framework/NookJavaHook.cpp`.

Expected result before implementation:

- missing header / unresolved symbol compile failure

**Step 2: Run build to verify it fails**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- compile failure due to missing deferred registry files

**Step 3: Implement minimal registry**

Define a request model:

```cpp
struct PendingJavaHookRequest {
    const char* class_name;
    const char* method_name;
    const char* signature;
    int is_static;
    NookJavaHookCallback callback;
};
```

Implement:

- registration
- duplicate suppression
- installed flag tracking
- test-only counters/reset

Use the same style as `src/native_hook/inline_hook/pending_inline_hook_registry.cpp`.

**Step 4: Run build to verify it passes**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build succeeds

**Step 5: Commit**

```bash
git add src/java_hook/deferred/pending_java_hook_registry.h src/java_hook/deferred/pending_java_hook_registry.cpp src/framework/NookJavaHook.cpp
git commit -m "feat: add pending java hook registry"
```

### Task 3: Route the deferred API through the registry

**Files:**
- Modify: `src/framework/NookJavaHook.cpp`
- Modify: `include/nook/NookJavaHook.h`
- Test: `examples/java_hook/nook_java_hook_example.cpp`

**Step 1: Write the failing behavior**

Change the deferred API stub expectation:

- immediate install should still be attempted first
- if immediate install fails, registration must still succeed and return a non-negative request id

Document this in the example payload log output.

Expected before implementation:

- deferred API still just mirrors immediate hook path

**Step 2: Run build / manual verification to confirm current behavior**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build passes, but no deferred behavior exists

**Step 3: Implement minimal deferred routing**

Update `NookJavaHookHookDeferred(...)` to:

1. validate arguments
2. call `NookJavaHookHook(...)` once
3. if successful, return the real hook id
4. if failed, register a pending request and return a request id

Do not add observer logic yet.

**Step 4: Run build to verify it passes**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build succeeds

**Step 5: Commit**

```bash
git add src/framework/NookJavaHook.cpp include/nook/NookJavaHook.h examples/java_hook/nook_java_hook_example.cpp
git commit -m "feat: route deferred java hook through pending registry"
```

### Task 4: Add the Java hook observer skeleton

**Files:**
- Create: `src/java_hook/deferred/java_hook_class_observer.h`
- Create: `src/java_hook/deferred/java_hook_class_observer.cpp`
- Create: `src/java_hook/deferred/java_hook_loader_resolver.h`
- Create: `src/java_hook/deferred/java_hook_loader_resolver.cpp`
- Modify: `src/framework/NookJavaHook.cpp`

**Step 1: Write the failing integration**

From `NookJavaHookHookDeferred(...)`, call a new function:

```cpp
EnsureJavaHookClassObserverInstalled();
```

Expected before implementation:

- compile failure because observer files do not exist

**Step 2: Run build to verify it fails**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- compile failure on missing observer symbol

**Step 3: Implement the observer skeleton**

Add:

```cpp
NookStatus EnsureJavaHookClassObserverInstalled(void);
```

Minimal stage:

- one-time initialization guard
- logging only
- no real hook yet

Add loader resolver helpers to:

- fetch `Application`
- fetch `Application.getClassLoader()`

using the same logic already present in `JavaHook::FindClass(...)`, but move it into reusable helper functions.

**Step 4: Run build to verify it passes**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build succeeds

**Step 5: Commit**

```bash
git add src/java_hook/deferred/java_hook_class_observer.h src/java_hook/deferred/java_hook_class_observer.cpp src/java_hook/deferred/java_hook_loader_resolver.h src/java_hook/deferred/java_hook_loader_resolver.cpp src/framework/NookJavaHook.cpp
git commit -m "feat: add java hook observer skeleton"
```

### Task 5: Move reusable loader resolution out of `JavaHook::FindClass`

**Files:**
- Modify: `src/java_hook/JavaHook.cpp`
- Modify: `src/java_hook/JavaHook.h`
- Modify: `src/java_hook/deferred/java_hook_loader_resolver.h`
- Modify: `src/java_hook/deferred/java_hook_loader_resolver.cpp`

**Step 1: Write the failing refactor target**

Identify the duplicated logic currently embedded in `JavaHook::FindClass(...)`:

- `ActivityThread.currentApplication()`
- `Application.getClassLoader()`
- `ClassLoader.loadClass(...)`

Temporarily replace the fallback block in `JavaHook::FindClass(...)` with calls to new helper functions.

Expected before implementation:

- missing helper compile errors

**Step 2: Run build to verify it fails**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- compile failure on missing loader resolver helpers

**Step 3: Implement the helpers**

Suggested helper surface:

```cpp
jobject GetCurrentApplication(JNIEnv* env);
jobject GetApplicationClassLoader(JNIEnv* env, jobject application);
jclass LoadClassWithClassLoader(JNIEnv* env, jobject class_loader, const char* class_name);
```

Then rewrite `JavaHook::FindClass(...)` to reuse them.

**Step 4: Run build to verify it passes**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build succeeds

**Step 5: Commit**

```bash
git add src/java_hook/JavaHook.cpp src/java_hook/JavaHook.h src/java_hook/deferred/java_hook_loader_resolver.h src/java_hook/deferred/java_hook_loader_resolver.cpp
git commit -m "refactor: extract java hook loader resolution helpers"
```

### Task 6: Install the `Application.attach(...)` readiness observer

**Files:**
- Modify: `src/java_hook/deferred/java_hook_class_observer.cpp`
- Modify: `src/framework/NookJavaHook.cpp`
- Test: `examples/java_hook/nook_java_hook_example.cpp`

**Step 1: Write the failing observer behavior**

Add logs that distinguish:

- observer installed
- `Application.attach(...)` observed
- app class loader captured

Expected before implementation:

- observer installs but no readiness event is ever emitted

**Step 2: Run manual verification to confirm it fails**

Run:

- `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`
- deploy payload
- trigger target app startup

Expected:

- no `Application.attach(...)` observer log yet

**Step 3: Implement minimal attach observer**

Use the existing Java hook core to hook:

- `android.app.Application.attach(Context)`

Observer callback should:

1. capture `this`
2. resolve the application class loader
3. notify the pending registry to retry all pending requests once

This observer should install once per process.

**Step 4: Run manual verification to verify it passes**

Run the same build + deploy flow.

Expected:

- logs show observer install
- logs show `Application.attach(...)` observed
- logs show pending registry flush attempt

**Step 5: Commit**

```bash
git add src/java_hook/deferred/java_hook_class_observer.cpp src/framework/NookJavaHook.cpp examples/java_hook/nook_java_hook_example.cpp
git commit -m "feat: observe application attach for deferred java hook"
```

### Task 7: Install the `ClassLoader.loadClass(...)` observer

**Files:**
- Modify: `src/java_hook/deferred/java_hook_class_observer.cpp`
- Modify: `src/java_hook/deferred/pending_java_hook_registry.cpp`
- Test: `examples/java_hook/nook_java_hook_example.cpp`

**Step 1: Write the failing event path**

Add logs for:

- `ClassLoader.loadClass(...)` observer install
- loaded class name
- matched pending request count

Expected before implementation:

- `Application.attach(...)` may flush once, but classes loaded later still do not trigger install

**Step 2: Run manual verification to confirm it fails**

Build and run the target app where the target class is loaded after startup.

Expected:

- no later observer-driven flush when the target class loads

**Step 3: Implement the loadClass observer**

Hook:

- `java.lang.ClassLoader.loadClass(String)`

Optional if straightforward:

- `java.lang.ClassLoader.loadClass(String, boolean)`

Observer callback should:

1. read the requested class name
2. notify the pending registry for matching requests
3. retry install using existing `NookJavaHookHook(...)`

**Step 4: Run manual verification to verify it passes**

Expected:

- when the target class is loaded later, deferred install succeeds without a sleep loop

**Step 5: Commit**

```bash
git add src/java_hook/deferred/java_hook_class_observer.cpp src/java_hook/deferred/pending_java_hook_registry.cpp examples/java_hook/nook_java_hook_example.cpp
git commit -m "feat: observe class loading for deferred java hook"
```

### Task 8: Remove payload-side retry polling

**Files:**
- Modify: `src/framework/NookJavaHookPayload.cpp`
- Modify: `include/nook/NookJavaHookMacros.h`
- Test: `examples/java_hook/nook_java_test_replace_num_macro.cpp`
- Test: `examples/java_hook/nook_adwall_loadad_block_macro.cpp`

**Step 1: Write the failing payload expectation**

Payload startup should:

- register declarations
- call deferred API once per declaration
- not spin a retry loop

Expected before implementation:

- payload still starts a thread and loops over retries

**Step 2: Run build / manual verification to confirm current behavior**

Run: `E:\SDK\ndk\25.2.9519653\ndk-build.cmd -j4`

Expected:

- build passes with old retry-based behavior

**Step 3: Replace polling with declarative deferred install**

Update `NookPayloadInstallThread()` and related macros so that:

- declarations are registered once
- each declaration calls `NookJavaHookHookDeferred(...)`
- `retry_count` / `retry_interval_ms` become deprecated for stage one or are ignored with a warning log

Do not remove the config structure yet if it would break callers.

**Step 4: Run build / manual verification to verify it passes**

Expected:

- payload no longer loops
- observer-driven install still works

**Step 5: Commit**

```bash
git add src/framework/NookJavaHookPayload.cpp include/nook/NookJavaHookMacros.h examples/java_hook/nook_java_test_replace_num_macro.cpp examples/java_hook/nook_adwall_loadad_block_macro.cpp
git commit -m "refactor: replace java hook payload polling with deferred install"
```

### Task 9: Add multi-loader fallback scan

**Files:**
- Modify: `src/java_hook/deferred/java_hook_loader_resolver.h`
- Modify: `src/java_hook/deferred/java_hook_loader_resolver.cpp`
- Modify: `src/java_hook/deferred/java_hook_class_observer.cpp`
- Optional reference: `src/java_hook/JavaHook.cpp`

**Step 1: Write the failing multi-loader case**

Define a scenario where:

- target class is not found by the app primary class loader
- but is resolvable through another loader

Expected before implementation:

- deferred hook remains pending forever

**Step 2: Run manual verification to confirm it fails**

Use a target with a secondary loader if available.

Expected:

- no install

**Step 3: Implement fallback loader scan**

Stage-one-safe version:

- maintain known loaders observed through callbacks
- retry `loadClass(...)` across known loaders when a pending request remains unresolved

Do not yet port full `GirlHook` ART `VisitClassLoaders` enumeration unless necessary.

If required later, add it as a separate fallback layer.

**Step 4: Run manual verification to verify it passes**

Expected:

- hook installs through fallback loader resolution

**Step 5: Commit**

```bash
git add src/java_hook/deferred/java_hook_loader_resolver.h src/java_hook/deferred/java_hook_loader_resolver.cpp src/java_hook/deferred/java_hook_class_observer.cpp
git commit -m "feat: add multi-loader fallback for deferred java hook"
```

### Task 10: Document stage-one limits and stage-two follow-up

**Files:**
- Modify: `docs/architecture.md`
- Modify: `include/nook/NookJavaHook.h`
- Modify: `docs/plans/2026-04-22-nook-java-hook-deferred-design.md`

**Step 1: Write the failing documentation gap**

Current docs do not explain:

- deferred Java hook semantics
- observer-driven install
- stage-one limitation for static-method class initialization delay hook

**Step 2: Review docs to confirm the gap exists**

Read:

- `docs/architecture.md`
- `include/nook/NookJavaHook.h`

Expected:

- no stage-one deferred Java hook explanation

**Step 3: Update docs minimally**

Add:

- stage-one architecture summary
- payload no longer owns timing
- stage-two class-init delay hook remains future work

**Step 4: Verify docs and code references**

Run:

- `rg -n "NookJavaHookHookDeferred|retry|loadClass|Application.attach" docs include src`

Expected:

- docs and code use the same API names and terminology

**Step 5: Commit**

```bash
git add docs/architecture.md include/nook/NookJavaHook.h docs/plans/2026-04-22-nook-java-hook-deferred-design.md
git commit -m "docs: describe deferred java hook stage one architecture"
```

### Task 11: Prepare the stage-two design handoff

**Files:**
- Create: `docs/plans/2026-04-22-nook-java-hook-class-init-delay-design.md`
- Reference: `docs/plans/2026-04-22-nook-java-hook-deferred-design.md`

**Step 1: Write the failing handoff requirement**

Stage one should finish without silently absorbing stage-two complexity.

Expected before implementation:

- no explicit follow-up document for class-init-aware delay hook

**Step 2: Review stage-one outputs**

Read the stage-one design doc and implementation notes.

Expected:

- stage two exists only as a brief section

**Step 3: Write the follow-up design stub**

Document:

- static method class initialization problem
- candidate approaches
- whether to borrow `Pine`-style class status checks
- whether to add ART-level class-init monitoring or Java-level fallback

Keep this as a scoped follow-up, not part of stage-one implementation.

**Step 4: Verify document placement**

Run:

- `Get-ChildItem docs\\plans`

Expected:

- new follow-up design doc is present

**Step 5: Commit**

```bash
git add docs/plans/2026-04-22-nook-java-hook-class-init-delay-design.md
git commit -m "docs: add stage-two class init delay hook design stub"
```
