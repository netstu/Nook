# Nook Gadget Main-Repo Migration Checklist

## Main-Repo Handoff Summary

If the main-repo optimization session wants the shortest safe execution path,
follow this summary first:

1. copy the self-contained gadget additions from section **A**
2. manually merge only the gadget-related hunks from section **B**
3. skip every file listed in section **C**
4. run the focused validation commands from section **E**

The most important practical rules are:

- do **not** sync the whole fork back into the main repository
- do **not** replace shared files like `cli.py`, `session.py`, `device.py`,
  `NookComm.cpp`, or `nook_script_runtime_bridge.cpp` wholesale
- do **not** import `server/*spawn*`, `server/*ninjector*`, `server/symbi*`, or
  unrelated communication tests as part of the gadget merge-back

The intended end state of this migration is:

- main repo gains the validated `nook-gadget v2` runtime, patch tooling,
  validation scripts, and gadget tests
- main repo does **not** inherit unrelated spawn/server/symbi churn from the
  fork

If there is any doubt while merging a shared file, prefer the narrower choice:

- keep the main-repo version
- merge only the gadget-specific behavior explicitly listed in section **B**
- leave anything else for a separate workstream

## Purpose

This checklist defines how the completed `nook-gadget v2` work in
`E:\tmp\Nook-Gadget` should be migrated back into the main Nook repository
without pulling unrelated spawn/server/symbi changes into the merge.

This document is written for the separate main-repo optimization session that
will perform the migration work.

## Migration strategy

Do **not** copy the entire fork back into the main repository.

The worktree contains two different kinds of changes:

1. actual `nook-gadget` feature work
2. unrelated or mixed-in runtime/server/spawn changes already present in the
   forked workspace

The safe strategy is:

1. migrate self-contained gadget additions first
2. then manually merge the shared runtime/host integration hunks
3. explicitly skip unrelated server/spawn/symbi files

## A. Files that can be migrated directly

These files are mostly self-contained gadget additions and should be the first
batch moved into the main repository.

### Gadget runtime and public surface

- `src/gadget/`
- `include/nook/NookGadget.h`

### Patch and validation tooling

- `tools/nook_patchapk.py`
- `tools/nook_gadget_patch_smoke.ps1`
- `tools/nook_gadget_apk_validation.ps1`
- `tools/nook_gadget_targetdemo_validation.ps1`
- `tools/nook_gadget_connect_validation.ps1`
- `tools/nook_gadget_trigger_packaged_startup.ps1`
- `tools/nook_cli_local.ps1`

### Gadget examples and helper scripts

- `examples/communication/nook_gadget_smoke.cpp`
- `host/nook-py/java_perform_startup_login.js`

### Gadget-focused tests

- `host/nook-py/tests/test_patchapk_tool.py`
- `host/nook-py/tests/test_patchapk_backend.py`
- `host/nook-py/tests/test_patchapk_bootstrap.py`
- `host/nook-py/tests/test_nook_gadget_apk_validation_surface.py`
- `host/nook-py/tests/test_nook_gadget_compatibility_matrix_surface.py`
- `host/nook-py/tests/test_nook_gadget_connect_validation_surface.py`
- `host/nook-py/tests/test_nook_gadget_detached_send_surface.py`
- `host/nook-py/tests/test_nook_gadget_patch_smoke_surface.py`
- `host/nook-py/tests/test_nook_gadget_runtime_control_fallback_surface.py`
- `host/nook-py/tests/test_nook_gadget_script_registry_init_surface.py`
- `host/nook-py/tests/test_nook_gadget_targetdemo_validation_surface.py`
- `host/nook-py/tests/test_nook_gadget_trigger_packaged_startup_surface.py`
- `tests/headers/nook_gadget_entry_test_stubs.cpp`
- `tests/headers/nook_gadget_runtime_rpc_test_stubs.cpp`
- `tests/headers/test_nook_gadget_build_surface.cpp`
- `tests/headers/test_nook_gadget_config.cpp`
- `tests/headers/test_nook_gadget_control_channel.cpp`
- `tests/headers/test_nook_gadget_entry_surface.cpp`
- `tests/headers/test_nook_gadget_public_header.cpp`
- `tests/headers/test_nook_gadget_runtime_bridge.cpp`
- `tests/headers/test_nook_gadget_runtime_init.cpp`
- `tests/headers/test_nook_gadget_smoke_surface.cpp`
- `tests/headers/test_nook_gadget_smoke_workflow_surface.cpp`
- `tests/headers/test_nook_gadget_startup_rpc.cpp`
- `tests/headers/test_nook_gadget_startup_script.cpp`
- `tests/headers/test_nook_patchapk_surface.cpp`

### Gadget docs

- `docs/plans/2026-05-18-nook-gadget-design.md`
- `docs/plans/2026-05-18-nook-gadget-implementation-plan.md`
- `docs/plans/2026-05-18-nook-gadget-runtime-inventory.md`
- `docs/plans/2026-05-18-nook-gadget-startup-script-design.md`
- `docs/plans/2026-05-18-nook-gadget-startup-script-implementation-plan.md`
- `docs/plans/2026-05-18-nook-gadget-targetdemo-validation.md`
- `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`
- `docs/plans/2026-05-19-nook-gadget-v2-design.md`
- `docs/plans/2026-05-19-nook-gadget-v2-implementation-plan.md`
- `docs/plans/2026-05-19-nook-gadget-v2-loader-notes.md`
- `docs/plans/2026-05-19-nook-gadget-v2-status.md`
- `docs/plans/2026-05-19-nook-cli-patchapk-design.md`
- `docs/plans/2026-05-19-nook-cli-patchapk-implementation-plan.md`

## B. Files that must be merged manually

These files are shared integration points and should **not** be replaced
wholesale in the main repository.

Only the gadget-related hunks should be merged.

### Host CLI / session integration

- `host/nook-py/nook/cli.py`
- `host/nook-py/nook/patchapk.py`
- `host/nook-py/nook/device.py`
- `host/nook-py/nook/session.py`
- `host/nook-py/tests/test_cli.py`
- `host/nook-py/tests/test_client.py`
- `host/nook-py/tests/test_nook_cli_local_surface.py`

### Shared runtime / communication layer

- `src/framework/NookComm.cpp`
- `src/framework/NookCommInternal.cpp`
- `src/framework/NookCommInternal.h`
- `src/agent_runtime/nook_script_runtime_bridge.cpp`
- `src/agent_runtime/nook_script_runtime_bridge.h`

### Manual-merge scope for these files

When merging these shared files, keep only the `nook-gadget`-related behavior:

- explicit gadget `listen` mode support
- explicit gadget `connect` mode support
- packaged startup script auto-load support
- packaged startup manual trigger support through
  `nook.gadget.load-configured-startup`
- gadget session / CLI support on the host side
- detached `listen` fallback where startup still proceeds if control setup and
  runtime-ready notify cannot reach a live server

Do **not** use this migration to import unrelated spawn/session/server cleanup
work from the fork unless that same work is being intentionally merged for
another reason.

### Per-file keep list for section B

Use this table as the authoritative merge filter for the shared files.

#### `host/nook-py/nook/cli.py`

Keep:

- gadget-oriented CLI/session behavior already needed by current `listen` /
  `connect` flows
- `patchapk` legacy subcommand registration
- top-level help additions that surface the short `patchapk` examples
- `patchapk` parser help text, defaults, and common-path flags
- the thin dispatch glue that normalizes `patchapk` args and forwards them into
  `host/nook-py/nook/patchapk.py`
- gadget attach/call output classification improvements where they distinguish:
  - gadget config error
  - unreachable gadget / connection failure
  - packaged startup-script failure
- any small glue already required by:
  - `nook.gadget.load-configured-startup`
  - gadget-specific local validation helpers

Do not keep:

- unrelated spawn routing churn
- unrelated strict-zygote-control or symbi CLI behavior changes unless the
  main-repo session is already merging those separately

#### `host/nook-py/nook/patchapk.py`

Keep:

- this file in full if the main repo is taking the `nook-cli patchapk`
  productization slice
- current responsibilities:
  - tool discovery for `apktool`, `apksigner`, `zipalign`
  - repo-default gadget library resolution
  - repo-default debug keystore resolution
  - default signing credentials
  - thin argv construction for `tools/nook_patchapk.py`
  - optional adb install / launch orchestration
  - connect-mode pre-launch guidance

Do not widen its scope during migration:

- no extra adb/device orchestration beyond the current helper behavior
- no unrelated generic Android packaging helpers
- no host optimization refactors unrelated to `patchapk`

#### `host/nook-py/nook/device.py`

Keep:

- device/session helpers needed so gadget sessions can be surfaced without
  assuming only the legacy server path
- any small compatibility changes required for connect-mode gadget targets

Do not keep:

- unrelated device/session lifecycle changes that were added for other host
  optimization workstreams

#### `host/nook-py/nook/session.py`

Keep:

- session behavior needed for first-class gadget sessions
- any runtime identity or attach/call handling specifically required for:
  - packaged startup trigger RPC
  - gadget connect/listen parity

Do not keep:

- unrelated session cleanup, spawn-specific timeout policy, or generic host
  refactors not required by gadget validation

#### `host/nook-py/tests/test_cli.py`

Keep:

- tests covering gadget CLI behavior
- tests covering gadget error classification
- tests covering patchapk-adjacent CLI work if that command is being migrated in
  the same effort
- specifically keep the `patchapk`-focused tests added for:
  - parser defaults / common flags
  - wrapper mapping
  - tool discovery
  - default signing
  - install / launch orchestration
  - help / user-facing error surface

Do not keep:

- tests added only for unrelated spawn/symbi/strict-zygote-control work

#### `host/nook-py/tests/test_client.py`

Keep:

- client/session tests that specifically prove gadget session behavior remains
  first-class

Do not keep:

- unrelated client regressions with no gadget dependency

#### `host/nook-py/tests/test_nook_cli_local_surface.py`

Keep:

- the gadget-local CLI surface assertions that validate helper command wording
  or user-facing gadget guidance
- the `patchapk` CLI surface assertions added for:
  - parser defaults
  - embedded `patchapk` help examples

Do not keep:

- local CLI assertions that only serve unrelated host CLI work

#### `src/framework/NookCommInternal.h`

Keep:

- public/internal declarations needed by gadget runtime:
  - outbound control-channel readiness
  - runtime-ready notification
  - any helper declarations required by packaged startup RPC routing

Do not keep:

- declarations added only for unrelated server/spawn ownership cleanup

#### `src/framework/NookCommInternal.cpp`

Keep:

- implementation for internal RPC handler support required by gadget packaged
  startup triggering
- any internal callback refresh plumbing needed so gadget RPCs remain wired when
  no normal attach script is loaded

Do not keep:

- unrelated internal dispatch changes tied to spawn/session/server cleanup

#### `src/framework/NookComm.cpp`

Keep:

- explicit outbound connect support used by gadget `interaction.type=connect`
- helper path for `EnsureOutboundControlChannelReadyForCurrentProcess`
- helper path for `NotifyRuntimeReadyToServer`
- gadget-safe control initialization behavior used by `listen` mode
- any runtime-ready / connection behavior required so gadget packaged startup
  works in validated `connect` mode

Do not keep:

- unrelated route-state, owner-state, spawn-finalize, zygote-control, or
  injector-side changes

Important:

- the detached listen-mode fallback itself is implemented in
  `src/gadget/nook_gadget_runtime.cpp`, not in `NookComm.cpp`
- only merge the `NookComm.cpp` hunks genuinely required to support the gadget
  runtime APIs that `nook_gadget_runtime.cpp` now calls

#### `src/agent_runtime/nook_script_runtime_bridge.h`

Keep:

- declarations needed for gadget startup-script load support
- declarations needed for internal packaged-startup RPC load behavior

Do not keep:

- unrelated bridge surface expansion not required by gadget

#### `src/agent_runtime/nook_script_runtime_bridge.cpp`

Keep:

- runtime bridge behavior required so packaged startup scripts can be created
  and loaded from gadget startup
- detached-message-safe behavior already validated for packaged startup scripts
  where `send(...)` may be dropped without a control channel but script load
  still succeeds

Do not keep:

- unrelated script/runtime bridge refactors not needed by gadget validation

## C. Files that should not be migrated as part of gadget merge-back

These files are outside the intended gadget migration scope and should be left
to the main-repo optimization session's own workstream.

### Server / spawn / symbi / ninjector

- `server/ninjector_compat.cpp`
- `server/ninjector_compat.h`
- `server/ninjector_spawn_injector.cpp`
- `server/ninjector_spawn_injector.h`
- `server/server_handlers.cpp`
- `server/session_registry.cpp`
- `server/session_registry.h`
- `server/spawn_controller.cpp`
- `server/symbi/stub_src/generated_stub.h`
- `server/symbi/stub_src/offset_check.c`
- `server/symbi_injector_local.cpp`

### Unrelated communication / server tests

- `tests/communication/test_ninjector_spawn_injector.cpp`
- `tests/communication/test_ninjector_spawn_injector_route_subset.cpp`
- `tests/communication/test_nook_internal_rpc_dispatch.cpp`
- `tests/communication/test_server_handlers.cpp`
- `tests/communication/test_server_handlers_spawn_ready_subset.cpp`
- `tests/communication/test_session_registry.cpp`
- `tests/communication/test_ninjector_spawn_injector_default_symbi_subset.cpp`
- `tests/communication/test_ninjector_spawn_injector_explicit_symbi_subset.cpp`
- `tests/communication/test_server_handlers_attach_runtime_identity_focus.cpp`
- `tests/communication/test_server_handlers_spawn_finalize_cleanup_focus.cpp`
- `tests/communication/test_session_registry_runtime_identity_focus.cpp`

### Mixed or unrelated framework/runtime edits

- `src/framework/nook_agent_runtime.cpp`
- `src/java_hook/JavaHook.cpp`
- `src/java_hook/router/hook_engine.c`
- `src/java_hook/router/hook_engine.h`
- `src/java_hook/router/hook_engine_mem.c`
- `src/java_hook/router/hook_engine_redir.c`

### Non-source artifacts and local scratch

- any `*.exe`
- `base.apk`
- `output.apk`
- `tests/Test_Lab/Frida-Labs`
- pulled sample APKs or decoded APK trees under temporary build directories

## D. Recommended migration order

Use this order in the main repository:

1. migrate all files from section **A**
2. compile/resolve obvious include-path or registration issues
3. manually merge section **B** shared files
4. migrate gadget-facing tests from section **A** that depend on the shared-file
   merges
5. run focused validation
6. only after the gadget merge is stable, decide whether any unrelated section
   **C** work should be merged through a separate effort

## E. Focused validation after migration

At minimum, run these host tests:

```powershell
python .\host\nook-py\tests\test_cli.py
python .\host\nook-py\tests\test_nook_cli_local_surface.py
python .\host\nook-py\tests\test_patchapk_tool.py
python .\host\nook-py\tests\test_patchapk_backend.py
python .\host\nook-py\tests\test_patchapk_bootstrap.py
python .\host\nook-py\tests\test_nook_gadget_apk_validation_surface.py
```

If the main repo wants a lower-risk first pass before the heavier host CLI suite,
the focused `patchapk` subset validated in the temporary workspace was:

```powershell
python -m unittest `
  host.nook-py.tests.test_cli.CliTests.test_parser_supports_patchapk_minimal_defaults `
  host.nook-py.tests.test_cli.CliTests.test_parser_supports_patchapk_common_path_flags `
  host.nook-py.tests.test_cli.CliTests.test_patchapk_command_maps_minimal_defaults_into_patch_engine_wrapper `
  host.nook-py.tests.test_cli.CliTests.test_patchapk_command_maps_explicit_overrides_into_patch_engine_wrapper `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_discovers_tools_and_default_signing_from_environment `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_prefers_explicit_tool_overrides_and_can_disable_signing `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_raises_user_facing_error_when_apktool_missing `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_installs_and_launches_when_requested `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_rejects_launch_without_install `
  host.nook-py.tests.test_cli.CliTests.test_run_patchapk_connect_launch_emits_server_precondition_guidance `
  host.nook-py.tests.test_cli.CliTests.test_main_patchapk_help_shows_common_path_examples `
  host.nook-py.tests.test_cli.CliTests.test_main_patchapk_reports_user_facing_apktool_error `
  host.nook-py.tests.test_cli.CliTests.test_main_help_prints_frida_style_invocations
python .\host\nook-py\tests\test_nook_cli_local_surface.py
```

If the main repository also builds the local header-based gadget tests, run:

```powershell
tests\headers\test_nook_gadget_runtime_init.exe
tests\headers\test_nook_gadget_runtime_bridge.exe
tests\headers\test_nook_gadget_control_channel.exe
```

If the main repository preserves the same Android validation environment, the
preferred real-device spot checks are:

- `TargetDemo` listen baseline
- `TargetDemo` connect baseline
- one proxy-loader custom-`Application` sample

## F. Practical warning

The gadget migration is **not** hard because the gadget code itself is
especially tangled.

The real risk is accidentally bundling:

- spawn backend work
- server/session cleanup work
- symbi/ninjector experiments
- unrelated host CLI churn

Treat this as a selective merge-back, not a fork-wide sync.
