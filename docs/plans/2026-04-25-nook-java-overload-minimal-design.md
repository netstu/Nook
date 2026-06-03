# Nook Minimal Java.overload Design

**Date:** 2026-04-25

**Status:** Approved

## Goal

在现有最小 `Java.perform` / `Java.use` / `implementation` / `callOriginal` 基础上，
增加第一版 `Java.use(...).method.overload(...)`，让重载方法可以按参数类型精确选中。

这一步仍然是“先跑通、再扩展”的策略，不追求一次性 Frida 全量兼容。

## Scope

本次只支持：

- `method.overload(...typeNames)`
- 用精确签名安装 `implementation`
- 用精确签名执行 `callOriginal(...)`

本次不支持：

- 数组类型
- Frida 全量类型别名
- 构造函数/字段/`Java.choose`
- 多层反射对象模型

## Supported Type Names

第一版接受这些类型名：

- primitive: `void`, `boolean`, `byte`, `char`, `short`, `int`, `long`, `float`, `double`
- object shortcuts: `java.lang.String`
- 普通 Java 类名，例如 `com.demo.target.LoginFragment`

映射规则：

- primitive 直接映射到 JNI descriptor
- 非 primitive 且不以 `[` 开头的类名，转换为 `Lpkg/name/Class;`

## API Shape

示例：

```javascript
var Demo = Java.use("com.demo.target.SomeClass");
var overload = Demo.test.overload("java.lang.String", "int");

overload.implementation = function (text, value) {
  var original = overload.callOriginal.call(this, text, value);
  return original;
};
```

说明：

- `Java.use(...).method` 仍然是默认 method wrapper
- `overload(...)` 返回一个新的“已绑定签名”的 wrapper
- 已绑定签名的 wrapper 不再依赖 wildcard 解析

## Recommended Approach

采用“JS wrapper 做签名绑定，桥接层继续吃 descriptor”的方案。

原因：

- 现有桥接层已经以 `signature` 为核心字段
- 只要给 method wrapper 增加 `signature metadata`，安装和 `callOriginal` 逻辑几乎不用重做
- 不会破坏当前 wildcard fallback

## Internal Changes

### JS Runtime

在 `src/agent_runtime/js_runtime.cpp`：

- method wrapper 增加 `overload(...typeNames)` 工厂
- method wrapper metadata 增加：
  - `$signature`
  - `$overloadTypeNames`
- `implementation` 安装时优先使用 `$signature`，否则保留 `*`

### Java Bridge

在 `src/agent_runtime/nook_java_js_bridge.cpp`：

- 新增类型名到 JNI descriptor 的转换函数
- 允许测试直接观察“安装请求是否从 `*` 变成精确 descriptor”

## Test Strategy

优先加 host/runtime 单测，不先依赖设备联调：

1. `overload("java.lang.String")` 返回 method wrapper
2. `implementation = fn` 时安装请求签名为 `"(Ljava/lang/String;)Z"`
3. `callOriginal("x")` 绑定到同一条精确记录
4. 未调用 `overload()` 的旧脚本仍保持 wildcard 路径

## Risks

- descriptor 映射做错会导致 hook 安装失败
- `callOriginal` 如果仍然引用未绑定 wrapper，可能退回 wildcard 语义

## Mitigation

- 单测直接断言安装请求和 original 调用记录中的 `signature`
- 保留旧 wildcard 测试，防止回归
