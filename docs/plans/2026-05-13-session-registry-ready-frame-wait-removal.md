# 2026-05-13 Session Registry Ready-Frame Wait Removal

## Goal

清理 `SessionRegistry` 中已经失去调用方的旧接口，减少后续 ready 语义继续混淆的表面积。

本次目标接口：

- `WaitForAgentReadyFrame(int pid, uint32_t timeout_ms, comm::Frame* frame)`

## Problem

在 attach / spawn 都已经完成如下收敛后：

- readiness 由显式 state 决定
  - control/RPC: authoritative-ready
  - script/runtime: runtime-ready
- cached `AGENT_READY` frame 仅作为 replay artifact

`WaitForAgentReadyFrame()` 已经没有任何服务端调用方。

保留它会带来两个问题：

- API 表面仍暗示“ready frame = ready state”
- 后续修改时容易被误用，把 frame cache 又当成 readiness gate

## Change

更新：

- [session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)

具体调整：

- 删除 `WaitForAgentReadyFrame(...)` 声明与实现
- 删除 attach 路径里不再使用的 `has_ready_frame` 中间变量

保留：

- `GetAgentReadyFrame(...)`

因为它仍负责 replay cached runtime `AGENT_READY` 给 host。

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_session_registry_runtime_cleanup.exe
./build/test_session_registry_runtime_cleanup.exe
```

Supplemental isolation:

- `test_server_handlers` 逐条 probe 运行 46 条用例均通过
- 说明本次改动未引入新的 handler 级语义回归

## Result

`SessionRegistry` 现在不再暴露“等待 cached ready frame”这种旧路径。

服务端 ready 语义继续保持为：

- state decides readiness
- cached frame only supports replay
