# Strict Zygote-Control Promoted Child Filter

Date: 2026-05-17

## Problem

`--strict-zygote-control` occasionally timed out in full-agent promotion after control-stage bootstrap had already succeeded.

The key failing log shape was:

- control-stage `AGENT_READY stage=1` reached the server
- server began spawned-child full-agent promotion
- injector treated the promoted child as `zygote64`
- full-agent inject later failed around ptrace cleanup / runtime-ready timing

One concrete earlier failure shape was:

- `inject_embedded_agent_by_pid_suspended failed: detach_process_failed:atomic_inject`

## Root Cause

`server/ninjector_compat.cpp` used a name-only `IsZygoteFamilyProcess(pid)` check:

- `zygote64`
- `zygote`
- `usap64`
- `usap32`

That was correct for the real zygote/usap processes, but wrong for strict zygote-control promoted children that still temporarily exposed an early `zygote64` process name during full-agent promotion.

Because of that misclassification, the injector forced the promoted child through the special legacy zygote attach path:

- legacy attach selection in `AttachProcess()`
- zygote-specific remote scratch allocation behavior via `UseRemoteMmapScratch()`
- other zygote-only handling keyed off the same helper

This polluted the spawned-child promotion path with real-zygote ptrace semantics.

## Fix

Tightened the zygote-family filter in `server/ninjector_compat.cpp`.

Added:

- `ReadProcessRealUid(pid_t pid, uid_t* uid)`
- `ReadProcessParentPid(pid_t pid, pid_t* parent_pid)`
- `IsActualZygoteFamilyProcess(pid_t pid)`

New policy:

- `zygote64` / `zygote` only count as actual zygote-family when:
  - real uid is `0`
  - parent pid is `1`
- `usap64` / `usap32` only count as actual zygote-family when:
  - real uid is `0`
  - parent command line basename is `zygote64` or `zygote`

Then `IsZygoteFamilyProcess(pid)` was changed to delegate to the stricter helper.

Also updated attach logging from:

- `forcing legacy attach for zygote-family`

to:

- `forcing legacy attach for actual zygote-family`

so device logs now make the distinction explicit.

## Regression Coverage

Added source-level regression:

- `tests/headers/test_ninjector_compat_zygote_filter_regressions.cpp`

This locks in:

- dedicated actual-zygote filtering helper
- real uid inspection
- parent pid inspection
- `parent_pid == 1` requirement for real `zygote` / `zygote64`
- parent-name validation for `usap`
- updated attach log wording

## Verification

Targeted regression tests passed:

- `tests/headers/test_ninjector_compat_zygote_filter_regressions.cpp`
- `tests/headers/test_zygote_control_regressions.cpp`

Rebuilt and pushed single-file server:

- remote server sha256: `39e08df61553175124a5e8f14305a866c2ba6e5e7fda713ae64f6c3c515475c7`
- running pid observed during verification: `16539`

Real-device log evidence after the fix:

- actual zygote still uses legacy attach:
  - `AttachProcess: forcing legacy attach for actual zygote-family pid=797`
- promoted strict child no longer uses that path:
  - `InjectEmbeddedSoByPidAtomic: begin pid=16802 ... init=NookAgentInitializeForSpawnChild`
  - no matching `forcing legacy attach for actual zygote-family pid=16802`
  - remote scratch for the child switched to `mode=malloc`
- spawn completed end-to-end:
  - `spawn success pkg=com.ad2001.frida0x1 pid=16802 agent=__embedded_agent__`
  - `forward runtime-stage AGENT_READY pid=16802`
  - script loaded and hook hits reached host:
    - `lab:frida-0x1:installed`
    - `lab:frida-0x1:hit:get_random`
    - `lab:frida-0x1:hit:check:left=5:right=14`

## Outcome

This fix does not complete all strict zygote-control stabilization work, but it removes one concrete and validated source of full-agent promotion instability:

- promoted children are no longer misrouted through the real-zygote attach/scratch path solely because their early process name still looks like `zygote64`.
