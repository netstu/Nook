# Symbi Experimental CLI And Minimal Zygote Gate Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Converge Nook spawn toward a Frida-like `symbi`-first model: keep the zygote side minimal, keep real runtime bring-up on the child side, preserve explicit `--symbi` semantics, and make the default spawn route use `symbi` first with legacy fallback still available.

**Architecture:** The default spawn backend is now `symbi-first` with legacy fallback. The explicit CLI/runtime `--symbi` route remains stricter and does not outer-fallback to legacy. The zygote-side `symbi` stub remains a minimal gate: match target package, report child hit, restore zygote state, and stop the child so the real runtime/agent bring-up stays on the child side.

**Tech Stack:** `nook-cli`, C++ server routing, `symbi` local injector, Android spawn/session registry flow, existing header-string regression tests, on-device `adb` validation.

---

## Design Summary

### Scope

- Add a dedicated `--symbi` CLI flag.
- Route that flag through host protocol and server spawn policy.
- Promote the default spawn route to `symbi-first` once device validation is green.
- Keep `--strict-zygote-control` separate from `--symbi`.
- Reuse current `symbi` prepare/callback/restore flow.
- Continue shrinking zygote-side stub responsibilities toward the Frida model.

### Non-Goals

- Do not revive full `zygote-control` as the mainline path.
- Do not remove `legacy-ncore`.
- Do not add SELinux patching.
- Do not rewrite runtime ownership architecture in this step.

### Frida Alignment Target

The specific Frida property to copy in this step is:

- zygote side does the minimum possible
- child side owns real runtime bring-up
- explicit `--symbi` remains observable and isolated even after default promotion
- default route should converge toward the same child-owned runtime model

This means Nook should treat `symbi` as:

- a zygote gate
- not a zygote-resident runtime

---

### Task 1: Add Explicit `--symbi` CLI Surface

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Modify: `src/framework/NookComm.cpp`
- Test: `tests/headers/test_nook_comm_zygote_control_opt_in_regression.cpp`
- Test: add or extend a small header/regression source if needed for `--symbi`

**Step 1: Write the failing regression**

- Add a regression asserting the host layer exposes an explicit `--symbi` option separately from `--strict-zygote-control`.
- Assert the transport/request layer carries a distinct backend selection instead of overloading strict zygote-control.

**Step 2: Run the regression to verify it fails**

Run the smallest available compile/run command for the touched regression executable.

Expected:
- the source-string or request-shape assertion fails because `--symbi` does not exist yet

**Step 3: Implement the minimal CLI/request change**

- Add `--symbi` to `nook-cli`
- propagate it into the spawn request/options object
- keep `--strict-zygote-control` untouched
- ensure the two flags are not implicitly coupled

**Step 4: Run the regression to verify it passes**

Expected:
- the new regression passes

**Step 5: Commit**

Commit message:

```bash
git commit -m "feat: add explicit symbi experimental cli flag"
```

---

### Task 2: Add Server-Side Experimental `symbi` Route Selection

**Files:**
- Modify: `server/ninjector_spawn_injector.cpp`
- Modify: `server/server_handlers.cpp`
- Modify: `server/spawn_controller.cpp`
- Test: `tests/communication/test_ninjector_spawn_injector_route_subset.cpp`
- Test: `tests/communication/test_ninjector_spawn_injector_finalize_retry_subset.cpp`

**Step 1: Write the failing route regression**

- Extend route subset tests to assert:
  - default spawn keeps selecting `legacy-ncore`
  - `--symbi` selects the `symbi` backend
  - `--strict-zygote-control` remains a separate route

**Step 2: Run the regression to verify it fails**

Expected:
- route assertions fail because `--symbi` is not recognized yet

**Step 3: Implement the minimal routing logic**

- add an explicit spawn backend selection enum/flag if needed
- plumb the host request option into `NinjectorSpawnInjector`
- log route selection clearly:
  - default legacy
  - explicit symbi
  - strict zygote-control
- ensure no fallback silently masquerades as another backend in the log line

**Step 4: Run the regression to verify it passes**

Expected:
- route tests pass

**Step 5: Commit**

Commit message:

```bash
git commit -m "feat: route explicit symbi spawn separately"
```

---

### Task 3: Narrow `symbi` Zygote Gate to Frida-Like Minimal Responsibilities

**Files:**
- Modify: `server/symbi_injector_local.cpp`
- Modify: `server/symbi/stub_src/stub.h`
- Modify: `server/symbi/stub_src/stub.c`
- Modify: `server/symbi/stub_src/offset_check.c`
- Regenerate: `server/symbi/stub_src/generated_stub.h`
- Test: `tests/headers/test_symbi_stub_minimal_helpers.cpp`
- Test: `tests/headers/test_symbi_restore_state_machine_surface.cpp`

**Step 1: Keep the failing test coverage current**

- Assert the stub keeps only the minimal helper surface required for:
  - package-name matching
  - callback handshake
  - child stop after ack
- Assert removed helper dependencies stay removed

**Step 2: Run the regression to verify the current source state**

Expected:
- tests fail if any new helper creep or wrong dependency remains

**Step 3: Implement the minimal zygote-gate behavior**

- keep package-name matching on `setArgV0(name)`
- keep local restore of original slot before continuing
- keep child notify/stop flow
- do not add runtime bootstrap responsibilities to the zygote stub
- keep heavy initialization out of the stub

**Step 4: Regenerate the stub blob**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_symbi_stub_header.ps1
```

**Step 5: Re-run the regressions**

Run:

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_stub_minimal_helpers.cpp -o tests/headers/test_symbi_stub_minimal_helpers.exe
tests/headers/test_symbi_stub_minimal_helpers.exe
tests/headers/test_symbi_restore_state_machine_surface.exe
```

Expected:
- all pass

**Step 6: Commit**

Commit message:

```bash
git commit -m "refactor: narrow symbi zygote gate responsibilities"
```

---

### Task 4: Make `--symbi` Validation Observable And Non-Ambiguous

**Files:**
- Modify: `server/server_handlers.cpp`
- Modify: `server/ninjector_spawn_injector.cpp`
- Modify: `server/server_main.cpp` if log ownership needs adjustment
- Test: `tests/headers/test_symbi_log_semantics_regressions.cpp`
- Test: extend source-string/log regression coverage if needed

**Step 1: Write failing observability assertions**

- assert logs clearly distinguish:
  - explicit `symbi` route
  - default legacy route
  - strict zygote-control route
- assert success/failure text identifies the backend explicitly

**Step 2: Run the regression to verify it fails**

Expected:
- source-string assertions fail until logs are updated

**Step 3: Implement precise route/stage logs**

- backend in route log
- backend in finalize log
- backend in timeout/failure detail where possible
- no misleading "zygote-control" label when the backend is actually `symbi`

**Step 4: Run the regression to verify it passes**

Expected:
- logging regression passes

**Step 5: Commit**

Commit message:

```bash
git commit -m "chore: clarify symbi backend logging"
```

---

### Task 5: Rebuild Single-Server Package And Run Device Validation SOP

**Files:**
- Modify if needed: `docs/plans/2026-05-11-project-sop-build-push-test.md`
- Artifact: `libs/arm64-v8a/nook-server`

**Step 1: Rebuild embedded artifacts in the correct order**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_symbi_stub_header.ps1
powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild
```

**Step 2: Push the rebuilt server and clear stale device state**

Run:

```powershell
adb push libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell su -c "chmod 755 /data/local/tmp/nook/nook-server && rm -f /data/local/tmp/nook/libncore.so /data/local/tmp/nook/libnook-agent.so /data/local/tmp/nook/libnook-zygote-helper.so /data/local/tmp/nook/server.out /data/local/tmp/nook/server.err"
```

**Step 3: Start the server**

Run:

```powershell
adb shell su -c "cd /data/local/tmp/nook && ./nook-server"
```

Or background if needed:

```powershell
adb shell su -c "cd /data/local/tmp/nook && nohup ./nook-server >/data/local/tmp/nook/server.out 2>/data/local/tmp/nook/server.err < /dev/null &"
```

**Step 4: Validate all three routes**

Run:

```powershell
nook-cli -U -f com.ad2001.frida0x1 -l tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
nook-cli -U -f com.ad2001.frida0x1 --symbi -l tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
nook-cli -U -f com.ad2001.frida0x1 --strict-zygote-control -l tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
```

Expected:
- default route still green
- `--symbi` works as isolated experimental route
- strict route behavior remains separately diagnosable

**Step 5: Confirm with device logs**

Run:

```powershell
adb logcat -d -v time -s NookServer NookNinjector NookNcore JavaHook
```

Look for:

- explicit backend route logs
- package match log on target app
- child callback / stop / resume flow
- no device reboot / white-screen regression

**Step 6: Commit**

Commit message:

```bash
git commit -m "build: validate explicit symbi experimental spawn route"
```

---

## Testing Notes

- Prefer source-string regressions first for route/log surface.
- Rebuild `generated_stub.h` whenever `stub.c` / `stub.h` changes.
- Rebuild single-server package whenever embedded payloads or server routing changes.
- Device validation must compare:
  - hook correctness
  - startup delay
  - timeout frequency
  - whether app enters UI normally

## Success Criteria

- `--symbi` exists as a first-class explicit experimental route.
- default spawn remains unchanged and green.
- `symbi` zygote stub remains minimal and package-gated.
- backend selection is unambiguous in logs.
- new package builds and pushes cleanly as single-file `nook-server`.

---

## Progress Update

### 2026-05-17: child-owned spawn context tightening

- Added a focused regression:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
- Tightened `symbi` so the zygote gate no longer prewarms:
  - `NOOK_RUNTIME_DIR`
  - `NOOK_SPAWN_TOKEN`
  on the zygote process before the callback handoff.
- Added `PrewarmSpawnChildContext()` in `server/ninjector_compat.cpp`.
- Moved `runtime_dir` / `spawn_token` delivery to the stopped child side for:
  - `SpawnViaSymbi()`
  - `SpawnViaSymbiEmbedded()`
- Kept the existing `symbi` semantics intact:
  - zygote side still only installs the temporary gate
  - child still stops after callback
  - host still injects the real runtime on the child side
- This is a concrete Frida-alignment improvement because it reduces zygote-owned mutable state without changing the default stable route.

### 2026-05-17: child-owned spawn token lifetime fix

- Symptom:
  - explicit `--symbi` reached child-side memfd injection, but host still timed out with:
    - `wait spawn response timed out`
  - default stable and strict zygote-control routes remained green.
- Root cause:
  - `InjectEmbeddedSoByPidAtomic()` injected the child successfully, but its cleanup path always
    unset `NOOK_SPAWN_TOKEN` from the target process.
  - For `NookAgentInitializeForSpawnChild`, runtime-stage `AGENT_READY` may be emitted later from
    spawn-gate bootstrap hooks or application lifecycle callbacks, not necessarily during the
    synchronous init call.
  - That meant the child could lose the authoritative spawn token before host-visible
    runtime-ready happened, so pending-spawn resolution never completed.
- Fix:
  - `server/ninjector_compat.cpp`
  - treat `NookAgentInitializeForSpawnChild` as a distinct atomic init class
  - preserve `NOOK_SPAWN_TOKEN` across injector cleanup for that path
  - keep immediate cleanup behavior for non-spawn-child atomic inject paths
- Regression coverage:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
  - asserts the atomic injector recognizes `spawn_child_init`
  - asserts `NOOK_SPAWN_TOKEN` is not unset during spawn-child cleanup

### 2026-05-17: runtime-stage spawn token self-cleanup

- Follow-up issue:
  - after preserving `NOOK_SPAWN_TOKEN` across child-owned atomic inject cleanup, the token now
    survives long enough for authoritative runtime-ready resolution, but it should not stay in the
    child environment indefinitely.
- Change:
  - `src/framework/NookComm.cpp`
  - add `ClearSpawnTokenAfterRuntimeReadyLocked(...)`
  - after a successful runtime-stage `AGENT_READY`, clear:
    - `g_spawn_token_override`
    - `NOOK_SPAWN_TOKEN` env
    when they match the token just sent
- Rationale:
  - preserve spawn identity until host-visible runtime-ready is complete
  - then drop residual spawn-only context from the child process
  - keep control-stage behavior unchanged so strict/helper-only zygote-control semantics are not
    disturbed
- Regression coverage:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
  - asserts runtime-stage `AGENT_READY` owns the token cleanup boundary

### 2026-05-17: external-agent symbi handoff aligned with child-owned model

- Remaining mismatch:
  - `SpawnViaSymbiEmbedded()` already delivered spawn context during the same child-owned inject
    phase.
  - `SpawnViaSymbi()` still used a separate `PrewarmSpawnChildContext()` attach before its
    host-side `dlopen`, which kept an older two-step child handoff model alive on the compatibility
    path.
- Change:
  - `server/ninjector_compat.cpp`
  - add `InvokeRemoteInitSymbolByHandleWithSpawnContext(...)`
  - update `SpawnViaSymbi()` so runtime dir + spawn token are supplied during the same
    child-owned `dlopen + init` stage
  - remove the standalone child env prewarm dependency from the external-agent symbi path
- Rationale:
  - keep both symbi variants on the same child-owned state boundary
  - reduce one more extra attach/detach seam on the stopped child
  - prevent future regressions where the external-agent path drifts away from the embedded path
- Regression coverage:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
  - asserts the external-agent symbi path logs a child-owned inject stage with runtime dir and
    spawn-token delivery
  - asserts the path no longer depends on `PrewarmSpawnChildContext(...)`

### 2026-05-17: remove obsolete standalone child prewarm helper

- Follow-up cleanup:
  - after both symbi variants moved to child-owned inject-stage spawn-context delivery,
    `PrewarmSpawnChildContext(...)` no longer had live callers.
- Change:
  - `server/ninjector_compat.cpp`
  - remove the dead standalone child env prewarm helper entirely
- Rationale:
  - keep the symbi handoff model unambiguous
  - prevent future regressions from accidentally reviving the older two-step child prewarm path
  - reduce redundant attach/detach helper surface in `ninjector_compat.cpp`
- Regression coverage:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
  - continues asserting no symbi child-owned handoff depends on standalone child prewarm

### 2026-05-17: symbi finalize ownership moved toward true child-owned state

- Remaining mismatch:
  - runtime bring-up for `--symbi` had already moved to a child-owned model, but server-side
    finalize ownership still depended on `shell_owner_state` carrying a synthetic `kSymbi`
    backend.
  - that kept `symbi` teardown coupled to legacy shell-owner compatibility semantics even though
    `symbi` no longer owns legacy prepare/clear lifecycle.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - broaden authoritative owner resolution so server-side finalize can resolve an owned spawn from:
    - `shell_owner_state` for legacy shell-owned paths
    - `spawn_state` for child-owned `symbi`
  - update `BuildPendingSpawnCommit(...)` so:
    - `legacy-ncore` still commits authoritative ownership through `shell_owner_state`
    - `symbi` now commits authoritative ownership directly in `spawn_state` with backend/id
  - update finalize failure write-back so legacy restores `shell_owner_state`, while child-owned
    backends restore `spawn_state`
- Rationale:
  - make `symbi` ownership semantics match its runtime model
  - reduce accidental dependence on shell-owner compatibility state
  - prepare for later cleanup where legacy and experimental backends are fully separated
- Regression coverage:
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - adds checks that:
    - `BuildPendingSpawnCommit(...)` keeps `symbi` authoritative owner in `spawn_state`
    - `CommitPendingSpawn(...)` does not mirror `symbi` into `shell_owner_state`

### 2026-05-17: deferred owner release now clears by authoritative backend

- Remaining mismatch:
  - after moving `symbi` authoritative ownership into `spawn_state`, deferred cleanup still used
    a broad compatibility clear path that reset both `shell_owner_state` and `spawn_state`
    together whenever the request/token matched.
  - that was still shaped like the older shell-owner model and obscured which backend actually
    owned the pending spawn record.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - update `ReleaseActiveOwnerAfterDeferredRouting(...)` to clear only the authoritative owner
    record selected by `ResolveAuthoritativeSpawnOwner(...)`
  - keep full two-state clearing only for the no-owner fallback case
  - update `symbi` route-result assertions so committed ownership is expected in `spawn_state`
    instead of `shell_owner_state`
- Rationale:
  - make deferred owner release follow the same ownership boundary as finalize
  - avoid silently preserving or dropping the wrong compatibility record
  - keep `legacy-ncore` and child-owned `symbi` on distinct server-side cleanup paths
- Verification:
  - `g++ -std=c++17 -I . -I include -I src -c tests/communication/test_ninjector_spawn_injector.cpp -o build/test-bin/test_ninjector_spawn_injector.o`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

### 2026-05-17: communication regressions now encode symbi as spawn-state owned

- Follow-up cleanup:
  - several communication tests still described `symbi` as if it were an explicit
    `shell_owner_state` owner, which no longer matches the server model after ownership was moved
    into `spawn_state`.
- Change:
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - rename the old shell-priority admission test to a `symbi`-specific ownership test
  - add a deferred-release regression that asserts:
    - matching `symbi` owner is cleared from `spawn_state`
    - shell compatibility residue without a backend is preserved
  - update `ApplySymbiRouteResult(...)` expectations so successful `symbi` commits are asserted in
    `pending_commit.spawn_state`
- Rationale:
  - keep regressions aligned with the current child-owned `symbi` architecture
  - prevent future changes from accidentally reintroducing shell-owned semantics for `symbi`
- Verification:
  - `g++ -std=c++17 -I . -I include -I src -c tests/communication/test_ninjector_spawn_injector.cpp -o build/test-bin/test_ninjector_spawn_injector.o`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

### 2026-05-17: zygote-control finalize regressions no longer depend on fake shell ownership

- Remaining mismatch:
  - several finalize/ownership regressions still modeled `zygote-control` as if it needed an
    authoritative `shell_owner_state.backend = kZygoteControl`.
  - in the current server model, zygote-control ownership is carried by the residual transaction;
    `spawn_state` only keeps compatibility token state for request correlation.
- Change:
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - update finalize-session and active-owner extraction regressions so they assert:
    - `finalize_owner = kZygoteControlOwned` comes from the transaction
    - `owned_spawn_state` may carry only token compatibility state with `backend = kNone`
    - retry preservation after finalize failure keeps the transaction plus compatibility token,
      without reviving a fake zygote-control shell owner record
- Rationale:
  - keep tests aligned with the actual ownership split already implemented in the injector
  - reduce pressure to preserve obsolete shell-owned zygote-control semantics in future refactors
- Verification:
  - `g++ -std=c++17 -I . -I include -I src -c tests/communication/test_ninjector_spawn_injector.cpp -o build/test-bin/test_ninjector_spawn_injector.o`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

### 2026-05-18: explicit symbi no longer falls back to legacy backend

- Remaining mismatch:
  - explicit `--nook-spawn-backend=symbi` still behaved like a soft preference in the policy
    layer, which let `legacy-ncore` win after symbi failure.
  - that made the explicit route semantics drift away from the Frida-like expectation where the
    chosen spawn backend should be observable and not silently replaced by a different backend.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - `BuildSpawnExecutionPolicy(...)` now disables outer legacy fallback when the request explicitly
    asks for `symbi`
  - keep internal symbi compatibility fallback behavior separate from outer route fallback
- Regression coverage:
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - add/update checks that:
    - explicit symbi policy does not allow legacy fallback
    - explicit symbi failure returns a symbi-only failure message
    - explicit symbi failure does not re-query the legacy backend path
  - `tests/communication/test_ninjector_spawn_injector_explicit_symbi_subset.cpp`
- Verification:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_explicit_symbi_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_explicit_symbi_subset.exe`
  - `.\build\test_ninjector_spawn_injector_explicit_symbi_subset.exe`

### 2026-05-18: default spawn can prefer symbi-first under a guarded server policy

- Remaining gap:
  - explicit `--symbi` was now cleanly isolated, but the default spawn route still hard-coded
    `legacy-default`, so there was no controlled way to validate a Frida-like default preference
    without changing the public CLI contract.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - add `NOOK_PREFER_SYMBI_BACKEND=1` as a guarded server-side policy that makes default spawn:
    - skip zygote-control
    - try `symbi` first
    - fall back to `legacy-ncore` on failure
  - keep explicit `--symbi` semantics stricter than the default guarded route
- Regression coverage:
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - add/update checks that:
    - default policy flips to `symbi-first` when the env is enabled
    - default spawn succeeds through `symbi` when available
    - default spawn falls back to legacy when preferred `symbi` fails
  - `tests/communication/test_ninjector_spawn_injector_default_symbi_subset.cpp`
  - `tests/headers/test_ninjector_spawn_route_log_regressions.cpp`
- Verification:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_default_symbi_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_default_symbi_subset.exe`
  - `.\build\test_ninjector_spawn_injector_default_symbi_subset.exe`
  - `g++ -std=c++17 -I . -I include -I src tests/headers/test_ninjector_spawn_route_log_regressions.cpp -o build/test_ninjector_spawn_route_log_regressions_default_symbi.exe`
  - `.\build\test_ninjector_spawn_route_log_regressions_default_symbi.exe`

### 2026-05-18: default spawn now promotes symbi-first, with explicit legacy opt-out

- Promotion step:
  - after validating the guarded policy on-device, the default server policy was promoted from
    `legacy-default` to `symbi-first`.
  - explicit `--symbi` remains stricter than the default route: explicit requests still do not
    outer-fallback to legacy, while default spawn may still fall back to legacy on symbi failure.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - `ShouldPreferSymbiBackendByDefault()` now:
    - prefers `symbi` by default
    - keeps `NOOK_PREFER_SYMBI_BACKEND=1` as a compatible explicit opt-in alias
    - accepts `NOOK_DISABLE_SYMBI_PREFERENCE=1` as an explicit opt-out back to legacy-default
  - `tests/communication/test_ninjector_spawn_injector.cpp`
  - update default policy and route tests so:
    - default spawn is `symbi-first`
    - disabled preference restores legacy-default behavior
- Regression coverage:
  - `tests/communication/test_ninjector_spawn_injector_default_symbi_subset.cpp`
  - `tests/communication/test_ninjector_spawn_injector_explicit_symbi_subset.cpp`
  - `tests/headers/test_ninjector_spawn_route_log_regressions.cpp`

### 2026-05-18: child-owned memfd write now prefers host-side `/proc/<pid>/fd/<n>` delivery

- Root-cause finding:
  - timing logs on the promoted default `symbi-first` route showed the zygote-side gate was not
    the slow stage.
  - the dominant latency was inside child-owned embedded agent delivery after the child had already
    been stopped by `symbi`.
  - observed timing on-device before this change:
    - `SpawnViaSymbiEmbedded: child stop ready ... total_ms=398`
    - `SpawnViaSymbiEmbedded: child-owned memfd inject timing ... inject_ms=3251 total_ms=3650`
  - this pointed at `RemoteWriteFullyToFd(...)`, which previously looped over:
    - `PTraceWrite(...)` into remote scratch
    - remote `write(...)` calls into the target memfd
- Change:
  - `server/ninjector_compat.cpp`
  - add `HostWriteFullyToRemoteProcFd(...)`
  - `RemoteWriteFullyToFd(...)` now:
    - first tries opening `/proc/<pid>/fd/<remote_fd>` on the host side
    - writes the embedded blob directly from the server process into the target memfd
    - falls back to the old ptrace scratch + remote `write(...)` loop if `/proc/<pid>/fd/<n>`
      access is unavailable on the device/runtime
- Rationale:
  - keep the single-file deployment model unchanged
  - avoid changing spawn route semantics or backend selection again
  - reduce the hottest cost in the current `symbi-first` path without making zygote-side logic
    heavier
- Regression coverage:
  - `tests/headers/test_ninjector_zygote_remote_scratch_regressions.cpp`
  - add source checks that:
    - the host-side `/proc/<pid>/fd` write helper exists
    - `RemoteWriteFullyToFd(...)` prefers the host write path first
    - the ptrace fallback path remains explicit and observable in logs
- Verification:
  - `g++ -std=c++17 -I . -I include -I src tests/headers/test_ninjector_zygote_remote_scratch_regressions.cpp -o build/test_ninjector_zygote_remote_scratch_regressions_fastwrite.exe`
  - `.\build\test_ninjector_zygote_remote_scratch_regressions_fastwrite.exe`
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector_default_symbi_subset.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp server/embedded_blob_defs.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_default_symbi_subset_fastwrite.exe`
  - `.\build\test_ninjector_spawn_injector_default_symbi_subset_fastwrite.exe`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1`

### 2026-05-17: finalize owner cleanup/write-back helpers unified

- Follow-up source cleanup:
  - the finalize path still had several small inline branches that treated shell-owned and
    child-owned backends differently in place, which made the ownership model harder to read even
    after the semantics were already corrected.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - add backend helpers for legacy shell-owned vs child-owned classification
  - add explicit helpers for:
    - clearing the authoritative owner slot
    - restoring retry state into the correct slot
    - checking whether a retry state is non-empty
  - use those helpers from:
    - `BuildPendingSpawnCommit(...)`
    - `TakeActiveOwnerForFinalize(...)`
    - `ReleaseActiveOwnerAfterDeferredRouting(...)`
    - `FinalizeSpawn(...)`
- Rationale:
  - keep the remaining ownership logic in one place
  - make future `agent-owned stable spawn` work less likely to reintroduce shell/child confusion
  - preserve all current backend behavior while reducing duplicated branching
- Verification:
  - `g++ -std=c++17 -I . -I include -I src -c tests/communication/test_ninjector_spawn_injector.cpp -o build/test-bin/test_ninjector_spawn_injector.o`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

### 2026-05-17: finalize local names now reflect owner model

- Follow-up cleanup:
  - the finalize path still used generic local names like `session` and backend-agnostic retry
    temporaries, which made the owned-state flow harder to read even after the behavior was
    correct.
- Change:
  - `server/ninjector_spawn_injector.cpp`
  - rename the local finalize variable to `finalize_session`
  - keep the helper-based owner cleanup/write-back flow, but make the call sites easier to scan
- Rationale:
  - reduce reader confusion around `FinalizeSpawn(...)`
  - keep source terminology aligned with the actual ownership model instead of the legacy shell
    abstraction
- Verification:
  - `g++ -std=c++17 -I . -I include -I src -c tests/communication/test_ninjector_spawn_injector.cpp -o build/test-bin/test_ninjector_spawn_injector.o`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`

### 2026-05-18: child-owned spawn conservatively arms the in-process spawn gate before JNIEnv is ready

- Root-cause finding:
  - default `symbi-first` already injects the full embedded agent on the child side
  - but the child-side gate arming decision still depended on `ShouldArmSpawnGateForCurrentProcess()`
  - in the earliest cold-start window, `JavaEnv` can still be unavailable even though the child is already the
    real spawn target and still carries `NOOK_SPAWN_TOKEN`
  - the old behavior returned `false` immediately when `JNIEnv*` was null
  - that made child-owned spawn occasionally skip the spawn gate entirely
  - once that happened, `Java.perform(...)` could still be waiting on the later loader/lifecycle ready path while
    startup-time Java methods like `get_random()` had already executed
- Change:
  - `src/framework/NookComm.cpp`
  - `ShouldArmSpawnGateForCurrentProcess()` now treats:
    - non-early process
    - `NOOK_SPAWN_TOKEN` present
    - `JNIEnv* == nullptr`
    as a conservative "arm the gate" case
  - ordinary attach/runtime processes still keep the old `JNIEnv == nullptr -> false` behavior
  - added a log marker:
    - `conservative spawn gate arming for spawned child without JNIEnv`
- Rationale:
  - keep the child-owned symbi architecture unchanged
  - avoid widening the zygote stub again
  - preserve normal attach/runtime behavior
  - reduce the race where startup-time Java methods run before the child-side Java hook bootstrap has a chance to gate app bootstrap
- Regression coverage:
  - `tests/headers/test_symbi_child_owned_spawn_context_regression.cpp`
  - add source assertions that:
    - conservative spawned-child gate arming is logged explicitly
    - the decision keys off `NOOK_SPAWN_TOKEN`
- Verification:
  - `g++ -std=c++17 -I . -I include -I src tests/headers/test_symbi_child_owned_spawn_context_regression.cpp -o build/test-bin/test_symbi_child_owned_spawn_context_regression_current.exe`
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_spawn_ready_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/handler/message_dispatcher.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test-bin/test_server_handlers_spawn_ready_subset_stage_current.exe`
  - `build/test-bin/test_server_handlers_spawn_ready_subset_stage_current.exe`
  - `powershell -ExecutionPolicy Bypass -File tools/build_single_server_package.ps1 -ForceRebuild`
