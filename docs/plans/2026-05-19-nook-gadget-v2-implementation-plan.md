# Nook Gadget V2 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Evolve `nook-gadget` beyond the current v1 bootstrap path by first adding Frida-Gadget-style runtime semantics (`listen` / `connect` / richer config / better host behavior), then upgrading APK patch compatibility toward a stronger loader/proxy model.

**Architecture:** Split implementation into two phases. Phase `v2.1` stabilizes the gadget runtime contract and host behavior while keeping the current patch/bootstrap model mostly intact. Phase `v2.2` then upgrades the patch/bootstrap architecture toward a proxy loader design without breaking the `v2.1` runtime/config/session contract.

**Tech Stack:** C++17, Android NDK, existing `src/gadget/*` runtime, `src/framework/NookComm*`, Python host CLI/SDK under `host/nook-py/nook/*`, PowerShell validation scripts under `tools/`, Python patch tool `tools/nook_patchapk.py`, host/device surface tests, Android APK rebuild/signing tools.

---

### Task 1: Write the failing config-surface tests for v2.1 interaction modes

**Files:**
- Modify: `host/nook-py/tests/test_patchapk_tool.py`
- Modify: `host/nook-py/tests/test_nook_gadget_apk_validation_surface.py`
- Modify: `tests/headers/test_nook_gadget_config.cpp`
- Modify: `src/gadget/nook_gadget_config.h`
- Modify: `src/gadget/nook_gadget_config.cpp`

**Step 1: Write the failing test**

Add tests that expect the gadget config model to support:

- `interaction.type = listen | connect`
- endpoint fields for host/address/port
- startup script load policy separate from `startup_mode`

Keep the first test focused on parse/serialize semantics, not runtime behavior.

**Step 2: Run test to verify it fails**

Run the smallest relevant checks:

```powershell
python .\host\nook-py\tests\test_patchapk_tool.py
```

and the narrow host-side config binary or source-surface test currently used for:

```powershell
tests\headers\test_nook_gadget_config.cpp
```

Expected: missing fields, parse mismatch, or unsupported config shape.

**Step 3: Write minimal implementation**

Extend gadget config parsing and default emission so v2.1 fields exist in a backward-compatible way:

- preserve v1 defaults
- add explicit interaction block
- add startup-script policy field

**Step 4: Run test to verify it passes**

Re-run the same narrow tests and expect green.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_patchapk_tool.py host/nook-py/tests/test_nook_gadget_apk_validation_surface.py tests/headers/test_nook_gadget_config.cpp src/gadget/nook_gadget_config.h src/gadget/nook_gadget_config.cpp
git commit -m "feat: add v2 gadget config surface"
```

### Task 2: Add failing patch-tool tests for v2.1 config emission

**Files:**
- Modify: `tools/nook_patchapk.py`
- Modify: `host/nook-py/tests/test_patchapk_bootstrap.py`
- Modify: `host/nook-py/tests/test_patchapk_backend.py`
- Modify: `tests/headers/test_nook_patchapk_surface.cpp`

**Step 1: Write the failing test**

Add tests that expect `nook_patchapk.py` to accept explicit v2.1 config arguments, for example:

- interaction type
- listen/connect host/port
- startup-script load policy

The tests should assert:

- CLI surface exists
- emitted `config.json` shape is correct
- legacy v1 invocation still emits a valid default config

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_patchapk_bootstrap.py
python .\host\nook-py\tests\test_patchapk_backend.py
```

Expected: parser or config assertions fail.

**Step 3: Write minimal implementation**

Extend `tools/nook_patchapk.py` with the narrowest CLI/config changes needed to satisfy the tests.

Do not implement connect runtime logic yet. Only make config emission authoritative.

**Step 4: Run test to verify it passes**

Re-run the same tests and expect green.

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests/test_patchapk_bootstrap.py host/nook-py/tests/test_patchapk_backend.py tests/headers/test_nook_patchapk_surface.cpp
git commit -m "feat: emit v2 gadget config metadata"
```

### Task 3: Add failing runtime tests for explicit listen-mode control startup

**Files:**
- Modify: `src/gadget/nook_gadget_runtime.cpp`
- Modify: `src/gadget/nook_gadget_runtime.h`
- Modify: `tests/headers/test_nook_gadget_control_channel.cpp`
- Modify: `tests/headers/test_nook_gadget_runtime_init.cpp`

**Step 1: Write the failing test**

Add tests that expect:

- `listen` is an explicit accepted interaction type
- runtime control initialization uses config rather than a hidden default
- unsupported interaction values fail cleanly

**Step 2: Run test to verify it fails**

Run the narrow gadget runtime/control tests that already cover control bring-up.

Expected: test failure because runtime still only understands the v1 default path.

**Step 3: Write minimal implementation**

Implement explicit `listen` handling in the gadget runtime:

- map legacy default to `listen`
- keep existing validated path intact
- preserve current runtime-dir / socket behavior unless config says otherwise

**Step 4: Run test to verify it passes**

Re-run the narrow runtime/control tests and expect green.

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_runtime.cpp src/gadget/nook_gadget_runtime.h tests/headers/test_nook_gadget_control_channel.cpp tests/headers/test_nook_gadget_runtime_init.cpp
git commit -m "feat: add explicit gadget listen mode"
```

### Task 4: Add failing runtime tests for connect-mode outbound session bring-up

**Files:**
- Modify: `src/gadget/nook_gadget_runtime.cpp`
- Modify: `src/framework/NookComm.cpp`
- Modify: `src/framework/NookCommInternal.h`
- Modify: `src/framework/NookCommInternal.cpp`
- Create or modify: `tests/headers/test_nook_gadget_runtime_bridge.cpp`
- Create or modify: `tests/headers/test_nook_gadget_startup_rpc.cpp`

**Step 1: Write the failing test**

Add host-side seam tests that expect:

- gadget runtime can attempt outbound `connect`
- connection failure is classified separately from config failure
- runtime can remain alive or fail according to config policy

Keep the first test dependency-injected. Do not require a real Android device here.

**Step 2: Run test to verify it fails**

Run the narrow runtime-bridge / startup RPC tests.

Expected: missing outbound connect path.

**Step 3: Write minimal implementation**

Introduce the minimal outbound control-session path needed for `interaction.type=connect`:

- config-driven target endpoint
- dedicated failure reporting
- no protocol fork

**Step 4: Run test to verify it passes**

Re-run the same focused tests and expect green.

**Step 5: Commit**

```bash
git add src/gadget/nook_gadget_runtime.cpp src/framework/NookComm.cpp src/framework/NookCommInternal.h src/framework/NookCommInternal.cpp tests/headers/test_nook_gadget_runtime_bridge.cpp tests/headers/test_nook_gadget_startup_rpc.cpp
git commit -m "feat: add gadget connect mode"
```

### Task 5: Add failing host CLI tests for first-class gadget interaction behavior

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Modify: `host/nook-py/nook/device.py`
- Modify: `host/nook-py/nook/session.py`
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/tests/test_client.py`
- Modify: `host/nook-py/tests/test_nook_cli_local_surface.py`

**Step 1: Write the failing test**

Add tests that expect:

- gadget sessions can be surfaced without server-specific assumptions
- connect-mode and listen-mode targets do not require different top-level UX
- CLI output distinguishes config errors, unreachable gadget, and startup-script failure

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_cli.py
python .\host\nook-py\tests\test_client.py
python .\host\nook-py\tests\test_nook_cli_local_surface.py
```

Expected: CLI/session assumptions do not match the new gadget contract yet.

**Step 3: Write minimal implementation**

Adjust host-side session/CLI logic so gadget interaction feels first-class while preserving existing flows.

Keep public surface minimal. Avoid inventing a second tool mode if existing commands can be extended cleanly.

**Step 4: Run test to verify it passes**

Re-run the same tests and expect green.

**Step 5: Commit**

```bash
git add host/nook-py/nook/cli.py host/nook-py/nook/device.py host/nook-py/nook/session.py host/nook-py/tests/test_cli.py host/nook-py/tests/test_client.py host/nook-py/tests/test_nook_cli_local_surface.py
git commit -m "feat: align host cli with gadget v2 sessions"
```

### Task 6: Add failing device-assisted validation for v2.1 listen and connect modes

**Files:**
- Modify: `tools/nook_gadget_apk_validation.ps1`
- Modify: `tools/nook_gadget_targetdemo_validation.ps1`
- Create: `tools/nook_gadget_connect_validation.ps1`
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- Modify: `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`
- Modify: `host/nook-py/tests/test_nook_gadget_targetdemo_validation_surface.py`

**Step 1: Write the failing test**

Add surface tests and script expectations for:

- explicit listen-mode validation
- explicit connect-mode validation
- PID-scoped log validation continuing to work

**Step 2: Run test to verify it fails**

Run the surface test for the validation scripts and observe missing switches or missing guidance.

**Step 3: Write minimal implementation**

Extend the PowerShell validators so they can validate both modes cleanly.

Do not weaken the existing authoritative login-path validation.

**Step 4: Run test to verify it passes**

Re-run the surface tests and the smallest script-only checks.

**Step 5: Commit**

```bash
git add tools/nook_gadget_apk_validation.ps1 tools/nook_gadget_targetdemo_validation.ps1 tools/nook_gadget_connect_validation.ps1 docs/plans/2026-05-18-nook-gadget-validation-status.md docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md host/nook-py/tests/test_nook_gadget_targetdemo_validation_surface.py
git commit -m "test: add gadget v2 validation flows"
```

### Task 7: Run real-device v2.1 validation and record evidence

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- Modify: `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`

**Step 1: Validate listen mode**

Run the updated `TargetDemo` validation in explicit listen mode.

Expected:

- patched app cold-starts
- packaged startup behavior matches config
- PID-scoped validation logs are clean

**Step 2: Validate connect mode**

Run the new connect validation flow against the same sample or another controlled target.

Expected:

- gadget initiates outbound control connection
- host receives the session
- script/RPC behavior works

**Step 3: Record exact evidence**

Write the actual validated commands, PID evidence, and success logs into the status documents.

**Step 4: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-validation-status.md docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md
git commit -m "docs: record gadget v2.1 validation evidence"
```

### Task 8: Write the failing loader/proxy design-anchored tests for v2.2

**Files:**
- Modify: `tools/nook_patchapk.py`
- Modify: `host/nook-py/tests/test_patchapk_tool.py`
- Modify: `tests/headers/test_nook_patchapk_surface.cpp`
- Create or modify: `docs/plans/2026-05-19-nook-gadget-v2-loader-notes.md`

**Step 1: Write the failing test**

Add surface tests that expect the patch tool to expose a distinct bootstrap strategy concept, for example:

- `bootstrap-mode=minimal`
- `bootstrap-mode=proxy-loader`

The tests should not yet require the full proxy implementation. They should only force the architectural seam to exist.

**Step 2: Run test to verify it fails**

Run the patch tool surface tests and expect parser/surface failure.

**Step 3: Write minimal implementation**

Add the narrowest bootstrap-mode seam and document loader notes for v2.2.

Keep `minimal` as the default until later tasks switch behavior.

**Step 4: Run test to verify it passes**

Re-run the same surface tests and expect green.

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests/test_patchapk_tool.py tests/headers/test_nook_patchapk_surface.cpp docs/plans/2026-05-19-nook-gadget-v2-loader-notes.md
git commit -m "feat: add bootstrap mode seam for gadget v2.2"
```

### Task 9: Add failing patch/bootstrap tests for proxy-loader `Application` takeover

**Files:**
- Modify: `tools/nook_patchapk.py`
- Modify: `host/nook-py/tests/test_patchapk_backend.py`
- Modify: `host/nook-py/tests/test_nook_gadget_compatibility_matrix_surface.py`
- Create or modify: `tests/headers/test_nook_gadget_entry_surface.cpp`

**Step 1: Write the failing test**

Add tests that expect:

- patch metadata can represent original `Application`
- proxy-loader mode records restoration data
- custom `Application` path is no longer validated only through a synthesized fallback story

**Step 2: Run test to verify it fails**

Run the patch/backend surface tests and expect missing metadata or unsupported bootstrap mode.

**Step 3: Write minimal implementation**

Implement the smallest proxy-loader metadata and patching behavior necessary to preserve original app lifecycle identity.

Do not broaden to provider takeover yet.

**Step 4: Run test to verify it passes**

Re-run the tests and expect green.

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py host/nook-py/tests/test_patchapk_backend.py host/nook-py/tests/test_nook_gadget_compatibility_matrix_surface.py tests/headers/test_nook_gadget_entry_surface.cpp
git commit -m "feat: add proxy loader application metadata"
```

### Task 10: Implement and validate v2.2 proxy-loader bootstrap on a real custom-Application sample

**Files:**
- Modify: `tools/nook_patchapk.py`
- Modify: `tools/nook_gadget_apk_validation.ps1`
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- Modify: `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`

**Step 1: Choose one real non-synthesized custom-Application APK**

Document the exact sample and why it covers the missing path.

**Step 2: Run failing validation first**

Show that the current minimal bootstrap path is weak or unsupported on that sample.

**Step 3: Implement minimal proxy-loader bootstrap**

Add only enough logic to:

- install the proxy loader
- preserve original `Application` identity/restore metadata
- keep the v2.1 gadget runtime contract untouched

**Step 4: Re-run real-device validation**

Expected:

- patched APK installs
- gadget starts
- original app startup still works
- gadget validation succeeds on the real custom-`Application` path

**Step 5: Commit**

```bash
git add tools/nook_patchapk.py tools/nook_gadget_apk_validation.ps1 docs/plans/2026-05-18-nook-gadget-validation-status.md docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md
git commit -m "feat: validate proxy loader bootstrap on real app"
```

### Task 11: Run final v2 audit and freeze the compatibility contract

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- Modify: `docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md`
- Create: `docs/plans/2026-05-19-nook-gadget-v2-status.md`

**Step 1: Enumerate completed v2.1 deliverables**

List the actual evidence for:

- config shape
- listen mode
- connect mode
- host behavior
- real-device validation

**Step 2: Enumerate completed v2.2 deliverables**

List the actual evidence for:

- proxy-loader bootstrap seam
- real custom-`Application` validation
- updated compatibility matrix

**Step 3: Freeze the supported v2 contract**

Document:

- what is supported
- what is still intentionally unsupported
- which validation commands are authoritative

**Step 4: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-validation-status.md docs/plans/2026-05-19-nook-gadget-compatibility-matrix.md docs/plans/2026-05-19-nook-gadget-v2-status.md
git commit -m "docs: freeze nook gadget v2 contract"
```
