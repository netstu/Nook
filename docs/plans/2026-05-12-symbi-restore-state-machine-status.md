# 2026-05-12 Symbi Restore State Machine Status

## 本次改动

按 [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md) 里的 A3 执行，只整理 `restore` 流程，使其更接近明确状态机。

修改文件：

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 做了什么

新增了两个轻量结构：

1. `RestoreStage`
   - `kAfterStartFailure`
   - `kAfterCallback`

2. `RestoreAttemptResult`
   - 是否恢复成功
   - 是否走过 primary restore
   - 是否走过 ptrace fallback

并新增统一入口：

- `restore_with_fallback()`

它把 restore 顺序固定成：

1. primary restore
2. primary 失败
3. ptrace fallback
4. 成功或最终失败分类

## 为什么这么做

之前流程虽然逻辑上没错，但阅读上是散的：

- start app 失败后直接调一次 `restore_original_slot()`
- callback 后再手写一段 primary/fallback 分支
- 日志和错误分类分散在多个位置

现在之后，两个 restore 场景都会走同一个状态机入口：

- `after-start-failure`
- `after-callback`

这样后面看日志时，更容易区分：

1. 是哪个阶段触发 restore
2. primary restore 有没有跑
3. ptrace fallback 有没有跑
4. 最终是恢复成功，还是所有恢复路径都失败

## 行为边界

这一步没有改变：

- callback 协议
- `start_target_app_symbi()` 行为
- `symbi` 后端选择
- `legacy ncore` / `zygote-control` 路由

现有外部错误码也基本保持兼容：

- `start_target_app_failed`
- `callback_wait_failed`
- `restore_original_slot_failed`

只是内部 restore sequencing 更明确了。

## 后续建议

如果继续按同一份文档推进，下一步更适合做的是：

1. 给 restore 日志补更精确的失败维度
   - stop 失败
   - write 失败
   - ptrace attach/wait 失败
2. 然后再看是否需要为这类路径补单测或宿主侧模拟测试
