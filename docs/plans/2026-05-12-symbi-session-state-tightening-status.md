# 2026-05-12 Symbi Session State Tightening Status

## Goal

Tighten the current default `symbi` spawn session semantics without changing the working device-visible behavior.

Specifically:

- control-stage `AGENT_READY` must not be treated as the authoritative runtime agent session
- runtime-stage `AGENT_READY` must remain the only script-capable session boundary
- stale control connections must not be able to clear the later runtime session mapping on close

## Problem

Real-device logs showed a stable but noisy two-stage pattern for the same child pid:

1. early `agent connected`
2. early `agent ready without bound host ... name=zygote64`
3. later runtime `agent connected`
4. later runtime `agent ready without bound host ... name=com.ad2001.frida0x1`

This was functionally working, but the server-side state model still allowed the control-stage connection to look too much like a formal agent session.

That left two risks:

- host-side state could observe the wrong agent session boundary
- an older control-stage connection closing later could remove the newer runtime session mapping by pid

## Change Applied

### 1. Only runtime `AGENT_READY` now registers the authoritative agent session

Updated:

- `server/server_handlers.cpp`

Behavior now:

- `kControl`
  - updates spawn state to `kWaitingRuntimeReady`
  - may resolve pending spawn token
  - does not register authoritative agent session
  - does not store authoritative cached `AGENT_READY`
- `kRuntime`
  - registers authoritative agent session
  - registers authoritative process name
  - stores cached `AGENT_READY`
  - allows normal replay / script lifecycle

### 2. Agent-session cleanup is now conditional on session identity

Updated:

- `server/session_registry.h`
- `server/session_registry.cpp`
- `server/server_main.cpp`

Added:

- `RemoveAgentSessionByPidIfMatches(int pid, Session* session)`

This prevents an older connection from removing a newer agent session that reused the same pid mapping.

## Resulting Semantics

After this tightening:

- control-stage connection is treated as a handoff/control signal, not a formal script-capable agent session
- runtime-stage connection is the only authoritative session for:
  - cached `AGENT_READY`
  - `FindAgentSessionByPid`
  - script create/load forwarding
  - replay after spawn success

This keeps the existing successful `symbi` behavior while making the state model closer to the actual architecture.

## Tests Added / Updated

Updated:

- `tests/communication/test_server_handlers.cpp`
- `tests/communication/test_session_registry.cpp`

Coverage added:

- control-stage `AGENT_READY` does not forward to host
- control-stage `AGENT_READY` does not register authoritative agent session
- control-stage `AGENT_READY` does not populate cached authoritative ready frame
- later runtime `AGENT_READY` replaces the earlier control-stage connection as the formal pid binding
- conditional pid cleanup helper is verified in registry tests

## Local Verification

Passed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe`
- `build/test_server_handlers.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry.exe`
- `build/test_session_registry.exe`

## Next Step

The next practical step is real-device validation to confirm the log shape becomes cleaner and that repeated default `spawn` runs still behave correctly.

After that, the next engineering target should be:

- repeated default `symbi` regression coverage
- explicit `--spawn-symbi` regression coverage
- fallback-path verification

Not recommended yet:

- broadening `zygote-control`
- changing the working default backend again
