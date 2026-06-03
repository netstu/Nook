# terminal tail helper extraction

## Context

After route orchestration extraction, `Spawn()` still ended with one shared inline tail:

- `ClassifyTerminalSpawnOutcome(...)`
- `FinalizeSpawnOutcome(...)`

This was already backend-agnostic, but it was still an explicit two-step sequence in the outer function.

## Change

Extracted this shared tail into:

- `ApplyTerminalSpawnOutcome(...)`

Files:

- [server/ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

Helper responsibilities:

- run `ClassifyTerminalSpawnOutcome(...)`
- run `FinalizeSpawnOutcome(...)`

`Spawn()` now delegates its remaining shared terminal-stage work to a single helper call after routing/orchestration completes.

## Tests

Added white-box regression in [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp):

- `TestApplyTerminalSpawnOutcomeClassifiesThenFinalizes()`

It verifies that an unresolved route-level outcome is:

1. classified into a terminal state
2. finalized into the expected public error surface

## Verification

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_terminal_tail_green.exe
& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_terminal_tail_green.exe"
```

## Why this matters

This completes the current structural extraction track.

`Spawn()` is now essentially reduced to:

- request validation / owner guard
- precompute zygote-control attempt when needed
- route orchestration helper
- terminal tail helper

At this point further progress should stop focusing on “extract helper X” and start focusing on replacing the remaining outer shell with a more explicit routing/terminal state model tied to the upcoming `agent-owned stable spawn` design.
