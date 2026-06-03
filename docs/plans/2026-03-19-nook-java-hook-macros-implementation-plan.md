# Nook Java Hook Macros Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 `Nook` 增加简单的 Java Hook 宏封装和 payload 自动启动模板，并将 `payload_hook_example` 改写为宏式声明。

**Architecture:** 新增一个公共宏头文件和一个轻量 payload runtime。宏负责 Hook 声明与注册，runtime 负责初始化、重试安装、constructor / `JNI_OnLoad` 双入口和统一卸载。对外仍然基于已有的 `NookJavaHook` API。

**Tech Stack:** C++, Android NDK, JNI, Nook Java Hook

---

### Task 1: 建立宏头文件和 runtime 骨架

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHookMacros.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookJavaHookPayload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_nook_macro_header.cpp`

**Step 1: Write the failing test**

创建头文件编译测试：

```cpp
#include "nook/NookJavaHookMacros.h"

int main() {
    return 0;
}
```

**Step 2: Run test to verify it fails**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_header.cpp -c -o .\tests\headers\test_nook_macro_header.o`
Expected: FAIL，头文件尚不存在

**Step 3: Write minimal implementation**

新增最小骨架：

- runtime 声明结构
- 注册函数声明
- 空的宏壳

**Step 4: Run test to verify it passes**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_header.cpp -c -o .\tests\headers\test_nook_macro_header.o`
Expected: PASS

**Step 5: Commit**

```bash
git add include/nook src/framework tests/headers
git commit -m "feat: add nook java hook macro skeleton"
```

### Task 2: 实现 Hook 声明注册与 payload 生命周期模板

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHookMacros.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookJavaHookPayload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_nook_macro_usage.cpp`

**Step 1: Write the failing test**

创建最小使用样例测试，要求能写：

```cpp
NOOK_PAYLOAD_CONFIG("T", 1, 1);
NOOK_JAVA_BLOCK("A", "m", "()V", 0);
```

**Step 2: Run test to verify it fails**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_usage.cpp -c -o .\tests\headers\test_nook_macro_usage.o`
Expected: FAIL，宏功能尚不完整

**Step 3: Write minimal implementation**

实现：

- Hook 声明结构
- 静态注册
- `NOOK_PAYLOAD_CONFIG`
- `NOOK_JAVA_HOOK`
- `NOOK_JAVA_BLOCK`
- runtime 中的安装重试与 `UnhookAll`

**Step 4: Run test to verify it passes**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_usage.cpp -c -o .\tests\headers\test_nook_macro_usage.o`
Expected: PASS

**Step 5: Commit**

```bash
git add include/nook src/framework tests/headers
git commit -m "feat: implement nook java hook payload macros"
```

### Task 3: 将 payload_hook_example 改写为宏式写法

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

定义文本目标：

- 使用 `NOOK_PAYLOAD_CONFIG`
- 使用 `NOOK_JAVA_BLOCK`
- 使用 `NOOK_JAVA_HOOK`
- 不再保留手写 `JNI_OnLoad`/constructor/`InstallHooks`

**Step 2: Run test to verify it fails**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'NOOK_PAYLOAD_CONFIG|NOOK_JAVA_BLOCK|NOOK_JAVA_HOOK'`
Expected: FAIL

**Step 3: Write minimal implementation**

改写 payload 为：

- 少量 helper + 业务 callback
- 新宏声明 payload 配置与 Hook 点

同时将 `NookJavaHookPayload.cpp` 加入 `TargetAppDemo/payloads/Android.mk` 的 `payload_hook_example` 模块。

**Step 4: Run test to verify it passes**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'NOOK_PAYLOAD_CONFIG|NOOK_JAVA_BLOCK|NOOK_JAVA_HOOK'`
Expected: PASS

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/payload_hook_example/payload.cpp
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/Android.mk
git commit -m "refactor: migrate payload hook example to nook macros"
```

### Task 4: 执行构建和注入验证

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`

**Step 1: Write the failing test**

先跑 `Nook` 和 `TargetAppDemo/payloads` 的构建，记录错误。

**Step 2: Run test to verify it fails**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\kanxue\Nook`

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`

Expected: 若有 include、重复定义或宏展开问题则 FAIL

**Step 3: Write minimal implementation**

根据失败结果最小修复：

- include 路径
- runtime 源文件接入
- 宏定义冲突
- payload 编译错误

**Step 4: Run test to verify it passes**

再次运行上述两条 `ndk-build` 命令
Expected: PASS，并保留 `libpayload_hook_example.so`

**Step 5: Commit**

```bash
git add .
git commit -m "build: verify nook macro-based payload flow"
```
