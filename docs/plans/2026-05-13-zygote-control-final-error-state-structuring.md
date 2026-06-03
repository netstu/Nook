# 2026-05-13 Zygote-Control Final Error State Structuring

## Goal

把 `ninjector_spawn_injector` 的最终 zygote-control 错误从主要依赖 `detail` 文本，
推进到显式带 `state=` 的结构化错误面。

## Problem

此前最终错误格式是：

- `zygote-control stage=<spawn|finalize> class=<soft|hard> detail=<message>`

问题在于：

- `class=` 只是 soft/hard
- 真正的失败阶段只隐含在 `detail` 文本里
- 调用方或后续日志分析仍要从自由文本反推：
  - `arm-control`
  - `install-hook`
  - `launch-app`
  - `ready-wait`
  等状态

而项目内部其实已经在记录：

- `last_zygote_control_failure_state_`
- `current_zygote_control_lifecycle_stage_`

## Change

更新：

- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

具体调整：

- 新增 `ZygoteControlFailureStateToString(...)`
- `FormatZygoteControlFinalError(...)` 改为输出：
  - `stage=...`
  - `class=...`
  - `state=...`
  - `detail=...`
- spawn / finalize 最终错误出口优先使用：
  1. recorded failure state
  2. recorded lifecycle stage
  3. fallback to `InferZygoteControlLifecycleStateFromError(detail)`

## Result

现在最终错误形态从：

- `zygote-control stage=spawn class=hard detail=start_target_app failed`

变成：

- `zygote-control stage=spawn class=hard state=launch-app detail=start_target_app failed`

这样后续上层看到错误时，不需要再优先解析自由文本猜阶段。

## Regression Coverage

更新的断言覆盖：

- `launch-app`
- `arm-control`
- `install-hook`

它们现在都要求最终错误带显式 `state=...`

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_structured_final_error.exe
cmd /c build\test_ninjector_spawn_injector_structured_final_error.exe
```
