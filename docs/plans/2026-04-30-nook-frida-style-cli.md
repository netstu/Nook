# Nook Frida-Style CLI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `nook-cli` feel closer to Frida by adding a Frida-style top-level interactive entry, fixing banner ordering, and keeping `console.*` output distinct from `send(...)`.

**Architecture:** Keep the existing subcommand-based CLI as a compatibility layer, and add a second top-level parser path for Frida-style interactive invocation. Move banner printing out of the inner REPL loop and into interactive session setup so it prints once and in the right place. Refine host-side display logic so `console.*` and `send(...)` are presented as separate channels.

**Tech Stack:** Python `argparse`, existing `nook-cli` host code, Python `unittest`

---

### Task 1: Add failing CLI tests for banner ordering and Frida-style top-level invocation

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Reference: `host/nook-py/nook/cli.py`

**Step 1: Write the failing tests**

Add tests covering:

1. `nook-cli -U -f com.demo.target -l script.js` is accepted and enters interactive mode
2. `nook-cli -U com.demo.target -l script.js` is accepted and enters interactive mode
3. banner appears before `Loading ...` and `Resuming ...` in interactive spawn mode
4. banner is printed only once per interactive session

Reuse the existing fake device / fake stdin patterns already used by current REPL tests.

**Step 2: Run the targeted tests to verify they fail**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliParser host.nook-py.tests.test_cli.TestCliMain
```

Expected:

- parser failures for the new top-level Frida-style argv
- banner ordering assertion failures for current `repl` behavior

**Step 3: Keep the failing surface focused**

If the new tests fail for unrelated reasons, tighten them so the failures are specifically about:

- unsupported argv shape
- incorrect banner order

### Task 2: Implement Frida-style top-level interactive parser and routing

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Test: `host/nook-py/tests/test_cli.py`

**Step 1: Add a dedicated top-level Frida-style parser path**

Implement a parser path that supports:

```text
nook-cli -U -f <package> -l <script>
nook-cli -U <target> -l <script>
```

without disturbing existing subcommands.

Recommended structure:

1. detect whether argv starts with a known subcommand
2. if yes, use the current parser
3. otherwise, use a Frida-style top-level parser

**Step 2: Convert parsed top-level args into an internal interactive config**

Map:

- `-U` -> `--usb`
- `-f <package>` -> spawn mode
- positional target -> attach mode
- `-l <path>` -> startup script

Default this path to REPL behavior.

**Step 3: Run the targeted tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliParser host.nook-py.tests.test_cli.TestCliMain
```

Expected:

- parser tests now pass
- banner-order tests may still fail until Task 3

### Task 3: Move banner printing to interactive session setup and fix ordering

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Modify: `host/nook-py/nook/output.py`
- Test: `host/nook-py/tests/test_cli.py`

**Step 1: Write or tighten the failing banner-order test if needed**

Ensure there is a test that asserts:

1. banner is present
2. banner precedes `[*] Loading ...`
3. banner precedes `[*] Resuming ...`
4. banner appears once

**Step 2: Move banner emission out of `_run_repl()`**

Print the banner during interactive context setup, before script load/resume work begins.

Ensure:

- banner prints only for human-readable interactive sessions
- banner does not print for JSON mode

**Step 3: Run the targeted tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliMain
```

Expected:

- banner ordering tests pass
- existing REPL tests remain green

### Task 4: Refine `console.*` vs `send(...)` presentation

**Files:**
- Modify: `host/nook-py/nook/output.py`
- Modify: `host/nook-py/tests/test_cli.py`
- Reference: `src/agent_runtime/js_runtime.cpp`

**Step 1: Add failing tests for console-vs-send presentation**

Add tests showing:

1. decoded `send(...)` payloads continue to use the message-channel display
2. `console.log/info` output uses the log channel instead of being shown as raw transport output
3. `console.warn` and `console.error` remain distinct

Keep the expected output aligned with the current Frida-style direction chosen for Nook's shell.

**Step 2: Implement the minimal host-side formatting changes**

Use the decoded payload shape already emitted by the runtime:

- `type = "log"`
- `level = "info" | "warning" | "error"`

Do not change the host/device protocol unless strictly necessary.

**Step 3: Run the targeted tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliMain
```

Expected:

- console and send display tests pass

### Task 5: Update README to present Frida-style usage first

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Write the failing documentation check**

Run:

```powershell
rg -n "nook-cli -U -f|nook-cli -U com|repl spawn" host/nook-py/README.md
```

Expected:

- Frida-style examples are missing or not primary yet

**Step 2: Update CLI usage docs**

Document first:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
nook-cli -U com.demo.target -l .\hook.js
```

Then keep:

- `spawn`
- `attach`
- `repl spawn`
- `repl attach`

as compatibility or explicit command-mode examples.

**Step 3: Re-run the documentation check**

Run:

```powershell
rg -n "nook-cli -U -f|nook-cli -U com|repl spawn" host/nook-py/README.md
```

Expected:

- Frida-style examples are present

### Task 6: Run full CLI regression verification

**Files:**
- Use: `host/nook-py/tests/test_cli.py`

**Step 1: Run the full CLI test suite**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected:

- all CLI tests pass

**Step 2: Spot-check parser behavior manually**

Run:

```powershell
python -c "from nook.cli import main; raise SystemExit(main(['--help']))"
```

and if needed:

```powershell
python -c "from nook.cli import _build_parser; print(_build_parser().format_help())"
```

Expected:

- help output remains sane for legacy subcommands
- Frida-style top-level path does not break subcommand help

### Task 7: Make `spawn` and `attach` interactive by default, with `--oneshot` escape hatch

**Files:**
- Modify: `host/nook-py/nook/cli.py`
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/README.md`

**Step 1: Write the failing tests**

Add tests covering:

1. `nook-cli spawn com.demo.target --usb` enters REPL by default
2. `nook-cli attach com.demo.target --usb` enters REPL by default
3. `nook-cli spawn ... --oneshot` keeps the current non-interactive behavior
4. `nook-cli attach ... --oneshot` keeps the current non-interactive behavior

**Step 2: Run the targeted tests to verify they fail**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.CliTests
```

Expected:

- existing `spawn` / `attach` behavior exits immediately instead of entering REPL

**Step 3: Implement the minimal routing change**

Add `--oneshot` to `spawn` and `attach`, and route default subcommand behavior into the same interactive session path already used by `repl` and top-level Frida-style mode.

**Step 4: Run the targeted tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.CliTests
```

Expected:

- new interactive-default tests pass
- explicit `--oneshot` tests pass

**Step 5: Update README examples**

Document:

```powershell
nook-cli spawn com.demo.target --usb
nook-cli attach com.demo.target --usb
```

as interactive-default commands, and show `--oneshot` as the compatibility escape hatch.
