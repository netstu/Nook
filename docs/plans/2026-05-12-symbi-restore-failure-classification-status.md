# 2026-05-12 Symbi Restore Failure Classification Status

## 本次改动

继续按 [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md) 的 A3 往下收，只细分 restore 失败维度。

修改文件：

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 新的失败维度

现在 restore 失败会区分这些内部原因：

- `primary_stop_failed`
- `write_open_remote_mem_failed`
- `write_mem_failed`
- `ptrace_attach_failed`
- `ptrace_waitpid_failed`

## 外部错误形态

之前外部通常只能看到：

- `spawn_symbi_failed:restore_original_slot_failed`

现在会变成更具体的形式，例如：

- `spawn_symbi_failed:restore_original_slot_failed:after-callback:primary_stop_failed:ptrace_attach_failed`
- `spawn_symbi_failed:restore_original_slot_failed:after-start-failure:write_mem_failed:ptrace_waitpid_failed`

这样做的目的不是改行为，而是把 restore 失败的真正断点暴露出来，减少“只知道 restore 失败，不知道卡在哪”的问题。

## 兼容性

这一步没有改：

- `get_last_spawn_symbi_error()` 接口
- `spawn_symbi_failed:` 前缀拼装逻辑
- callback / restore 的主流程

只是把 `restore_original_slot_failed` 从单一错误升级成带阶段和失败维度的错误串。

## 意义

这一步完成后，A3 的“更可审计的 restore sequencing”基本就落到位了：

1. 先有统一的 restore 状态机入口
2. 再把 restore 失败分类具体化

后面如果真机再遇到 restore 相关问题，日志和 CLI 失败串都会更容易判断问题点。
