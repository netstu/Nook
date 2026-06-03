# Agent-Owned Stable Spawn Owned Uninstall Gating

## Date

2026-05-15

## Scope

This step continues tightening the experimental `zygote-control` ownership
model on the way toward `agent-owned stable spawn`.

It does not switch the default stable backend.

The working real-device path remains:

- default stable spawn -> `legacy-ncore`
- `zygote-control` remains opt-in / experimental
- single visible deployment artifact remains `nook-server`

## What Changed

### 1. Dedicated `spawn.uninstall` is now gated by explicit ownership

`server/zygote_control_rpc.cpp` previously allowed the dedicated
`SpawnUninstall` control-plane message whenever there was an immediate
control-ready session for the target zygote/usap process.

That was too broad.

For install, using a control-ready session to establish ownership is fine.
For uninstall, using the dedicated agent-owned teardown path should require
that the server already considers the target explicitly owned.

After this step:

- `HasImmediateControlSession(...)` still exists for install-side probing
- new helper `HasOwnedImmediateControlSession(...)` requires:
  - `IsOwnedZygoteControlProcess(process_name)`
  - plus an immediate control-ready session
- `UninstallZygoteForkHookWithSendersForTest(...)` now uses the owned helper
  for the dedicated `spawn.uninstall` fast path

So the dedicated teardown path is now:

- `owned + control-ready`

instead of:

- `control-ready only`

### 2. Non-owned teardown falls back to compatibility RPC

If a zygote process has a control-ready session but is not explicitly owned by
the current server lifecycle, uninstall now skips the dedicated
`SpawnUninstall` message and goes directly through the compatibility RPC path:

- `nook.spawn.uninstallForkHook`

That keeps teardown authority aligned with the ownership model already used by:

- shutdown cleanup
- finalize ownership classification
- owned-session reuse gating in `server_main.cpp`

## Why This Matters

The target `agent-owned stable spawn` architecture cannot treat
“there is a usable control-ready session” as equivalent to
“this server lifecycle owns the spawn controller state in that process”.

That conflation is especially dangerous on teardown:

- it can make the server issue an agent-owned uninstall against a process whose
  session is merely reachable
- but whose controller state is not part of the current owned transaction

This step makes the uninstall boundary match the intended model:

- install may establish ownership
- uninstall must respect ownership

## Validation

Passed locally:

- `build/test_zygote_control_rpc_owned_uninstall.exe`
- `build/test_server_zygote_control_rpc_regressions.exe`
- `tools/build_single_server_package.ps1 -ForceRebuild`

Added coverage in
[test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp):

- dedicated uninstall is used for owned targets
- dedicated uninstall falls back on compat timeout
- dedicated uninstall is **not** used when the session is control-ready but the
  target is not explicitly owned

Real-device validation:

- rebuilt and pushed `nook-server`
- started cleanly from `/data/local/tmp/nook/nook-server`
- default stable spawn for `com.ad2001.frida0x1` still worked:
  - `[*] Waiting for agent runtime ready...`
  - `[+] Script loaded`
  - `[+] Process resumed`
  - `lab:frida-0x1:installed`
  - `[+] Script unloaded`

## Current Runtime Impact

No intended user-visible change on the default stable route.

The effect is limited to experimental `zygote-control` teardown semantics:

- dedicated agent-owned uninstall now respects explicit ownership
- non-owned control-ready sessions no longer qualify for the dedicated
  uninstall fast path

## Next Step

Continue removing the remaining places where experimental `zygote-control`
still derives route authority from generic session presence rather than
explicit owned transaction state, then only after that move into the first real
default-route `agent-owned stable spawn` replacement slice.
