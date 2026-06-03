# 2026-05-12 Symbi Child-Owned Memfd Design

## Goal

Move explicit `symbi` from the current runtime temporary-file path to a true child-owned memfd path, while preserving:

- single-file deployment on device
- current stable default spawn behavior
- Android 11 real-device safety

This work is explicitly about the experimental `symbi` path, not the default stable spawn backend.

## Why This Is Needed

Current explicit `symbi` is working, but only because we stopped using zygote-owned agent memfd delivery.

Today the successful path is:

1. `nook-server` embeds the agent blob
2. explicit `symbi` request materializes `libnook-agent.so` into runtime dir
3. symbi forks target child
4. host injects the child using that temporary real file path

This is stable enough for testing, but it is not the intended end state.

## Confirmed Constraint

On the current Android 11 real device:

- zygote-owned memfd for `libnook-agent` is not safe

Evidence:

- zygote abort:
  - `Not whitelisted (52): /memfd:libnook-agent (deleted)`

So any next design must obey this hard rule:

- do not leave the agent memfd in zygote fd table across fork

## Required Property

The agent payload must become child-owned, not zygote-owned.

That means the payload fd must either:

1. be created in the child after fork, or
2. be transferred into the child in a way that does not make zygote fork-time fd validation reject it

If neither condition is true, the current device will reproduce the old crash.

## Desired End State

Explicit `symbi` should eventually behave like this:

1. device still only deploys visible `nook-server`
2. host/server still owns the embedded agent blob
3. symbi still performs the zygote hook / child stop handshake
4. after child exists and is stopped:
   - the agent is delivered to the child through child-safe memfd/fd injection
5. host resumes child
6. no runtime temporary agent file remains on disk

## Candidate Implementation Directions

### Option A: Child-Side Memfd Creation After Fork

Flow:

1. symbi creates child
2. child stops and reports back
3. host attaches child directly
4. host creates agent memfd inside child
5. host writes blob into child memfd
6. host `dlopen("/proc/self/fd/N")` inside child
7. host closes child memfd
8. resume child

Pros:

- directly avoids zygote whitelist issue
- most aligned with current attach embedded logic
- architecture is easier to reason about

Cons:

- requires careful integration with existing symbi stop/resume handshake
- child-side loader path must be made as reliable as current attach memfd path

### Option B: Child-Owned FD Transfer Through IPC

Flow:

1. keep symbi callback handshake
2. establish a dedicated fd-passing channel
3. transfer agent fd only after child exists
4. child or host-side child injection consumes fd

Pros:

- more Frida-like in spirit
- can avoid temporary file completely

Cons:

- significantly more moving parts
- fd-passing reliability and lifecycle handling become harder
- higher implementation and debugging cost

### Option C: Keep Current Temporary File Path As Experimental Stable Baseline

This is not the final architecture, but it is a valid staging point.

Meaning:

- default stable spawn stays unchanged
- explicit `symbi` remains file-backed for now
- child-owned memfd work happens only after dedicated diagnostics/tests are added

Pros:

- low risk
- preserves current real-device success

Cons:

- not yet Frida-aligned
- still uses runtime file materialization

## Recommendation

Recommended next implementation target:

- Option A: child-side memfd creation after fork

Reason:

- it directly addresses the confirmed zygote whitelist failure
- it reuses an already proven concept from Nook attach embedded injection
- it is the shortest path toward a real memfd symbi model without depending on a more complex fd-passing architecture

## Non-Goals

This stage should not:

- change default stable spawn backend
- make `symbi` the default backend
- widen scope to `attach`
- attempt full SELinux policy patching like Frida

## Validation Requirements

Before this can replace the current explicit `symbi` file-backed mode, it must pass:

1. no zygote crash
2. no device reboot
3. explicit `--spawn-symbi` hook success on real device
4. repeated runs:
   - spawn
   - hook
   - finalize
   - rerun
5. no visible `libnook-agent.so` dependency in pre-launch deployment

## Practical Rollout Plan

Suggested rollout order:

1. keep current working explicit `symbi` as baseline
2. add a separate internal child-memfd experimental path
3. verify it on real device repeatedly
4. only after it is stable:
   - switch explicit `--spawn-symbi` to child-owned memfd by default

This avoids breaking the newly restored explicit `symbi` usability while continuing the Frida-alignment work.
