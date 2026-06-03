# CLI Frida-Style Output Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rework Nook's human-facing CLI output so `spawn`, `attach`, `repl`, and script-message rendering feel much closer to Frida while preserving existing `--json` behavior.

**Architecture:** Add a dedicated `output.py` rendering helper inside the host Python package. Move non-JSON formatting policy there, including prefixes, prompt rendering, tables, banner, and script-message presentation. Keep protocol and JSON output unchanged.

**Tech Stack:** Python CLI host, `unittest`, existing fake-device CLI tests

---

### Task 1: Add failing tests for Frida-style control output

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing tests**

Add tests covering:

- `spawn` non-JSON output uses Frida-style status lines
- `attach` non-JSON output uses Frida-style status lines
- REPL prompt uses the new Frida-like shape

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- FAIL because current output still uses `spawn response ok`, `attach ok`, and old prompt format

### Task 2: Add failing tests for script-message rendering

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing tests**

Add tests covering:

- single-line `send` payload prints raw payload
- multi-line payload prints as multi-line block
- unparseable message falls back to raw legacy form
- `--json` still emits the old structured JSON line

**Step 2: Run test to verify it fails**

Run the same test command again.

Expected:

- FAIL because script messages still print as `script message: ... json=...`

### Task 3: Add the output helper module

**Files:**
- Create: `host/nook-py/nook/output.py`
- Modify: `host/nook-py/nook/__init__.py`

**Step 1: Write minimal implementation**

Add:

- color enum / helper
- `Console` renderer
- prompt formatter
- script-message formatter
- basic status helpers

Expose version support through `__init__.py` if needed by banner rendering.

**Step 2: Run tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- some tests still fail until `cli.py` is rewired

### Task 4: Rewire `cli.py` to use the renderer

**Files:**
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Minimal integration**

Replace scattered non-JSON `print(...)` calls with renderer methods for:

- apps
- processes
- spawn
- attach
- resume
- script load / unload
- REPL help
- REPL info where relevant
- script-message rendering

Keep `--json` paths unchanged.

**Step 2: Run tests to verify they pass**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- PASS

### Task 5: Update documentation examples

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Update user-facing examples**

Refresh example snippets that currently show the old CLI strings so they match
the new Frida-style terminal output.

**Step 2: Re-run tests**

Run the same CLI test suite again.

Expected:

- PASS

### Task 6: Final verification

**Files:**
- No new files

**Step 1: Run verification**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- PASS with clean output

**Step 2: Summarize remaining boundaries**

Record that:

- human CLI output changed
- `--json` did not
- transport / protocol did not
