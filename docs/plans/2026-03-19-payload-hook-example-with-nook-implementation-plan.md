# Payload Hook Example With Nook Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在 `TargetAppDemo/payloads` 下新增一个直接使用 `Nook` 的 `payload_hook_example`，复现 `payload_adblock` 的 Java Hook 效果。

**Architecture:** 使用一个独立 payload 模块，不依赖 `hookkit`。`payload.cpp` 自己负责 JNI 生命周期、后台初始化与 Hook 安装，底层直接调用 `NookJavaHook` 的公开 API，并在 `Android.mk` 中编入 `Nook` 源码。

**Tech Stack:** C++, Android NDK, JNI, Nook Java Hook

---

### Task 1: 创建 payload_hook_example 骨架

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

先验证目标文件不存在。

**Step 2: Run test to verify it fails**

Run: `Test-Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp'`
Expected: `False`

**Step 3: Write minimal implementation**

创建最小文件并包含 `Nook` 头文件。

```cpp
#include "nook/NookJavaHook.h"
```

**Step 4: Run test to verify it passes**

Run: `Test-Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp'`
Expected: `True`

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/payload_hook_example/payload.cpp
git commit -m "feat: add nook payload hook example scaffold"
```

### Task 2: 实现 payload 逻辑

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

通过文本校验要求源码至少包含：

- `NookJavaHookInitialize`
- `NookJavaHookHook`
- `AdWallFragment`
- `loadAd`
- `ContentAdapter`
- `onBindViewHolder`

**Step 2: Run test to verify it fails**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'NookJavaHookInitialize|NookJavaHookHook|AdWallFragment|loadAd|ContentAdapter|onBindViewHolder'`
Expected: FAIL 或匹配不完整

**Step 3: Write minimal implementation**

实现：

- `ClearJniException`
- `HideViewNow`
- `BlockLoadAd`
- `HideAdItemOnBind`
- `InstallHooks`
- `JNI_OnLoad`
- 后台初始化/重试逻辑

并直接使用：

```cpp
NookJavaHookInitialize();
NookJavaHookHook(...);
NookJavaHookUnhookAll();
```

**Step 4: Run test to verify it passes**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'NookJavaHookInitialize|NookJavaHookHook|AdWallFragment|loadAd|ContentAdapter|onBindViewHolder'`
Expected: PASS

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/payload_hook_example/payload.cpp
git commit -m "feat: implement nook-based payload hook example"
```

### Task 3: 接入 Android NDK 构建

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`

**Step 1: Write the failing test**

确认 `Android.mk` 目前不包含 `payload_hook_example`。

**Step 2: Run test to verify it fails**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk' -Pattern 'payload_hook_example|nook'`
Expected: FAIL 或无匹配

**Step 3: Write minimal implementation**

在 `Android.mk` 中新增独立模块，编入：

- `payload_hook_example/payload.cpp`
- `Nook/src/framework/Nook.cpp`
- `Nook/src/framework/NookJavaHook.cpp`
- `Nook/src/native_hook/NookNativeHook.cpp`
- `Nook/src/java_hook/JavaHook.cpp`
- `Nook/src/java_hook/JVM.cpp`
- `Nook/src/common/JavaHookLog.cpp`
- `Nook/src/common/ArtStructDetector.cpp`
- `Nook/third_party/xdl/*.c`

并添加所需 include 路径。

**Step 4: Run test to verify it passes**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk' -Pattern 'payload_hook_example|Nook.cpp|NookJavaHook.cpp|JavaHook.cpp'`
Expected: PASS

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/Android.mk
git commit -m "build: add nook payload hook example module"
```

### Task 4: 执行构建验证

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

先运行 payloads 现有 NDK 构建命令，记录失败信息。

**Step 2: Run test to verify it fails**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`
Expected: 若 include、源码列表或链接有问题则 FAIL

**Step 3: Write minimal implementation**

根据失败信息做最小修复，仅修正：

- include 路径
- 源文件列表
- 缺失依赖
- Nook 头文件或 JNI 相关编译错误

**Step 4: Run test to verify it passes**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`
Expected: PASS，并生成 `libpayload_hook_example.so` 或等价产物

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads
git commit -m "build: make nook payload hook example compile"
```
