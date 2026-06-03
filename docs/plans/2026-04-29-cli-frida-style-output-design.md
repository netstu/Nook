# Nook CLI Frida-Style Output Design

## Goal

Make Nook's human-facing CLI output feel much closer to Frida by redesigning the
terminal presentation for:

- `spawn`
- `attach`
- `resume`
- `repl`
- script messages
- help / listing output

without changing the transport protocol or the existing `--json` machine-readable
mode.

## Confirmed Scope

This pass will:

- add a dedicated host-side output helper module
- switch the non-JSON CLI to Frida-style status prefixes such as:
  - `[*]`
  - `[+]`
  - `[!]`
  - `[-]`
- redesign the REPL prompt to a Frida-like shape
- render script messages in a Frida-like human format instead of the current raw
  `script message: ... json=...` line
- render multi-line payloads such as `hexdump(...)` as raw multi-line blocks
- convert app/process listing output to table-style formatting
- refresh REPL help formatting

This pass will not:

- change the underlying script message payload protocol
- change RPC semantics
- change `--json` output structure
- redesign every command's machine-consumable API

## Why This Scope

The current CLI has the right capabilities, but the human output still feels like
a debug shell:

- `script create ok: ...`
- `resume ok: ...`
- `script message: script_id=... json=...`

That is functional, but it is not aligned with how users expect a Frida-style
tool to feel in daily use.

The biggest pain-point is not the `hexdump(...)` implementation itself. It is
that hexdump strings are currently shown through the generic script-message
wrapper, which destroys the terminal presentation.

So the right layer to fix first is the CLI rendering layer.

## Recommended Approach

### Approach A: Introduce a dedicated `output.py` rendering layer

This is the recommended approach.

Shape:

1. add `host/nook-py/nook/output.py`
2. centralize:
   - color support
   - status prefixes
   - banner
   - prompt formatting
   - script-message rendering
   - table formatting
3. keep `cli.py` responsible for command flow only
4. let `cli.py` call rendering helpers instead of scattered `print(...)`

Why this is the right shape:

- keeps style policy out of the command logic
- makes later refinements easier
- limits regressions by centralizing output decisions
- allows Frida-like rendering while preserving `--json`

## Alternatives Considered

### Approach B: Replace strings inline inside `cli.py`

Pros:

- smaller diff up front

Cons:

- mixes presentation policy with command control flow
- hard to evolve
- easy to miss output sites

Conclusion:

Not good enough for a full CLI style pass.

### Approach C: Only special-case `hexdump(...)`

Pros:

- solves the immediate ugly case

Cons:

- leaves the rest of the terminal experience inconsistent
- does not satisfy the broader “Frida-like” goal

Conclusion:

Too narrow.

## Output Model

### 1. Control events

These should become Frida-like prefixed status lines:

- `[*] Attaching to 'com.demo.target'...`
- `[+] Attached (pid: 2100, session: 7)`
- `[*] Loading 'hook.js'...`
- `[+] Script loaded (id: 1000)`
- `[*] Resuming pid 4321...`
- `[+] Process resumed`

### 2. Script messages

Rules:

- `{"type":"send","payload":"hello"}`:
  - print `hello`
- `{"type":"send","payload":"<multi-line>"}`:
  - print the payload raw, line-for-line
- messages that look like console warnings / errors:
  - render with warning / error prefixes
- unparseable payloads:
  - fall back to the current raw message line

This keeps hexdump blocks readable without inventing a separate hexdump protocol.

### 3. REPL prompt

Target shape:

- `[USB Device::com.demo.target]->`
- `[USB Device::com.demo.target] (suspended) [hook.js]->`

### 4. Listings

Apps/processes should switch from count + loose lines to Frida-style tables.

### 5. Banner

Banner should be shown in the REPL entry flow, not on every one-shot command.

## Color Policy

Use color only for non-JSON human terminal output.

Rules:

- disable colors automatically on non-TTY streams
- keep Windows support conservative
- do not require color for correctness

## Compatibility Boundary

This pass intentionally preserves:

- `--json`
- transport payloads
- script-side APIs

So compatibility risk is mostly:

- changed human-readable strings
- updated tests that assert old output
- README example drift

## Testing Strategy

Host tests first:

- prompt formatting
- attach/spawn control messages
- script-message rendering for:
  - single-line `send`
  - multi-line `send`
  - raw fallback
- app/process table output
- `--json` unchanged

README updates second:

- revise example output so docs match the new CLI

## Recommendation

Implement a dedicated rendering layer and move the non-JSON CLI output toward a
Frida-like presentation in one coherent pass. This gives the user-visible
improvement they asked for without destabilizing the protocol or scripting mode.
