# Payload Hook Example With Nook Design

**Date:** 2026-03-19

**Status:** Approved

## Goal

在 `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\` 下新增一个独立的 `payload_hook_example`，直接使用 `Nook` 的公开 Java Hook API 实现与 `payload_adblock` 相同的 Hook 效果，以验证 `Nook` 框架本身是否有效。

## Context

现有 `payload_adblock` 通过 `hookkit` 宏封装来安装 Hook，但 `hookkit` 底层仍依赖旧 `JavaHook` 框架。用户本次要求不是复用旧封装，而是保留相同的 Hook 点与行为，改为直接调用 `Nook`。

因此，新示例应满足：

- Hook 点参考 `payload_adblock`
- 生命周期和安装流程由 payload 自己负责
- 不依赖 `hookkit`
- 目标是验证 `Nook` 是否能成功完成同样的 Java Hook

## Scope

### Included

- 新增 `TargetAppDemo/payloads/payload_hook_example/payload.cpp`
- 直接调用 `NookJavaHook` API
- 在 `TargetAppDemo/payloads/Android.mk` 中加入新的模块
- 将 `Nook` 源码编入该 payload 模块
- 行为对齐 `payload_adblock`

### Excluded

- 不修改 `hookkit` 框架
- 不让新 payload 依赖 `hookkit`
- 不修改 `payload_adblock`
- 不做 Native Hook

## Hook Targets

### Hook 1

阻断广告加载：

- Class: `com/demo/target/AdWallFragment`
- Method: `loadAd`
- Signature: `(Ljava/lang/String;Ljava/lang/String;)V`

### Hook 2

在列表绑定时隐藏广告项：

- Class: `com/demo/target/AdWallFragment$ContentAdapter`
- Method: `onBindViewHolder`
- Signature: `(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V`

## File Structure

```text
TargetAppDemo/payloads/
  payload_hook_example/
    payload.cpp
```

## Implementation Strategy

### Payload Structure

`payload.cpp` 自己负责：

- `JNI_OnLoad`
- `JavaVM` 传入与保存
- 后台线程初始化
- 重试安装 Hook
- JNI helper

### Nook Integration

payload 直接调用：

- `NookJavaHookInitialize()`
- `NookJavaHookHook(...)`
- `NookJavaHookUnhookAll()`

同时在 `JNI_OnLoad` 中通过 `JavaEnv::SetJavaVM(vm)` 配合 `Nook` 内部 Java 运行时逻辑。

### Local Helpers

保留两个局部 helper：

- `ClearJniException(JNIEnv*)`
- `HideViewNow(JNIEnv*, jobject)`

这些 helper 仅服务于示例逻辑，不对外抽象。

## Build Strategy

在 `TargetAppDemo/payloads/Android.mk` 中新增一个独立 payload 模块，直接编译：

- `payload_hook_example/payload.cpp`
- `Nook/src/framework/*.cpp`
- `Nook/src/java_hook/*.cpp`
- `Nook/src/common/*.cpp`
- `Nook/third_party/xdl/*.c`

并引入：

- `Nook/include`
- `Nook/src/java_hook`
- `Nook/src/common`
- `Nook/src/framework`
- `Nook/third_party/*`

不依赖预编译 `libnook.so`，避免额外的运行时分发问题。

## Validation Strategy

- 新增的 `payload.cpp` 文件存在
- 源码中包含两个目标 Hook 点
- `Android.mk` 中包含 `payload_hook_example` 模块定义
- `TargetAppDemo/payloads` 下的 `ndk-build` 能成功构建出 `payload_hook_example` 对应产物

## Constraints

- `TargetAppDemo/payloads` 不在当前工作区可写根内，写入时可能需要提权
- `Android.mk` 当前采用显式枚举 payload 模块，因此新 payload 必须显式接入
- 这次只验证 Java Hook，不引入任何 Native Hook 代码路径
