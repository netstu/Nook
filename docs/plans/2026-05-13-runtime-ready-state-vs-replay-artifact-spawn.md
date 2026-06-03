# 2026-05-13 Runtime-Ready State vs Replay Artifact in Spawn

## Goal

继续收敛 spawn 路径里的 ready 语义，去掉另一处把 cached runtime
`AGENT_READY` frame 当作 runtime-ready 判据的逻辑。

这次目标是 `spawn_controller.cpp`。

## Problem

此前 `ExecuteSpawnRequest()` 在发送 `SPAWN_RESPONSE` 之后，仍然通过：

- `GetAgentReadyFrame(authoritative_pid, &cached_ready)`

来决定 spawn suspended entry 应进入：

- `kReadyForScriptLoad`
- 或 `kWaitingRuntimeReady`

这意味着：

1. target runtime 是否已经 script-capable
2. server 是否手头还有一份可重放的 cached runtime `AGENT_READY`

仍然被耦合在一起。

但 cached frame 只是 replay artifact，不应再充当 ready state。

## Change

更新：

- [spawn_controller.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/spawn_controller.cpp)
- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)

调整后语义：

- `registry->IsAgentRuntimeReady(authoritative_pid)` 决定是否进入
  `kReadyForScriptLoad`
- cached runtime `AGENT_READY` frame 如果存在，则仅用于 replay 给已绑定 host
- 即使没有 cached frame，只要 runtime-ready state 已经成立，spawn 也会进入
  `kReadyForScriptLoad`

## Regression Coverage

新增：

- `TestSpawnRequestUsesRuntimeReadyStateWithoutCachedReadyFrame`

覆盖点：

- authoritative pid 已具备显式 runtime-ready state
- registry 中没有 cached `AGENT_READY` frame
- spawn response 仍然成功
- suspended entry 状态进入 `kReadyForScriptLoad`
- host 不会因为缺少 cached frame 而被卡在 `kWaitingRuntimeReady`

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers_spawn_runtime_ready_green.exe
./build/test_server_handlers_spawn_runtime_ready_green.exe
```

## Result

attach 与 spawn 现在都遵循同一条语义：

- readiness 看显式 runtime-ready state
- cached runtime `AGENT_READY` frame 只做 replay

这为下一步把 spawn/zygote-control 收敛到真正的 agent-owned ready boundary
做了必要清理。
