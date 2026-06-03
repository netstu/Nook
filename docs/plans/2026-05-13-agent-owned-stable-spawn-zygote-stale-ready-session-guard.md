# 2026-05-13 agent-owned stable spawn zygote stale-ready session guard

## 背景

host 侧 `zygote-control` 路由已经收敛到单入口后，剩余一个高概率真实问题是：

- zygote 进程内的 `NOOK_ZYGOTE_MONITOR_READY=1` 会跨 server 生命周期残留
- 但 server 的 `SessionRegistry` 会在重启后清空
- 旧逻辑只看 `IsZygoteMonitorReady(pid)`，会把“env 残留但当前 server 没有 authoritative agent session”的 zygote 误判成已就绪
- 随后的 `WaitForZygoteControlReady()` / `InstallZygoteForkHook()` 只能在 RPC ready 等待阶段超时

这和之前真实设备上出现的“server 重启后 spawn 超时，但 app/zygote 状态并非完全坏掉”的现象是吻合的。

## 改动

文件：

- `server/ninjector_spawn_injector.h`
- `server/ninjector_spawn_injector.cpp`
- `tests/communication/test_ninjector_spawn_injector.cpp`

改动点：

1. 给 `NinjectorSpawnOps` 新增 `is_zygote_monitor_ready`
   - 默认接 `ninjector::IsZygoteMonitorReady`
   - 让测试能单独控制“远端 env ready”而不必耦合真实注入逻辑

2. 收紧 `TrySpawnViaZygoteControl()` 的 zygote ready 判定
   - 先看远端 `monitor_ready`
   - 再看当前 server 是否已经有该 zygote 的 authoritative session
   - 如果 `monitor_ready == true` 但 `has_preexisting_zygote_session == false`
     - 视为 stale-ready
     - 直接强制走 reinject，而不是跳过注入

3. 新增两条回归测试
   - `TestSpawnReinjectsWhenMonitorReadyButSessionMissing`
   - `TestSpawnSkipsReinjectWhenMonitorReadyAndSessionPresent`

## 结果

这次修复不改变 `zygote-control` 主体协议，只修正 host 侧“是否认为 zygote 已经可用”的入口条件。

它的目标不是解决全部 `zygote-control` 超时，而是先去掉一个明确的错误前提：

- 不能再把“旧 env 残留”当成“当前 server 可直接 RPC”的证据

## 验证

本地 host 回归通过：

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_zygote_ready_guard.exe
.\build\test_ninjector_spawn_injector_zygote_ready_guard.exe
```

## 下一步

继续回到真实设备路径，重点检查：

1. `WaitForZygoteControlReady()` 是否还把“authoritative session 出现”与“RPC handler 真正可服务”混为一谈
2. `NotifyZygoteControlReadyToServer()` 到 `SessionRegistry::MarkAgentAuthoritativeReady()` 之间是否还存在 host 可见性时序洞
3. `installForkHook` / `uninstallForkHook` 的 RPC ready 窗口是否需要进一步做 session-stage / rpc-stage 双判定
