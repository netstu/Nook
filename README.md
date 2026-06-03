# Nook

Nook 是一个围绕同一套 Hook 核心构建的 Android 动态插桩工具集。当前仓库主要提供三种可直接使用的形态：

- `libnook.so`：用于应用侧或注入侧负载的嵌入式框架
- `nook-server`：面向 Root 设备的 Frida 风格 `spawn` / `attach` 运行时
- `nook-gadget`：面向非 Root 重打包流程的 APK 内嵌运行时

当前仓库已经支持 `nook-gadget` 的 APK patch / listen / startup-script 工作流。  
Host CLI 也已经支持 `dexdump`，可以直接从运行中的目标进程导出内存中的 Dex。

当前仓库按可复用框架、运行时、工具链和示例负载组织，主要支持的开发目标为 **arm64-v8a**。

## 框架示例工作流

### 模式 1：Root 设备上的 `nook-server`

在仓库根目录构建 server 包：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1 -ForceRebuild
```

预期产物：

```text
build/single-server-package/arm64-v8a/nook-server
```

推送到设备并启动：

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

然后在宿主侧使用 CLI：

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
nook-cli -U com.demo.target -l .\hook.js
nook-cli sodump com.demo.target -U --module libfoo.so
nook-cli dexdump com.demo.target -U
```

`nook-cli sodump` 会输出原始内存镜像以及修复后的 ELF64 产物，便于后续分析。

不带 `--gadget` 时，`nook-cli -U ...` 默认连接正在运行的 Root `nook-server`。

### 模式 2：重打包进 APK 的 `nook-gadget`

构建 gadget 动态库：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_nook_gadget.ps1 -ForceRebuild
```

重打包 APK 并内置启动脚本：

```powershell
nook-cli patchapk .\target.apk -s .\startup.js
```

或者使用更短的辅助命令：

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\startup.js
```

以 `listen` 模式重打包 APK，并在宿主连接前暂停应用启动：

```powershell
nook-cli patchapk .\target.apk --on-load wait
```

从宿主侧连接 gadget 化目标：

```powershell
nook-cli -U --gadget com.demo.target -l .\hook.js
nook-cli -U --gadget com.demo.target
nook-cli sodump -U --gadget com.demo.target --module libfoo.so
nook-cli dexdump -U --gadget com.demo.target
```

gadget 语义说明：

- `--gadget` 会显式切换到 gadget `listen` socket 路径
- `--on-load resume` 是 gadget 默认行为
- `--on-load wait` 会暂停应用，直到宿主加载脚本并恢复进程
- 内置 startup-script 模式和宿主附加 `listen` 模式都已支持

## 当前支持矩阵

| Feature | Status | Notes |
| --- | --- | --- |
| Java Hook | Supported | Built into `libnook.so` |
| PLT Hook | Supported | ELFIO-first implementation with fallback parser |
| Inline Hook (arm64) | Supported for current workflow | Direct address hook and deferred symbol hook are available |
| Inline Hook (arm32) | Not supported yet | Planned later |
| App-side `System.loadLibrary(...)` workflow | Supported | Best for app-integrated testing |
| Injector / remote `dlopen(...)` workflow | Supported | Current examples assume `Ninjector`-style runtime placement |

## 仓库结构

```text
include/nook/                 public headers
src/framework/                public API entrypoints
src/java_hook/                Java Hook implementation
src/native_hook/core/         shared native-hook helpers
src/native_hook/plt_hook/     PLT hook implementation
src/native_hook/inline_hook/  arm64 inline hook implementation
src/common/                   shared utilities
examples/java_hook/           Java Hook payload examples
examples/native_hook/         native payload examples
build/android/                Android.mk, Application.mk, CMakeLists.txt
tests/headers/                host-side verification tests
third_party/                  embedded third-party code
```

补充的运行时和工具路径：

```text
host/nook-py/                 Python CLI and gadget helper
server/                       nook-server sources
src/gadget/                   nook-gadget runtime
tools/                        build, patch, and validation scripts
```

## 对外头文件

- [`include/nook/Nook.h`](./include/nook/Nook.h)
- [`include/nook/NookJavaHook.h`](./include/nook/NookJavaHook.h)
- [`include/nook/NookJavaHookMacros.h`](./include/nook/NookJavaHookMacros.h)
- [`include/nook/NookPltHook.h`](./include/nook/NookPltHook.h)
- [`include/nook/NookInlineHook.h`](./include/nook/NookInlineHook.h)
- [`include/nook/NookNativeHook.h`](./include/nook/NookNativeHook.h)

## API 概览

### Java Hook

- `NookJavaHookInitialize`
- `NookJavaHookHook`
- `NookJavaHookUnhook`
- `NookJavaHookUnhookAll`

推荐的负载写法：优先使用 `NookJavaHookMacros.h` 中的宏。

### PLT Hook

- `NookPltHookInitialize`
- `NookPltHookSymbol`

适用于通过 PLT/GOT 重定位导入的目标函数。

### Inline Hook

- `NookInlineHookInitialize`
- `NookInlineHookAddress`
- `NookInlineHookSymbol`
- `NookInlineHookSymbolDeferred`
- `NookInlineUnhook`

当目标模块可能尚未加载时，使用 `NookInlineHookSymbolDeferred`。

### Native Hook Facade

- `NookNativeHookInitialize`
- `NookNativeHookHookSymbol`

注意：**当前 `NookNativeHook*` 只走 PLT Hook 路径。**  
如果你需要 Inline 行为，请直接调用 `NookInlineHook*`。

## 构建

### 依赖

- 带 `ndk-build` 的 Android NDK
- Windows PowerShell，或任何可调用 `ndk-build` 的 shell

当前 Android 构建配置：

- ABI: `arm64-v8a`
- Platform: `android-30`
- STL: `c++_shared`

见 [`build/android/Application.mk`](./build/android/Application.mk)。

### 构建命令

在仓库根目录执行：

```powershell
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

如果 `ndk-build` 不在 `PATH` 中，可显式指定本地 NDK 路径，例如：

```powershell
& "$env:ANDROID_NDK_HOME\ndk-build.cmd" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./build/android/Android.mk NDK_APPLICATION_MK=./build/android/Application.mk -j4
```

### 主要输出产物

生成在 `libs/arm64-v8a/` 下：

- `libnook.so`
- `libnook_inline_observer_probe.so`
- `libnook_java_hook_example.so`
- `libnook_native_strcmp_test.so`
- `libnook_native_inline_test.so`
- `libnook_native_verify_password_inline_test.so`

## 快速使用

当前仓库主要有两种实际使用方式：

1. 通过 `System.loadLibrary(...)` 在应用侧加载
2. 通过 `Ninjector` 在注入侧加载

`examples/native_hook/` 中的示例负载是样例，不是框架本体。

## 工作流 1：应用侧 `System.loadLibrary(...)`

这是在你可控 APK 内部测试负载最简单的方式。

### Java 侧加载顺序

以 inline verify-password 示例为例，加载顺序为：

```java
System.loadLibrary("c++_shared");
System.loadLibrary("nook");
System.loadLibrary("nook_native_verify_password_inline_test");
```

不要手动加载：

- `libnook_inline_observer_probe.so`

probe 库会在 deferred inline observer 需要时由内部自动加载。

### 说明

- 如果目标模块稍后才会加载，请使用 `NookInlineHookSymbolDeferred`。
- 在示例项目中，`libnative-lib.so` 是由目标 Fragment 自己加载的，因此 deferred 安装是正确路径。

## 工作流 2：通过 `Ninjector` 注入加载

这种模式下你只注入 payload `.so`，但 payload 的运行时依赖仍需提前放在设备上。

### 被注入的 payload

以 verify-password inline 示例为例：

- 注入：`libnook_native_verify_password_inline_test.so`

### 设备上仍需存在的依赖

- `libc++_shared.so`
- `libnook.so`
- `libnook_inline_observer_probe.so`

这些依赖的放置方式取决于你自己的注入器和设备环境。

## 现状说明

- 当前主线目标是 Android `arm64-v8a`
- 当前公开仓库默认以代码为主，不包含本地验证文档和发布过程产物
- `nook-server`、`nook-gadget`、`dexdump`、`sodump` 都由同一套核心代码演进而来
