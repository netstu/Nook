# Agent-Owned Stable Spawn Terminal Classification Helper

## Context

The successful route path had already been converged behind explicit owner
helpers, but terminal classification still wrote outcome state inline through a
large set of repeated combinations:

- `final_status`
- `terminal_primary_backend`
- `terminal_secondary_backend`

That meant terminal lifecycle semantics were still embedded directly in branch
logic instead of being applied through an explicit contract.

## Change

Extracted explicit helper:

- `ApplyTerminalOutcomeClassification()`

`ClassifyTerminalSpawnOutcome()` now focuses on deciding which terminal
combination applies, while the helper performs the actual terminal state write.

This centralizes the contract for terminal outcome application and removes the
repeated field-by-field writes spread through classification branches.

## Why It Matters

This is the same cleanup pattern used earlier for:

- pending owner construction
- successful route commit

Now terminal classification also has an explicit application boundary. That
makes the host-side spawn lifecycle more uniform:

1. route/state logic decides
2. explicit helper applies lifecycle state

That will make the next step easier when converging failure-path outcome
application into similar owner/session-oriented helpers.

## Verification

Red:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_terminal_classification_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `ApplyTerminalOutcomeClassification`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_terminal_classification_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_terminal_classification_green.exe"
```
