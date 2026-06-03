# Nook Java.overload Real Device Validation Design

**Date:** 2026-04-25

**Status:** Approved

## Goal

在已经通过单测和真机单方法验证的基础上，补一条“真实同名重载方法”的端到端验证路径，证明当前最小 `Java.use(...).method.overload(...)` 不只是语法可用，而是真的能区分不同签名。

## Scope

本次只做最小验证闭环：

- 在 `TargetDemoApp` 里增加一个真实的 Java 同名重载方法组
- 复用现有页面逻辑触发这两个 overload
- 新增一个 host smoke 脚本同时安装两个 overload hook
- 用真机输出证明两个 overload 都被精确绑定并且 `callOriginal(...)` 正常

本次不做：

- 更完整的 Java API 扩展
- 数组 overload
- 构造函数 overload
- Frida 全量 Java 兼容

## Candidate Approaches

### Option A: 改 `TargetDemoApp`，新增可控 overload 目标

做法：

- 在现有 fragment 中新增 `formatBalance(double)` / `formatBalance(String)` 这样的同名重载
- 页面进入时自动触发
- 用 smoke 脚本分别或同时 hook 两个 overload

优点：

- 目标完全可控
- 参数类型都在当前最小支持范围内
- 结果稳定，适合写进文档和后续回归

缺点：

- 需要修改 demo app

### Option B: 直接 hook Android 系统现成 overload

做法：

- 选择 `TextView.setText(...)` 等已有重载方法做验证

优点：

- 不改 demo app

缺点：

- 生命周期和触发点更复杂
- 容易混入系统行为噪音
- 不适合做长期稳定回归基线

## Recommendation

选择 Option A。

原因很简单：这一步的目标不是“尽量少改 app”，而是“得到一条稳定、可重复、可解释的真实 overload 验证路径”。可控性比省一次 app 改动更重要。

## Chosen Target

选择 `TargetDemoApp/src/main/java/com/demo/target/TextFragment.java`。

原因：

- 已有 `formatBalance(double)`，语义自然
- `TextFragment` 页面已有现成 UI 触发链
- 不需要新增按钮、JNI 或复杂交互

## Proposed Method Shape

保留并扩展为：

```java
public String formatBalance(double amount)
public String formatBalance(String amountText)
```

触发策略：

- `updateUserInfo()` 继续走 `formatBalance(double)`
- `formatBalance(double)` 内部再调用 `formatBalance(String)`

这样页面进入时一次链路就能触发两个 overload：

1. 外层 double overload
2. 内层 String overload

## Smoke Strategy

新增 `host/nook-py/java_overload_textfragment_smoke.js`：

- `TextFragment.formatBalance.overload("double")`
- `TextFragment.formatBalance.overload("java.lang.String")`

两个都安装 `implementation`，分别打印：

- overload wrapper 绑定的签名
- enter 参数
- original 返回值

这样一次切到 `TextFragment` 页面，就能看到两个 overload 的独立日志。

## Success Criteria

真机上必须同时满足：

1. 能安装两个 overload hook
2. 初始日志里能看到两个不同签名
3. 切到 `TextFragment` 页面时，两个 overload 都进入
4. `callOriginal(...)` 返回各自原始结果
5. 页面无明显异常或卡死

## Risks

- 如果 JS callback receiver 在同名多 hook 情况下复用了错误 record，可能出现 `callOriginal(...)` 串到另一个 overload
- 如果 demo app 页面没有稳定触发 `TextFragment` 初始化，日志可能不出现

## Mitigation

- smoke 日志同时打印 double / string 两条独立 enter/leave
- 让 double overload 明确调用 string overload，保证一条用户操作就能触发两者
- 文档里写清楚需要切到 `TextFragment` 页签
