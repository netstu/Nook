# Nook Gadget Startup Script Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Frida-Gadget-style startup script path so `nook-gadget` can auto-load `assets/nook-gadget/startup.js` from a patched APK without requiring host-side `attach -l`.

**Architecture:** Keep the feature thin and layered. Extend the patch tool to package a startup script asset and config metadata, add a tiny gadget config/asset reader in the runtime, and reuse the existing script runtime by exposing one gadget-local "create and load from source" entrypoint. On failure, log and fall back to attach/control-only mode instead of failing gadget initialization.

**Tech Stack:** Python 3 patch tooling, C++17 gadget/runtime code, Android NDK build, existing Nook script runtime bridge, APK asset/config packaging, header/string regression tests, Python unit tests, real-device `TargetDemo` validation.

---

### Task 1: Add failing patch-tool tests for startup script packaging

**Files:**
- Modify: `host/nook-py/tests/test_patchapk_tool.py`
- Modify: `host/nook-py/tests/test_patchapk_backend.py`
- Modify: `tests/headers/test_nook_patchapk_surface.cpp`

**Step 1: Write the failing Python test for asset emission**

Add one focused test that runs `nook_patchapk.py` with `--startup-script <file>` and expects:

- `assets/nook-gadget/startup.js` exists in the output APK
- `assets/nook-gadget/config.json` contains a `startup_script` object
- the configured asset path is `assets/nook-gadget/startup.js`

**Step 2: Write the failing compatibility test**

Add one test that runs without `--startup-script` and expects:

- current config output still exists
- no startup script asset is required

**Step 3: Add a narrow surface regression**

Update `tests/headers/test_nook_patchapk_surface.cpp` to assert source-level support for:

- `--startup-script`
- `assets/nook-gadget/startup.js`
- `startup_script`

**Step 4: Run the narrow tests and confirm they fail**

Run:

```bash
python -m unittest discover -s host\nook-py\tests -p "test_patchapk_tool.py"
python -m unittest discover -s host\nook-py\tests -p "test_patchapk_backend.py"
tests\headers\test_nook_patchapk_surface.exe
```

Expected:

- failures due to missing startup-script support

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_patchapk_tool.py host/nook-py/tests/test_patchapk_backend.py tests/headers/test_nook_patchapk_surface.cpp
git commit -m "test: add failing startup script patchapk coverage"
```

### Task 2: Implement patch-tool support for startup script assets

**Files:**
- Modify: `tools/nook_patchapk.py`
- Modify: `tools/nook_gadget_patch_smoke.ps1`
- Modify: `tools/nook_gadget_targetdemo_validation.ps1`

**Step 1: Add parser support**

Extend `nook_patchapk.py` with:

- `--startup-script <path>`

Update the plan/config generation helpers so the startup script is represented in config only when present.

**Step 2: Emit the startup script asset**

Add the minimal logic to:

- copy the host script to `assets/nook-gadget/startup.js`
- preserve current behavior when no startup script is provided

**Step 3: Extend config emission**

Update default/derived config generation to include:

```json
"startup_script": {
  "mode": "asset",
  "path": "assets/nook-gadget/startup.js",
  "required": false
}
```

only when startup-script packaging is requested.

**Step 4: Update smoke scripts**

Make the helper scripts document/pass through the new `--startup-script` option for `TargetDemo` validation.

**Step 5: Re-run the narrow tests and confirm they pass**

Run:

```bash
python -m unittest discover -s host\nook-py\tests -p "test_patchapk_tool.py"
python -m unittest discover -s host\nook-py\tests -p "test_patchapk_backend.py"
tests\headers\test_nook_patchapk_surface.exe
```

Expected:

- all three pass

**Step 6: Commit**

```bash
git add tools/nook_patchapk.py tools/nook_gadget_patch_smoke.ps1 tools/nook_gadget_targetdemo_validation.ps1
git commit -m "feat: package nook gadget startup scripts"
```

### Task 3: Add failing runtime tests for gadget config and fallback behavior

**Files:**
- Create: `src/gadget/nook_gadget_config.h`
- Create: `src/gadget/nook_gadget_config.cpp`
- Modify: `tests/headers/test_nook_gadget_runtime_init.cpp`
- Modify: `tests/headers/test_nook_gadget_runtime_bridge.cpp`
- Create or modify: `tests/headers/test_nook_gadget_startup_script.cpp`

**Step 1: Write a failing config parse test**

Add a small host-side test for:

- config with no startup script
- config with one asset startup script
- malformed config rejected or treated as missing startup script

**Step 2: Write a failing runtime fallback test**

Add a test seam around gadget runtime startup that expects:

- startup script loader is called when config provides one
- startup script failure does not make `InitializeRuntime()` fail
- runtime remains initialized after fallback

Use fake readers/loaders instead of real Android asset I/O in host tests.

**Step 3: Run the narrow tests and confirm they fail**

Run the dedicated header/runtime test executables for the touched files.

Expected:

- missing config module
- missing fallback path

**Step 4: Commit**

```bash
git add src/gadget/nook_gadget_config.h src/gadget/nook_gadget_config.cpp tests/headers/test_nook_gadget_runtime_init.cpp tests/headers/test_nook_gadget_runtime_bridge.cpp tests/headers/test_nook_gadget_startup_script.cpp
git commit -m "test: add failing gadget startup script runtime coverage"
```

### Task 4: Implement gadget config loading and startup-script bootstrap

**Files:**
- Modify: `src/gadget/nook_gadget_runtime.h`
- Modify: `src/gadget/nook_gadget_runtime.cpp`
- Modify: `src/gadget/nook_gadget_entry.cpp`
- Create: `src/gadget/nook_gadget_config.h`
- Create: `src/gadget/nook_gadget_config.cpp`

**Step 1: Add the config model**

Implement a tiny config structure that can represent:

- no startup script
- one asset-backed startup script
- `required` flag

**Step 2: Add runtime-owned startup bootstrap**

After control + bridge init succeed, run gadget-owned startup bootstrap that:

- loads config
- resolves startup script path
- reads script source
- invokes a gadget-local script loader

**Step 3: Implement fallback behavior**

If any part fails:

- log the failure
- mark fallback to attach-only mode
- still return `NOOK_STATUS_OK` from gadget init

Do not fail app startup.

**Step 4: Add explicit log markers**

Add concise log points for:

- config read success/failure
- startup script found/missing
- startup script create success/failure
- startup script load success/failure
- fallback activation

**Step 5: Re-run the narrow runtime tests and confirm they pass**

Run the dedicated gadget runtime/header test executables.

Expected:

- config/runtime tests pass
- fallback behavior is covered

**Step 6: Commit**

```bash
git add src/gadget/nook_gadget_runtime.h src/gadget/nook_gadget_runtime.cpp src/gadget/nook_gadget_entry.cpp src/gadget/nook_gadget_config.h src/gadget/nook_gadget_config.cpp
git commit -m "feat: auto-load startup script in nook gadget"
```

### Task 5: Add a gadget-local script load entrypoint with failing-first tests

**Files:**
- Modify: `src/agent_runtime/nook_script_runtime_bridge.h`
- Modify: `src/agent_runtime/nook_script_runtime_bridge.cpp`
- Modify: `tests/communication/test_js_runtime_native_attach.cpp`
- Create or modify: `tests/headers/test_nook_gadget_runtime_bridge.cpp`

**Step 1: Write the failing test**

Add one small test seam proving the runtime can:

- create a script from source in-process
- load it without going through host attach/create/load transport

Keep the new API thin, for example a helper like:

- `LoadEmbeddedStartupScript(const char* source, const char* name, uint32_t* script_id)`

**Step 2: Run the narrow test and confirm it fails**

Expected:

- missing bridge API

**Step 3: Implement the minimal bridge entry**

Reuse existing script registry/runtime internals. Do not add a parallel script subsystem.

**Step 4: Re-run the narrow tests and confirm they pass**

Expected:

- gadget-local startup load path succeeds

**Step 5: Commit**

```bash
git add src/agent_runtime/nook_script_runtime_bridge.h src/agent_runtime/nook_script_runtime_bridge.cpp tests/communication/test_js_runtime_native_attach.cpp tests/headers/test_nook_gadget_runtime_bridge.cpp
git commit -m "feat: expose gadget-local startup script load entry"
```

### Task 6: Update validation docs and add a real-device startup-script target flow

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-targetdemo-validation.md`
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`
- Modify: `host/nook-py/tests/test_nook_gadget_targetdemo_validation_surface.py`

**Step 1: Write the failing doc/test update**

Extend the validation-surface test to require documentation of:

- `--startup-script`
- startup-without-attach validation
- attach used only for observation/debugging after auto-start

**Step 2: Run the doc test and confirm it fails**

Run:

```bash
python -m unittest discover -s host\nook-py\tests -p "test_nook_gadget_targetdemo_validation_surface.py"
```

Expected:

- doc wording missing startup-script validation flow

**Step 3: Update validation docs**

Document the new authoritative flow:

- patch APK with login startup script
- install
- launch app
- do not `attach -l`
- trigger login hook
- optional later `attach --usb` only for logs/debug

**Step 4: Re-run the doc test and confirm it passes**

Run:

```bash
python -m unittest discover -s host\nook-py\tests -p "test_nook_gadget_targetdemo_validation_surface.py"
```

Expected:

- pass

**Step 5: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-targetdemo-validation.md docs/plans/2026-05-18-nook-gadget-validation-status.md host/nook-py/tests/test_nook_gadget_targetdemo_validation_surface.py
git commit -m "docs: add startup script gadget validation flow"
```

### Task 7: Run focused verification and capture the real-device pass criteria

**Files:**
- Modify: `docs/plans/2026-05-18-nook-gadget-validation-status.md`

**Step 1: Build the gadget artifact**

Run:

```powershell
E:\SDK\ndk\25.1.8937393\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk APP_MODULES=nook_gadget -j4
```

Expected:

- `libs/arm64-v8a/libnook-gadget.so` updated successfully

**Step 2: Patch `TargetDemo` with startup script**

Run the patch helper with the login-page startup script wired in.

Expected:

- APK contains `assets/nook-gadget/startup.js`
- config contains startup-script metadata

**Step 3: Install and launch the APK**

Expected:

- app starts normally
- no crash from gadget init

**Step 4: Validate no-manual-load login hook**

Do not run `attach -l`.

Instead:

- launch app
- navigate to login
- submit password

Expected:

- hook behavior is already active from startup script

**Step 5: Validate post-start attach still works**

Run a plain attach for observation/debugging and confirm the process remains controllable.

**Step 6: Record the result**

Update `docs/plans/2026-05-18-nook-gadget-validation-status.md` with:

- pass/fail
- exact command used
- exact observed runtime behavior
- any remaining limitations

**Step 7: Commit**

```bash
git add docs/plans/2026-05-18-nook-gadget-validation-status.md
git commit -m "test: validate gadget startup script on targetdemo"
```
