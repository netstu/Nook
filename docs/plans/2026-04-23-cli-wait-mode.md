# CLI Wait Mode Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Frida-like `--wait` mode to `nook-cli` so `spawn` and `attach` can keep the session alive and continuously print incoming script messages.

**Architecture:** Extend the existing Python CLI instead of adding a new command. `spawn` and `attach` will optionally enter a blocking message loop after script load, using the existing `Device.wait_for_script_message()` path. Human-readable output and JSON line output will share the same normalized message formatter.

**Tech Stack:** Python, `argparse`, current `Device` / `Session` / `Script` host SDK, `unittest`

---

### Task 1: Add failing CLI tests for wait mode

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing test**

Add tests that cover:
- `spawn ... -l script.js --resume --wait` keeps reading messages until interrupted
- `attach ... -l script.js --wait --json` emits one JSON object per message line

**Step 2: Run test to verify it fails**

Run: `python -m unittest host.nook-py.tests.test_cli`

Expected: FAIL because `--wait` is not a recognized CLI argument yet.

### Task 2: Implement minimal wait loop in CLI

**Files:**
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Add parser flags**

Add `--wait` and `--message-timeout` to `spawn` and `attach`.

**Step 2: Add message formatter**

Implement a small helper that:
- prints `script message: script_id=... json=... data_len=...` in text mode
- prints one JSON object per line in `--json` mode

**Step 3: Add blocking wait loop**

After successful script load:
- repeatedly call `device.wait_for_script_message(...)`
- print each message
- break on `KeyboardInterrupt`

**Step 4: Run tests to verify they pass**

Run: `python -m unittest host/nook-py/tests/test_cli.py`

Expected: PASS

### Task 3: Update README and verify whole Python host package

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Document wait mode**

Add examples for:
- `nook-cli spawn ... -l hook.js --resume --wait`
- `nook-cli attach ... -l hook.js --wait`

Clarify that first version is message-stream only, not a full REPL.

**Step 2: Run verification**

Run:

```powershell
python -m unittest discover host/nook-py/tests
python -m compileall host\nook-py\nook host\nook-py\tests
```

Expected: all tests pass and compile succeeds.
