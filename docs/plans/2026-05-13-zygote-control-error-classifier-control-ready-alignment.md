# 2026-05-13 Zygote-Control Error Classifier Control-Ready Alignment

## Goal

让 `ninjector_spawn_injector` 的 zygote-control 失败分类器识别新的
`control-ready` 文案，确保前面做的错误命名收敛不会破坏 fallback/soft-hard 判定。

## Problem

上一轮中，`zygote_control_rpc.cpp` / `nook_zygote_control.cpp` 的关键错误文案从：

- `zygote agent session not found`
- `zygote control ready wait timed out`

收敛到了：

- `zygote control-ready agent session not found`
- `zygote control-ready wait timed out`

但 `server/ninjector_spawn_injector.cpp` 的分类器仍只匹配旧字符串。

这会导致两个风险：

- 同样的 ready-wait 失败可能不再被归到 `kReadyWait`
- soft fallback 策略可能被误判成 hard failure

## Change

更新：

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

具体调整：

- `InferZygoteControlLifecycleStateFromError(...)`
  - 增加对 `zygote control-ready agent session not found`
  - 增加对 `zygote control-ready wait timed out`
  的识别
- `IsSoftZygoteControlInstallFailure(...)`
  - 同步增加上述新文案匹配

兼容性策略：

- 新旧字符串都接受
- 保留旧匹配，避免历史路径或文档未完全收敛时出现分类断层

## Result

现在这一条链条重新闭合：

- `zygote_control_rpc` 输出更准确的 control-ready 错误文案
- `ninjector_spawn_injector` 仍把这些错误判为 `ReadyWait/soft`
- fallback 行为不会因为文案收敛而改变

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_control_ready_classify.exe
& "E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\build\\test_ninjector_spawn_injector_control_ready_classify.exe"
```
