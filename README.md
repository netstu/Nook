# Nook

Nook 是一个面向 Android 的 Hook 框架，在同一套代码中同时承载：

- Java Hook
- PLT Hook
- Inline Hook

详细介绍可参考看雪主页文章：

- https://bbs.kanxue.com/user-home-985628.htm

当前仓库：

- 对外头文件位于 `include/nook/`
- 框架实现位于 `src/`
- Android 构建入口位于 `build/android/`
- `examples/` 仅保留参考源码，默认**不参与构建**

当前主要支持目标为 `arm64-v8a`。

这个仓库当前的定位是：

- 提供可直接集成的 Hook 框架源码
- 提供可复用的 Android NDK 构建入口
- 提供参考示例源码，帮助你在自己的目标工程里落地

## 当前能力状态

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| Java Hook | 已支持 | 导出自 `libnook.so` |
| PLT Hook | 已支持 | ELFIO 主路径 + fallback 解析路径 |
| Inline Hook（arm64） | 已支持 | 支持地址 Hook、符号 Hook、延迟符号 Hook |
| Inline Hook（arm32） | 未支持 | 后续再补 |

## 仓库结构

```text
include/nook/                 对外公开头文件
src/framework/                对外 API 入口层
src/java_hook/                Java Hook 实现
src/native_hook/core/         Native Hook 公共支撑层
src/native_hook/plt_hook/     PLT Hook 实现
src/native_hook/inline_hook/  arm64 Inline Hook 实现
src/common/                   公共工具与日志
examples/                     参考源码，不默认构建
build/android/                Android.mk / Application.mk / CMakeLists.txt
third_party/                  内置第三方依赖
```

## 公开头文件

- [`include/nook/Nook.h`](./include/nook/Nook.h)
- [`include/nook/NookJavaHook.h`](./include/nook/NookJavaHook.h)
- [`include/nook/NookJavaHookMacros.h`](./include/nook/NookJavaHookMacros.h)
- [`include/nook/NookPltHook.h`](./include/nook/NookPltHook.h)
- [`include/nook/NookInlineHook.h`](./include/nook/NookInlineHook.h)

## API 概览

### 基础接口

- `NookGetVersion`

### Java Hook

- `NookJavaHookInitialize`
- `NookJavaHookIsAvailable`
- `NookJavaHookHook`
- `NookJavaHookUnhook`
- `NookJavaHookUnhookAll`

如果你是写 payload，推荐直接使用 `NookJavaHookMacros.h` 中的宏封装。

### PLT Hook

- `NookPltHookInitialize`
- `NookPltHookIsAvailable`
- `NookPltHookSymbol`

适用于目标函数通过 PLT/GOT 导入链路被调用的场景。

### Inline Hook

- `NookInlineHookInitialize`
- `NookInlineHookIsAvailable`
- `NookInlineHookAddress`
- `NookInlineHookSymbol`
- `NookInlineHookSymbolDeferred`
- `NookInlineUnhook`

如果目标模块可能尚未加载，使用 `NookInlineHookSymbolDeferred`。

## 构建

### 依赖

- Android NDK
- 可调用 `ndk-build` 的终端环境

当前 Android 构建配置：

- ABI: `arm64-v8a`
- Platform: `android-30`
- STL: `c++_shared`

详见 [`build/android/Application.mk`](./build/android/Application.mk)。

### 默认构建命令

在仓库根目录执行：

```powershell
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

如果 `ndk-build` 不在 `PATH` 中，可以显式指定 NDK 路径：

```powershell
& "$env:ANDROID_NDK_HOME\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

### 构建产物

默认会在 `libs/arm64-v8a/` 下生成：

- `libnook.so`
- `libnook_inline_observer_probe.so`

其中：

- `libnook.so` 是主框架库
- `libnook_inline_observer_probe.so` 仅在 **deferred inline hook** 场景下使用

Java Hook、PLT Hook、直接地址 Inline Hook 主要围绕 `libnook.so` 使用。

## 推荐使用方式

这个仓库本身不提供注入器，也不再默认产出测试 payload。

更推荐的使用方式是：

1. clone 当前仓库
2. 编译出 `libnook.so` 与所需配套库
3. 在你自己的 payload / App 工程中引入 `include/` 与 `src/`，或者直接链接生成的框架库
4. 使用你自己的注入器、加载方式或 App 集成方式验证 Hook 效果

## 如何使用

当前仓库已经不再默认构建示例 payload 的 `.so`。如果你需要参考接入方式，可以直接查看 `examples/` 下的源码，并在你自己的目标工程里编译。

### Java Hook 最小示例

```cpp
#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("HOOK_EXAMPLE", 5, 200);

NOOK_JAVA_BLOCK(
    "com/demo/target/AdWallFragment",
    "loadAd",
    "(Ljava/lang/String;Ljava/lang/String;)V",
    0
);
```

### PLT Hook 最小示例

```cpp
#include "nook/NookPltHook.h"

void* original = nullptr;
NookPltHookInitialize();
NookPltHookSymbol("libtarget.so", "strcmp", reinterpret_cast<void*>(replacement), &original);
```

### Inline Hook 最小示例

```cpp
#include "nook/NookInlineHook.h"

void* original = nullptr;
void* hook_handle = nullptr;
NookInlineHookInitialize();
NookInlineHookAddress(target_address,
                      reinterpret_cast<void*>(replacement),
                      &original,
                      &hook_handle);
```

## 示例源码

`examples/` 目录保留为参考代码，不属于稳定 SDK 接口的一部分。

当前保留的 native 示例包括：

- `examples/native_hook/nook_native_strcmp_test/`
- `examples/native_hook/nook_native_inline_test/`
- `examples/native_hook/nook_native_verify_password_inline_test/`

公共示例运行时胶水代码见：

- [`examples/native_hook/common/nook_runtime_loader.h`](./examples/native_hook/common/nook_runtime_loader.h)

需要注意：这里的 runtime loader 只是开发过程中的示例适配代码，不应视为正式框架 API。

## 当前边界

- arm32 inline hook 尚未实现
- x86 / x86_64 inline hook 尚未实现
- deferred inline hook 目前仍依赖 `libnook_inline_observer_probe.so`
- 当前仓库更偏向研究/集成型框架源码，而不是已经完全 SDK 化的成品

## 参考项目

https://github.com/bytedance/android-inline-hook

https://github.com/Lynnette177/GirlHook

https://github.com/canyie/pine
