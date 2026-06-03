# Nook Nonblocking Hook Observer Design

**Goal:** Add an explicit nonblocking observer mode for native hooks so heavy logging/backtrace scripts do not stall the hooked target thread.

**Problem:**
- Inline hooks currently enqueue an enter/leave event and synchronously wait for JS callback completion.
- This is necessary when JS wants to mutate arguments or return values.
- It is too expensive for observer-only scripts such as backtrace, logging, send/post, and metrics.

**Design:**
- Add `blocking` as an optional attach callback option.
- Default remains `true` to preserve existing mutation semantics.
- When `blocking: false`:
  - JS callbacks still run on the dispatch thread
  - the hooked native thread does not wait for callback completion
  - argument overrides and return-value replacement are ignored

**API shape:**
- `Nook.Native.attach({ ..., blocking: false, onEnter() {} })`
- `Interceptor.attach(target, { blocking: false, onEnter() {} })`

**Why explicit opt-in instead of changing default:**
- Existing tests and features rely on synchronous mutation semantics.
- `args[i] = ...`, `this.context.x0 = ...`, and `retval.replace(...)` must keep working.
- Observation-heavy scripts can opt into lower latency without breaking existing behavior.

**Expected result:**
- Heavy observer scripts like `thread_backtrace_hook.js` can use `blocking: false`.
- UI thread stalls drop sharply because hook callbacks no longer gate target-thread progress.

**Validation:**
- JS runtime tests for `blocking:false` parsing
- bridge test proving nonblocking hook invocation returns without waiting for JS completion
- existing mutation tests remain green in default blocking mode
- CLI tests, Android build, device push
