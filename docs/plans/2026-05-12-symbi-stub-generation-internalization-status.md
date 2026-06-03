# 2026-05-12 Symbi Stub Generation Internalization Status

## 本次完成项

已把 `symbi stub` 的“可再生成能力”收回到 Nook 仓库内部：

- 新增 [build_symbi_stub_header.ps1](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tools/build_symbi_stub_header.ps1)
- 使用 Nook 仓库内已有的：
  - `server/symbi/stub_src/stub.c`
  - `server/symbi/stub_src/stub.h`
  - `server/symbi/stub_src/helper.lds`
  - `server/symbi/stub_src/bin2header.py`
- 在本地完成：
  - `stub.c -> stub_local.so -> stub_local.bin -> generated_stub.h`

这意味着后续修改 `server/symbi/stub_src/*` 时，不再需要回外部 `Ninjector` 仓库生成 `generated_stub.h`。

## 本次不做的事

这一步只处理生成链路内收，不改运行时逻辑：

- 不改 `symbi` 注入时序
- 不改 `TStub` ABI
- 不改 `spawn` 默认后端选择
- 不重开 `zygote-control`

## 当前价值

这一步解决的是“构建依赖边界”问题，不是“运行稳定性”问题。

具体收益：

1. `symbi` stub 的源码、链接脚本、header 生成脚本现在都在 Nook 内部闭环
2. 后续如果要删 `so_path`、继续缩 `TStub` 字段，不需要再跨仓库改动
3. 可以把外部 `Ninjector` 从“生成真源”降到“历史参考实现”

## 下一步建议

下一步适合做两件事中的第一件：

1. 缩 `TStub` ABI，先移除已确认死字段 `so_path`
2. 再补最小校验，确保每次重生 `generated_stub.h` 后 marker / payload 非空

推荐顺序：

- 先做 `so_path` 移除
- 再加 `generated_stub.h` 的轻量校验脚本

原因：

- `so_path` 仍然占据 stub ABI，继续保留只会增加后续维护成本
- 先缩 ABI，再补校验，避免刚加的校验马上又要跟着改一次
