# 2026-05-12 Symbi Stop Window Tightening Status

## 本次改动

按 [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md) 里的 A4 执行，只收缩 `zygote` 停顿窗口，不改后端路由和运行语义。

修改文件：

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 具体做法

把原来 `stop_process(zygote)` 之后才做的两类工作前移到了 stop 之前：

1. stub payload 组包
2. `/proc/<zygote>/mem` 打开

现在的执行顺序变成：

1. `collect_symbi_context()`
2. `OpenSymbiCallbackListener()`
3. `prepare_stub_patch()`
4. `open_remote_mem()`
5. `stop_process()`
6. `apply_prepared_stub_patch()`
7. `resume_process()`

## 为什么这样改

之前 stop window 里仍然混着一些本地工作：

- marker 查找
- `stub_copy` 分配和填充
- `TStub` 字段写入
- `open_remote_mem()`

这些都不是“zygote 必须停住”时才能做的事。

按现在的版本，zygote 真正停住期间只剩下：

- 向 shellcode 区写入 prepared stub
- 向 `ArtMethod` slot 写入新入口指针

这更接近文档里“small gate payload, short critical section”的方向。

## 本次不做的事

这一步明确没有做：

- 不改 `restore_original_slot()` 状态机
- 不改 callback 握手协议
- 不改 `symbi` / `legacy ncore` / `zygote-control` 路由
- 不改 child 侧 agent delivery

## 预期收益

1. zygote 被 `SIGSTOP` 的时间更短
2. 出问题时更容易判断是“远程写入失败”还是“stop window 太长”
3. 后续如果继续做 stop window 收缩，边界已经更清晰
