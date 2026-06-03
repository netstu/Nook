# Nook Frida-Style CLI Design

## Context

Nook's Python CLI already has the core operations needed for daily use:

- `spawn`
- `attach`
- `repl`
- `load`
- `wait`
- RPC

But its current interaction model is still command-centric instead of Frida-centric:

- `repl` is a separate subcommand instead of the default interactive path
- the banner is only printed inside `_run_repl()`
- in `repl spawn ... -l ... --resume`, the banner is printed after load/resume output
- top-level invocation does not feel like `frida -U -f ... -l ...`

The user also wants output behavior to converge toward Frida before resuming deeper deployment work.

## Goal

Make `nook-cli` feel substantially closer to Frida in two ways:

1. interactive CLI shape
2. terminal output order and presentation

## Non-Goals

1. Do not remove existing subcommands immediately
2. Do not break JSON mode
3. Do not redesign the host/device protocol
4. Do not couple CLI refactoring to agent delivery work

## Problems To Solve

### 1. Banner ordering is wrong

Current interactive flow can look like:

```text
[*] Loading 'script.js'...
[+] Script loaded (id: 1)
[*] Resuming pid 31296...
[+] Process resumed

    _   __            __
   / | / /___  ....
```

This is backwards. In Frida, the visual shell is established first, then the interactive session proceeds.

### 2. Interactive mode is not the default top-level mental model

Frida users expect a shape like:

```powershell
frida -U -f com.example.app -l hook.js
frida -U com.example.app -l hook.js
```

with the session landing in an interactive shell by default.

Nook currently requires:

```powershell
nook-cli repl spawn com.example.app -l hook.js --resume --usb
```

which is mechanically valid but not Frida-like.

### 3. Console log and send output should be treated as different channels

Nook currently already distinguishes decoded script messages and `console.*` payloads internally, but its presentation is still closer to Nook's internal transport model than Frida's user-facing shell model.

## Approaches Considered

### Option 1: Only move the banner

Pros:

- smallest change

Cons:

- does not solve the top-level Frida CLI mismatch
- leaves `repl` as a separate explicit mental model

### Option 2: Add Frida-style top-level interactive mode while keeping old commands

Pros:

- closes most of the UX gap
- preserves backward compatibility
- allows gradual migration of docs and tests

Cons:

- parser logic becomes more complex
- requires broader CLI regression coverage

### Option 3: Replace the current CLI model outright

Pros:

- maximum Frida likeness

Cons:

- highest breakage risk
- unnecessary for this phase

## Decision

Use Option 2.

## Target CLI Model

### New top-level Frida-style paths

Support:

```powershell
nook-cli -U -f com.demo.target -l hook.js
nook-cli -U com.demo.target -l hook.js
```

Semantics:

- `-U` maps to `--usb`
- `-f <package>` means spawn
- top-level positional target means attach
- `-l <script>` loads a startup script
- top-level interactive mode becomes the default for this shape

### Existing command model remains valid

Keep these working:

```powershell
nook-cli spawn ...
nook-cli attach ...
nook-cli repl spawn ...
nook-cli repl attach ...
```

This preserves existing automation and smoke workflows.

### Updated subcommand interaction model

To converge further toward Frida, `spawn` and `attach` themselves should become
interactive by default.

Target behavior:

```powershell
nook-cli spawn com.demo.target --usb
nook-cli spawn com.demo.target -l hook.js --resume --usb
nook-cli attach com.demo.target --usb
nook-cli attach com.demo.target -l hook.js --usb
```

All of the above should enter REPL by default.

To preserve one-shot command behavior for automation and scripted flows, add:

```powershell
--oneshot
```

Meaning:

- default: interactive shell
- `--oneshot`: current non-interactive command behavior

This keeps backward capability while shifting the default user experience toward
Frida.

## Banner Rules

Banner rules must become deterministic:

1. Print banner once per interactive session
2. Print banner before load/resume progress lines
3. Do not print banner for non-interactive one-shot commands unless explicitly chosen later
4. Do not print banner in JSON mode

This means the banner must move out of `_run_repl()` and into the interactive entry setup path.

## Output Rules

### Progress/status output

Keep the current Frida-like progress prefixes:

- `[*]`
- `[+]`
- `[!]`
- `[-]`

These already fit the intended style well enough.

### `console.*`

Treat `console.log/info/warn/error` as the script logging channel, not as generic transport events.

Presentation goal:

- `console.log/info`: display as direct user-facing log lines
- `console.warn`: warning-style output
- `console.error`: error-style output

The exact final Frida-like formatting can be refined separately, but the first necessary rule is to stop conflating them with transport-level decoded `send(...)` messages.

### `send(...)`

Treat `send(...)` as the message channel:

- preserve decoded display
- keep it distinct from `console.*`
- preserve structured host automation behavior

## Parser Strategy

Add a top-level Frida-style parse path without deleting the current subcommands.

Recommended implementation shape:

1. detect whether argv begins with an existing subcommand
2. if yes, keep current parser path
3. otherwise, parse with a Frida-style top-level parser
4. convert that result into an internal interactive session config

This avoids destabilizing the current command parser too early.

## Session Semantics

### Spawn path

For:

```powershell
nook-cli -U -f com.demo.target -l hook.js
```

recommended default semantics:

1. spawn target
2. print banner
3. load script
4. if auto-resume is enabled, resume
5. enter REPL

For legacy subcommand shape:

```powershell
nook-cli spawn com.demo.target -l hook.js --resume --usb
```

the same default should now apply: enter REPL unless `--oneshot` is present.

### Attach path

For:

```powershell
nook-cli -U com.demo.target -l hook.js
```

recommended default semantics:

1. attach target
2. print banner
3. load script
4. enter REPL

For legacy subcommand shape:

```powershell
nook-cli attach com.demo.target -l hook.js --usb
```

the same default should now apply: enter REPL unless `--oneshot` is present.

## Testing

Add or update tests for:

1. interactive banner prints before load/resume lines
2. banner prints only once
3. legacy `repl ...` commands still work
4. Frida-style top-level attach path enters REPL
5. Frida-style top-level spawn path enters REPL
6. JSON mode remains banner-free

## Documentation

README should present Frida-style examples first and legacy subcommands second.

Recommended primary examples:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
nook-cli -U com.demo.target -l .\hook.js
```

Legacy examples should remain documented as compatibility paths.
