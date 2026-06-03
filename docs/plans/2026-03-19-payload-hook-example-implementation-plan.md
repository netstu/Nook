# Payload Hook Example Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在 `TargetAppDemo/payloads` 下新增一个 `payload_hook_example`，使用现有 payload 封装层复现 `payload_adblock` 的 Java Hook 效果。

**Architecture:** 采用与 `payload_adblock` 相同的单文件 payload 结构。复用相同的 Hook 点和 JNI 辅助逻辑，只替换 payload 标识与示例命名，确保这是一个回归样例而不是新的行为实现。

**Tech Stack:** C++, Android NDK, hookkit payload macros, JNI

---

### Task 1: 创建 payload_hook_example 骨架

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

先验证目标文件尚不存在。

**Step 2: Run test to verify it fails**

Run: `Test-Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp'`
Expected: `False`

**Step 3: Write minimal implementation**

创建目录和最小文件：

```cpp
#include "hookkit/hookkit.h"

HK_PAYLOAD_CONFIG(300, 200, "HOOK_EXAMPLE");
```

**Step 4: Run test to verify it passes**

Run: `Test-Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp'`
Expected: `True`

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/payload_hook_example/payload.cpp
git commit -m "feat: add payload hook example scaffold"
```

### Task 2: 实现与 payload_adblock 一致的 Hook 行为

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`

**Step 1: Write the failing test**

通过文本校验要求新 payload 至少包含以下内容：

- `HK_PAYLOAD_CONFIG(300, 200, "HOOK_EXAMPLE")`
- `HK_JAVA_BLOCK`
- `com/demo/target/AdWallFragment`
- `loadAd`
- `HK_JAVA_HOOK`
- `com/demo/target/AdWallFragment$ContentAdapter`
- `onBindViewHolder`

**Step 2: Run test to verify it fails**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'HK_JAVA_BLOCK|AdWallFragment|loadAd|HK_JAVA_HOOK|ContentAdapter|onBindViewHolder'`
Expected: FAIL 或匹配不完整

**Step 3: Write minimal implementation**

将 `payload_adblock/payload.cpp` 的核心逻辑迁入新文件，并保持行为一致：

```cpp
HK_JAVA_BLOCK(
    "com/demo/target/AdWallFragment",
    "loadAd",
    "(Ljava/lang/String;Ljava/lang/String;)V",
    0
);

HK_JAVA_HOOK(hk_adwall_onbind, "com/demo/target/AdWallFragment$ContentAdapter", "onBindViewHolder",
             "(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V", 0) {
    // hide itemView immediately
}
```

同时保留 `ClearJniException` 和 `HideViewNow` 这两个 helper。

**Step 4: Run test to verify it passes**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp' -Pattern 'HK_JAVA_BLOCK|AdWallFragment|loadAd|HK_JAVA_HOOK|ContentAdapter|onBindViewHolder'`
Expected: PASS，所有关键片段都能匹配到

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/payload_hook_example/payload.cpp
git commit -m "feat: add hook example payload with adblock behavior"
```

### Task 3: 检查 payload 构建接入方式

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`

**Step 1: Write the failing test**

先确认 `Android.mk` 是否显式列举 payload 目录，或者是否用通配/宏自动发现。

**Step 2: Run test to verify it fails**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk' -Pattern 'payload_hook_example|payload_adblock|foreach|wildcard'`
Expected: 看到现有接入策略；如果不包含新 payload，则视为需要修改

**Step 3: Write minimal implementation**

仅当 `Android.mk` 需要显式列举 payload 时，加入 `payload_hook_example`；若已自动发现，则不修改。

**Step 4: Run test to verify it passes**

Run: `Select-String -Path 'E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk' -Pattern 'payload_hook_example|payload_adblock|foreach|wildcard'`
Expected: 输出能够证明新 payload 会被构建系统纳入

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads/Android.mk
git commit -m "build: register payload hook example if needed"
```

### Task 4: 执行构建验证

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\payload_hook_example\payload.cpp`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`

**Step 1: Write the failing test**

先运行 payload 现有构建命令，记录失败信息。

**Step 2: Run test to verify it fails**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`
Expected: 如果存在未接入或编译错误，则 FAIL

**Step 3: Write minimal implementation**

根据失败信息做最小修复，仅处理：

- 新 payload 未纳入构建
- include 路径错误
- 宏使用或 JNI 代码的编译错误

**Step 4: Run test to verify it passes**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`
Expected: PASS，生成对应的 `libpayload_hook_example.so` 或等价产物

**Step 5: Commit**

```bash
git add E:/Learn/my_program/all_my_hook/TargetAppDemo/payloads
git commit -m "build: make payload hook example compile"
```
