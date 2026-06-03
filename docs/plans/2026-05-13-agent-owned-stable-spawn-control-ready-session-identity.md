# 2026-05-13 agent-owned stable spawn control-ready session identity

## 背景

前一轮已经给 `SessionRegistry` 增加了显式 ready stage，但 `zygote_control_rpc` 仍然是：

1. 先 `WaitForAuthoritativeAgentSessionByIdentity()`
2. 再额外检查 `GetAgentReadyStage()`

这说明 host 侧语义仍然没有真正收口，还是在用“authoritative session + 后验 stage 校验”的两段式判断。

对于 `zygote-control`，更合理的语义应该是：

- 直接等待和查找 `control-ready` session identity
- 不再把“authoritative”当成主判定，再去补 stage

## 改动

文件：

- `server/session_registry.h`
- `server/session_registry.cpp`
- `server/zygote_control_rpc.cpp`
- `tests/communication/test_session_registry.cpp`
- `tests/communication/test_zygote_control_rpc.cpp`
- `tests/headers/test_server_zygote_control_rpc_regressions.cpp`

### SessionRegistry

新增：

- `FindControlReadyAgentSessionByPid()`
- `FindControlReadyAgentSessionByProcessName()`
- `WaitForControlReadyAgentSessionByIdentity()`

语义：

- 要求 session 存在
- 要求 authoritative ready 为真
- 要求 ready stage 已记录，且为 `kControl` 或 `kRuntime`

### zygote_control_rpc

`CallZygoteControlRpcWithSenderForTest()` 现在直接调用：

- `WaitForControlReadyAgentSessionByIdentity()`

不再先 `WaitForAuthoritativeAgentSessionByIdentity()` 再做后验 stage 检查。

同时，process-name fallback 的日志条件也改为基于：

- `FindControlReadyAgentSessionByPid()`

## 测试

新增 / 调整：

- `TestWaitForControlReadyAgentSessionByIdentityRequiresRecordedReadyStage`
- `TestCallZygoteControlRpcRequiresControlReadySession`

并把之前成功路径里的 `test_zygote_control_rpc` 用例补齐：

- `MarkAgentReadyStage(...)`

使这些用例真正构造出 control-ready session，而不是只有 authoritative bit。

header regression 也同步更新为检查：

- `WaitForControlReadyAgentSessionByIdentity(`

## 验证

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_session_registry_control_ready.exe
cmd /c "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_session_registry_control_ready.exe"
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_zygote_control_rpc_dbg.exe
& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_zygote_control_rpc_dbg.exe'
```

```powershell
g++ -std=c++17 -I . tests/headers/test_server_zygote_control_rpc_regressions.cpp -o build/test_server_zygote_control_rpc_regressions_v4.exe
cmd /c "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_server_zygote_control_rpc_regressions_v4.exe"
```

## 结果

到这里，`zygote-control` host 侧等待语义已经从宽到窄收口成：

- stale-ready env 不算 ready
- small-budget ready poll 不再被内部 session wait 吞掉
- session 不仅要 authoritative，还要显式 control-capable
- `zygote_control_rpc` 直接等待 control-ready identity，而不是事后补 stage 校验

这意味着 host 侧已经基本具备重新回到真实设备验证 `zygote-control` 的条件。
