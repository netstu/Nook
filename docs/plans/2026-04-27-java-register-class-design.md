# Nook Java.registerClass Design

## Goal

为 Nook 增加一个最小可用、偏 Frida 风格的 `Java.registerClass(spec)`，优先覆盖 Android Java 接口回调 / listener 场景，而不是一次性实现完整的动态类生成系统。

## Confirmed Scope

本阶段只支持：

- `Java.registerClass(spec)`
- `spec.name`
- `spec.implements: [InterfaceWrapper, ...]`
- `spec.methods: { methodName: function (...) { ... } }`
- 返回一个 class-like 对象，至少提供 `$new()`
- `$new()` 返回的对象必须能作为 `implements` 中声明的 Java 接口对象传回 Java

本阶段明确不支持：

- `extends`
- fields
- 自定义构造器
- overload 声明对象
- 真实生成 `spec.name` 对应的 Java class
- 通用 Java 类生成 / dex 生成框架

## Problem

`Java.registerClass(...)` 和 `Java.use(...)`、`Java.cast(...)` 不同，它不是单纯包装已有 Java 对象，而是要在目标进程中“制造”一个 Java 可接受的对象，并让 Java 对该对象的方法调用能够回到 JS。

如果直接在 JS 层伪造 wrapper，只能骗过脚本，不能骗过 ART/JNI，也就不能作为真实接口对象传回 Java。要最小落地，必须选一个 Java/ART 已经认可的对象构造机制。

## Chosen Approach

### Approach A: `Proxy.newProxyInstance(...)` + helper `InvocationHandler` 实现

这是本次采用的方案。

实现要点：

1. 在 agent 中提供一个很小的 Java helper 类，实现 `java.lang.reflect.InvocationHandler`
2. helper 内部只保存一个 native handle / callback id
3. helper 的 `invoke(Object proxy, Method method, Object[] args)` 调回 Nook native bridge
4. native bridge 再把方法名、参数等转发给当前 QuickJS runtime 中对应的 JS function
5. `Java.registerClass(spec)` 返回的 class-like 对象在 `$new()` 时：
   - 创建 helper handler 实例
   - 调用 `Proxy.newProxyInstance(...)`
   - 返回生成的代理对象 wrapper

### Why This Is The Right First Step

- 它能产生一个“真 Java 对象”，而不是 JS 假对象
- 这个对象天然可以实现一个或多个接口，正好覆盖 listener/callback 主场景
- 它不需要本阶段引入 dex 生成、字节码拼接、构造器/字段布局等高复杂度问题
- 它和 Nook 当前已有的 `Java.use / invoke / cast / retain / setClassLoader / ClassFactory` 能自然组合

## Alternatives Considered

### Approach B: 运行时生成 dex / 真实类

优点：

- 语义更接近 Frida 的完整 `registerClass`
- 可以继续扩展到 `extends`、fields、真实类名等

缺点：

- 当前代价明显过大
- 会同时引入 dex 生成、类加载、签名拼装、构造器映射、字段桥接等多个新问题
- 不适合作为 phase 1

结论：暂缓。

### Approach C: 纯 JS wrapper / fake class

优点：

- 实现快

缺点：

- 不能作为真实 Java 接口对象传回 Java
- 对用户几乎没有实际价值

结论：不采用。

## API Shape

### JS Surface

```javascript
Java.perform(function () {
  var OnClickListener = Java.use('android.view.View$OnClickListener');

  var MyListener = Java.registerClass({
    name: 'com.nook.ProxyClickListener',
    implements: [OnClickListener],
    methods: {
      onClick: function (view) {
        send('clicked:' + view.$className);
      }
    }
  });

  var listener = MyListener.$new();
});
```

### Phase-1 Semantics

- `name` 先只作为元数据保留，便于后续补齐，不承诺生成同名 Java class
- `implements` 中每一项必须是 Java interface wrapper
- `methods` 里的 key 必须与接口方法名一致
- 如多个接口存在同名方法，本阶段按“方法名唯一”处理；若冲突则直接报错，避免引入半成品分派语义
- `$new()` 暂不接收构造参数

## Runtime Architecture

### JS Runtime Layer

在 `src/agent_runtime/js_runtime.cpp` 中扩展 bootstrap：

- 暴露 `Java.registerClass`
- 解析 `spec`
- 构造 class-like 返回对象
- `$new()` 调用新的 native bridge：
  - 校验接口列表
  - 注册 JS method table
  - 构建 Java `Proxy` 实例

### Native Bridge Layer

在 `src/agent_runtime/nook_java_js_bridge.h/.cpp` 中新增最小桥接能力：

- 注册一个 JS-backed Java proxy 记录
- 根据接口列表创建 helper handler + `Proxy` 实例
- 保存 `callback_id -> JS method table` 映射
- 处理 helper `invoke(...)` 回调：
  - 提取方法名
  - 取出 JS 函数
  - 转换参数
  - 转换返回值

### Java Helper Layer

新增一个极小 helper Java 类，例如：

- `nook.java.NookJsInvocationHandler`

职责：

- 实现 `java.lang.reflect.InvocationHandler`
- 构造时接受 native callback id / handle
- `invoke(...)` 只做一次 native 转发，不放复杂逻辑

这样可以把复杂性留在 native + JS runtime，helper 类保持稳定。

## Error Handling

phase 1 错误策略尽量直接：

- `implements` 为空：抛错
- `implements` 中存在非 interface wrapper：抛错
- `methods` 缺失接口要求的方法：抛错
- 多接口同名方法冲突：抛错
- JS 回调缺失：抛错
- JS 返回值与接口返回类型不兼容：抛错

不做隐式兜底，以免形成后续兼容债务。

## Testing Strategy

### Host tests first

先在 `tests/communication/test_js_runtime_native_attach.cpp` 中补失败测试，至少覆盖：

- `typeof Java.registerClass === 'function'`
- `Java.registerClass(...).$new` 存在
- 能把 interface wrapper 和 methods 表传到 native bridge
- helper/native bridge 回调能够按方法名命中 JS method

### Device smoke second

真机优先做接口回调场景，不选复杂 UI/生命周期链路：

- 首选 `android.view.View$OnClickListener`
- 或一个 demo app 中更可控的接口回调点

目标是验证：

- `$new()` 返回对象是可传递的 Java 接口对象
- Java 触发接口调用时，JS 收到回调
- 无明显卡顿和闪退

## Compatibility Boundary

完成本阶段后，Nook 会得到：

- 一个可用的 `Java.registerClass(...)` 最小接口代理版
- 接近 Frida 常见 listener/callback 场景的实际能力

但仍然不会得到：

- 完整 Frida `registerClass` 语义
- 任意 Java class 动态生成
- `extends` / fields / 构造器 / overload-rich dispatch

## Recommendation

先把这个最小接口代理版打通并真机验证，再决定是否进入下一阶段：

1. 支持 `spec.name` 真正落为 Java class 名
2. 支持 `extends`
3. 支持字段与构造器
4. 支持更完整的 Frida `registerClass` 语义
