# 2026-05-12 Next-Phase Architecture Evaluation

## Goal

Decide what Nook should do next after the current Android 11 real-device path reached a stable, working default spawn flow.

This document is not an implementation plan.

It is a route-selection document for the next engineering phase.

## Current Stable Reality

The current tested baseline is:

- default `spawn` stays on the stable legacy backend
- explicit `--spawn-symbi` works as the experimental symbi path
- `attach` works on the validated path
- hook is effective on the tested cases
- device-visible deployment is single-file:
  - `nook-server`

Current validated properties:

- embedded agent delivery is working
- child-owned memfd injection is working on the tested path
- session/state semantics were tightened so runtime-ready is authoritative
- CLI feedback is clearer during spawn
- log semantics now distinguish:
  - socket connection
  - control-stage ready
  - runtime-stage ready

## What Is Actually Solved

Nook has now solved the practical problem:

- get a real Android 11 device
- push one visible server binary
- spawn a target app
- load a script
- resume the process
- get working hook results

That is a meaningful engineering milestone.

## What Is Not Solved Yet

Even with that milestone, Nook has not yet reached a final Frida-like end state.

The main unresolved areas are:

1. legacy ncore is the stable default today, but it is not the final Frida-like architecture target
2. `symbi` is working, but still visibly two-stage at the connection/handoff level
3. `zygote-control` is still experimental and not the default stable path
4. the current success story is validated mainly on the tested Android 11 real-device path
5. long-term spawn architecture is not yet fully simplified into one universally preferred model

## Candidate Next Directions

There are two realistic next-phase directions.

### Direction A: Keep the default stable path, continue explicit `symbi` stabilization

Meaning:

- keep default spawn on the stable legacy path
- continue tightening correctness, observability, and regression coverage for explicit `symbi`
- do not expand `zygote-control` surface yet

Typical work items:

- repeated real-device regression matrix
- explicit `--spawn-symbi` regression matrix
- fallback-path verification
- clearer internal state transitions
- reducing remaining two-stage diagnostic noise
- validating more apps / more script patterns on the same platform baseline

Pros:

- lowest risk
- preserves the stable default path that is already working
- still improves the explicit symbi path without changing default routing
- preserves current user-visible success
- easier to debug because the stable path is already known

Cons:

- does not immediately advance the more ambitious `zygote-control` architecture
- may leave some deeper architectural cleanup deferred

### Direction B: Resume `zygote-control` as the main next target

Meaning:

- re-open the design where zygote-side agent control becomes a stronger long-term spawn architecture
- potentially move closer to a different stable model than current `symbi`

Typical work items:

- make zygote agent session acquisition reliable
- make zygote RPC readiness deterministic
- stabilize hook install/uninstall timing
- make fork-hook installation safe across repeated runs
- validate across zygote/usap/version variations

Pros:

- potentially more architecturally elegant long-term
- may eventually reduce some handoff complexity
- aligns with the older design ambition in step10/step11

Cons:

- much higher risk right now
- past attempts repeatedly caused:
  - timeouts
  - unstable readiness
  - fallback confusion
  - device instability or reboot
- debugging surface is broader and less constrained than the current working `symbi` path
- easy to destabilize the new stable default path before enough guardrails exist

## Practical Comparison

At this moment, the question is not:

- which direction is more theoretically interesting

The real question is:

- which direction is most likely to improve Nook without re-breaking the first actually stable default spawn path

On that question, Direction A is clearly stronger.

## Recommendation

Recommended next phase:

- Direction A: continue `symbi`-first stabilization

Why:

1. It builds on the first real stable default path that already works on device.
2. It increases confidence and regression protection before attempting larger architectural shifts.
3. It avoids reopening the highest-risk area (`zygote-control`) before the project has enough stable guardrails.
4. It matches the current project need better: preserve momentum and reduce re-break cycles.

## Recommended Order

### Phase N1: Stabilize the current explicit `symbi` surface

Focus:

- repeated real-device regression coverage
- attach/spawn cross-check matrix
- explicit `--spawn-symbi` regression matrix
- fallback verification
- more script/use-case diversity on the same tested platform

Exit criteria:

- repeated runs remain stable
- no surprising fallback behavior
- no session-state regressions
- logs are sufficiently readable for debugging

### Phase N2: Reduce remaining `symbi` observability complexity

Focus:

- make the two-stage nature easier to reason about
- decide whether connection-layer duplication should remain merely observable or be further normalized

Exit criteria:

- there is no ambiguity about which event is authoritative
- logs and state transitions are straightforward for future debugging

### Phase N3: Re-evaluate `zygote-control`

Only after N1 and N2.

At that point, reassess:

- is `zygote-control` still worth the complexity?
- does it unlock something the stable `symbi` path cannot?
- is the cost justified compared with continuing to harden the current default model?

## What Should Not Happen Next

Not recommended now:

- switching the default path again
- broadening `zygote-control` immediately
- large spawn-architecture rewrites before more regression protection exists
- optimizing for theoretical elegance over current validated stability

## Short Conclusion

Nook now has its first genuinely practical default spawn path.

The correct next move is not to chase the most ambitious architecture immediately.

The correct next move is:

- stabilize the path that finally works
- widen validation
- only then decide whether `zygote-control` still deserves to be the next major investment
