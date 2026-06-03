# Nook Persistent REPL Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a persistent `nook-cli repl` mode that keeps a single `Device + Session + Script` alive and supports iterative `post`, `call`, `load`, `reload`, `unload`, and `resume` actions in one host process.

**Architecture:** Build the feature entirely in the Python host CLI layer, reusing the current `Device`, `Session`, and `Script` APIs. Introduce a small REPL context plus a message-loop thread, then extend parser coverage, command dispatch, and tests without changing the wire protocol.

**Tech Stack:** Python 3, `argparse`, existing Nook Python SDK (`device.py`, `session.py`, `script.py`), current fake-device CLI tests, Android device smoke via `nook-cli`.

---

### Task 1: Add failing parser tests for `repl`

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add parser-level coverage for:

- `nook-cli repl spawn com.demo.target -l hook.js --resume --usb`
- `nook-cli repl attach com.demo.target -l hook.js --usb`

Assert the parsed namespace contains:

- `command == "repl"`
- `repl_mode == "spawn"` or `repl_mode == "attach"`
- the correct target field
- `script_path`
- `resume` only on spawn

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliParser
```

Expected: parser failure because `repl` subcommand does not exist yet.

**Step 3: Write minimal implementation**

Add a `repl` top-level parser in `host/nook-py/nook/cli.py` with nested `spawn` and `attach` subparsers.

Use the same connection arguments already used by:

- `spawn`
- `attach`

Initial parser shape:

- `repl spawn <package> [-l path] [--resume]`
- `repl attach <target> [-l path]`

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: parser test passes.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl command parser"
```

### Task 2: Add failing prompt and REPL command parsing tests

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add unit tests for helper behavior:

- prompt with no script: `[com.demo.target:4321] > `
- prompt with script: `[com.demo.target:4321 hook.js] > `
- prompt with suspended state: `[com.demo.target:4321 SUSPENDED hook.js] > `
- `%call ping ["hello"]` parses into method `ping` and args `["hello"]`
- plain text `hello` becomes a `post` command

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplHelpers
```

Expected: missing helper functions or assertion failures.

**Step 3: Write minimal implementation**

Add small helpers in `host/nook-py/nook/cli.py`:

- prompt formatter
- line parser
- `%call` args parser

Keep the grammar intentionally small:

- `%command ...`
- non-`%` input => `post`
- blank input => no-op

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: helper tests pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl prompt and parser helpers"
```

### Task 3: Add failing tests for REPL session bootstrap

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add fake-device tests covering:

- `repl spawn ... -l hook.js --resume`
- `repl spawn ... -l hook.js` without resume
- `repl attach ... -l hook.js`

Assert:

- the correct SDK method was called (`spawn` or `attach`)
- `resume()` is only called when expected
- initial script load occurs when `-l` is present

Use a fake stdin sequence that exits immediately after bootstrap, for example:

```text
%exit
```

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplBootstrap
```

Expected: `main()` has no REPL execution path yet.

**Step 3: Write minimal implementation**

Add a `ReplContext` and a small REPL bootstrap path that:

- creates `device`
- performs `spawn` or `attach`
- optionally loads a script
- optionally resumes spawned targets
- enters a command loop

Support `%exit` only at this stage.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: bootstrap tests pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl bootstrap"
```

### Task 4: Add failing tests for `%info`, `%help`, and graceful exit

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add tests that:

- `%info` prints pid, process name, mode, script state, and resume state
- `%help` prints the supported command list
- `%unknown` prints `unknown command, type %help`
- `%exit` stops the REPL and closes the device

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplMetaCommands
```

Expected: commands are unimplemented.

**Step 3: Write minimal implementation**

Implement:

- `%help`
- `%info`
- `%exit`
- unknown-command handling

Also add a `threading.Event` stop flag so REPL shutdown is coordinated.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: tests pass and no thread leak occurs.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl meta commands"
```

### Task 5: Add failing tests for `%load`, `%reload`, and `%unload`

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add tests covering:

- `%load hook.js` creates and loads a new script
- `%load other.js` unloads the old script first
- `%reload` reloads the last remembered path
- `%reload` fails clearly when no path is remembered
- `%unload` clears active script state but keeps remembered path

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplScriptLifecycle
```

Expected: lifecycle commands are unimplemented or state assertions fail.

**Step 3: Write minimal implementation**

Implement REPL helpers that:

- read script source via the existing `_read_script_source(...)`
- unload before replace
- update `script`, `script_id`, `script_path`, and `script_name`
- keep remembered path for `%reload`

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: lifecycle tests pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl script lifecycle commands"
```

### Task 6: Add failing tests for `%post` and async message printing

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add tests covering:

- `%post {"type":"post","payload":"hello"}` forwards to the active script
- plain text input is treated as post
- `%post` without an active script prints a clear error
- message loop prints script messages while REPL remains active

Use a fake device that returns queued script messages and exits after `%exit`.

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplPostAndMessages
```

Expected: post command or background message loop is missing.

**Step 3: Write minimal implementation**

Implement:

- `%post`
- default non-command input => post
- a message-loop thread polling `wait_for_script_message(timeout_ms=...)`
- graceful stop using `threading.Event`

Reuse the existing `_emit_script_message(...)` formatter.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: post and message tests pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl post and message loop"
```

### Task 7: Add failing tests for `%call` and `%resume`

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`

**Step 1: Write the failing test**

Add tests covering:

- `%call ping ["hello"]` returns RPC result
- `%call ping` defaults to `[]`
- `%call` without an active script fails clearly
- `%call ping bad-json` prints a JSON error
- `%resume` resumes a suspended spawn session
- `%resume` on already resumed spawn prints stable feedback
- `%resume` in attach mode prints not-applicable feedback

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli.TestCliReplRpcAndResume
```

Expected: command handling or state assertions fail.

**Step 3: Write minimal implementation**

Implement:

- `%call`
- default `[]` args when omitted
- RPC result printing via existing JSON formatter style
- `%resume`
- resumed-state tracking in `ReplContext`

Add explicit timeout/error handling around RPC calls.

**Step 4: Run test to verify it passes**

Run the same command as step 2.

Expected: RPC and resume tests pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py
git commit -m "feat: add repl rpc and resume commands"
```

### Task 8: Run the full Python test suite and clean output

**Files:**
- Modify: `host/nook-py/tests/test_cli.py`
- Modify: `host/nook-py/nook/cli.py`
- Modify: `host/nook-py/README.md`

**Step 1: Run the targeted REPL tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected: all CLI tests pass.

**Step 2: Run the full Python host suite**

Run:

```powershell
python -m unittest discover host/nook-py/tests
python -m compileall host\nook-py\nook host\nook-py\tests
```

Expected: all tests pass and files compile cleanly.

**Step 3: Document the new workflow**

Update `host/nook-py/README.md` with:

- `repl spawn` example
- `repl attach` example
- supported REPL commands
- one short example showing `%post`, `%call`, and `%reload`

**Step 4: Re-run the focused CLI tests**

Run:

```powershell
python -m unittest host.nook-py.tests.test_cli
```

Expected: README edits do not affect tests; CLI tests still pass.

**Step 5: Commit**

```bash
git add host/nook-py/tests/test_cli.py host/nook-py/nook/cli.py host/nook-py/README.md
git commit -m "feat: add persistent repl workflow"
```

### Task 9: Run device smoke validation

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Start the device-side server**

Run:

```powershell
adb shell "su -c 'pkill -x nook-server 2>/dev/null || true'"
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

Expected: server starts and listens on `27042`.

**Step 2: Validate `repl spawn`**

Run:

```powershell
nook-cli repl spawn com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\hook.js --resume --usb
```

In the REPL, manually execute:

```text
%info
%post {"type":"post","payload":"hello-from-repl"}
%call ping ["hello"]
%unload
%load E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\hook.js
%call ping ["again"]
%exit
```

Expected:

- prompt enters REPL correctly
- async script messages print
- `%post` gets an echo
- `%call` returns RPC result
- unload and reload both succeed

**Step 3: Validate suspended spawn flow**

Run:

```powershell
nook-cli repl spawn com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\hook.js --usb
```

In the REPL, manually execute:

```text
%info
%resume
%exit
```

Expected:

- prompt initially shows `SUSPENDED`
- `%resume` succeeds
- prompt updates to non-suspended state

**Step 4: Validate `repl attach`**

Run:

```powershell
nook-cli repl attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\hook.js --usb
```

In the REPL, manually execute:

```text
%call ping ["attach"]
%exit
```

Expected: attach REPL behaves the same except `%resume` reports not applicable.

**Step 5: Commit**

```bash
git add host/nook-py/README.md
git commit -m "docs: validate repl smoke workflow"
```
