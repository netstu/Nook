# Nook DexDump Parity Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `nook-cli dexdump` recover real app main dex artifacts with behavior close to `frida-dexdump` while preserving Nook's existing CLI and metadata model.

**Architecture:** Upgrade the device-side search pipeline to use `map_list`-aware validation, broader deep-search heuristics, and less aggressive readonly-range filtering. Keep host-side orchestration in Python, but improve option derivation, candidate metadata handling, and regression coverage around the aboutbear failure mode.

**Tech Stack:** Python, QuickJS-compatible JavaScript, `unittest`, existing Nook host/device RPC and script-message transport.

---

### Task 1: Add failing host-side regression coverage for parity behavior

**Files:**
- Modify: `host/nook-py/tests/test_dexdump.py`

**Step 1: Write the failing tests**

Add tests that assert:

- `build_search_options()` no longer caps `max_range_size` at `16 MB`
- larger readonly regions remain eligible when `max_dex_size` is moderate
- metadata-preserving helpers can carry `declared_size`, `real_size`, and `fallback_size`

**Step 2: Run the tests to verify they fail**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m unittest host.nook-py.tests.test_dexdump
```

Expected:

- failure on the old `16 MB` ceiling assumptions

**Step 3: Implement minimal host changes**

Update the dexdump host module only enough to satisfy the new option-shaping assertions.

**Step 4: Run the tests to verify they pass**

Run the same command and confirm the new assertions pass.

### Task 2: Port `frida-dexdump` map-list size resolution into Nook's device script

**Files:**
- Modify: `host/nook-py/nook/dexdump.js`

**Step 1: Add the failing behavior target**

Base the implementation on the validated reference behavior:

- `getMapsAddress`
- `getMapsEnd`
- `verifyByMaps`
- `verifyIdsOff`
- `resolveRealDexSize`

**Step 2: Implement minimal device-side helpers**

Update candidate construction so each candidate can report:

- `declared_size`
- `real_size`
- `fallback_size`
- `maps_ok`
- `ids_ok`

**Step 3: Keep current Nook metadata shape**

Do not remove existing fields like `source`, `confidence`, `deep`, `range_base`, or `range_size`.

**Step 4: Re-run host tests**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m unittest host.nook-py.tests.test_dexdump
```

Expected:

- host tests stay green after the script-logic update

### Task 3: Broaden deep-search and readonly range coverage

**Files:**
- Modify: `host/nook-py/nook/dexdump.py`
- Modify: `host/nook-py/nook/dexdump.js`

**Step 1: Write the failing option expectations**

Add assertions for the new `max_range_size` derivation and any helper behavior that is host-observable.

**Step 2: Implement minimal changes**

Change:

- `max_range_size` derivation to a larger ceiling
- readonly range handling so realistic `20 MB` to `32 MB` app ranges are not skipped
- deep-search candidate generation so broken-header recovery matches the reference approach more closely

**Step 3: Verify host regressions**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m unittest host.nook-py.tests.test_dexdump host.nook-py.tests.test_cli
```

Expected:

- all tests pass

### Task 4: Validate on real device with `frida0x1`

**Files:**
- No source changes expected unless regression is found

**Step 1: Deploy the latest server if needed**

Use the latest built `nook-server` already known-good for binary payload transport.

**Step 2: Run smoke validation**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m nook.cli dexdump --spawn com.ad2001.frida0x1 -U --sleep 1000 --max-results 1 --message-timeout 60000 -o .\build\dexdump-frida0x1-parity
```

Expected:

- at least one dex artifact emitted
- no timeout

### Task 5: Validate on real device with `aboutbear`

**Files:**
- No source changes expected unless failure requires another debug cycle

**Step 1: Reproduce on the real APK**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m nook.cli dexdump --spawn cn.n1ng.aboutbear -U --sleep 1000 --message-timeout 180000 -o .\build\dexdump-aboutbear-parity
```

**Step 2: Compare against the APK baseline**

Compare dumped `classes.dex` with:

- `build/aboutbear-original-classes.dex`

Check:

- exact byte length
- exact SHA256

Expected:

- exact match for the main dex

### Task 6: Update docs if runtime behavior changed

**Files:**
- Modify: `docs/nook-dexdump-usage.md`
- Modify: `README.md`
- Modify: `host/nook-py/README.md`

**Step 1: Document the parity behavior**

Describe:

- default mode vs `--deep`
- larger readonly range coverage
- better broken-header recovery
- real-device validation notes

**Step 2: Re-run targeted tests**

Run:

```powershell
$env:PYTHONPATH='E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py'
python -m unittest host.nook-py.tests.test_dexdump host.nook-py.tests.test_cli
```

Expected:

- passing tests
