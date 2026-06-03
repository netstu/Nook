# Payload Hook Example Design

**Date:** 2026-03-19

**Status:** Approved

## Goal

在 `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\` 下新增一个新的 payload 示例，命名为 `payload_hook_example`。该示例不是发明新的 Hook 行为，而是使用现有 payload 封装层复现 `payload_adblock` 的效果，用作 Java Hook 封装层的回归样例。

## Context

`TargetAppDemo/payloads/payload_adblock/payload.cpp` 已经展示了一种完整 payload 形态：

- 使用 `hookkit/hookkit.h`
- 通过 `HK_PAYLOAD_CONFIG` 声明 payload
- 使用 `HK_JAVA_BLOCK` 阻断指定 Java 方法
- 使用 `HK_JAVA_HOOK` 注入自定义 JNI 回调
- 直接通过 JNI 操作对象与字段以实现 UI 行为修改

本次新增的 `payload_hook_example` 需要保持相同的 Hook 点与行为，以验证 payload 封装层本身工作正常，而不是验证新的业务逻辑。

## Scope

### Included

- 在 `TargetAppDemo/payloads/` 下新增 `payload_hook_example/`
- 在目录中新增 `payload.cpp`
- payload 配置名称改为 `HOOK_EXAMPLE`
- 复用与 `payload_adblock` 相同的两个 Hook 点
- 行为与 `payload_adblock` 保持一致

### Excluded

- 不重构 `payload_adblock`
- 不抽取公共 JNI helper 到共享库
- 不新增新的 Hook 点
- 不修改 `hookkit` 框架设计

## Hook Targets

### Hook 1

阻断广告加载：

- Class: `com/demo/target/AdWallFragment`
- Method: `loadAd`
- Signature: `(Ljava/lang/String;Ljava/lang/String;)V`
- Macro: `HK_JAVA_BLOCK`

### Hook 2

在列表绑定时隐藏广告项：

- Class: `com/demo/target/AdWallFragment$ContentAdapter`
- Method: `onBindViewHolder`
- Signature: `(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V`
- Macro: `HK_JAVA_HOOK`

## File Structure

```text
TargetAppDemo/payloads/
  payload_hook_example/
    payload.cpp
```

## Implementation Strategy

- 保持和 `payload_adblock` 一样的单文件 payload 结构
- 保留两个局部 helper：
  - `ClearJniException`
  - `HideViewNow`
- 复制相同的 JNI 逻辑路径，确保效果一致
- 只修改 payload 标识和必要注释，不引入额外抽象

## Validation Strategy

- 确认新目录和 `payload.cpp` 已创建
- 确认源码中包含：
  - `HK_PAYLOAD_CONFIG(..., ..., "HOOK_EXAMPLE")`
  - `HK_JAVA_BLOCK` 针对 `AdWallFragment.loadAd`
  - `HK_JAVA_HOOK` 针对 `ContentAdapter.onBindViewHolder`
- 使用 `TargetAppDemo/payloads` 现有构建方式完成编译
- 行为目标与 `payload_adblock` 一致：阻断广告加载并隐藏广告项

## Constraints

- 当前工作目录不是 `TargetAppDemo/payloads`，新增文件需要直接写入外部路径
- 这次工作的目标是“复现同效果”，不是“代码去重”
- 若构建系统对 payload 目录做显式枚举，需要同步检查 `TargetAppDemo/payloads/Android.mk`
