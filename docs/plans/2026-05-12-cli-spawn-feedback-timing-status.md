# 2026-05-12 CLI Spawn Feedback Timing Status

## Goal

Improve user-visible CLI feedback during spawn, without changing spawn/runtime behavior.

## Problem

In the current spawn flow, the CLI printed:

- `Loading 'script.js'...`

only after the server had already completed:

- spawn request
- control-stage handoff
- runtime-stage `AGENT_READY`
- authoritative session binding

On real device this made the terminal appear idle for around one second before script loading feedback appeared, even though the app launch had already started.

This was a UX/timing issue, not a runtime correctness issue.

## Change Applied

Updated:

- `host/nook-py/nook/cli.py`

Behavior now:

1. print `Spawning 'pkg'...`
2. print `Waiting for agent runtime ready...`
3. wait for `device.spawn(...)`
4. print `Spawned (pid: ...)`
5. print `Loading 'script.js'...`

This keeps the existing semantics but makes the waiting phase explicit.

## Tests

Updated:

- `host/nook-py/tests/test_cli.py`

Added assertions that spawn flows now include:

- `Spawning ...`
- `Waiting for agent runtime ready...`
- `Loading ...`

and that the ordering is:

1. spawning
2. waiting
3. loading

## Local Verification

Passed:

- `python -m unittest host/nook-py/tests/test_cli.py`

## Practical Result

The app may still take the same real time to reach runtime-ready, but the user now sees accurate progress feedback during that window instead of an apparent silent stall.
