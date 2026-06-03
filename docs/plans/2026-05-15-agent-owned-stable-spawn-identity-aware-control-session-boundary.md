# Agent-Owned Stable Spawn Identity-Aware Control Session Boundary

## Date

2026-05-15

## Scope

This step continues tightening the experimental `zygote-control` authority
surface without changing the default stable backend.

The default real-device path remains:

- stable spawn default -> `legacy-ncore`
- `zygote-control` remains opt-in / experimental
- single visible deployment artifact remains `nook-server`

## What Changed

### 1. Owned zygote-control targets now carry identity, not just process name

`server/session_registry.{h,cpp}` previously tracked owned zygote-control
targets as:

- `process_name -> bool`

That was too weak because ownership could survive on the name surface even when
the actual process identity had changed.

The registry now stores:

- `OwnedZygoteControlTarget { pid, process_name }`

and exposes:

- `IsOwnedZygoteControlTarget(pid, process_name)`
- `GetOwnedZygoteControlTarget(...)`

So owned cleanup / dedicated uninstall decisions can now bind to the current
target identity instead of just the process label.

### 2. Control-ready lookup now has an identity-aware immediate probe

Added:

- `FindControlReadyAgentSessionByIdentity(pid, process_name)`

This helper does not accept:

- a ready session for the wrong pid
- a stale pid that still happens to have old ready metadata
- a process-name mapping that has already moved to a new pid

This helper is now used by `server/zygote_control_rpc.cpp` for immediate
control-session presence checks and related observability.

### 3. Same-name pid migration now clears stale reverse identity state

`RegisterAgentProcessName()` now clears the old
`agent_pid_to_process_name_` entry when a process name moves from one pid to
another.

Without that cleanup, a same-name zygote/usap restart could leave the old pid
looking like it still owned the process name, which polluted the new
identity-aware lookup and made the host authority model less trustworthy.

## Why This Matters

The project is converging toward an `agent-owned stable spawn` model where the
host must be able to say:

- which zygote/usap target is owned
- which session belongs to that exact target
- which cleanup path is allowed to act on it

That model cannot safely rest on:

- generic ready-session presence
- process-name-only ownership
- stale pid/name reverse mappings

This step removes another source of implicit authority leakage before any
default-route switch is attempted.

## Validation

Passed locally:

- `build/test_session_registry_identity_v2.exe`
- `build/test_zygote_control_rpc_identity_v2.exe`
- `build/test_server_zygote_control_rpc_regressions.exe`

Coverage added / tightened:

- same-name pid reuse does not preserve old owned-target identity
- identity-aware control-ready lookup rejects superseded same-name pid state
- `zygote_control_rpc` must probe immediate ready state through
  `FindControlReadyAgentSessionByIdentity(...)`

## Runtime Impact

No intended user-visible change on the default stable spawn route.

The effect is limited to experimental `zygote-control` authority semantics:

- owned target state is identity-aware
- immediate control-ready probing is identity-aware
- stale same-name pid metadata is cleaned up earlier

## Next Step

Continue reducing the remaining places where experimental `zygote-control`
derives behavior from generic session availability rather than explicit
owner/session identity, then move the next state slice toward a true
agent-owned active control-plane model.
