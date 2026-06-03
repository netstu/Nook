# 2026-05-13 Authoritative-Ready API Naming Alignment

## Goal

把服务端 registry / zygote-control 里仍然使用模糊 `Ready` 命名、但实际表达
`authoritative/control-ready` 的接口显式化。

## Problem

此前有一组接口名字是：

- `FindReadyAgentSessionByPid(...)`
- `FindReadyAgentSessionByProcessName(...)`
- `WaitForReadyAgentSessionByIdentity(...)`

但它们实际依赖的是：

- `agent_authoritative_ready_`

而不是 runtime-ready。

在当前项目里，`ready` 已经被拆成两种边界：

- authoritative/control-ready
- runtime-ready

继续保留模糊 `Ready` 命名，会直接误导后续 zygote-control / owner-state 收敛。

## Change

更新：

- [session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [test_session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_session_registry.cpp)
- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
- [test_server_zygote_control_rpc_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_server_zygote_control_rpc_regressions.cpp)

接口改名为：

- `FindAuthoritativeAgentSessionByPid(...)`
- `FindAuthoritativeAgentSessionByProcessName(...)`
- `WaitForAuthoritativeAgentSessionByIdentity(...)`

行为未变，仅命名与语义对齐。

## Result

现在服务端三类概念分得更清楚：

- `Find/WaitAgentSession...`
  - 只表示“session 存在”
- `Find/WaitAuthoritativeAgentSession...`
  - 表示 control/RPC ready
- `Is/WaitAgentRuntimeReady`
  - 表示 script/runtime ready

这一步是继续推进 zygote-control 和真正 agent-owned stable spawn 的前置清理。

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_authoritative_naming.exe
./build/test_session_registry_authoritative_naming.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_server_zygote_control_rpc_regressions.cpp -o build/test_server_zygote_control_rpc_regressions_authoritative_naming.exe
./build/test_server_zygote_control_rpc_regressions_authoritative_naming.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_spawn_client_runtime_naming.exe
./build/test_host_spawn_client_runtime_naming.exe
```
