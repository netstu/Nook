Date: 2026-05-10

## Summary

`NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL` 的默认语义发生了回归。

文档已经明确要求实验性 zygote-control 只在显式设置 `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1` 时启用，但 `server/ninjector_spawn_injector.cpp` 一度漂移成了“未设置即开启”。这会让普通 `spawn` 默认走实验路径，并在目标 MIUI / Android 11 设备上重新触发 `zygote64` 崩溃、设备重启和 `spawn agent-ready timed out`。

## User-visible symptom

- `nook-cli -U -f ...` 直接超时
- 设备出现白屏、服务重启，严重时整机重启
- logcat 中反复出现：
  - `JNI FatalError called: (zygote) Unable to get socket name`
  - 进程名：`zygote64`

## Root cause

回归点在：

- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

错误逻辑曾是：

- 环境变量未设置或为空 -> `enable_zygote_control = true`
- 只有明确 `=0` 才关闭

这与既有设计和文档不一致：

- [docs/step10.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/step10.md)
- [docs/step11.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/step11.md)

## Fix

恢复为严格 opt-in：

- 只有 `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1` 时启用 zygote-control
- 未设置、空字符串、`0` 等情况全部走 legacy spawn

同时补充单测回归保护：

- [tests/communication/test_ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_ninjector_spawn_injector.cpp)

覆盖语义：

- 未设置 env -> 默认关闭
- `=1` -> 开启
- `=0` -> 关闭

## Why this matters

这不是普通功能差异，而是安全默认值被改坏：

- 用户即使没有显式打开实验路径，也会被强制带入不稳定的 zygote 注入链路
- 在当前设备上，该链路已被日志证明会破坏 `zygote64` fork 阶段

## Current rule

当前稳定规则应保持为：

1. 默认使用 legacy spawn
2. zygote-control 仅用于显式实验验证
3. 没有完成真实无害的 zygote 方案前，不能再次把它改成默认开启
