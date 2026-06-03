# 2026-05-13 Runtime-Ready State vs Replay Artifact in Attach

## Goal

Continue the ready-state convergence by removing another place where cached
runtime `AGENT_READY` frames still acted as a proxy for runtime readiness.

This pass targeted the attach path only.

## Problem

After introducing explicit registry runtime-ready state, one inconsistency
remained:

- `attach` still treated cached runtime `AGENT_READY` frame presence as the main
  signal that the target was runtime-ready

That kept two concerns partially coupled:

1. target runtime is script-capable
2. server has a cached runtime-ready frame available for replay

The second is optional replay state, not readiness itself.

## Change

Updated:

- [session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Added:

- `WaitForAgentRuntimeReady(int pid, uint32_t timeout_ms)`

Attach behavior now:

- if a target already has:
  - bound agent session
  - explicit runtime-ready state

then attach succeeds immediately even when no cached runtime `AGENT_READY` frame
exists

- during fresh injection attach waits for:
  - explicit runtime-ready state

and only then tries to fetch cached runtime `AGENT_READY` frame for replay when
available

## Result

`attach` now uses:

- explicit runtime-ready state for readiness
- cached runtime `AGENT_READY` frame only for replay

This makes attach semantics align with the newer state split instead of
continuing to treat frame caching as readiness.

## Regression Coverage

Updated:

- [test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue\Nook/tests/communication/test_session_registry.cpp)
- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Coverage added:

- wait-for-runtime-ready does not require cached runtime-ready frame
- attach can reuse an already runtime-ready agent session without cached ready
  frame
- existing attach replay behavior still works when cached ready frame exists

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_runtime_wait.exe
build\test_session_registry_runtime_wait.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_runtime_wait_v2.exe
.\build\test_server_handlers_runtime_wait_v2.exe
```

## Next Step

The next narrow convergence pass should target `spawn_controller`:

- use explicit runtime-ready state as the readiness boundary
- keep cached runtime `AGENT_READY` frame only as replay data for the bound host
