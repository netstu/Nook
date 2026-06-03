# 2026-05-13 Zygote-Control Terminal Log State Alignment

## Goal

让 `FormatZygoteControlTerminalOutcomeLog(...)` 与上一轮完成的
`FormatZygoteControlFinalError(...)` 保持同一结构，不再让 terminal outcome log
缺少明确的 lifecycle `state=...`。

## Problem

上一轮后，最终错误已经带：

- `stage=...`
- `class=...`
- `state=...`
- `detail=...`

但 terminal outcome log 仍只有：

- `stage=...`
- `event=...`
- `primary=...`
- `secondary=...`
- `detail=...`

这意味着：

- 终端日志和最终错误不能直接对齐
- 分析日志时仍要从 `detail` 反推失败阶段

## Change

更新：

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

具体调整：

- `FormatZygoteControlTerminalOutcomeLog(...)` 新增 `state_name` 参数
- terminal outcome log 现在输出：
  - `state=<...>`
- spawn/finalize 的 terminal outcome 调用点统一传入：
  - `ZygoteControlFailureStateToString(zygote_error_state)`

## Result

现在 zygote-control 的两类关键输出已经结构对齐：

1. terminal outcome log
2. final error string

它们都会显式包含：

- `stage=...`
- `state=...`
- `detail=...`

这让后续做：

- 失败分类统计
- 真实设备日志归因
- fallback policy 收敛

时不需要优先依赖自由文本解析。

## Verification

Passed:

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_terminal_state_green.exe
& "E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\build\\test_ninjector_spawn_injector_terminal_state_green.exe"
```
