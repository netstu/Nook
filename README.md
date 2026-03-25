# Nook

Nook 是一个面向 Android Java Hook 的最小化仓库版本，整理后用于发布到 GitHub。

详细介绍：https://bbs.kanxue.com/homepage-985628.htm

这个裁剪后的仓库只保留了完成以下目标所需的内容：

- 编译核心库 `libnook.so`
- 编译一个测试 payload：`libnook_java_test_replace_num_macro.so`
- 配合你自己的注入器验证 Java Hook 流程

## 仓库结构

```text
include/nook/        对外头文件
src/framework/       框架入口与 payload 启动逻辑
src/java_hook/       ART / Java Hook 核心实现
src/native_hook/     Native Hook 占位实现
src/common/          通用组件
third_party/         当前源码依赖的第三方库
examples/java_hook/  保留的 hook num 示例
build/android/       最小 ndk-build 构建脚本
```

## 保留的示例

当前仓库只保留了一个 Java Hook 示例：

- `examples/java_hook/nook_java_test_replace_num_macro.cpp`

这个示例 Hook 的目标是：

- 类名：`cn/n1ng/javatest/JavaHookTest`
- 方法名：`get_num_from_java_method`
- 方法签名：`()I`

它的行为是：

- 打印 Hook 生效日志
- 将返回值替换为 `999`

## 构建前提

- 已安装 Android NDK
- 环境中可以直接使用 `ndk-build`，或者你知道 NDK 的完整路径
- 当前 ABI 固定为 `arm64-v8a`
- 当前 Android 平台版本设置为 `android-30`

## 编译方法

在仓库根目录下执行：

```bash
cd build/android
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

如果 `ndk-build` 不在 `PATH` 中，也可以直接使用完整路径，例如：

```bash
cd build/android
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

编译成功后，预期产物为：

- `build/android/libs/arm64-v8a/libnook.so`
- `build/android/libs/arm64-v8a/libnook_java_test_replace_num_macro.so`
- `build/android/libs/arm64-v8a/libc++_shared.so`

## 测试方式

1. 将 `libnook_java_test_replace_num_macro.so` 注入到目标 App 进程。
2. 确保 payload 已被目标进程成功加载。
3. 触发 `cn.n1ng.javatest.JavaHookTest.get_num_from_java_method()`。
4. 确认该方法返回值由原始值变为 `999`。

这个仓库本身不提供注入器，推荐的使用方式是：

- 在本仓库中编译出 payload `.so`
- 使用你自己的注入器完成注入
- 通过日志和目标行为确认 Hook 是否生效

## 说明

- 这个仓库有意只保留一个 Java Hook 示例，方便别人快速理解最小可用流程。
- 当前 Hook 实现中包含 ARM64 专用内联汇编，因此这个最小仓库只面向 `arm64-v8a`。
- `build/android/Android.mk` 已被收敛为最小模块集合，只保留构建和测试所需内容。
- 当前 `native_hook` 仍然只是占位实现，不是本仓库的重点。

参考项目：https://github.com/Lynnette177/GirlHook

https://github.com/canyie/pine
