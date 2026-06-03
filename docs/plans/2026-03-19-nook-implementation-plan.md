# Nook Migration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将现有 Java Hook 项目迁移并重组为 `Nook` 统一框架，提供对外 `Nook` API、Android NDK 构建入口和 Java Hook 示例，同时为未来 Native Hook 预留模块边界。

**Architecture:** 采用框架层与能力层分离的单库方案。对外以 `Nook` 命名暴露 `framework`、`java_hook`、`native_hook` 三层接口，内部临时保留部分 `JavaHook` 实现命名，通过适配层将旧实现纳入新目录结构与新构建系统。

**Tech Stack:** C++, Android NDK, Android.mk, CMake, ELFIO, xdl

---

### Task 1: 建立 Nook 目录骨架

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\Nook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookNativeHook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\common\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\java_hook\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\json\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\elfio\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\xdl\.gitkeep`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Application.mk`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_structure_manifest.txt`

**Step 1: Write the failing test**

创建 `tests/structure/test_structure_manifest.txt`，列出必须存在的目录和文件路径，故意在仓库尚未创建这些路径前执行校验。

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_structure_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL，输出缺失的 `include\nook`、`src\framework`、`build\android` 等路径。

**Step 3: Write minimal implementation**

创建最小目录骨架、占位文件和 3 个公开头文件：

```cpp
// include/nook/Nook.h
#pragma once

typedef enum NookStatus {
    NOOK_STATUS_OK = 0,
    NOOK_STATUS_NOT_IMPLEMENTED = -1,
    NOOK_STATUS_INVALID_ARGUMENT = -2,
    NOOK_STATUS_INTERNAL_ERROR = -3
} NookStatus;
```

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_structure_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS，无错误输出。

**Step 5: Commit**

```bash
git add include src examples third_party build tests
git commit -m "chore: scaffold nook project layout"
```

### Task 2: 迁移第三方依赖

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\json\json.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\elfio\...`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\third_party\xdl\...`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_third_party_manifest.txt`

**Step 1: Write the failing test**

在 `tests/structure/test_third_party_manifest.txt` 中列出：
- `third_party\json\json.h`
- `third_party\elfio\elfio`
- `third_party\xdl\xdl.h`

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_third_party_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL，显示第三方文件缺失。

**Step 3: Write minimal implementation**

从源项目复制：
- `include\json.h` 到 `third_party\json\json.h`
- `include\ELFIO\*` 到 `third_party\elfio\`
- `include\xdl\*` 到 `third_party\xdl\`

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_third_party_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS。

**Step 5: Commit**

```bash
git add third_party tests/structure
git commit -m "chore: migrate nook third-party dependencies"
```

### Task 3: 迁移公共基础设施代码

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\common\JavaHookLog.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\common\JavaHookLog.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\common\ArtStructDetector.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\common\ArtStructDetector.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\HookStore.h`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_core_manifest.txt`

**Step 1: Write the failing test**

列出公共基础设施文件清单，先运行路径校验。

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_core_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL。

**Step 3: Write minimal implementation**

复制并重排以下源文件：
- `core\utility\JavaHookLog.*` -> `src\common\`
- `core\utility\ArtStructDetector.*` -> `src\common\`
- `core\store\HookStore.h` -> `src\framework\`

仅修正 include 路径，不做行为改写。

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_core_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS。

**Step 5: Commit**

```bash
git add src tests/structure
git commit -m "refactor: move shared hook infrastructure into nook layout"
```

### Task 4: 迁移 Java Hook 与 JVM 实现

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JavaHook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JavaHook.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JavaHookC.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JVM.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\java_hook\JVM.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_java_hook_manifest.txt`

**Step 1: Write the failing test**

编写 Java Hook 核心文件清单测试，覆盖 `JavaHook.*` 和 `JVM.*`。

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_java_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL。

**Step 3: Write minimal implementation**

从源项目复制并修正 include：
- `core\hook\JavaHook.h/.cpp/.c` 风格文件到 `src\java_hook\`
- `core\jvm\JVM.h/.cpp` 到 `src\java_hook\`

删除 `.bak` 和已废弃实验文件，不带入新结构。

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_java_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS。

**Step 5: Commit**

```bash
git add src tests/structure
git commit -m "refactor: migrate java hook runtime into nook module"
```

### Task 5: 建立 Nook 框架公开接口

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\Nook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookNativeHook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\Nook.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`

**Step 1: Write the failing test**

创建 `tests/headers/test_public_headers.cpp`：

```cpp
#include "nook/Nook.h"
#include "nook/NookJavaHook.h"
#include "nook/NookNativeHook.h"

int main() {
    return 0;
}
```

**Step 2: Run test to verify it fails**

Run: `clang++ -I .\include .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers.o`
Expected: FAIL，提示头文件缺少声明或依赖未满足。

**Step 3: Write minimal implementation**

补齐：
- `NookStatus`
- `NookGetVersion`
- `NookJavaHook` 对外函数声明
- `NookNativeHook` 占位函数声明
- `Nook.cpp` 中的最小实现

**Step 4: Run test to verify it passes**

Run: `clang++ -I .\include .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers.o`
Expected: PASS。

**Step 5: Commit**

```bash
git add include src/framework tests/headers
git commit -m "feat: add public nook framework headers"
```

### Task 6: 适配 Nook 对外 Java Hook 包装层

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookJavaHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHook.h`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_java_hook_symbols.cpp`

**Step 1: Write the failing test**

创建 `test_java_hook_symbols.cpp`，引用 `NookJavaHook.h` 并调用一个最小 API，如查询支持状态。

**Step 2: Run test to verify it fails**

Run: `clang++ -I .\include -I .\src .\tests\headers\test_java_hook_symbols.cpp -c -o .\tests\headers\test_java_hook_symbols.o`
Expected: FAIL，提示导出声明不完整。

**Step 3: Write minimal implementation**

新增 `NookJavaHook.cpp`，将新 API 转发到内部 `JavaHook` 实现，优先提供：
- 初始化
- Hook 安装入口
- 状态查询

**Step 4: Run test to verify it passes**

Run: `clang++ -I .\include -I .\src .\tests\headers\test_java_hook_symbols.cpp -c -o .\tests\headers\test_java_hook_symbols.o`
Expected: PASS。

**Step 5: Commit**

```bash
git add include src/framework tests/headers
git commit -m "feat: add nook java hook adapter layer"
```

### Task 7: 预留 Native Hook 占位实现

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\NookNativeHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookNativeHook.h`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_stub.cpp`

**Step 1: Write the failing test**

创建测试代码，调用 `NookNativeHook` 占位 API 并断言返回未实现状态。

```cpp
#include "nook/NookNativeHook.h"

int main() {
    return NookNativeHookIsAvailable() == 0 ? 0 : 1;
}
```

**Step 2: Run test to verify it fails**

Run: `clang++ -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -c -o .\tests\headers\test_native_hook_stub.o`
Expected: FAIL。

**Step 3: Write minimal implementation**

实现占位函数，统一返回 `NOOK_STATUS_NOT_IMPLEMENTED` 或等价结果。

**Step 4: Run test to verify it passes**

Run: `clang++ -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -c -o .\tests\headers\test_native_hook_stub.o`
Expected: PASS。

**Step 5: Commit**

```bash
git add include src/native_hook tests/headers
git commit -m "feat: add native hook placeholder api"
```

### Task 8: 重建 Android NDK 构建入口

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Application.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\build\test_ndk_build_manifest.txt`

**Step 1: Write the failing test**

定义构建验证目标：`libnook.so` 必须被构建系统声明，且源文件列表涵盖 `framework`、`java_hook`、`common`。

**Step 2: Run test to verify it fails**

Run: `Select-String -Path .\build\android\Android.mk -Pattern 'libnook|Nook.cpp|JavaHook.cpp'`
Expected: FAIL，匹配不到完整目标。

**Step 3: Write minimal implementation**

重写构建脚本：
- 模块名改为 `nook`
- 头文件搜索路径指向 `include`、`src`、`third_party`
- 添加 Java Hook 核心源文件和框架包装层

**Step 4: Run test to verify it passes**

Run: `Select-String -Path .\build\android\Android.mk -Pattern 'libnook|Nook.cpp|JavaHook.cpp'`
Expected: PASS，能看到核心模块与源文件配置。

**Step 5: Commit**

```bash
git add build/android
git commit -m "build: add android ndk entry for nook"
```

### Task 9: 迁移并改造示例

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\java_hook\nook_java_hook_example.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\examples\test_example_includes.cpp`

**Step 1: Write the failing test**

创建示例包含测试：

```cpp
#include "nook/Nook.h"
#include "nook/NookJavaHook.h"

int main() {
    return 0;
}
```

**Step 2: Run test to verify it fails**

Run: `clang++ -I .\include .\tests\examples\test_example_includes.cpp -c -o .\tests\examples\test_example_includes.o`
Expected: FAIL，直到对外头文件与示例依赖闭合。

**Step 3: Write minimal implementation**

将源项目 `example\hook_example.cpp` 迁移并改为：
- 包含 `nook/Nook*.h`
- 注释与日志统一为 `Nook`
- 调用新的框架包装接口

**Step 4: Run test to verify it passes**

Run: `clang++ -I .\include .\tests\examples\test_example_includes.cpp -c -o .\tests\examples\test_example_includes.o`
Expected: PASS。

**Step 5: Commit**

```bash
git add examples tests/examples
git commit -m "docs: migrate java hook example to nook api"
```

### Task 10: 执行完整构建验证

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Application.mk`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`

**Step 1: Write the failing test**

在执行修复前，先运行完整 NDK 构建，记录失败原因。

**Step 2: Run test to verify it fails**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk`
Expected: 初次 FAIL，暴露 include 路径、源码列表或导出声明问题。

**Step 3: Write minimal implementation**

针对构建错误做最小修复，仅修正：
- include 路径
- 缺失源文件
- 导出声明不匹配
- 第三方依赖引入问题

**Step 4: Run test to verify it passes**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk`
Expected: PASS，生成 `libs\arm64-v8a\libnook.so` 或等价输出。

**Step 5: Commit**

```bash
git add build src include examples
git commit -m "build: make nook android library compile"
```

### Task 11: 更新文档

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\README.md`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\docs\architecture.md`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\README.md`

**Step 1: Write the failing test**

定义文档最低要求：
- 项目定位为 Java Hook + Native Hook 框架
- 当前状态说明 Native Hook 未实现
- 构建命令存在

**Step 2: Run test to verify it fails**

Run: `Select-String -Path .\README.md -Pattern 'Java Hook \+ Native Hook|Native Hook|ndk-build'`
Expected: FAIL，文件缺失或内容不匹配。

**Step 3: Write minimal implementation**

编写最小 README 与架构文档，说明：
- 框架目标
- 当前模块
- 构建方式
- 后续扩展方向

**Step 4: Run test to verify it passes**

Run: `Select-String -Path .\README.md -Pattern 'Java Hook \+ Native Hook|Native Hook|ndk-build'`
Expected: PASS。

**Step 5: Commit**

```bash
git add README.md docs
git commit -m "docs: document nook framework architecture"
```
