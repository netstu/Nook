# Nook Thread.backtrace Modes Design

**Goal:** Split `Backtracer.ACCURATE` and `Backtracer.FUZZY` into distinct native implementations while keeping the current JS API stable.

**Context:**
- `Thread.backtrace()`, `Thread.backtrace(Backtracer.ACCURATE)`, and `Thread.backtrace(Backtracer.FUZZY)` already exist.
- `this.context` currently exposes `sp`, `fp`, `lr`, and `pc`.
- Today both backtracer modes reuse the same implementation, so the JS-visible enum exists but the runtime behavior is not actually differentiated.

**Recommended approach:**
1. `ACCURATE`
   - current-thread path: keep native unwind capture
   - hook-context path: keep `pc/lr/fp` reconstruction
2. `FUZZY`
   - current-thread path: scan stack memory near the current stack pointer
   - hook-context path: scan stack memory starting from `this.context.sp`
   - only keep candidates that land in executable ranges
   - prepend `pc` / `lr` when available so the result stays useful for hook debugging

**Why this approach:**
- It creates a real semantic split without needing a full Gum-grade backtracer engine yet.
- It reuses existing primitives already present in Nook:
  - `sp` in hook context
  - `/proc/self/maps` parsing
  - executable-range classification
  - address normalization
- It is easy to test with synthetic stack contents in the native runtime test suite.

**Trade-offs:**
- `FUZZY` will be noisier than Frida/Gum and may include false positives from executable pointers on the stack.
- `ACCURATE` remains stronger for precise hook-context unwinding.
- This is still a staging step toward fuller Frida parity, not the final backtracer architecture.

**Behavior after change:**
- `Thread.backtrace(this.context, Backtracer.ACCURATE)`:
  deterministic `pc/lr/fp` walk
- `Thread.backtrace(this.context, Backtracer.FUZZY)`:
  `pc/lr` plus executable-pointer scan from `sp`
- `Thread.backtrace(Backtracer.ACCURATE)`:
  unwind-based current-thread frames
- `Thread.backtrace(Backtracer.FUZZY)`:
  current-thread stack scan using approximate current `sp`

**Validation plan:**
- native tests proving fuzzy mode can recover executable addresses from synthetic stack memory when accurate mode cannot
- native tests proving current-thread fuzzy mode is non-empty
- CLI tests
- Android build and device push
