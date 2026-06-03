# 2026-05-13 agent-owned stable spawn zygote control stage readiness

## 背景

继续检查 `zygote-control` 的 ready/rpc 时序后，确认了一个重要事实：

- 设备侧 `NookZygoteMonitorInitialize()` 中
  - 先 `RegisterInternalRpcRequestHandler("*", HandleZygoteControlRpc)`
  - 再 `RefreshAgentCallbacksForInternalRpc()`
  - 再 `NotifyZygoteControlReadyToServer()`
  - 最后发送 `AGENT_READY stage=control`

也就是说，当前代码里并不存在“先发 control-stage ready，后装 rpc handler”的顺序错误。

真正的问题变成了 server 侧状态语义不够明确：

- `zygote_control_rpc` 只依赖 `authoritative ready`
- 但 `SessionRegistry` 之前没有显式记录 authoritative ready 对应的是 `control` 还是 `runtime`
- 这让 host 侧判断仍然依赖隐式约定

## 改动

文件：

- `server/session_registry.h`
- `server/session_registry.cpp`
- `server/server_handlers.cpp`
- `server/zygote_control_rpc.cpp`
- `tests/communication/test_zygote_control_rpc.cpp`
- `tests/communication/test_server_handlers.cpp`
- `tests/communication/test_server_handlers_stage_subset.cpp`

改动点：

1. `SessionRegistry` 新增显式 ready stage 记录
   - `MarkAgentReadyStage(int pid, AgentReadyStage stage)`
   - `GetAgentReadyStage(int pid, AgentReadyStage* stage)`
   - `IsAgentControlReady(int pid)`

2. `HandleAgentReady()` 在 control/runtime 两条路径都记录 ready stage

3. `CallZygoteControlRpcWithSenderForTest()` 进一步收紧
   - 不再只依赖 authoritative session
   - 额外要求 registry 中存在显式 ready stage
   - 当前接受 `kControl` 或 `kRuntime` 作为 control-capable stage

4. 新增回归
   - `TestCallZygoteControlRpcRequiresRecordedReadyStage`
   - `test_server_handlers` 中补充 control/runtime stage 断言
   - 新增最小子集 `test_server_handlers_stage_subset.cpp`，只验证新增的 stage 语义

## 验证

### 已通过

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_zygote_control_rpc.cpp server/zygote_control_rpc.cpp server/session_registry.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_zygote_control_rpc.exe
& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_zygote_control_rpc.exe'
```

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers_stage_subset.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/handler/message_dispatcher.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/communication/protocol/frame.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp -o build/test_server_handlers_stage_subset.exe
cmd /c "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_server_handlers_stage_subset.exe"
```

### 备注

整套 `test_server_handlers.exe` 在当前本地环境里触发了一个既有 `std::bad_alloc`，但最小 stage 子集已通过，因此这次新增状态面本身没有发现回归。

## 结论

到这一步，host 侧 `zygote-control` 已经完成三层收敛：

1. 避免 zygote stale-ready env 残留误判
2. 避免 session wait 吞掉 ready-wait 小预算
3. 避免用宽泛的 authoritative bit 代替显式 control-capable stage

## 下一步

继续回到真实设备链路，重点看：

1. attach/spawn 真机超时是否还集中在 `remote_dlopen_failed` / zygote rpc timeout
2. `zygote-control` 默认路径是否已经足够稳定到可以重新做默认/实验路径切换
3. 是否需要给 `zygote_control_rpc` 再加一层“control-stage ready frame cache / metrics”用于真机日志定位
