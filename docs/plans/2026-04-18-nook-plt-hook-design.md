# Nook PLT Hook Design

**Date:** 2026-04-18

**Status:** Approved

## Goal

在当前 `Nook` 项目中补齐 Native Hook 的 PLT Hook 能力，复用备份目录中的 `elfhooker` 逻辑，但按照现有 Java Hook 的目录风格组织到 `src/native_hook/` 下，并通过 `NookNativeHook` 对外暴露稳定 API。

## Architecture

本次实现沿用当前框架层和能力层分离的结构：
- `include/nook/NookNativeHook.h` 只暴露 `Nook` 风格的公开 C 接口。
- `src/native_hook/NookNativeHook.cpp` 负责参数校验、初始化状态和对内部 ELF hook 实现的封装。
- `src/native_hook/elfhooker/` 存放从备份迁移来的 ELF 解析和 GOT/PLT 重写实现，作为 `native_hook` 的内部私有实现。

这样可以保持与 `src/java_hook/` 相同的分层方式，避免把旧工程命名和实现细节直接暴露到对外 API。

## Target Layout

```text
include/
  nook/
    NookNativeHook.h
src/
  native_hook/
    NookNativeHook.cpp
    elfhooker/
      def.h
      elf_arm.h
      elf_reader.h
      elf_reader.cpp
      logger.h
      tools.h
      tools.cpp
tests/
  structure/
    test_native_hook_manifest.txt
  headers/
    test_native_hook_stub.cpp
build/
  android/
    Android.mk
    CMakeLists.txt
```

## API Strategy

### Public API

保留现有接口：
- `NookNativeHookInitialize(void)`
- `NookNativeHookIsAvailable(int* available)`

新增 PLT Hook 接口：
- `NookNativeHookHookSymbol(const char* module_name, const char* symbol_name, void* replacement, void** original)`

接口约束：
- `module_name` 表示 `/proc/self/maps` 中共享库路径的前缀匹配字符串。
- `symbol_name` 为需要替换的导入符号名。
- `replacement` 不能为空。
- `original` 不能为空，用于返回原始函数地址。

返回值统一使用 `NookStatus`：
- 成功返回 `NOOK_STATUS_OK`
- 参数错误返回 `NOOK_STATUS_INVALID_ARGUMENT`
- hook 失败或 ELF 解析失败返回 `NOOK_STATUS_INTERNAL_ERROR`

### Internal Implementation

- `NookNativeHookInitialize` 在当前版本仅用于标记 native hook 可用，避免保留未实现状态。
- `NookNativeHookIsAvailable` 在初始化后返回可用。
- `NookNativeHookHookSymbol` 内部通过 `ElfHooker::get_module_base` 获取模块加载基址，创建 `ElfReader`，执行 `parse()` 和 `hook()`。
- 迁移代码时只做必要适配：头文件路径、日志 tag、命名和 Android/Clang 兼容性修正，不重写核心 ELF 逻辑。

## Testing Strategy

按 TDD 执行最小可验证闭环：
- 先补 `tests/structure/test_native_hook_manifest.txt`，验证 native hook 相关文件都存在。
- 先修改 `tests/headers/test_native_hook_stub.cpp`，让它编译期依赖新的 PLT Hook API，并先观察失败。
- 在实现完成后重新运行结构校验和头文件编译校验。

当前仓库没有现成的 Android 运行时集成测试基建，因此本次只承诺：
- 文件结构正确
- 公开头可编译
- 构建脚本已纳入新源码

不在本次范围内：
- 真机/模拟器上的运行时 PLT Hook 行为验证
- 多架构完整回归

## Constraints

- 当前目录不是 Git 仓库，无法按技能要求提交 commit。
- 备份中的 `elfhooker` 带有旧项目命名和部分历史兼容代码，本次只做最小必要整理。
- 现有构建脚本同时支持 `Android.mk` 与 `CMakeLists.txt`，两者都需要同步更新，避免后续构建入口漂移。
