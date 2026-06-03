# Java Deopt Diagnostics Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `Java.deopt()` return actionable diagnostics and improve best-effort JIT cache discovery on real devices.

**Architecture:** Extend the native Java hook layer with a deopt diagnostics struct instead of a single boolean pair, keep the existing best-effort behavior, and expose the richer result through the QuickJS `Java.deopt()` binding. Widen runtime scanning conservatively and record why candidates were rejected so device failures become debuggable without reading native logs first.

**Tech Stack:** C++, QuickJS, Android ART internals, existing Nook smoke scripts, header-based regression checks.

---

### Task 1: Lock the new JS contract with a failing regression

**Files:**
- Modify: `tests/headers/test_java_hook_runtime_regressions.cpp`
- Modify: `src/java_hook/JavaHook.h`
- Modify: `src/agent_runtime/js_runtime.cpp`

**Step 1: Write the failing test**

Add a source-level regression check that requires:
- a native diagnostics type or fields for `Java.deopt()`
- `JsJavaDeopt(...)` to expose more than `ok` / `invalidated`
- at least one diagnostic field describing the scan result

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions.exe
.\build\test_java_hook_runtime_regressions.exe
```

Expected: non-zero exit because the new diagnostics surface does not exist yet.

**Step 3: Write minimal implementation**

Add the missing declarations and JS result fields without changing the external command shape yet.

**Step 4: Run test to verify it passes**

Run the same command again and expect exit code `0`.

### Task 2: Improve native JIT cache discovery and diagnostics

**Files:**
- Modify: `src/java_hook/JavaHook.cpp`
- Modify: `src/java_hook/JavaHook.h`

**Step 1: Add diagnostics storage**

Introduce a small diagnostics struct that records:
- whether required symbols were found
- scan range used
- candidate count / readable candidate count
- chosen runtime offset if found
- failure reason string or enum

**Step 2: Expand best-effort discovery**

Keep the current `Jit::GetCodeCache()` path but:
- widen the scan window conservatively
- count rejected candidates
- log richer failure detail
- preserve the existing “do not block hook flow” behavior

**Step 3: Keep compatibility wrapper**

Keep `JavaHook::DeoptimizeJit(...)` usable by existing hook-install code, but have it populate diagnostics internally.

### Task 3: Expose diagnostics to JS and update smoke output

**Files:**
- Modify: `src/agent_runtime/js_runtime.cpp`
- Modify: `host/nook-py/java_debug_smoke.js`

**Step 1: Extend `Java.deopt()` result**

Expose:
- `ok`
- `invalidated`
- `reason`
- `scanStart`
- `scanEnd`
- `candidatesSeen`
- `readableCandidates`
- `runtimeOffset`

Keep the old fields so existing smoke output remains understandable.

**Step 2: Update smoke script**

Print the new diagnostics in a single compact line so device validation immediately shows whether failure is due to:
- symbol lookup
- runtime discovery
- candidate scan miss
- successful invalidation

### Task 4: Verify and document the new behavior

**Files:**
- Modify: `docs/code_review.md`

**Step 1: Run local verification**

Run:

```powershell
g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions.exe
.\build\test_java_hook_runtime_regressions.exe
```

**Step 2: Record the change**

Append a short note describing:
- what `Java.deopt()` now reports
- what failure mode the diagnostics are meant to disambiguate
- that this is still best-effort and not yet full Frida parity
