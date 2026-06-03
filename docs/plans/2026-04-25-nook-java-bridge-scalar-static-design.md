# Nook Java Bridge Scalar And Static Design

**Date:** 2026-04-25

**Status:** Approved

## Goal

在现有最小 Java hook 主路径已经支持 `boolean` / `int` / `String` / `double`、实例方法、`overload(...)`、`callOriginal(...)` 的基础上，补齐下一批最常用能力：

- 常用标量类型桥接
- `static` 方法 hook 主路径

目标不是一次性补齐全部 Frida Java API，而是继续把最常见签名打通，让真实 Java hook 更少撞边界。

## Current State

当前已验证：

- `Java.perform(...)`
- `Java.use(...)`
- `implementation = fn`
- `callOriginal(...)`
- `overload(...)`
- 实例方法重载真实真机验证
- `boolean` / `int` / `String` / `double` 的最小桥接

当前明显缺口：

- `long(J)` 参数/返回值
- `float(F)` 参数/返回值
- 更完整的 `double(D)` 返回值 smoke
- `static` 方法 hook 安装与原始调用

## Scope

本次纳入：

- 参数/返回值：`void(V)`、`boolean(Z)`、`int(I)`、`long(J)`、`float(F)`、`double(D)`、`java.lang.String`
- `static` 方法 hook 的最小 install + callback + `callOriginal(...)`
- host runtime 单测
- 一条对应的 smoke/验证路径文档

本次不纳入：

- `short` / `byte` / `char`
- 数组
- 任意对象类型
- 字段、构造函数、`Java.choose()`

## Candidate Approaches

### Option A: 先补标量类型，再补 `static`

做法：

- 先扩 `JavaJsValueKind`
- 打通 `JNI value <-> JavaJsValue <-> QuickJS number/bool/string`
- 然后让 JS wrapper 可以标记并安装 `static` 方法

优点：

- 把当前最核心的 Java hook 主链补厚
- 真正减少“换个签名就不行”的问题
- `static` 完全复用同一条桥接链

缺点：

- 需要多加几条单测

### Option B: 先补 `static`，类型后补

做法：

- 先让 `Java.use(...).method` 暴露静态方法安装能力
- 类型还维持当前窄范围

优点：

- 更快得到一块新 API 能力

缺点：

- `static` 一旦落到 `long/float` 等常见签名仍会失败
- 提升的是“功能面”，不是“可用面”

## Recommendation

选择 Option A。

原因：

现在最值钱的不是继续横向铺很多 Java API，而是把已经打通的主链从“少数演示签名可用”提升为“更常用签名可用”。补标量类型比单独补 `static` 更重要，而 `static` 又可以顺势建立在同一条桥接链上。

## Proposed Type Mapping

JS 层保持最小且稳定：

- `boolean` <-> JS boolean
- `int` <-> JS integer/number
- `long` <-> 先收敛为 JS number
- `float` <-> JS number
- `double` <-> JS number
- `String` <-> JS string
- `void` <-> `undefined`

注意：

- 这里的 `long` 先不引入 BigInt 或 Frida 风格 Int64 对象
- 只把它作为“常用且不过度复杂”的第一版桥接
- 文档里明确说明这是最小实现，不承诺 64-bit 边界完全无损

## Proposed Static Hook Shape

最小目标仍然沿用当前 API 形状：

```javascript
Java.perform(function () {
  var MainActivity = Java.use("com.demo.target.MainActivity");
  MainActivity.someStatic.overload("int").implementation = function (value) {
    return this.someStatic.callOriginal(value);
  };
});
```

实现要求：

- wrapper 能区分实例 / 静态
- install request 带上 `is_static`
- overload 解析时能按 `is_static` 匹配
- `callOriginal(...)` 能在静态上下文工作

## Internal Changes

### Java bridge

在 `src/agent_runtime/nook_java_js_bridge.*`：

- `ParseTypeDescriptor(...)` 扩展支持 `J` / `F`
- `ConvertNookJavaHookValueToJavaJsValue(...)` 扩展 `long` / `float`
- `ConvertJavaJsValueToNookJavaHookValue(...)` 扩展 `long` / `float`
- `ResolveJavaMethodSignature(...)` 继续按 `is_static` 参与精确匹配
- `callOriginal(...)` 对静态调用继续沿用当前桥接，但要明确允许 `thiz == null`

### JS runtime

在 `src/agent_runtime/js_runtime.cpp`：

- `MakeJavaJsValue(...)` / `ParseJavaJsValue(...)` 继续把 JS number 映射到 Java 数值种类
- `Java.use(...)` wrapper 增加静态方法元信息入口
- overload signature resolver 需要能接收 `is_static`
- `JsJavaInstallImplementation(...)` 把静态信息带入请求

### Tests

在 `tests/communication/test_js_runtime_native_attach.cpp`：

- `overload("float")` / `overload("long")` wrapper 选择
- `float` / `long` / `double return` 的 `callOriginal(...)`
- `static` overload install / exact signature / `callOriginal(...)`

## Risks

- JS number 同时承载 `int/long/float/double`，如果 coercion 顺序错误，可能把原本整数路径误判成浮点
- `long` 用 JS number 的最小方案有精度边界
- `static` 调用如果错误复用实例上下文，容易在 `callOriginal(...)` 崩溃

## Mitigation

- 明确 coercion 顺序：先保留已有 `boolean` / `string` / 整数稳定路径，再处理浮点
- `long` 第一版只声明“常用范围可用”，先不碰 BigInt
- host 单测单独覆盖 `static` install 和 `static callOriginal(...)`

## Success Criteria

完成后必须满足：

1. host 单测覆盖 `long/float/double return/static`
2. 现有 Java hook 主链回归不退化
3. `static` 方法能安装 hook 并走通 `callOriginal(...)`
4. 文档明确说明 `long` 当前仍是最小实现，不是完整 64-bit 精度承诺
