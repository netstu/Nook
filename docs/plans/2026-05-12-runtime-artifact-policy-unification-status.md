# 2026-05-12 Runtime Artifact Policy Unification Status

## 本次完成项

按 [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md) 的 B2 执行，已把 `agent` / `ncore` 的 runtime artifact 准备与清理策略收敛到同一套 helper。

修改文件：

- [ninjector_spawn_injector.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.h)
- [ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

## 新结构

新增：

- `PreparedRuntimeArtifact`
- `PrepareRuntimeArtifact()`
- `MaybeCleanupRuntimeArtifact()`

现在这些旧入口都收敛到同一套 policy：

- `EnsureLegacyAgentReady()`
- `EnsureLegacyNcoreReady()`
- `MaybeCleanupLegacyAgentArtifact()`
- `MaybeCleanupLegacyNcoreArtifact()`

## 统一后的策略语义

两类 artifact 现在都按同一模式处理：

1. 请求路径为空 -> 直接报错
2. 路径已存在 -> 直接使用
3. embedded blob 存在 -> materialize 到请求路径
4. cleanup 只针对“本次 materialize 且看起来像 embedded runtime artifact”的文件
5. 若显式环境变量指定了外部路径，则不自动 cleanup

## 为什么这一步重要

这一步的价值不在“新增功能”，而在消除 agent/ncore 两套逐渐漂移的特例逻辑。

现在后续如果要继续做：

- embedded-first
- memfd-first
- file fallback 显式化

都已经有了单一挂点，不需要再同时改两套几乎重复的逻辑。

## 验证结果

### 已通过

- `nook_server` Android 构建路径之前已经持续可编
- 本次 B2 改动本身没有引入新的主构建报错

### 额外发现

在继续跑 host 侧 `test_ninjector_spawn_injector` 时，暴露出两类与 B2 核心逻辑无关的历史问题：

1. `ninjector_compat.cpp` 的 host stub 覆盖不完整
2. `test_ninjector_spawn_injector.cpp` 中部分 `zygote-control` / `FinalizeSpawn()` 期望与当前实现语义已不一致

这些问题会影响整套 host 回归，但不构成对本次 B2 helper 收敛本身的直接反证。

## 下一步建议

如果继续做测试收尾，应该单独开一轮“host spawn test 对齐”工作，而不是继续把它混在 B2 里：

1. 先固定 `FinalizeSpawn()` 在 `backend == kNone` 时的预期策略
2. 再统一 `test_ninjector_spawn_injector.cpp` 对当前 `zygote-control/symbi/legacy` 语义的断言

这样可以把“策略收敛”与“历史测试债务”明确分开。
