# 2026-05-13 agent-owned stable spawn zygote rpc session wait budget

## 背景

在修完 `stale-ready` 误判后，`zygote-control` host 侧仍有一个明显的时序问题：

- `WaitForZygoteControlReady()` 的外层重试预算是 5 秒
- 每次轮询调用 `CallZygoteControlRpcWithSenderForTest()`
- 但该函数内部固定先做一次 `WaitForAuthoritativeAgentSessionByIdentity(..., 4500ms)`

结果是：

- 表面上是 250ms 一次的 `nook.spawn.status` 轮询
- 实际上第一次调用就可能在“等 session 出现”这里阻塞 4.5 秒
- “等 AGENT_READY 可见”与“等 RPC handler 真可用”被揉成了一次重阻塞

这会放大 `zygote-control` 的 host 侧超时和白屏窗口。

## 改动

文件：

- `server/zygote_control_rpc.cpp`
- `tests/communication/test_zygote_control_rpc.cpp`

改动点：

1. 去掉 `CallZygoteControlRpcWithSenderForTest()` 内部固定的 `4500ms` session wait
2. 改为使用本次调用传入的 `request_timeout_ms` 作为 session wait budget
   - `request_timeout_ms > 0 ? request_timeout_ms : 1`
3. 新增回归测试
   - `TestCallZygoteControlRpcUsesBoundedSessionWaitBudget`
   - 验证短超时调用不会再被内部 session wait 拉长到秒级

## 结果

现在 `WaitForZygoteControlReady()` 的轮询模型才和表面参数一致：

- 每次 status 轮询只消耗本次小预算
- session 未出现时快速失败并进入下一次 retry
- 不再因为内部固定等待 4.5 秒而吞掉整个 ready-wait 窗口

## 验证

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_zygote_control_rpc.exe
& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_zygote_control_rpc.exe'
```

## 下一步

继续检查真实设备链路上还有没有下面这类问题：

1. `AGENT_READY stage=control` 已进 registry，但内部 RPC handler 还没真正刷新到 connection
2. `NotifyZygoteControlReadyToServer()` 与 host `WaitForAuthoritativeAgentSessionByIdentity()` 之间是否仍有 stage/rpc 可见性差
3. 是否需要把 host 对 zygote session 的判定从“authoritative ready”进一步收紧为“authoritative + control-rpc capable”
