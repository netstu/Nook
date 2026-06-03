# Nook Framework Design

**Date:** 2026-03-19

**Status:** Approved

## Goal

将 `E:\Learn\my_program\all_my_hook\inject_and_hook\javahook_from_codex` 中现有的 Java Hook 项目迁移到当前目录，并重组为名为 `Nook` 的统一 Hook 框架。`Nook` 的长期目标是同时承载 Java Hook 与 Native Hook，两者共享统一的框架层、构建入口和对外命名体系。

## Architecture

`Nook` 采用框架层与能力层分离的结构：

- 框架层负责统一初始化、错误码、日志、运行时探测和未来能力扩展入口。
- `java_hook` 子模块承载当前已有的 Java Hook 能力。
- `native_hook` 子模块在本次仅预留目录和对外接口，不实现具体 Hook 逻辑。

首版产物统一为单个动态库 `libnook.so`，以降低构建和集成复杂度。对外 API 统一使用 `Nook` 命名，内部允许暂时保留部分 `JavaHook` 实现名，以避免将迁移工作变成高风险的大规模机械重命名。

## Target Layout

```text
Nook/
  docs/
  include/
    nook/
      Nook.h
      NookJavaHook.h
      NookNativeHook.h
  src/
    framework/
    java_hook/
    native_hook/
    common/
  examples/
    java_hook/
  third_party/
    elfio/
    xdl/
    json/
  build/
    android/
      Android.mk
      Application.mk
      CMakeLists.txt
  tests/
```

## Migration Scope

### Included

- 源项目 `core/hook`、`core/jvm`、`core/store`、`core/utility` 中的核心实现。
- 源项目 `include/ELFIO`、`include/xdl` 和 `json.h`。
- 源项目 `example/hook_example.cpp`，迁移为 `Nook` 示例。
- 源项目现有的 Android NDK 构建逻辑，重组为 `build/android/` 下的新入口。

### Excluded

- `libs/` 与 `obj/` 构建产物。
- 原项目根目录中仅为旧结构服务的脚本布局。
- 将旧 `JavaHook` 公共头文件直接暴露为 `Nook` 正式接口。

## API Strategy

### Public API

- `include/nook/Nook.h`
  - 框架级初始化、版本、能力查询、统一错误码。
- `include/nook/NookJavaHook.h`
  - Java Hook 能力入口。
- `include/nook/NookNativeHook.h`
  - Native Hook 能力入口占位，当前返回未实现状态。

### Naming

- 对外头文件、导出函数、示例、日志 tag、文档、库名均使用 `Nook` 命名。
- 内部 `.cpp/.h` 实现文件允许暂时保留部分 `JavaHook` 命名。
- 旧 `JavaHook` 风格 API 不作为新框架对外契约的一部分。

## Build Strategy

- 构建入口统一迁移到 `build/android/`。
- 首版只要求 Android NDK 能产出 `libnook.so`。
- 第三方依赖放在 `third_party/`，通过新的 `Android.mk`/`CMakeLists.txt` 引入。
- 示例代码以新公开头文件验证包含链与符号导出。

## Error Handling

- 提供统一的 `NookStatus` 或等价错误码体系。
- `NookNativeHook` 在未实现阶段返回明确的未实现错误。
- Java Hook 相关错误在框架层对外做统一映射，内部保留细分日志。

## Testing Strategy

- 迁移过程中优先建立最小的构建级验证与头文件包含验证。
- 以示例代码编译通过作为对外 API 的首轮集成验证。
- 通过单次完整 NDK 构建验证 `libnook.so` 产物。
- 本次不承诺 Native Hook 运行时能力，仅验证占位接口存在且行为明确。

## Constraints

- 当前目录不是 git 仓库，因此无法按流程提交设计文档 commit。
- 迁移以保守重构为主，避免对已工作的 Java Hook 核心做不必要重写。
- 本次为后续 Native Hook 预留边界，但不提前引入未验证的复杂抽象。
