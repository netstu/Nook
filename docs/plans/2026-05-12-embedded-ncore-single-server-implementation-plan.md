# Embedded Ncore Single-Server Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make stable default `spawn` work with only `nook-server` deployed on device by using embedded `ncore` memfd delivery as the primary backend.

**Architecture:** Keep `legacy ncore` semantics, but change its default delivery from sidecar file to embedded memfd. Preserve sidecar `libncore.so` only as an explicit fallback/debug path. Do not expand scope into restoring `zygote-control` as the default backend.

**Tech Stack:** `nook-server`, `libncore.so` embedded blob generation, `NinjectorSpawnInjector`, Android ptrace/dlopen/memfd injection.

---

### Task 1: Lock the new default boundary in tests

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_ninjector_spawn_injector.cpp`

**Step 1: Write the failing test**

- Add a regression that models stable default spawn with no sidecar `libncore.so` present.
- Assert that the embedded path is chosen first and no file materialization is required for the default path.

**Step 2: Run test to verify it fails**

- Run the existing local spawn-injector test target if available in the environment.
- Expected: current behavior still permits or prefers sidecar/file assumptions in the stable path.

**Step 3: Write minimal implementation expectation updates**

- Update assertions so stable default behavior means embedded `ncore`, not file-backed `libncore.so`.

**Step 4: Run test to verify it passes**

- Re-run the same target.

### Task 2: Change stable default spawn to embedded ncore-first, sidecar opt-in

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\ninjector_spawn_injector.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\server_runtime.cpp`

**Step 1: Identify current default path resolution**

- Trace where `config_.ncore_path` and embedded sentinel/file materialization are selected.

**Step 2: Write minimal implementation**

- Make default stable legacy spawn prefer embedded `ncore` directly.
- Gate file-backed `libncore.so` fallback behind an explicit env/config switch.
- Avoid default runtime-dir file materialization when embedded path is healthy.

**Step 3: Verify transaction cleanup**

- Ensure finalize/cleanup still works with embedded handle-only state.

### Task 3: Keep build output, remove default deployment dependency

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tools\build_single_server_package.ps1`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\plans\2026-05-11-project-sop-build-push-test.md`

**Step 1: Keep `libncore.so` as build-time artifact**

- Do not remove `nook_ncore` build target.
- Keep generating `server/generated/nook_embedded_ncore_blob.h`.

**Step 2: Change default packaging/deployment**

- Default single-server package should only stage `nook-server`.
- `libncore.so` should no longer be a required push artifact in the default path.

### Task 4: Rebuild embedded chain in the correct order

**Files:**
- Modify if needed: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tools\build_embedded_ncore_blob.ps1`

**Step 1: Build canonical `libncore.so`**

- Rebuild `libncore.so`.

**Step 2: Refresh embedded ncore blob**

- Regenerate `server/generated/nook_embedded_ncore_blob.h`.

**Step 3: Rebuild `nook-server`**

- Ensure final server binary includes the refreshed blob.

### Task 5: Verify on device with single visible artifact

**Files:**
- No code changes required

**Step 1: Deploy only `nook-server` to device**

- Clear `/data/local/tmp/nook`
- Push only `nook-server`

**Step 2: Run real-device spawn validation**

- User runs:
  - `nook-cli -U -f com.ad2001.frida0x1 -l ...\\script.js`

**Step 3: Check logs**

- Confirm:
  - embedded agent memfd path
  - embedded ncore path
  - no required `libncore.so` sidecar
  - `AGENT_READY stage=1 -> stage=0`
  - hook output

