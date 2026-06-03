# 2026-05-12 Symbi Gate Handoff Boundary Status

## 本次改动

按 [2026-05-12-frida-symbi-code-task-breakdown.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-05-12-frida-symbi-code-task-breakdown.md) 的 A2 执行，把 `symbi` 路径中的两个关注点在代码里显式拆开：

1. zygote-side gate install
2. child-side delivery handoff

修改文件：

- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 现在的结构

新增了两个 helper：

- `install_zygote_gate()`
- `complete_child_delivery_handoff()`

以及两个结果结构：

- `SymbiGateInstallResult`
- `SymbiChildHandoffResult`

## 拆分后的语义

### 1. `install_zygote_gate()`

只负责 zygote 侧：

- 打开 `/proc/<zygote>/mem`
- `SIGSTOP` zygote
- 写入 prepared stub
- patch `ArtMethod` slot
- `SIGCONT` zygote

这个阶段明确不做 child runtime delivery。

### 2. `complete_child_delivery_handoff()`

只负责 child 侧交接：

- start target app
- wait child callback
- restore zygote state
- 返回 child pid / package

这个阶段假设 zygote gate 已经装好。

## 为什么这一步重要

之前虽然逻辑上已经分前后，但代码入口还是揉在一个函数里，后续很容易再次把“zygote gate 逻辑”和“child delivery 逻辑”混写。

现在拆开后，边界更清楚：

- `symbi` 的 zygote payload 只做 gate
- child runtime bring-up 仍然是后续 handoff 阶段的事情

这更接近当前文档里要求的：

- 避免把 child delivery fallback 决策混进 zygote patch 逻辑
- 让后续 memfd-first delivery 收敛时有明确挂点

## 本次不做的事

没有改：

- `SpawnViaSymbi()` 对外接口
- `symbi` 后端选择
- callback 协议
- attach / legacy ncore / zygote-control

这一步只是把现有路径的边界从“隐含”变成“显式”。
