# 2026-05-12 CLI Strict Zygote-Control

## Goal

Expose strict experimental `zygote-control` spawn mode through `nook-cli` without changing the default stable spawn behavior.

## Problem

The server already supported a strict experimental mode through:

- `NOOK_STRICT_ZYGOTE_CONTROL=1`

but that control surface was process-global and inconvenient for real testing. It also did not match the desired CLI workflow where one spawn request can opt into strict behavior while the next request can stay on the stable fallback path.

## Change Applied

Updated:

- `host/nook-py/nook/cli.py`
- `server/ninjector_spawn_injector.cpp`

### CLI surface

Added:

- `--strict-zygote-control`

Supported on spawn-capable CLI flows:

- `spawn`
- frida-style `-U -f ...`
- `repl spawn`
- spawn-backed `call`
- spawn-backed `post`
- spawn-backed `unload`

### Internal transport

The CLI encodes the flag as a request-scoped internal spawn argv marker:

- `--nook-strict-zygote-control`

This marker is sent only with the current spawn request.

### Server semantics

`NinjectorSpawnInjector` now treats strict mode as enabled when either of these is true:

- environment variable `NOOK_STRICT_ZYGOTE_CONTROL=1`
- request argv contains `--nook-strict-zygote-control`

When strict mode is enabled:

- `zygote-control` failure returns immediately
- no fallback to `symbi`
- no fallback to legacy `ncore`

## Why This Shape

This keeps the stable path untouched while making the experimental path explicit and reproducible from the CLI.

Request-scoped transport is preferable to a global environment toggle because:

1. it affects only one spawn request
2. it fits frida-style command usage
3. it avoids leaking experimental policy into later commands

## Tests

Updated:

- `host/nook-py/tests/test_cli.py`
- `tests/communication/test_ninjector_spawn_injector.cpp`

Coverage added:

- parser accepts `--strict-zygote-control`
- host spawn argv contains `--nook-strict-zygote-control`
- request marker alone is sufficient to force strict no-fallback behavior

## Local Verification

Passed:

- `python -m unittest host.nook-py.tests.test_cli.CliTests.test_parser_supports_strict_zygote_control_for_spawn host.nook-py.tests.test_cli.CliTests.test_parser_supports_strict_zygote_control_for_frida_style_spawn host.nook-py.tests.test_cli.CliTests.test_spawn_command_with_strict_zygote_control_passes_internal_marker host.nook-py.tests.test_cli.CliTests.test_frida_style_spawn_with_strict_zygote_control_passes_internal_marker`
- `build/test_ninjector_spawn_injector_cli_strict_green.exe`
- `build/test_zygote_control_rpc_cli_strict_regress.exe`
- `build/test_session_registry_cli_strict_regress.exe`
- `build/test_server_zygote_control_rpc_regressions_cli_strict.exe`

## Practical Usage

Legacy style:

```powershell
nook-cli spawn com.demo.target -l .\hook.js --resume --strict-zygote-control --usb
```

Frida style:

```powershell
nook-cli -U -f com.demo.target -l .\hook.js --strict-zygote-control
```
