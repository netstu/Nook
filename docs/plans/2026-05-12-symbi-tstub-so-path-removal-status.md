# 2026-05-12 Symbi TStub so_path Removal Status

## 本次改动

已从 `symbi` zygote stub ABI 中移除死字段 `so_path`：

- [stub.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.h)
- [offset_check.c](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/offset_check.c)
- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 变化说明

之前：

- `TStub` 同时带 `so_path` 和 `socket_name`
- 但当前 active stub 逻辑只使用 `socket_name`
- `symbi_notify_and_wait_ack()`、`stub_replacement_set_argv0()` 都不会读取 `so_path`
- server 侧 `collect_symbi_context()` 也没有真正依赖它

现在：

- `TStub.so_path` 已删除
- `offset_check.c` 不再输出其偏移
- `collect_symbi_context()` 去掉了无效的 `so_path` 形参

## 影响

这是一次 ABI 收缩，不是运行逻辑改造：

- 不改变当前 `symbi` 的 child callback 握手流程
- 不改变 `spawn` 默认后端选择
- 不改变 `legacy ncore` / `attach` 行为

真正改变的是：

- zygote stub 负载更小
- `TStub` 字段语义更接近当前真实使用面
- 后续继续删 debug-only 字段时，边界更清晰

## 后续建议

下一步可以继续收缩但要分开做：

1. 评估 `log_print` 是否继续保留为 debug-only 可选字段
2. 不动 `raise`，因为当前 child stop/gate 仍依赖它
3. 如果继续改 `TStub`，每次都必须先重生 `generated_stub.h` 再编 `nook_server`
