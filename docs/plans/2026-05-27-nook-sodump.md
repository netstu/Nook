# Nook SoDump Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a native `nook-cli sodump` workflow that dumps a named in-memory `.so`, rebuilds a repaired ELF artifact, and writes metadata for server, spawn, and gadget flows.

**Architecture:** Reuse the `dexdump` host/script transport pattern for module discovery and chunked byte export, then feed the raw dump into a new Nook-owned ELF repair module. Keep the command surface top-level in `nook.cli` and keep the repair code host-side so it can be unit tested without a device.

**Tech Stack:** Python 3, existing `nook-cli` argparse surface, Nook JS RPC scripts, Python `struct`/`hashlib`/`json`, pytest.

---

### Task 1: Add the `sodump` CLI surface

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Test: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing test**

Add a CLI unit test that invokes:

```python
["sodump", "com.demo.target", "-U", "--module", "libfoo.so"]
```

and asserts:

- the parsed command is `sodump`
- the normalized options reach `_run_sodump`
- `--gadget` remains available on the command

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_cli.py -k sodump -v
```

Expected: failure because the subcommand or runner does not exist yet.

**Step 3: Write minimal implementation**

- import `run_sodump`
- add `sodump` to `_KNOWN_SUBCOMMANDS`
- add help text block
- add parser and normalization path
- dispatch to `_run_sodump`

**Step 4: Run test to verify it passes**

Run the same `pytest` command.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 2: Implement host-side `sodump` orchestration

**Files:**
- Create: `host/nook-py/nook/sodump.py`
- Test: `host/nook-py/tests/test_sodump.py`

**Step 1: Write the failing test**

Add tests that cover:

- default output directory naming
- script source loading
- result metadata writing
- raw artifact write when repair is disabled
- partial-success behavior when repair raises

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_sodump.py -v
```

Expected: import or attribute failures.

**Step 3: Write minimal implementation**

Implement:

- `SODUMP_SCRIPT_NAME`
- `SoDumpArtifact` dataclass
- script source loader
- default output dir helper
- chunk collector adapted from `dexdump`
- `run_sodump(options, device, stdout=None, stderr=None)`

The runner should:

- attach or spawn
- create and load `sodump.js`
- resolve the target module
- begin a chunked module dump
- write `raw.so`
- optionally invoke the repair module
- always write `.json`

**Step 4: Run test to verify it passes**

Run the `test_sodump.py` suite.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 3: Add device-side module discovery and chunk export

**Files:**
- Create: `host/nook-py/nook/sodump.js`
- Test: `host/nook-py/tests/test_sodump.py`

**Step 1: Write the failing test**

Add a host-side fake-script test that expects these RPC methods:

- `listmodules`
- `findmodule`
- `beginmoduledump`

and verifies chunk payload handling matches the host collector contract.

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_sodump.py -k chunk -v
```

Expected: missing script method assumptions or collector mismatch.

**Step 3: Write minimal implementation**

Implement `rpc.exports` methods that:

- enumerate modules
- return exact-match module metadata
- make the range readable if needed
- emit chunked binary messages tagged with tokens and chunk indices

**Step 4: Run test to verify it passes**

Run the filtered `pytest` command again.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 4: Add a Nook-owned ELF repair module

**Files:**
- Create: `host/nook-py/nook/sofix/__init__.py`
- Create: `host/nook-py/nook/sofix/elf.py`
- Create: `host/nook-py/nook/sofix/rebuilder.py`
- Test: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the failing test**

Create focused unit tests for:

- ELF64 header parsing
- `PT_LOAD` program header rewriting for loaded images
- basic repaired image emission
- hard failure on non-ELF input

Use a synthetic minimal ELF64 sample in the test file to keep the fixture self-contained.

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_sofix.py -v
```

Expected: module import failures.

**Step 3: Write minimal implementation**

Implement a first-pass repair path that:

- validates ELF magic and class
- parses ELF64 header and program headers
- rewrites `PT_LOAD.p_offset = p_vaddr`
- rewrites `PT_LOAD.p_filesz = p_memsz`
- emits the modified image

Keep the interface shaped so later dynamic-section and section reconstruction can extend it.

**Step 4: Run test to verify it passes**

Run the `test_sofix.py` suite.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 5: Integrate repair into `sodump` metadata and failure policy

**Files:**
- Modify: `host/nook-py/nook/sodump.py`
- Modify: `host/nook-py/nook/sofix/rebuilder.py`
- Test: `host/nook-py/tests/test_sodump.py`

**Step 1: Write the failing test**

Add tests that verify:

- repair enabled writes `.fix.so`
- repair failure still preserves `.raw.so` and `.json`
- JSON records `fix_applied`, `fix_success`, and hashes

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_sodump.py -k fix -v
```

Expected: metadata fields missing or wrong.

**Step 3: Write minimal implementation**

Wire the repair result into the artifact writer and JSON serializer.

**Step 4: Run test to verify it passes**

Run the filtered `pytest` command again.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 6: Document user-facing `sodump` usage

**Files:**
- Modify: `README.md`
- Create: `docs/nook-sodump-usage.md`

**Step 1: Write the failing check**

Manually compare the documented usage against the implemented CLI:

- attach mode
- spawn mode
- gadget mode
- output layout
- known first-version limitations

**Step 2: Run check to verify a gap exists**

Run:

```powershell
nook-cli -h
```

Expected: no `sodump` usage documented yet.

**Step 3: Write minimal documentation**

Add concise usage examples and explain:

- what gets dumped
- where artifacts land
- what first-version repair does and does not do

**Step 4: Run check to verify it matches**

Re-run:

```powershell
nook-cli -h
```

and manually compare against the docs.

**Step 5: Commit**

Do not commit unless explicitly requested by the user.

### Task 7: Run regression and focused validation

**Files:**
- Test: `host/nook-py/tests/test_cli.py`
- Test: `host/nook-py/tests/test_dexdump.py`
- Test: `host/nook-py/tests/test_sodump.py`
- Test: `host/nook-py/tests/test_sofix.py`

**Step 1: Run focused test suite**

Run:

```powershell
python -m pytest .\host\nook-py\tests\test_cli.py .\host\nook-py\tests\test_sodump.py .\host\nook-py\tests\test_sofix.py .\host\nook-py\tests\test_dexdump.py -v
```

Expected: pass.

**Step 2: Run CLI smoke help checks**

Run:

```powershell
python -m nook.cli -h
python -m nook.cli sodump -h
```

Expected: help text includes `sodump`.

**Step 3: Manual device validation**

Run on a real rooted device after implementation:

```powershell
nook-cli sodump com.demo.target -U --module libtarget.so
nook-cli sodump --spawn com.demo.target -U --module libtarget.so
nook-cli sodump -U --gadget com.demo.target --module libtarget.so
```

Expected:

- raw artifact exists
- JSON exists
- repaired artifact exists for ordinary ELF64 targets

**Step 4: Commit**

Do not commit unless explicitly requested by the user.
