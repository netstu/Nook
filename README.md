# Nook

Nook 是一个面向 rooted Android 设备的动态插桩工具。

当前公开版本聚焦这些能力：

- Android
- root 环境
- arm64-v8a
- `spawn` / `attach`
- Java / Native / Memory Hook
- 单文件部署的 `nook-server`
- Python CLI：`nook-cli`

## 快速开始

直接在release中下载server，和Frida一样push到/data/local/tmp后运行，

然后宿主侧执行

```
python -m pip install --upgrade nook-cli
```

或者自己跟着下面步骤编译。

### 1. 编译 `nook-server`

在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1 -ForceRebuild
```

产物位于：

```text
build/single-server-package/arm64-v8a/nook-server
```

如果 `ndk-build` 不在 `PATH`，先设置：

```powershell
$env:NOOK_NDK_BUILD="E:\SDK\ndk\25.2.9519653\ndk-build.cmd"
```

### 2. 安装 `nook-cli`

进入 `host/nook-py` 后执行：

```powershell
python -m pip install .
```

安装完成后可直接使用：

```powershell
nook-cli --help
```

### 3. 推送并启动服务端

```powershell
adb shell "su -c 'mkdir -p /data/local/tmp/nook'"
adb push .\build\single-server-package\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server'"
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

部分设备如果不能直接执行，可改用：

```powershell
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook:$LD_LIBRARY_PATH /system/bin/linker64 /data/local/tmp/nook/nook-server'"
```

## 基础用法

### Attach

```powershell
nook-cli -U com.demo.target -l .\hook.js
```

### Spawn

```powershell
nook-cli -U -f com.demo.target -l .\hook.js
```

### 查看应用 / 进程

```powershell
nook-cli apps
nook-cli ps
```

## 仓库结构

```text
build/android/      Android NDK 构建文件
docs/               核心设计与架构文档
examples/           基础示例
host/nook-py/       Python SDK 与 CLI
include/            对外头文件
server/             nook-server 源码
src/                核心框架与运行时
tests/              单元测试与回归测试
third_party/        第三方依赖
tools/              构建与辅助脚本
```

## 说明

- 当前仓库是源码发布仓库，适合 clone 后自行构建和开发。
- `nook-cli` 不内置 Android 端二进制，使用时需要自行下载或编译 `nook-server`。
- 当前公开支持范围以 rooted Android arm64 为主。
