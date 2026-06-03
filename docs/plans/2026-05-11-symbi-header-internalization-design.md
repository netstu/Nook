# Symbi Header Internalization Design

## 目标

把 `symbi` 路径对外部 `Ninjector/jni` 的头文件级依赖先收回到 Nook 仓库内部，不改变当前 `spawn` 运行逻辑。

## 范围

本次只处理：

- `symbi_injector.h`
- `symbi_stub.h`
- `stub_src/*`
- `common/log.h` 的替代
- `build/android/Android.mk` 的 include 收口

本次不处理：

- `symbi` 算法逻辑
- `zygote-control` / `legacy ncore` 行为
- `spawn` 后端选择策略
- `symbi` 生成链重构

## 方案

采用最小风险方案：

1. 在 Nook 内新增本地 `server/symbi/` 头文件副本
2. 把 `stub_src` 所需头文件一并迁入 Nook
3. 提供 Nook 本地日志头，替代 `../../Ninjector/jni/common/log.h`
4. 修改 `server/symbi_injector_local.cpp` 只引用 Nook 本地头
5. 修改 `build/android/Android.mk`，去掉外部 `Ninjector/jni` include
6. 编译验证 `nook_server`

## 原因

当前 `symbi` 实现主体已经在 Nook 仓库里，真正阻碍“去 Ninjector 化”的是构建和头文件边界，而不是运行时代码本身。

先做这一层有三个好处：

1. 风险低，不直接碰脆弱的 zygote/symbi 行为
2. 能快速证明 Nook 主构建是否仍暗含外部 include 依赖
3. 为后续彻底迁移 `symbi` 实现和删除历史兼容路径打基础

## 验证标准

完成后应满足：

1. `server/symbi_injector_local.cpp` 不再 include 外部 `Ninjector` 头
2. `build/android/Android.mk` 不再 include `../Ninjector/jni`
3. `nook_server` 能在当前环境正常编译通过
4. 不宣称运行时行为变更；这一步只验证构建和依赖边界
