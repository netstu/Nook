# Nook Thread.backtrace Shape Design

> For this iteration, keep the JS-visible contract stable first and defer deeper backtracer differentiation to a later step.

**Goal:** Make `Thread.backtrace()` behave more like Frida at the script layer by accepting the no-argument form and explicit mode-only form consistently.

**Scope:**
- support `Thread.backtrace()`
- support `Thread.backtrace(Backtracer.ACCURATE)`
- support `Thread.backtrace(Backtracer.FUZZY)`
- keep `Thread.backtrace(this.context, Backtracer.ACCURATE)` working
- document the current semantic split between runtime-thread backtrace and hook-context backtrace

**Non-Goals:**
- no new fuzzy stack-scanning engine in this step
- no attempt to make no-argument backtrace return the hooked native thread
- no ABI/runtime redesign

**Approaches Considered:**

1. **Contract-first compatibility layer**
   - treat no-argument and mode-only calls as current-runtime-thread backtrace
   - keep `ACCURATE` and `FUZZY` sharing the same collector for now
   - best choice for short feedback loop and Frida-style API convergence

2. **Immediate accurate/fuzzy split**
   - add a second backtrace engine now
   - more complete, but adds more moving parts and verification surface

**Decision:** Use approach 1 now. Freeze the call shape first, then split the implementation underneath later.

**Behavior:**
- `Thread.backtrace()` -> current JS runtime thread frames
- `Thread.backtrace(Backtracer.ACCURATE)` -> current JS runtime thread frames
- `Thread.backtrace(Backtracer.FUZZY)` -> current JS runtime thread frames for now
- `Thread.backtrace(this.context, Backtracer.ACCURATE)` -> hooked native thread frames reconstructed from `pc/lr/fp`
- `Thread.backtrace(this.context, Backtracer.FUZZY)` -> same reconstructed path for now

**Validation:**
- native runtime tests for no-arg and `FUZZY`
- existing CLI tests
- Android `ndk-build`
- device push of `libnook-agent.so`, `libnook.so`, and `nook-server`
