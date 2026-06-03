# Nook Java Main Thread Helpers Design

## Goal

为 Nook 增加最小可用、语义接近 Frida 的：

- `Java.isMainThread()`
- `Java.scheduleOnMainThread(fn)`

用于 Android Java 场景下的主线程判断和主线程回调调度。

## Why This Next

当前 Nook 已经具备：

- `Java.ready(...)`
- `Java.performNow(...)`
- `Java.registerClass(...)`
- Java 直接方法调用
- Java wrapper 生命周期 primitive

下一步如果继续向 Frida 常用 Java API 看齐，主线程辅助比继续堆叠生命周期 primitive 更实用，也更贴近真实脚本使用场景。

## User-Facing Target

期望支持：

```javascript
Java.performNow(function () {
  if (!Java.isMainThread()) {
    Java.scheduleOnMainThread(function () {
      console.log("now on main thread");
    });
  }
});
```

phase-1 不追求完整 `Java.vm` 模型，也不引入新的 VM session API。

## Approaches Considered

### Option 1: 纯 JS bootstrap 组合已有 Java 能力

做法：

- `Java.isMainThread()` 使用：
  - `android.os.Looper.myLooper()`
  - `android.os.Looper.getMainLooper()`
- `Java.scheduleOnMainThread(fn)` 使用：
  - `android.os.Handler`
  - `Java.registerClass(...)` 创建 `java.lang.Runnable`
  - `Handler.post(runnable)`

优点：

- 变更最小
- 复用现有 `Java.use / invoke / registerClass / $new`
- 不新增 native bridge

风险：

- 依赖现有 framework class 调用在设备上稳定
- 需要给 `Runnable` 类名生成简单唯一名

### Option 2: 新增 native bridge 做主线程判断和投递

做法：

- 新增 C++/JNI helper：
  - 检查当前线程是否是主线程
  - 通过 `Looper/Handler` 或 `ActivityThread` 投递主线程任务

优点：

- 运行时路径可控
- 可以减少 JS 层拼装逻辑

缺点：

- 范围更大
- 会新增 bridge surface 和测试负担

### Option 3: 直接引入更完整 `Java.vm`

优点：

- 更接近 Frida 长期形态

缺点：

- 明显超出当前最小增量
- 会把问题从“主线程 helper”升级为“新的 VM API 设计”

## Recommendation

先做 Option 1。

理由：

- 这是最小、最可验证、最符合当前仓库演进节奏的方案
- 如果后续发现 framework class 调用或 callback 投递不稳定，再收敛为 native helper 也不晚

## Proposed Semantics

### `Java.isMainThread()`

- 无参数
- 返回 `boolean`
- 非 Android / 无 Java 上下文时，允许沿用现有 Java surface 的错误模型

### `Java.scheduleOnMainThread(fn)`

- `fn` 必须是函数，否则抛 `TypeError`
- 将 `fn` 包装为 `java.lang.Runnable`
- 投递到主线程 `Handler`
- 返回 `undefined`

phase-1 约束：

- 不保证返回取消句柄
- 不保证任务队列可观测
- 不做复杂异常传播模型；回调异常沿用现有 JS runtime 行为

## Testing Strategy

### Desktop / host regression

最小覆盖：

- `typeof Java.isMainThread === 'function'`
- `typeof Java.scheduleOnMainThread === 'function'`
- 非函数参数校验
- `scheduleOnMainThread(...)` 路径会：
  - 调用 `Looper.getMainLooper()`
  - 构造 `Handler`
  - 注册 `Runnable`
  - 调用 `Handler.post(...)`

### Device smoke

验证：

- attach 后 bindings 存在
- `Java.performNow(...)` 中先打印一次 `Java.isMainThread()`
- `Java.scheduleOnMainThread(fn)` 回调中再次打印 `Java.isMainThread()`
- 预期回调里为 `true`

## Boundary

本次不做：

- `Java.vm`
- `Java.available`
- 主线程任务取消
- 通用线程调度框架
- 非 Android 平台支持

## Success Criteria

- `Java.isMainThread()` / `Java.scheduleOnMainThread()` 在设备上可用
- 主线程回调能稳定执行
- 桌面回归覆盖基础路径
- 不回退现有 `Java.ready / performNow / registerClass` 能力
