# Nook Java Hook Macro Helpers Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 `Nook` Java Hook 宏层补充最常用的返回值替换宏和对象参数辅助。

**Architecture:** 继续沿用现有 `NookJavaHookMacros.h` 单头文件方案。返回值替换宏通过 `NOOK_JAVA_HOOK` 特化实现；对象辅助通过 `static inline` 函数提供，再用薄宏暴露给用户。

**Tech Stack:** C++, Android NDK, JNI, Nook Java Hook macros

---

### Task 1: 为替换宏建立失败测试

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_nook_macro_usage.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_nook_macro_replace_usage.cpp`

**Step 1: Write the failing test**

新增最小测试，要求以下代码可以编译：

```cpp
NOOK_JAVA_REPLACE_INT("A", "m", "()I", 0, 7);
NOOK_JAVA_REPLACE_BOOL("A", "b", "()Z", 0, 1);
NOOK_JAVA_REPLACE_LONG("A", "l", "()J", 0, 9);
```

**Step 2: Run test to verify it fails**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_replace_usage.cpp -c -o .\tests\headers\test_nook_macro_replace_usage.o`
Expected: FAIL，宏尚不存在

**Step 3: Write minimal implementation**

在 `NookJavaHookMacros.h` 中新增 3 个 replace 宏。

**Step 4: Run test to verify it passes**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_replace_usage.cpp -c -o .\tests\headers\test_nook_macro_replace_usage.o`
Expected: PASS

**Step 5: Commit**

```bash
git add include/nook tests/headers
git commit -m "feat: add nook java replace macros"
```

### Task 2: 为对象辅助建立失败测试

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_nook_macro_object_helpers.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookJavaHookMacros.h`

**Step 1: Write the failing test**

创建测试，要求以下代码可编译：

```cpp
jobject self = NOOK_JAVA_THIS_OBJECT(env, thiz);
jobject arg0 = NOOK_JAVA_ARG_OBJECT(env, args, 0);
```

**Step 2: Run test to verify it fails**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_object_helpers.cpp -c -o .\tests\headers\test_nook_macro_object_helpers.o`
Expected: FAIL，helper 尚不存在

**Step 3: Write minimal implementation**

新增：

- `static inline jobject NookJavaThisObject(...)`
- `static inline jobject NookJavaArgObject(...)`
- 对应宏别名

**Step 4: Run test to verify it passes**

Run: `g++ -I .\include .\tests\headers\test_nook_macro_object_helpers.cpp -c -o .\tests\headers\test_nook_macro_object_helpers.o`
Expected: PASS

**Step 5: Commit**

```bash
git add include/nook tests/headers
git commit -m "feat: add nook java object helper macros"
```

### Task 3: 执行构建验证

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads\Android.mk`

**Step 1: Write the failing test**

先运行 `Nook` 和 `TargetAppDemo/payloads` 构建，记录错误。

**Step 2: Run test to verify it fails**

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\kanxue\Nook`

Run: `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\Android.mk NDK_APPLICATION_MK=.\Application.mk`
Workdir: `E:\Learn\my_program\all_my_hook\TargetAppDemo\payloads`

Expected: 若宏实现有问题则 FAIL

**Step 3: Write minimal implementation**

根据错误做最小修复，仅处理：

- 头文件兼容问题
- JNI 类型声明问题
- 宏展开冲突

**Step 4: Run test to verify it passes**

再次运行上述两条 `ndk-build` 命令
Expected: PASS

**Step 5: Commit**

```bash
git add .
git commit -m "build: verify nook macro helper additions"
```
