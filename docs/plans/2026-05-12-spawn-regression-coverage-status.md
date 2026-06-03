# 2026-05-12 Spawn Regression Coverage Status

## Goal

Add focused regression coverage around the current default stable spawn surface and the explicit `symbi` entry path, without changing runtime behavior.

This round specifically covered:

- repeated default spawn lifecycle on the host/client side
- explicit `--spawn-symbi` CLI entry behavior
- default CLI spawn entry behavior remaining marker-free

## Why This Was Needed

The recent real-device work established that:

- default spawn stays on stable legacy
- explicit `--spawn-symbi` works again
- device-visible deployment is still single-file

But that still leaves a regression risk at two boundaries:

1. host-side repeated spawn cycles could accidentally reuse stale `AGENT_READY` state
2. CLI entry behavior could silently drift and start passing or omitting the internal `symbi` marker incorrectly

## Coverage Added

### 1. Repeated default spawn cycle host regression

Updated:

- `tests/communication/test_host_spawn_client.cpp`

Added:

- `TestSpawnAndWaitSucceedsAcrossRepeatedDefaultSpawnCycles()`

This verifies:

- first default spawn completes with control-stage then runtime-stage ready
- second default spawn also completes in the same client session
- second spawn does not accidentally consume stale ready state from the first cycle
- each spawn resolves to its own runtime pid and runtime process name

### 2. CLI default spawn path remains marker-free

Updated:

- `host/nook-py/tests/test_cli.py`

Added:

- `test_spawn_command_default_path_does_not_pass_internal_symbi_marker()`

This verifies:

- normal `spawn` does not inject `--nook-spawn-backend=symbi`
- backend selection for the default path remains a server-side decision

### 3. Explicit CLI symbi path remains pinned

Already existing tests still pass:

- `test_spawn_command_with_spawn_symbi_passes_internal_marker()`
- `test_frida_style_spawn_with_spawn_symbi_passes_internal_marker()`

This confirms:

- explicit CLI opt-in still forwards `--nook-spawn-backend=symbi`
- default and explicit paths stay intentionally distinct

## Local Verification

Passed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_spawn_client.exe`
- `build/test_host_spawn_client.exe`
- `python -m unittest host/nook-py/tests/test_cli.py`

## Practical Meaning

After this test pass, the project now has coverage for three different layers of the current spawn story:

1. injector/backend preference tests
2. server/session-state tightening tests
3. host/client repeated spawn and CLI entry tests

This does not prove full real-device reliability by itself, but it materially reduces the risk of accidentally regressing the now-working default `symbi` path while continuing the architecture work.
This does not prove full real-device reliability by itself, but it materially reduces the risk of accidentally regressing the default stable path or the explicit symbi path while continuing the architecture work.

## Next Step

The next most useful step is device-side repeated-run validation:

- spawn -> hook -> finalize
- spawn -> hook -> finalize again
- explicit `--spawn-symbi`

If those remain stable, the next engineering topic can move to:

- reducing remaining two-stage connection log noise
- or revisiting `zygote-control` / longer-term stable spawn architecture
