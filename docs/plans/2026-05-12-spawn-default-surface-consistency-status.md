# 2026-05-12 Spawn Default Surface Consistency Status

## Goal

After re-anchoring the default spawn backend to stable legacy ncore, do a narrow consistency pass so the active code surface and current-use docs no longer imply that default spawn prefers `symbi`.

This is a surface-alignment pass only.

It does not change runtime behavior.

## Scope

Touched:

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)
- [2026-05-12-real-device-spawn-attach-regression-sop.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-real-device-spawn-attach-regression-sop.md)
- [2026-05-12-next-phase-architecture-evaluation.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-next-phase-architecture-evaluation.md)
- [2026-05-12-spawn-regression-coverage-status.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-spawn-regression-coverage-status.md)

Not touched:

- historical design/status docs whose job is to record the earlier timeline
- runtime routing logic
- zygote-control RPC contract

## What Was Inconsistent

After the routing fix, the runtime policy became:

- default spawn = stable legacy ncore
- explicit `--nook-spawn-backend=symbi` = experimental symbi preference
- `zygote-control` = separate experimental path

But some active surfaces still implied:

- default spawn prefers `symbi`
- stable validation means validating the default symbi path
- current architecture recommendation should keep default on symbi

That mismatch was small but dangerous because it could pull future debugging and test interpretation in the wrong direction.

## Changes

### 1. Source comments

`NinjectorSpawnInjector` comments now match the actual routing contract:

- zygote-control is attempted only when explicit symbi is not requested first
- symbi is documented as an explicit experimental backend, not the default path

### 2. Host test naming

Two temp directory names in spawn injector tests were still carrying the old assumption:

- `default_symbi`
- `default_symbi_fallback_legacy`

They were renamed so the fixture names no longer contradict the assertions.

### 3. Current-use docs

Three documents that read as "current truth" were updated:

- real-device regression SOP
- next-phase architecture evaluation
- spawn regression coverage status

Those now describe:

- default stable spawn as legacy
- explicit `--spawn-symbi` as separate experimental coverage
- legacy fallback during default validation as expected, not as a regression by itself

## Why This Matters

This keeps three layers aligned:

1. runtime routing policy
2. host regression intent
3. operator-facing validation docs

Without this, the project would keep passing tests while still explaining the wrong backend policy to the next debugging session.

## Verification

Verification for behavior remains the same host spawn injector regression and related docs/tests.

This pass is expected to be behavior-neutral.
