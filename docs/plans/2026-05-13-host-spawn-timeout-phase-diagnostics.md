# 2026-05-13 Host Spawn Timeout Phase Diagnostics

## Goal

Avoid collapsing two different spawn timeout phases into the same host-visible
`operation timed out` error.

The practical problem was simple:

- when `Device.spawn()` timed out before `SPAWN_RESPONSE`
- and when it timed out after `SPAWN_RESPONSE` but before runtime-stage `AGENT_READY`

the CLI printed the same message shape:

- `spawn agent-ready timed out ... operation timed out`

That made real-device debugging slower because the failure stage remained
ambiguous until device logs were collected.

## Change

Updated:

- [device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)

Behavior is now split into two explicit timeout errors:

- `wait spawn response timed out`
- `wait runtime agent ready timed out`

Current flow:

1. send `SPAWN_REQUEST`
2. wait for `SPAWN_RESPONSE`
   - timeout now raises `TimeoutError("wait spawn response timed out")`
3. wait for runtime-stage `AGENT_READY`
   - timeout now raises `TimeoutError("wait runtime agent ready timed out")`

This is only a host-side diagnostics change.

It does not change:

- spawn backend routing
- server state machine
- agent runtime behavior
- device deployment

## Regression Coverage

Added tests in:

- [test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)
- [test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

Covered cases:

- `Device.spawn()` times out before `SPAWN_RESPONSE`
- `Device.spawn()` times out while waiting for runtime-stage `AGENT_READY`
- CLI preserves stage-specific timeout text in user-visible errors

## Verification

Passed:

```powershell
python -m unittest host.nook-py.tests.test_client
python -m unittest host.nook-py.tests.test_cli
```

## Practical Result

Future failures will separate these two classes immediately:

- host/device transport or server-side spawn-response path problems
- post-spawn runtime-ready handoff problems

That narrows the first debugging step before touching server/runtime code.
