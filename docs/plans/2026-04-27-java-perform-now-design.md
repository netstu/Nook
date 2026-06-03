# Nook Java.performNow Design

## Goal

为 Nook 增加一个最小可用、语义接近 Frida 的 `Java.performNow(fn)`，用于“立即执行 Java 回调”，但不等待 app class loader ready。

## Reference

参考 Frida 官方 JavaScript API 文档：

- `Java.perform(fn)`:
  - 当 app class loader 尚未就绪时，会延后执行
- `Java.performNow(fn)`:
  - 只保证当前线程已附着到 VM
  - 立即执行 `fn`
  - 不等待 app class loader ready

文档来源：

- https://frida.re/docs/javascript-api

## Current Nook Context

当前 Nook 的 Java 侧特征与 Frida 不完全相同：

- `Java.ready(fn)` 已存在，并承担“等待 class loader ready 再执行”的职责
- Java 相关 native bridge 在真正进入 JNI 时，会自行通过 `JavaEnv` 做 attach
- 当前没有暴露 Frida 那种完整的 `Java.vm` / `VM.perform(...)` 用户态对象

这意味着：

- Nook 不需要为了 `performNow` 再单独引入一套新的 VM session / attach 生命周期模型
- 最小实现可以落在 JS bootstrap 层
- 语义重点应放在：
  - 立即执行
  - 不进入 `readyCallbacks`
  - 不依赖 app class loader ready

## Chosen Design

### Public API

新增：

```javascript
Java.performNow(function () {
  // immediate execution
});
```

行为：

- 参数必须是函数，否则抛 `TypeError`
- 调用时立即执行 `fn`
- 不进入 `Java.ready(...)` 的排队逻辑
- 不主动等待 class loader ready

### Implementation Strategy

在 `src/agent_runtime/js_runtime.cpp` 的 Java bootstrap 中直接加入：

- `Java.performNow = function (fn) { ... }`

实现内容仅包括：

- 参数校验
- 立即调用 `fn()`

不新增新的 native bridge。

## Why This Is Enough

对当前 Nook 来说，Frida 文档中 “ensures that the current thread is attached to the VM” 的要求，已经由现有 Java native bridge 的 `JavaEnv` attach-on-demand 机制覆盖：

- `Java.use(...)`
- `Java.cast(...)`
- `Java.openClassFile(...)`
- `Java.registerClass(...)`
- Java method invoke / field read / field write

只要 `fn()` 内部通过这些现有入口访问 Java，对 JNI attach 的需要已经被满足。

因此 phase-1 的 `performNow` 不需要新增：

- `Java.vm.perform(...)`
- 显式 thread attach API
- 额外 runtime state

## Alternatives Considered

### Option 1: JS-only immediate wrapper

优点：

- 最小改动
- 与当前 Nook 结构匹配
- 风险最低

缺点：

- 不显式暴露“线程已附着”这个事实

### Option 2: 新增 native `__nookJavaPerformNow(...)`

优点：

- 形式上更接近 Frida 内部实现风格

缺点：

- 对当前 Nook 属于重复建设
- 会引入新的 native callback / JS callback 调度路径
- 验证面扩大，没有明显收益

### Option 3: 顺手重构 `Java.ready/perform/performNow`

优点：

- 统一 Java bootstrap 语义

缺点：

- 风险过大
- 超出当前 step6 的收口目标

结论：

- 选择 Option 1

## Validation Plan

### Host tests

新增覆盖：

- `typeof Java.performNow === 'function'`
- 非函数参数抛 `TypeError`
- `Java.performNow(fn)` 会立即执行回调
- 回调内可直接访问现有 Java wrapper API，不走 `Java.ready(...)` 延迟队列

### Device smoke

新增 smoke：

- 调用 `Java.performNow(fn)`
- 在回调内直接调用 boot / framework 可访问 API
- 建议验证：
  - `android.app.ActivityThread.currentApplication()`
  - 或 `java.lang.System.currentTimeMillis()`

重点确认：

- 回调立即执行
- 不依赖 `Java.ready(...)`
- 无卡顿 / 无崩溃

## Out of Scope

这一步不做：

- `Java.perform(fn)` 语义重构
- `Java.vm` API
- 更细粒度的 attach state introspection
- 对 app class loader ready 状态做新的隐式处理

## Expected Outcome

完成后，Nook 在 `step6` 中这三项的状态将变成：

- `Java.openClassFile(...)`: 已完成
- `Java.registerClass(...)`: 已完成
- `Java.performNow(...)`: 已完成

这样后续就可以把重点切回：

- spawn / zygote / ready 稳定性
- Java 高阶类型 / overload / 容器边界
