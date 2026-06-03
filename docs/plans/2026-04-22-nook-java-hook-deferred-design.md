# Nook Java Hook Deferred Design

**Date:** 2026-04-22

**Status:** Approved

## Goal

Replace the current payload-side retry loop used by `Nook` Java Hook with a framework-owned deferred install flow.

The first stage should solve the most common problem:

- the target app `ClassLoader` is not ready yet
- the target class is not yet resolvable when the payload constructor runs

The second stage should extend the design to support true delay hook for static methods whose declaring class has not been initialized yet.

## Current Problem

The current Java hook payload flow is implemented in `src/framework/NookJavaHookPayload.cpp`.

It works by:

1. starting a detached worker thread
2. sleeping briefly after payload load
3. retrying `NookJavaHookHook(...)` up to `retry_count`
4. sleeping `retry_interval_ms` between attempts

This is workable for demos, but it has the same architectural problem the inline hook path used to have:

- the payload owns timing
- install is based on polling instead of runtime events
- every failure looks the same from the caller side
- the framework cannot distinguish "class loader not ready" from "real hook failure"

## Key Observation

Current Java hook install failure is not just "class not loaded yet".

`JavaHook::FindClass()` already tries two strategies:

1. direct `env->FindClass(...)`
2. `ActivityThread.currentApplication() -> Application.getClassLoader() -> ClassLoader.loadClass(...)`

So current retry attempts are really waiting for a broader Java runtime readiness condition:

- `Application` exists
- the app `ClassLoader` can be obtained
- the target class can be resolved in the correct loader
- the target method can be found and converted to `ArtMethod`

Therefore the deferred design should observe Java runtime readiness and class load activity, not just loop on a timer.

## External References Reviewed

### Pine

`Pine` is the most relevant reference for the deferred Java hook architecture.

Important ideas:

- explicit delay hook / pending hook support
- framework-owned pending registry
- callback when the declaring class becomes initialized
- flush pending hooks on class initialization

This is the best reference for the second-stage design.

### GirlHook

`GirlHook` is the most relevant reference for class / loader discovery capability.

Important ideas:

- enumerate ART `ClassLinker` loaders
- enumerate loaded classes
- fall back to `ClassLoader.loadClass(...)` across multiple class loaders

This is useful as a discovery fallback, not as the primary deferred architecture.

### Frida

`Frida` is not a direct implementation reference here, but it reinforces the right timing model:

- wait for Java runtime readiness
- operate in the correct class loader
- install hooks in response to lifecycle events instead of blind retries

## Approaches Considered

### Approach 1: Keep polling, improve `FindClass` only

Pros:

- smallest code change
- no new runtime components

Cons:

- payload still owns timing
- still event-blind
- same architectural issue remains

### Approach 2: Deferred hook based on `Application` / `ClassLoader` readiness only

Pros:

- removes payload-side polling
- aligns with current `Inline Hook` deferred structure
- lower maintenance cost than ART-internal class-init monitoring
- solves the majority of current failures

Cons:

- does not fully solve static-method delay hook
- still needs a later phase for class initialization semantics

### Approach 3: Full deferred design in two stages

Stage 1:

- pending Java hook registry
- Java observer for `Application.attach(...)` and `ClassLoader.loadClass(...)`
- optional multi-loader fallback scan

Stage 2:

- delay hook for static methods whose declaring class is not initialized yet
- class initialization observer similar in spirit to `Pine`

Pros:

- best long-term architecture
- removes polling now
- keeps stage-one risk controlled
- creates a clean path to a real delay hook feature later

Cons:

- larger design surface
- requires explicit phase split

## Recommendation

Adopt **Approach 3**.

The implementation should be split into two stages:

### Stage 1

Build a deferred Java hook flow around:

- pending hook registry
- Java runtime observer
- class-loader readiness
- class-load events

This stage should replace payload retry loops as the default framework story.

### Stage 2

Add class-initialization-aware delay hook for static methods.

This stage should be a focused extension after the stage-one architecture is stable.

## Public API Design

Keep existing immediate APIs unchanged:

```c
NookStatus NookJavaHookInitialize(void);
int NookJavaHookHook(const char* class_name,
                     const char* method_name,
                     const char* signature,
                     int is_static,
                     NookJavaHookCallback callback);
NookStatus NookJavaHookUnhook(int hook_id);
void NookJavaHookUnhookAll(void);
```

Add a new explicit deferred API:

```c
int NookJavaHookHookDeferred(const char* class_name,
                             const char* method_name,
                             const char* signature,
                             int is_static,
                             NookJavaHookCallback callback);
```

Stage-one semantics:

1. validate arguments
2. ensure Java hook runtime is initialized
3. try immediate `NookJavaHookHook(...)` once
4. if it succeeds, return the real hook id
5. if it fails due to timing / discovery issues, register a pending request
6. ensure Java observer is installed
7. return a non-negative deferred request id

Stage-one intentionally does not change the behavior of `NookJavaHookHook(...)`.

## Deferred Request Model

Create a dedicated internal pending registry for Java hook requests.

Suggested request fields:

- `class_name`
- `method_name`
- `signature`
- `is_static`
- `callback`
- `installed`
- `hook_id`
- `request_id`

Design requirements:

- thread-safe
- idempotent registration
- repeated observer callbacks must not install the same request twice
- request identity must not depend on payload retry state

## Stage 1 Internal Architecture

### 1. Pending Java Hook Registry

New directory:

```text
src/java_hook/deferred/
  pending_java_hook_registry.h/.cpp
```

Responsibilities:

- register pending Java hook requests
- match class-load / runtime-ready notifications against requests
- attempt install through the existing `NookJavaHookHook(...)` path
- mark requests installed once successful

### 2. Java Hook Observer

New files:

```text
src/java_hook/deferred/
  java_hook_class_observer.h/.cpp
  java_hook_loader_resolver.h/.cpp
```

Responsibilities:

- install one-time Java-side observation hooks
- learn the correct app `ClassLoader`
- observe class-load events
- notify the pending registry

## Stage 1 Observer Strategy

The observer should be Java-runtime oriented, not ELF/linker oriented.

Recommended observation points:

### Primary observer: `Application.attach(Context)`

Purpose:

- detect that the app process has fully entered application-side Java runtime
- capture a stable app `ClassLoader`

Why this is useful:

- this is the earliest stable point where "use app class loader" stops being guesswork
- it gives the framework a clean "Java side ready" event

### Secondary observer: `ClassLoader.loadClass(...)`

Observe:

- `ClassLoader.loadClass(String)`
- `ClassLoader.loadClass(String, boolean)` when practical

Purpose:

- detect when target classes become resolvable
- flush matching pending hook requests as soon as the class is actually loaded

### Fallback discovery: multi-loader scan

Borrowing from `GirlHook`, the framework may optionally enumerate known class loaders and retry install when:

- the app class loader is ready
- observer install succeeded
- but the target class still has not been matched through primary events

This should remain a fallback path, not the primary mechanism.

## Stage 1 Install Flow

```text
payload / injector
  -> NookJavaHookHookDeferred(...)
  -> immediate install attempt
      -> success: return real hook id
      -> fail: register pending request
  -> ensure Java observer installed

observer event
  -> Application.attach observed
      -> capture app class loader
      -> flush pending requests once
  -> ClassLoader.loadClass observed
      -> compare loaded class against pending requests
      -> attempt install through existing JavaHook::HookMethod path
      -> mark installed on success
```

## Stage 2 Design Boundary

Stage 2 is not part of the first implementation, but the stage-one design must leave room for it.

The stage-two goal is:

- support delay hook for static methods whose declaring class is not initialized yet

This should follow a structure conceptually similar to `Pine`:

- detect whether the declaring class is initialized
- if not, defer the final entry patch
- flush when class initialization actually happens

Stage 2 should be implemented only after stage one is verified stable.

## Payload Layer Changes

Current payload helper macros in `include/nook/NookJavaHookMacros.h` and runtime logic in `src/framework/NookJavaHookPayload.cpp` should be updated in stage one.

Desired change:

- payload auto-start should no longer run a retry loop
- payload should register Java hook declarations once
- payload should ask framework deferred API to install them

In other words, Java payloads should become declarative, just like the current inline payloads did after deferred install was added.

## Error Handling

Stage-one requirements:

- invalid arguments should still return negative failure
- real immediate install failure should remain distinguishable from successful deferred registration
- deferred registration must not report success if observer setup fails

Because the current Java hook API returns `int`, stage one should define a clear request-id / hook-id strategy.

Recommended rule:

- real installed hook ids remain the current non-negative ids
- deferred request ids are also non-negative, but tracked internally as pending until installed
- add a future query helper if needed instead of overloading error semantics further

## Testing Strategy

### Stage 1

1. Immediate success case

- target class already resolvable
- `NookJavaHookHookDeferred(...)` should install immediately

2. App runtime not ready yet

- deferred request is registered
- `Application.attach(...)` readiness event eventually causes install

3. Target class loaded later

- request remains pending until `ClassLoader.loadClass(...)` sees the target
- install occurs without payload retry loop

4. Multi-loader case

- observer path misses the target loader at first
- fallback loader scan still resolves the class and installs hook

### Stage 2

1. static method on not-yet-initialized declaring class
2. class initialization event flushes delayed hook
3. no unintended early class initialization introduced by the framework

## Out of Scope for Stage 1

- ART-level class initialization monitor
- direct `ClassLinker::DefineClass` style hook path
- generalized enumeration of every loaded class on every observer event
- replacement of the current ArtMethod patch core

## Expected Outcome

After stage-one implementation, `Nook` Java Hook should:

- stop relying on payload-side retry loops
- own install timing inside the framework
- align architecturally with the current inline-hook deferred flow
- support reliable Java hook installation when payloads load before app Java runtime is fully ready

After stage two, `Nook` Java Hook should additionally support:

- class-initialization-aware delay hook for static methods
