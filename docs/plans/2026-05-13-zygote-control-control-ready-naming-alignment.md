# 2026-05-13 Zygote-Control Control-Ready Naming Alignment

## Goal

继续收敛 `zygote-control` 路径中的阶段语义，避免把 zygote RPC 可用边界继续模糊表述成通用 `ready`。

这一步只做命名/文案对齐，不改行为。

## Problem

在当前实现中，`zygote-control` 的 RPC 路径真正依赖的是：

- zygote agent session 已建立
- control-stage `AGENT_READY` 已到达
- server 已标记 authoritative/control-ready

但错误文案和日志里仍有两种模糊表述：

- `zygote agent session not found`
- `zygote control ready wait timed out`

以及设备侧：

- `zygote monitor authoritative ready notify failed`

这些词没有把 “control/RPC ready” 与 “runtime/script ready” 明确区分开。

## Change

更新：

- [zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
- [nook_zygote_control.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/nook_zygote_control.cpp)
- [test_zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_zygote_control_rpc.cpp)

文案收敛为：

- `zygote control-ready agent session not found ...`
- `zygote control-ready wait timed out ...`
- `zygote monitor control-ready notify failed ...`

## Result

现在 `zygote-control` 这条链路里的三个概念分工更明确：

- `control-ready`
  - zygote agent 已可接收内部 RPC
- `runtime-ready`
  - 子进程 agent 已可创建/加载脚本
- `ready`
  - 不再作为这两个阶段的混用词出现在关键错误文案里

这为后续继续收敛 `zygote-control` 状态机边界、以及真正的 agent-owned stable spawn，减少了日志和错误分类上的歧义。

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_zygote_control_rpc_control_ready_naming.exe
.\build\test_zygote_control_rpc_control_ready_naming.exe
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/headers/test_server_zygote_control_rpc_regressions.cpp -o build/test_server_zygote_control_rpc_regressions_control_ready_naming.exe
.\build\test_server_zygote_control_rpc_regressions_control_ready_naming.exe
```
