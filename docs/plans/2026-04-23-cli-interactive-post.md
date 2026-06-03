# CLI Interactive Post Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Frida-like interactive post mode to `nook-cli` so a running `spawn/attach ... -l script.js --wait` session can forward JSON lines from stdin into the loaded script.

**Architecture:** Extend the existing `spawn` and `attach` CLI flows with an opt-in interactive flag. After script load, the CLI will keep the message stream alive and, when interactive mode is enabled, start a stdin reader thread that forwards each non-empty input line to the loaded script via `Script.post()`. Incoming script messages continue to print through the existing wait loop.

**Tech Stack:** Python, `argparse`, `threading`, current `Device` / `Session` / `Script` host SDK, `unittest`

---

### Task 1: Add failing tests for interactive stdin post

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`

**Step 1: Write the failing test**

Add tests that cover:
- `spawn ... -l script.js --resume --wait --interactive` reads one JSON line from stdin and forwards it to `script.post()`
- `attach ... -l script.js --wait --interactive --json` still streams inbound messages while stdin forwarding is enabled

**Step 2: Run test to verify it fails**

Run: `python -m unittest host/nook-py/tests/test_cli.py`

Expected: FAIL because `--interactive` is not recognized and no stdin forwarding exists.

### Task 2: Implement minimal interactive post loop

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Modify: `host/nook-py/tests/test_cli.py`

**Step 1: Add parser flags**

Add `--interactive` to `spawn` and `attach`.

**Step 2: Add stdin pump**

Implement a small helper that:
- reads one line at a time from stdin
- strips trailing newline
- skips blank lines
- forwards each line via `script.post(line)`
- exits cleanly on EOF

**Step 3: Integrate with wait mode**

When `--interactive` is enabled:
- require a loaded script
- start the stdin pump in a daemon thread
- keep the existing message wait loop active in the main thread

**Step 4: Run tests to verify they pass**

Run: `python -m unittest host/nook-py/tests/test_cli.py`

Expected: PASS

### Task 3: Update README and verify package

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Document interactive post**

Add an example like:

```powershell
nook-cli spawn com.demo.target -l .\hook.js --resume --wait --interactive --usb
```

Clarify that each stdin line is forwarded as one script post message.

**Step 2: Run verification**

Run:

```powershell
python -m unittest discover host/nook-py/tests
python -m compileall host\nook-py\nook host\nook-py\tests
```

Expected: all tests pass and compile succeeds.
