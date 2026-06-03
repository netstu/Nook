# 2026-05-13 Zygote-Control Runtime-Ready State Split

## Goal

Continue the `zygote-control` state-boundary convergence by removing one more
implicit coupling in the server registry.

Before this change:

- authoritative control readiness was already explicit
- but runtime readiness still remained implicit through cached runtime-stage
  `AGENT_READY` frames

That meant two different concerns were still partially coupled:

1. whether the process has reached runtime/script-capable readiness
2. whether the server still has a cached `AGENT_READY` frame to replay

Those are related, but they are not the same state.

## Change

Updated:

- [session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

Added explicit registry state:

- `MarkAgentRuntimeReady(int pid)`
- `IsAgentRuntimeReady(int pid)`

Internal storage added:

- `agent_runtime_ready_`

Behavior now:

- control-stage `AGENT_READY`
  - registers session/process identity
  - marks authoritative control ready
  - does **not** mark runtime ready
  - does **not** cache runtime ready frame
- runtime-stage `AGENT_READY`
  - registers session/process identity
  - marks authoritative control ready
  - marks runtime ready
  - stores cached runtime `AGENT_READY` frame

Cleanup semantics were also kept aligned:

- removing an agent session now clears:
  - authoritative ready
  - runtime ready
  - cached runtime `AGENT_READY`

## Why This Matters

This is a narrow but real state-model improvement:

- authoritative-ready is now explicit
- runtime-ready is now explicit
- cached runtime-ready frame is now only a replay artifact

That makes the model easier to reason about for the next `zygote-control`
timing/debugging passes.

In particular, later work can now distinguish:

- "server knows the control channel is authoritative"
- "server knows runtime is script-capable"
- "server still has a runtime ready frame available for replay"

without overloading one signal to mean all three.

## Regression Coverage

Updated tests:

- [test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

Coverage added:

- runtime-ready can be explicit without requiring a cached runtime-ready frame
- control-stage `AGENT_READY` leaves runtime-ready false
- runtime-stage `AGENT_READY` sets runtime-ready true

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_runtime_ready.exe
build\test_session_registry_runtime_ready.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_runtime_ready.exe
build\test_server_handlers_runtime_ready.exe
```

## Practical Next Step

The next rational step is not broadening `zygote-control` yet.

The next narrow convergence target should be one of:

1. make spawn/controller gating query explicit runtime-ready state instead of
   inferring through cached frame presence where possible
2. tighten any remaining server-side places that still use cached runtime-ready
   frame as a proxy for state instead of a replay artifact
