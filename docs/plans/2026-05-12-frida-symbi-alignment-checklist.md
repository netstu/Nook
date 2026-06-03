# 2026-05-12 Frida Symbi Alignment Checklist

## Goal

Extract the parts of Frida's current Android `spawn` model that are actually worth borrowing for Nook's current codebase.

This is not a broad "become Frida" document.

It is a concrete alignment checklist for the current Nook `spawn`/`symbi` architecture.

## Current Nook Reality

The current Nook codebase already has a real `symbi`-style Android `spawn` path:

- locate zygote
- resolve `android_os_Process_setArgV0`
- find the corresponding `ArtMethod` slot
- patch that slot to a small stub
- let the target app start
- receive a callback from the child path
- restore the original zygote slot and shellcode area

Relevant implementation:

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

The key implementation points are already visible in:

- `collect_symbi_context()`
- `write_stub_and_patch_slot()`
- `restore_original_slot()`
- `restore_original_slot_ptrace()`
- `inject_spawn_symbi_by_pids()`

So the current question is no longer:

- should Nook move toward a Frida-like lightweight zygote gate

The real question is:

- how should Nook reduce complexity and risk while continuing on the path it already has

## Frida Layers Worth Studying

The most relevant local Frida sources are:

- [agent.vala](/E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-core/lib/agent/agent.vala)
- [linjector.vala](/E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-core/src/linux/linjector.vala)
- [patch.c](/E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-core/lib/selinux/patch.c)

These three files map to three different concerns:

1. `agent.vala`
   - transition lifecycle
   - fork/spawn ownership
   - runtime session continuity
2. `linjector.vala`
   - memfd-first library delivery
   - file fallback structure
3. `patch.c`
   - SELinux policy patching
   - execution permissions for memfd/file payloads

Nook should not treat all three as equally urgent.

## What Nook Should Borrow First

### 1. Keep the zygote-side payload minimal

Frida's strongest idea is not "patch zygote".

Nook already does that.

The stronger idea is:

- the zygote-side payload should do as little as possible

What that means for Nook:

- the stub in `symbi` should remain a pure gate / signal path
- avoid moving heavy runtime logic into zygote
- avoid making the stub depend on more remote symbols than necessary
- avoid any design where a large agent payload remains owned by zygote across fork

Why:

- most of the instability seen so far came from doing too much around the zygote boundary
- Android 11 whitelist behavior already proved that zygote-owned payload state is fragile

Nook implication:

- continue shrinking the responsibilities of `TStub`
- keep all heavyweight runtime bring-up on the child side, not zygote side

Priority:

- `P0`

### 2. Unify delivery into a strict memfd-first abstraction

Frida's injector model is clear:

- try fd/memfd delivery first
- fall back to temp file only when necessary

Nook is moving in this direction, but the logic is still spread across multiple paths:

- agent embedded delivery
- explicit `symbi`
- legacy `ncore`
- attach injection

What Nook should do:

- normalize agent delivery behind one shared memfd-first interface
- normalize `ncore` delivery behind one shared embedded-first interface
- keep file materialization as an explicit fallback layer, not an accidental side effect of a backend

Why:

- today the same conceptual behavior is implemented through too many backend-specific branches
- this increases bug surface and makes repeated device debugging harder

Nook implication:

- refactor delivery policy, not just individual backend fixes
- reduce special-case file cleanup logic per backend

Priority:

- `P0`

### 3. Continue tightening authoritative runtime readiness

Frida's big architectural advantage is that fork/spawn transition semantics are owned by the runtime.

Nook does not need to fully replicate Frida's runtime model immediately, but it should continue tightening one principle:

- only the real child runtime-ready event is authoritative

This work has already started and is correct:

- control-stage ready is not authoritative
- runtime-stage ready is authoritative

What remains:

- reduce remaining diagnostic ambiguity
- make repeated spawn/attach transitions easier to reason about
- keep stale earlier connections from influencing later authoritative state

Why:

- many previous "hook failed" symptoms were actually state-binding bugs, not injection bugs

Nook implication:

- keep investing in state model clarity before attempting bigger architecture changes

Priority:

- `P0`

### 4. Push more lifecycle responsibility out of host glue and into stable runtime semantics

This is the part where Frida still has the largest structural advantage.

`agent.vala` shows that Frida's runtime explicitly owns:

- fork transition
- spawn transition
- recovery after process transition

Nook is not there yet.

What Nook can borrow without overreaching:

- make spawn lifecycle boundaries explicit and testable
- reduce reliance on host-side inference
- gradually move toward runtime-owned transition semantics

What Nook should not do yet:

- immediate large rewrite to emulate Frida's full `ForkHandler`/`SpawnHandler` architecture

Why:

- current Nook finally has a working default path
- a premature rewrite here would recreate the same destabilization pattern seen with `zygote-control`

Priority:

- `P1`

## What Nook Should Not Borrow Yet

### 1. Do not make SELinux patching a near-term mainline task

Frida does this:

- patch kernel SELinux policy
- add `frida_memfd` / `frida_file`
- grant execute/map permissions

This is technically real and useful.

But for Nook right now, it should remain:

- an experimental branch
- not a default-path milestone

Why:

- it is high-risk
- ROM variance is large
- recovery/debugging cost is high
- it does not directly solve the current highest-value task, which is keeping the working path stable

Priority:

- `P3`

### 2. Do not reopen `zygote-control` as the immediate answer to Frida alignment

It is tempting to say:

- Frida is more runtime-owned
- therefore Nook should immediately revive `zygote-control`

That conclusion is wrong for the current project state.

Why:

- recent real-device history already showed that `zygote-control` is the highest-risk surface
- repeated failures were not superficial:
  - timeout
  - wrong ready source
  - fallback confusion
  - zygote corruption risk
  - device reboot

Current best use of Frida as a reference:

- improve the current `symbi` path
- not immediately replace it with the riskiest unfinished architecture

Priority:

- `P3`

## Concrete Nook Improvement Targets

### Target A: Make `symbi` payload narrower

Desired outcome:

- the zygote patch does only:
  - identify the right child
  - report callback
  - restore state predictably

Concrete review areas:

- remote helper symbol count in `collect_symbi_context()`
- stub config surface in `write_stub_and_patch_slot()`
- restore sequencing in `inject_spawn_symbi_by_pids()`

Expected benefit:

- fewer zygote-side failure modes
- easier ROM portability

### Target B: Consolidate delivery policy

Desired outcome:

- all embedded payload delivery follows the same policy:
  - memfd first
  - child-owned when crossing fork-sensitive boundaries
  - file fallback only when explicitly necessary

Concrete review areas:

- explicit `symbi` delivery
- default `spawn` delivery
- `attach` delivery
- embedded `ncore` materialization policy

Expected benefit:

- less backend divergence
- fewer "works on one path, breaks on another" bugs

### Target C: Keep state semantics stronger than transport symptoms

Desired outcome:

- connection count and early ready noise do not matter
- only authoritative runtime readiness matters

Concrete review areas:

- pending spawn resolution
- agent session binding by pid
- cleanup on old connection close
- cached ready replay policy

Expected benefit:

- fewer false injection diagnoses
- more predictable repeated spawn behavior

## Recommended Order

### Phase F1

Stabilize current `symbi` implementation details:

- shrink zygote stub responsibility
- harden callback/restore sequencing
- keep repeated real-device regressions running

### Phase F2

Refactor delivery policy:

- unify memfd-first logic
- isolate file fallback behavior
- reduce backend-specific artifact handling

### Phase F3

Continue state-model cleanup:

- make authoritative readiness semantics easier to audit
- add more regression coverage around repeated spawn/attach

### Phase F4

Only after F1-F3:

- evaluate whether a Frida-like stronger runtime-owned transition model is worth a controlled design phase
- evaluate whether SELinux patching deserves a separate experimental branch

## Short Conclusion

Nook does not need a new spawn idea first.

Nook already has the right family of idea.

What it needs now is to make that idea:

- smaller at the zygote boundary
- more uniform in payload delivery
- stricter in runtime state semantics

That is the part of Frida worth copying first.
