# Nook Java Hook Macros Design

**Date:** 2026-03-19

**Status:** Approved

## Goal

为 `Nook` 增加一层简单的宏封装，使 Java Hook payload 在使用时更接近 Frida 的“几句声明式代码”，同时保留 `Nook` 当前的 C++/JNI/ART 实现。第一版聚焦于自动生成 payload 生命周期模板和 Java Hook 声明注册，不引入脚本运行时。

## Context

当前 `Nook` 已经可以完成 Java Hook，但 payload 代码需要手写大量样板：

- constructor / `JNI_OnLoad`
- 初始化线程
- `NookJavaHookInitialize()`
- Hook 安装重试
- 卸载清理

这些样板让一个简单 Hook 看起来很长。用户明确希望先做“简单的宏封装”，优先隐藏这部分样板，而不是马上做 Frida 风格脚本层。

## Scope

### Included

- 新增 `Nook` Java Hook 宏头文件
- 新增轻量运行时实现用于保存 Hook 声明和自动安装
- 提供以下宏：
  - `NOOK_PAYLOAD_CONFIG(tag, retry_count, retry_interval_ms)`
  - `NOOK_JAVA_HOOK(name, class, method, sig, is_static)`
  - `NOOK_JAVA_BLOCK(class, method, sig, is_static)`
- 将 `payload_hook_example` 改写为使用新宏

### Excluded

- 不做脚本引擎
- 不做 Frida 式 `Java.use` / `Java.perform`
- 不做复杂 DSL
- 不做 Native Hook 宏封装

## Target API

```cpp
NOOK_PAYLOAD_CONFIG("HOOK_EXAMPLE", 5, 200);

NOOK_JAVA_BLOCK(
    "com/demo/target/AdWallFragment",
    "loadAd",
    "(Ljava/lang/String;Ljava/lang/String;)V",
    0
);

NOOK_JAVA_HOOK(HideAdItemOnBind,
    "com/demo/target/AdWallFragment$ContentAdapter",
    "onBindViewHolder",
    "(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V",
    0) {
    // custom logic
}
```

## Architecture

### Public Header

新增一个头文件，例如：

- `include/nook/NookJavaHookMacros.h`

其中包含：

- 宏定义
- 内部声明结构
- 用于运行时注册的最小辅助声明

### Runtime

新增一个轻量实现文件，例如：

- `src/framework/NookJavaHookPayload.cpp`

负责：

- 保存 Java Hook 声明表
- 自动启动初始化线程
- 调用 `NookJavaHookInitialize()`
- 按重试策略遍历安装 Hook
- 调用 `NookJavaHookUnhookAll()`

### Ownership Split

- 宏负责“声明 + 注册 + 生成入口”
- runtime 负责“状态机 + 线程 + 安装”

这样可以避免把所有逻辑硬塞进宏展开。

## Validation Strategy

- 头文件编译通过
- `payload_hook_example` 使用新宏后仍能构建
- `TargetAppDemo/payloads` 的 NDK 构建通过
- 注入后日志仍能看到：
  - `NookJavaHookInitialize -> 0`
  - 两个 Hook 安装成功
  - Hook 命中成功

## Constraints

- 要保持当前 `Ninjector` 注入路径可用，因此 constructor + `JNI_OnLoad` 双入口必须保留
- 第一版要严格 YAGNI，不把常见 JNI helper 也抽成庞大框架
- payloads 目录在工作区之外，改造示例时需要外部路径写权限
