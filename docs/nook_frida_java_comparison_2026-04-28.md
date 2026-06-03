# Nook 与 Frida Java 能力对照

日期：2026-04-28

更新：2026-04-29

## 目的

这份文档总结当前 Nook 在 Java 侧已经具备的能力、相对 Frida 还缺失的能力，以及两者在实现方案和整体架构上的关键差异。

这里的“Frida”分成两个参照物：

1. 完整的 Frida 方向
2. 本地参考实现 `E:\Learn\my_program\all_my_hook\kanxue\rustFrida_upstream`

这两者不能完全等同。`rustFrida_upstream` 更像一个有代表性的上游思路切片，不等于完整 Frida Java API 的全部能力。

---

## 一、Nook 当前已经具备的 Java 能力

### 1. 生命周期与执行入口

Nook 已具备以下入口：

- `Java.ready(fn)`
- `Java.perform(fn)`
- `Java.performNow(fn)`
- `Java.vm.perform(fn)`
- `Java.vm.getEnv()`
- `Java.vm.tryGetEnv()`

对应实现主要在：

- `src/agent_runtime/js_runtime.cpp`

其中：

- `Java.performNow(fn)` 当前已经重构为委托到 `Java.vm.perform(fn)`
- `Java.perform(fn)` 当前是 `Java.ready(...)` 与 `Java.vm.perform(...)` 的组合层
- `Java.ready(fn)` 已经专门针对 Android `spawn` 场景做过多轮稳定性修正

### 2. Java 类、对象、方法调用

Nook 已具备以下常用 Frida 风格 Java 操作：

- `Java.use(className)`
- Java 方法直接调用
- `.overload(...)`
- `.implementation = function (...) { ... }`
- `callOriginal(...)`
- `$new(...)`

这意味着常见的 Frida 风格用法已经可跑：

- 直接 hook Java 方法
- 直接调用 Java 实例方法 / 静态方法
- 构造对象后继续调用实例方法

### 3. Java 对象辅助能力

Nook 当前已有：

- `Java.cast(objectWrapper, classWrapper)`
- `Java.retain(objectWrapper)`
- `$dispose()`
- 脚本卸载时的 Java 包装对象清理
- 基于 `Script.bindWeak(...)` 的弱清理路径

这部分说明 Nook 已不只是“能 hook”，而是开始具备相对完整的 Java wrapper 生命周期管理。

### 4. 类枚举与类加载器能力

Nook 当前已有：

- `Java.enumerateLoadedClasses({ onMatch, onComplete })`
- `Java.enumerateClassLoaders({ onMatch, onComplete })`
- `Java.setClassLoader(loader)`
- `Java.ClassFactory.get(loader)`

并且 `ClassFactory` 下面的主要能力也已经通：

- `factory.use(className)`
- `factory.choose(className, callbacks)`
- `factory.cast(objectWrapper, classWrapper)`
- `factory.retain(objectWrapper)`
- `factory.$new(className, ...args)`
- `factory.use(className).$new(...args)`
- `factory.openClassFile(path).load()`

这已经覆盖了非常实用的一条 Frida 工作流：

- 先枚举 class loader
- 再选择目标 loader
- 再通过 `ClassFactory.get(loader)` 做 `use / choose / $new / cast / retain / openClassFile`

### 5. 动态注册 Java 类

Nook 当前已有：

- `Java.registerClass(spec)`

当前已验证的重点能力是：

- interface / listener 风格代理对象
- `$new()` 创建实例
- Java 回调进入 JS
- method declaration object
- method declaration array
- same-name multi-signature dispatch

它已经足够支撑这类实际场景：

- `Runnable`
- `OnClickListener`
- 其他接口回调对象

但这仍然不是完整动态类系统。

当前还需要明确两点：

- 底层仍然是 `Proxy.newProxyInstance(...)` + `InvocationHandler`
- `fields` / `superClass` 现在会显式拒绝，而不是静默接受

### 6. 主线程辅助

Nook 当前已有：

- `Java.isMainThread()`
- `Java.scheduleOnMainThread(fn)`

这部分是复用现有能力在 JS bootstrap 层组合出来的，不是新增一整套 native 主线程调度框架。

### 7. Java 数组能力

Nook 当前已有：

- `Java.array(typeName, elements)`

并且已经验证通过：

- primitive arrays
  - `int[]`
  - `boolean[]`
  - `byte[]`
  - `short[]`
  - `char[]`
  - `long[]`
  - `float[]`
  - `double[]`
- object arrays
- multi-dimensional arrays
- array mutation
- reference array 协变
  - 例如 `String[] -> CharSequence[]`
  - 例如 `String[] -> Object[]`

这部分已经足够支撑很多 Frida 常见脚本里的数组参数传递。

### 8. `Java.vm.getEnv()` / `Env` 能力

Nook 当前已经具备的 `Env` helper 包括：

- `exceptionCheck()`
- `exceptionOccurred()`
- `exceptionClear()`
- `findClass()`
- `getObjectClass()`
- `getSuperclass()`
- `isAssignableFrom()`
- `isSameObject()`
- `isInstanceOf()`
- `newStringUtf()`
- `getStringUtfChars()`
- `releaseStringUtfChars()`
- `newGlobalRef()`
- `deleteGlobalRef()`
- `newWeakGlobalRef()`
- `deleteWeakGlobalRef()`

这一层已经从“只有 `JNIEnv*` 指针”推进到了“有一组真正可用的 JNI 辅助原语”。

---

## 二、对比 Frida：已经对齐的能力

如果从“用户写脚本时的常见工作流”看，Nook 目前已经对齐了很大一块：

### 1. 常见 Java hook 路径

已经可以稳定覆盖：

- `Java.ready(...)`
- `Java.perform(...)`
- `Java.use(...)`
- `.implementation = ...`
- 直接调用 hook 后的方法包装器

### 2. 类加载器工作流

已经可以覆盖：

- `enumerateClassLoaders()`
- `ClassFactory.get(loader)`
- `factory.use(...)`
- `factory.choose(...)`
- `factory.$new(...)`
- `factory.cast(...)`
- `factory.retain(...)`
- `factory.openClassFile(...).load()`

这条链路是 Frida Java 实战里非常核心的一条线。

### 3. Java 对象生命周期常用路径

已经具备：

- `retain`
- 显式 `dispose`
- unload 清理
- weak cleanup 基础设施

说明 Nook 已经不只是“API 名字像 Frida”，而是在向 Frida 的对象生命周期模型靠拢。

### 4. `Java.vm` 与 `Env` 分层

已经实现：

- `Java.vm.perform(...)`
- `Java.vm.getEnv()`
- `Java.vm.tryGetEnv()`
- 一组逐步扩展出来的 `Env` helper

这让 Nook 开始具备 Frida 那种更清晰的分层：

- 生命周期层
- VM 执行层
- JNI Env helper 层

---

## 三、对比 Frida：当前仍明显缺失或只部分对齐的能力

### 1. `Env.newLocalRef()` / `Env.deleteLocalRef()`

这是当前最明确的“不能直接照搬”的缺口。

不是因为还没做，而是因为已经验证：

- 当前 Nook 架构下直接暴露这一对 API 是不安全的

原因见后面的架构差异分析。

### 2. 更完整的 JNI `Env` 原语

相对完整 Frida 方向，Nook 当前还缺：

- `newLocalRef()`
- `deleteLocalRef()`
- `ensureLocalCapacity()`
- `pushLocalFrame()`
- `popLocalFrame()`
- `monitorEnter()`
- `monitorExit()`
- 更多 method/field/ref-type 级原语

其中：

- `newLocalRef/deleteLocalRef`、`pushLocalFrame/popLocalFrame`、`monitorEnter/monitorExit`
  都已经确认不能按当前 Nook `Env` 架构直接暴露
- 因此这里真正剩下的是：
  - `ensureLocalCapacity()` 这类是否值得做的低优先项
  - 更多确实跨调用安全的 helper
  - 更细的 method/field/ref-type 辅助

### 3. 更完整的 `registerClass` 语义

当前 Nook 的 `Java.registerClass(spec)` 已可用，但还不等于完整 Frida 语义：

- 现在更偏 interface/listener 代理
- 已支持 method declaration object / array / multi-signature dispatch
- 还不是完整动态 Java 类生成体系
- 当前明确不支持：
  - `fields`
  - `superClass`

### 4. 更完整的 `ClassFactory`

虽然实用闭环已经很强，但仍不是“完整 Frida ClassFactory clone”。

当前缺口更偏这些方向：

- 更多工厂级配置语义
- 更完整的 class file / dex loading object model
- 更强的 loader 作用域细节对齐

### 5. `Java.array(...)` 的完整语义

当前 `Java.array(...)` 已经能用于实用调用，但仍不是完整 Frida 级别：

- 不是 live mutable array wrapper
- object array 语义仍偏“够用优先”
- mutation 已经补齐
- reference-array covariance 已经补到实用层
- 仍未对齐的是更完整的 live wrapper 语义

### 6. 更完整的弱引用 / 持久引用上层语义

当前已经有：

- `newGlobalRef/deleteGlobalRef`
- `newWeakGlobalRef/deleteWeakGlobalRef`

但还缺更高层行为，例如：

- weak ref liveness 辅助
- resurrection / reprobe 辅助
- 更完整的高级 ownership helper

这部分目前是“先暴露底层原语，再决定是否真的需要上层助手”。

---

## 四、对比本地 `rustFrida_upstream`

这一点要单独说，因为它和“完整 Frida”不是同一层级。

### 1. `rustFrida_upstream` 在 JNI `Env` helper 上未必比 Nook 更全

从本地参考实现里看到的 `jni_boot.js` 来看，它当前共享 env helper 主要有：

- `getObjectClass`
- `getSuperclass`
- `isSameObject`
- `isInstanceOf`
- `exceptionCheck`
- `exceptionOccurred`
- `exceptionClear`
- `readJString`
- `getClassName`
- `getObjectClassName`

参考文件：

- `quickjs-hook/src/jsapi/jni/jni_boot.js`

对比下来，Nook 当前在 `Env` 原语层并不比这个参考实现少，反而已经额外具备：

- `findClass`
- `newStringUtf`
- `getStringUtfChars`
- `releaseStringUtfChars`
- `newGlobalRef/deleteGlobalRef`
- `newWeakGlobalRef/deleteWeakGlobalRef`
- `getEnv/tryGetEnv`

### 2. `rustFrida_upstream` 更接近“JS 直接驱动 JNI 表”

本地参考实现的风格是：

- JS 里维护 JNI 函数表索引
- JS 里按索引取 `JNIEnv` vtable 地址
- JS 里直接发起 `_callJni(...)`

这一点与 Nook 的实现路线差别很大。

### 3. `rustFrida_upstream` 的 Java bootstrap 也更 JS-heavy

本地参考实现里可以看到：

- `Java.ready(...)`
- `Java.classLoaders()`
- `Java.findClassWithLoader(...)`
- `Java.setClassLoader(...)`

这些都在 `java_boot.js` 里有很明显的 JS 侧主导痕迹。

也就是说：

- 它更像“先有低层通用 JNI 基建，再在 JS 里大量拼装 Java API”
- Nook 则更像“先有 runtime bridge，再在 JS 层做有限组合”

---

## 五、Nook 与 Frida 的实现方案差异

### 1. Nook：C++ runtime 主导，JS bootstrap 组合

Nook 当前的核心路线是：

- C++ runtime 负责大部分真实能力
- QuickJS bootstrap 负责把 runtime 原语组合成更像 Frida 的公开 API

例如：

- `Java.performNow(fn)` 委托给 `Java.vm.perform(fn)`
- `Java.perform(fn)` 组合 `Java.ready(...)` 与 `Java.vm.perform(...)`
- `Java.scheduleOnMainThread(fn)` 复用 `Java.registerClass(...)`、`Java.use(...)`、`$new(...)`

因此 Nook 的特点是：

- 真实能力更集中在 runtime
- 桌面测试和 Android 测试路径更容易统一
- 每补一个能力，通常要同时补：
  - runtime
  - bootstrap
  - host test hook
  - smoke test

### 2. Frida / 本地参考实现：更偏 JS 直连 JNI

本地 `rustFrida_upstream` 的实现路线更偏：

- JS 直接拿 `JNIEnv*`
- JS 直接解析 JNI function table
- JS 直接调用底层 JNI entry

这种路线的特点是：

- 低层暴露速度快
- 新增 JNI helper 的成本低
- 灵活

但对应也意味着：

- 很多语义天然更“原始”
- 要靠上层 bootstrap 或脚本编写习惯约束正确用法

### 3. Nook 更强调“runtime 统一约束”

Nook 当前很多 Java 行为并不是单纯暴露原始 JNI，而是加了运行时约束和桥接：

- Java wrapper 生命周期控制
- loader-aware wrapper construction
- invoke 参数解析
- overload 推断
- owned handle cleanup

这让 Nook 在行为上更稳定，但 API 扩展速度会比直接 JS 直连 JNI 慢。

---

## 六、架构设计上的本质差异

### 1. Nook 的 `Env` 是“runtime 代理型 Env”

这是最关键的差异。

在 Nook 当前架构里：

- `Env` 公开给脚本的是一个 helper wrapper
- 每次 `env.xxx()` 调用，底层都会重新进入一次 native/JNI 路径
- 真正的 JNI 操作发生时，runtime 会去拿当下有效的 `JNIEnv*`
- `env.handle` 更偏诊断/观察用途，不是一个可以随意长期持有并直接驱动所有 JNI 行为的“live env object”

这意味着：

- 持久型 JNI ref 可以安全做
- 帧局部型 JNI ref 不一定安全

### 2. Frida 风格更接近“当前线程真实 Env 的直接能力”

不管是完整 Frida 方向，还是你本地参考实现，其思路都更接近：

- 当前线程上拿到一个更直接的 `JNIEnv*`
- 然后以更低包装层级使用 JNI 表能力

所以很多原语天然更好暴露：

- local ref
- local frame
- monitor
- ref type

### 3. 这正是 `local ref` 现在不能直接照搬的原因

在 Nook 里已经验证过：

- `newLocalRef(...)` 在一次 JNI 调用里创建 local ref
- `deleteLocalRef(...)` 在下一次独立 JNI 调用里再消费这个 ref

这在当前架构下并不安全，因为：

- local ref 的生命周期天然依赖创建它的 JNI 调用帧
- Nook 当前的 `Env` helper 调用是“分离的多次 native 进入”

所以：

- `newGlobalRef/deleteGlobalRef` 可做
- `newWeakGlobalRef/deleteWeakGlobalRef` 可做
- `newLocalRef/deleteLocalRef` 不能按同样方式直接暴露
- `monitorEnter/monitorExit` 也不能按同样方式直接暴露

这不是“功能还没写完”，而是“当前架构决定不能这样写”。

---

## 七、当前对齐程度的结论

### 1. 从脚本使用体验看

Nook 现在已经不再只是“长得像 Frida”。

它已经具备一条相当完整的 Frida 风格 Java 使用路径：

- `ready / perform / performNow / vm.perform`
- `use / invoke / overload / implementation / callOriginal / $new`
- `choose / cast / retain / dispose`
- `enumerateClassLoaders / ClassFactory.get(loader)`
- `registerClass`
- `array`
- `vm.getEnv()` + 一组实用 `Env` helper

### 2. 从完整能力面对比看

Nook 仍然不是完整 Frida Java clone。

当前更准确的判断是：

- 已覆盖常见实战工作流的大半
- 已进入“可以继续按 Frida 方向精细补齐”的阶段
- 还没有到“API 和语义都可以认为完全同构”的程度

### 3. 从架构上看

Nook 和 Frida 不是同构实现。

更准确地说：

- Frida / 参考实现更偏“低层 JNI 能力尽量直接暴露”
- Nook 更偏“runtime 统一承接，再在 JS 层组合成 Frida 风格 API”

这两种架构都能走向类似的公开 API，但它们对一些能力的可实现性并不相同。

`local ref` 就是最典型的例子。

---

## 八、后续建议

如果继续按“向 Frida 看齐”的路线推进，建议优先级如下：

1. 先继续补“跨调用安全”的 `Env` helper，而不是再碰 `monitor/local ref/local frame`
2. 继续收紧并文档化 `registerClass` 的真实支持边界
3. 仅在出现真实兼容性缺口时再补 `Java.array(...)` 的更高阶语义
4. 持续做 spawn/ready、loader、overload 的稳定性回归
5. 只有在明确要重构 `Env` 执行模型时，才重新讨论 `local ref` / `monitor`

不建议当前直接硬补：

- `newLocalRef()`
- `deleteLocalRef()`
- `monitorEnter()`
- `monitorExit()`

除非先改变 `Env` 的底层执行模型。

---

## 九、一句话总结

Nook 当前已经具备“可用于真实 Frida 风格 Java 脚本”的一组核心能力，尤其是在 `ClassFactory`、`registerClass`、`Java.array`、`Java.vm.getEnv()` 与持久引用原语上已经形成闭环；但它与 Frida 的底层实现路线明显不同，是“runtime 代理型 Env + C++ bridge 主导”的架构，因此某些 Frida 语义不能直接照搬，最典型的就是 `local ref`、`local frame` 和 `monitor`。
## 2026-04-29 addendum

This addendum supersedes the earlier comparison conclusion that
`monitorEnter/monitorExit` were safe to expose in the current Nook `Env`
architecture.

Device verification disproved that assumption:

- `env.monitorEnter(obj)` succeeds
- `env.monitorExit(obj)` fails on the same object on the next helper call

Updated boundary relative to Frida:

- safe today:
  - global refs
  - weak global refs
  - string helpers
  - exception helpers
- not safe to expose directly in the current model:
  - local refs
  - local frames
  - `monitorEnter/monitorExit`

Reason:

- Frida-style monitor pairing assumes a stable live thread-local `JNIEnv*`
- Nook's current public `Env` is a helper facade where each `env.xxx()` is a separate JNI re-entry
- on Android this path uses temporary `JavaEnv` attach/detach lifetime
