# Nook Persistent REPL Design

**Goal**

Add a Frida-like persistent host-side REPL mode to `nook-cli` so one CLI process can keep a single `Device + Session + Script` alive and repeatedly perform `post`, `call`, `load`, `unload`, and `resume` operations without reconnecting for every command.

**Context**

Nook's current Python host tooling already supports these one-shot workflows:

- `spawn`
- `attach`
- `resume`
- `post`
- `unload`
- `call`
- `--wait`
- `--interactive`

Those commands are usable, but each command currently rebuilds the whole control path:

1. open connection
2. `spawn` or `attach`
3. optionally load script
4. do one action
5. exit process

That keeps the implementation small, but it does not feel like Frida because there is no long-lived shell that holds onto process state and a loaded script. The result is awkward when iterating on hooks, sending multiple messages, reloading scripts, or repeatedly calling `rpc.exports`.

## Why A Persistent REPL

The target user experience is:

1. start one CLI session
2. keep one process session alive
3. keep one active script loaded
4. interact with it repeatedly
5. see async script messages while staying in the shell

That gives Nook the first real "live session" UX layer without introducing a background daemon or cross-command persistence.

## Scope

This design intentionally targets only a single CLI process lifetime.

In scope:

- new `nook-cli repl ...` command
- support `spawn` and `attach` entry modes
- support one active script per REPL session
- support async message printing while waiting for user input
- support manual `%resume` for suspended spawned targets
- support reload-oriented script iteration

Out of scope:

- cross-process persistence across multiple CLI invocations
- multi-script management
- daemonized host session store
- reconnect/recover after connection loss
- Promise-based RPC
- Frida CLI compatibility beyond the minimum useful command set

## Recommended Command Surface

Add a new top-level command:

- `nook-cli repl spawn <package> [-l script.js] [--resume] [--usb ...]`
- `nook-cli repl attach <target> [-l script.js] [--usb ...]`

Inside the REPL, support these commands:

- `%help`
- `%info`
- `%load <path>`
- `%reload`
- `%unload`
- `%post <message>`
- `%call <method> [args_json]`
- `%resume`
- `%exit`

Behavior rules:

- non-empty input that does not start with `%` is treated as `%post <line>`
- empty or whitespace-only input is ignored
- `%call` takes a JSON array when args are provided
- `%reload` unloads the active script and reloads the last loaded path

## Prompt Model

The prompt should reflect current session state:

- no script: `[com.demo.target:4321] > `
- active script: `[com.demo.target:4321 hook.js] > `
- suspended spawn session: `[com.demo.target:4321 SUSPENDED hook.js] > `

This is important because REPL state is otherwise invisible, especially after `unload`, `reload`, or manual `%resume`.

## Architecture

The recommended implementation is a small host-side REPL shell built on top of the existing Python `Device`, `Session`, and `Script` objects. No protocol change is needed.

### Core Object

Introduce a lightweight `ReplContext` in the Python CLI layer that owns:

- `device`
- `session`
- `script`
- `script_id`
- `script_path`
- `script_name`
- `entry_mode` (`spawn` or `attach`)
- `resumed`
- `stop_event`

This object centralizes state transitions and keeps REPL behavior out of the lower-level protocol classes.

### Why Keep It In CLI

The current host API already has the operations REPL needs:

- `Device.spawn(...)`
- `Device.attach(...)`
- `Device.resume(...)`
- `Session.create_script(...)`
- `Script.load()`
- `Script.unload()`
- `Script.post(...)`
- `Script.call(...)`
- `Device.wait_for_script_message(...)`

Because that API is already sufficient, the REPL should stay in `host/nook-py/nook/cli.py` or a tiny helper module near it. There is no need to widen the transport or protocol surface for this feature.

## Event Model

Use two threads:

- input thread or main loop reading stdin and dispatching commands
- message loop thread waiting on `device.wait_for_script_message(timeout_ms=...)`

The message loop should:

1. poll script messages with a short timeout
2. print async messages as they arrive
3. exit when `stop_event` is set

The input side should:

1. render prompt
2. read one line
3. parse REPL commands
4. execute via `ReplContext`

This mirrors the current `--wait` model while allowing interactive command entry.

## Script Lifecycle Model

The REPL owns at most one active script.

### `%load <path>`

Expected flow:

1. if an active script exists, unload it first
2. read source from path
3. `session.create_script(...)`
4. `script.create()`
5. `script.load()`
6. record path, script name, and `script_id`

### `%reload`

Expected flow:

1. require a remembered script path
2. unload current script if present
3. reload from the remembered path

### `%unload`

Expected flow:

1. require an active script
2. call `script.unload()`
3. clear active script state
4. keep remembered path so `%reload` still works

## Resume Semantics

`spawn` mode may start in a suspended state if the user omits `--resume`.

The REPL must support both:

- eager resume via `--resume`
- manual resume via `%resume`

Rules:

- `%resume` is only meaningful for `spawn`
- if already resumed, print a stable "already resumed" style message
- `attach` mode should report that `%resume` is not applicable

## Error Handling

Important user-facing behaviors:

- `%unknown` -> `unknown command, type %help`
- `%post` with no active script -> clear error
- `%call` with no active script -> clear error
- `%call <method> bad-json` -> clear JSON parse error
- `%call` timeout -> explicit timeout message
- `%reload` without prior `%load` or `-l` -> clear error
- `%resume` in attach mode -> clear error

The REPL should remain alive after command errors. Only fatal connection loss should terminate the session.

## Testing Strategy

### Python Unit Tests

Add focused tests for:

- parser support for `repl spawn` and `repl attach`
- prompt formatting
- REPL command dispatch
- `%load/%reload/%unload`
- `%post`
- `%call`
- no-active-script error paths
- `%resume` behavior by entry mode

Use the existing fake-device style already present in `host/nook-py/tests/test_cli.py`.

### Device Smoke

Use the existing `hook.js` sample to validate:

1. `repl spawn ... -l hook.js --resume --usb`
2. receive initial `send(...)`
3. `%post {"type":"post","payload":"hello"}`
4. receive echoed message
5. `%call ping ["hello"]`
6. `%unload`
7. `%load <hook.js>`
8. `%call ping ["again"]`
9. `%exit`

Also validate suspended spawn mode:

1. `repl spawn ... -l hook.js --usb`
2. prompt shows `SUSPENDED`
3. `%resume`
4. prompt no longer shows `SUSPENDED`

## Files Expected To Change

Primary files:

- `host/nook-py/nook/cli.py`
- `host/nook-py/tests/test_cli.py`
- `host/nook-py/README.md`

Possible helper extraction if needed:

- `host/nook-py/nook/repl.py` (new, optional)

Supporting files:

- `host/nook-py/hook.js` only if smoke examples need refresh

## Success Criteria

The feature is complete when:

1. `nook-cli repl spawn ...` enters a usable interactive shell
2. `nook-cli repl attach ...` enters a usable interactive shell
3. the shell keeps one `Device + Session + Script` alive for repeated commands
4. async script messages remain visible during the session
5. `%post`, `%call`, `%load`, `%reload`, `%unload`, `%resume`, `%info`, `%help`, and `%exit` all behave predictably
6. unit tests cover the new command paths
7. device smoke confirms the expected loop feels meaningfully closer to Frida
