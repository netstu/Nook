# Spawn Default Stable Routing Reanchoring

## Problem

`nook-cli -U -f ...` on the `frida-0x1` lab regressed back to partial hook behavior because default spawn was re-entering `zygote-control` whenever the server launched with `--enable-zygote-control`.

## Root Cause

`server/ninjector_spawn_injector.cpp` used `config_.enable_zygote_control` as the gate for `ApplySpawnRoutingAttempts()`, so the default route could still take the zygote-control branch even when the request was not strict.

## Fix

- Default spawn now only enters zygote-control when `policy.strict_zygote_control` is true.
- Strict zygote-control tests were updated to request strict mode explicitly.
- A focused host probe was added to verify:
  - default execution policy stays on the stable path
  - default spawn skips zygote-control even when the server supports it

## Verification

- `build/test_ninjector_spawn_injector.exe` still contains legacy white-box cases, but the new focused probe passes:
  - `build/spawn_route_probe.exe`
- Server was rebuilt and pushed with fresh embedded blobs:
  - `build/single-server-package/arm64-v8a/nook-server`
- Device server started successfully with:
  - `linker64 /data/local/tmp/nook/nook-server --enable-zygote-control`

## Notes

- Default stable path is now the intended public spawn route.
- `--strict-zygote-control` remains the explicit zygote-control path for experiments and debugging.

## Follow-up On 2026-05-17

Started promoting this routing split into the larger injector white-box suite.

Findings from that follow-up:

- part of `tests/communication/test_ninjector_spawn_injector.cpp` still encoded the pre-fix assumption that:
  - default spawn would enter zygote-control whenever support was enabled
- that assumption now conflicts with the intended model and with the device-validated behavior
- one example already corrected:
  - the routing-attempt regression that previously expected default-path zygote fallback now asserts:
    - default path skips zygote-control
    - route state becomes `kSkipped`
    - legacy becomes the first entered backend

This means the remaining work in that suite is now mostly regression debt cleanup, not new injector behavior changes:

- remove or rewrite legacy white-box tests that still treat zygote-control as the default public route
- keep strict-only expectations behind explicit strict policy / strict request setup
- preserve the newer focused regressions as the authoritative reference when old white-box cases disagree

## Frida Alignment Direction

This routing split is also the correct Frida-facing direction.

For the user-visible product surface, the target behavior is:

- default spawn uses the stable public route
- experimental zygote-control / strict paths stay explicit
- CLI semantics remain Frida-like:
  - spawn/load/resume order stays the normal one-shot default
  - experimental backend selection must not leak into the default command shape

Practical implication for Nook:

- `nook-cli -U -f ... -l ...` should keep behaving like the stable public path by default
- `--strict-zygote-control` remains an explicit opt-in debug/experimental route
- injector white-box tests that assume `enable_zygote_control=true` automatically changes the default spawn route are now considered stale and must be rewritten

The next cleanup step should therefore avoid full-suite thrash and instead build a focused injector regression subset around:

- default stable route with zygote-control support enabled
- explicit strict zygote-control route
- explicit symbi route

That subset should become the authoritative bridge before more of the legacy `test_ninjector_spawn_injector.cpp` expectations are rewritten.

## Follow-up On 2026-05-17 Route Subset

The focused injector route subset is now checked in at:

- [tests/communication/test_ninjector_spawn_injector_route_subset.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector_route_subset.cpp)

Current authoritative subset coverage:

- strict zygote-control prefers the zygote-control route when it succeeds
- default stable route skips zygote-control even when support is enabled
- default stable route does not probe symbi just because the backend exists
- strict zygote-control may still degrade to the stable legacy backend on soft control-stage failure
- explicit symbi remains opt-in and is verified separately from the default route

Verification:

- rebuilt:
  - `build/test_ninjector_spawn_injector_route_subset.exe`
- passed:
  - `build/test_ninjector_spawn_injector_route_subset.exe`

Practical policy after this follow-up:

- `strict` means zygote-control is attempted first
- `strict` does not mean every zygote-control failure must hard-stop the spawn
- soft install / ready-wait failures are still allowed to fall back to the stable legacy route
- the default public route remains stable legacy unless the request explicitly opts into an experimental backend

This subset is now the preferred first-line regression signal for spawn routing work. The large `test_ninjector_spawn_injector.cpp` file still contains historical white-box cases, but those are now cleanup work instead of the authoritative definition of public routing behavior.

## Follow-up On 2026-05-17 Full Injector Suite Cleanup

After the focused subset was green, the remaining work in the full injector suite turned out to be exactly the expected regression debt:

- tests that still assumed `enable_zygote_control=true` automatically moved the default public route onto zygote-control
- tests that treated `strict` as "hard-stop on any zygote-control failure"
- tests that expected strict helper-local control to invoke the RPC install callback

Cleanups applied in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- zygote-control-specific cases now opt in explicitly with `--nook-strict-zygote-control`
- strict soft install failures now assert fallback/deferred behavior instead of unconditional abort
- helper/finalize assertions were updated to match the current control lifecycle and fallback formatting

Verification after cleanup:

- rebuilt:
  - `build/test_ninjector_spawn_injector_run10.exe`
- passed:
  - `build/test_ninjector_spawn_injector_run10.exe`

Practical result:

- the focused route subset remains the authoritative first-line signal
- the full injector suite is no longer fighting the current route model
- future routing work can now use both:
  - fast route subset for iteration
  - full injector suite for broader regression confidence

## Follow-up On 2026-05-17 Regression Harness Alignment

Two regression-harness issues were cleaned up after the route re-anchoring work:

- the source-string regression in [tests/headers/test_zygote_control_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_regressions.cpp) was still checking `server/server_handlers.cpp` for spawned-child promotion, but the actual promotion bridge now lives in [server/spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- the Windows host injector regression harness in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp) used throwing `fs::remove_all()` cleanup in many temporary directories, which could fail spuriously after runtime-artifact side effects had already removed child temp files

Cleanups applied:

- updated the source regression to assert against `server/spawn_controller.cpp` for `InjectSpawnChildAgent(...)`
- changed injector test temp-directory cleanup to best-effort `std::error_code` removal so missing child temp files no longer abort the test process

Verification:

- rebuilt:
  - `build/test_zygote_control_regressions.exe`
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
- passed:
  - `build/test_zygote_control_regressions.exe`
  - `build/test_ninjector_spawn_injector.exe`
  - `build/test_ninjector_spawn_injector_route_subset.exe`
