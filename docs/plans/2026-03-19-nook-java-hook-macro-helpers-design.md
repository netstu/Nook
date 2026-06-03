# Nook Java Hook Macro Helpers Design

**Date:** 2026-03-19

**Status:** Approved

## Goal

在现有 `Nook` Java Hook 宏封装基础上，继续补充一组最常用的高频辅助能力：

- 返回值替换宏
- Java 对象参数辅助

目标是进一步缩短简单 Hook 的用户代码，但保持实现仍然是轻量头文件级增强，而不是升级成更复杂的 DSL。

## Scope

### Included

- 新增：
  - `NOOK_JAVA_REPLACE_BOOL`
  - `NOOK_JAVA_REPLACE_INT`
  - `NOOK_JAVA_REPLACE_LONG`
- 新增对象辅助函数与宏：
  - `NookJavaThisObject(env, thiz)`
  - `NookJavaArgObject(env, args, index)`
  - `NOOK_JAVA_THIS_OBJECT(env, thiz)`
  - `NOOK_JAVA_ARG_OBJECT(env, args, index)`

### Excluded

- 不做 `jstring` 替换宏
- 不做数组/集合 helper
- 不做字段读写 helper
- 不做 UI 专用 helper
- 不修改 `payload_hook_example` 的业务逻辑

## API Shape

```cpp
NOOK_JAVA_REPLACE_INT(
    "com/demo/target/Foo",
    "getValue",
    "()I",
    0,
    123
);

NOOK_JAVA_HOOK(OnBind,
    "com/demo/target/Adapter",
    "onBindViewHolder",
    "(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V",
    0) {
    jobject holder = NOOK_JAVA_ARG_OBJECT(env, args, 0);
    jobject self = NOOK_JAVA_THIS_OBJECT(env, thiz);
}
```

## Implementation Strategy

### Replace Macros

这些宏基于 `NOOK_JAVA_HOOK` 实现，和现有 `NOOK_JAVA_BLOCK` 同一层级：

- `NOOK_JAVA_REPLACE_BOOL`
- `NOOK_JAVA_REPLACE_INT`
- `NOOK_JAVA_REPLACE_LONG`

它们只负责写入 `result`，并返回“不调用原方法”。

### Object Helpers

对象辅助采用 `static inline` 函数实现，而不是纯宏：

- 类型更稳定
- 调试更容易
- 避免宏重复求值

第一版只做最保守的 JNI 本地引用包装：

- `NookJavaThisObject` -> `env->NewLocalRef(thiz)`
- `NookJavaArgObject` -> `env->NewLocalRef(reinterpret_cast<jobject>(args[index].l))`

失败时统一返回 `nullptr`。

## Validation Strategy

- 新增头文件测试覆盖这些宏和 helper
- 确认 `g++` 主机编译通过
- 确认 `Nook` 的 NDK 构建通过
- 确认 `TargetAppDemo/payloads` 的 NDK 构建不回归

## Constraints

- 第一版 helper 只假设当前回调里的对象值可通过 `NewLocalRef` 包装
- 如果后续发现需要兼容 ART 内部对象指针，再单独扩展，不提前过度设计
