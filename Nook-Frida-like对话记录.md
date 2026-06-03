## 问答记录

### `Java.perform` 是什么

在 Frida 风格脚本里，`Java.perform(fn)` 的核心含义不是“立刻执行这段 Java 代码”，而是：

- 等 Java 虚拟机可用
- 等当前线程拿到可用的 JNI 环境
- 等应用侧 ClassLoader 准备好
- 然后再执行传入的回调

所以它本质上是一个 Java 运行时就绪包装器。  
脚本作者写下 `Java.perform(function () { ... })`，真正依赖的是后面这层“等环境 ready 再进回调”的语义，而不是一个普通同步函数。

### Nook 里怎么处理 `Java.perform`

Nook 侧给脚本暴露出来的 `Java.perform` 是在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp) 里注入的启动脚本逻辑。

它的处理分两步：

- 如果 `Java._isClassLoaderReady()` 已经成立，就直接走 `Java.vm.perform(fn)`
- 如果还没 ready，就先通过 `Java.ready(fn)` 把回调挂进等待队列

这里的 `Java.ready(...)` 本质上是在维护一个 `readyCallbacks` 队列，并结合 `_isClassLoaderReady()`、`_isLifecycleReady()` 这些条件决定什么时候统一放行。

真正执行回调时，会落到原生侧的 `Java.vm.perform(...)`，对应的桥接入口是：

- `JsJavaVmPerform`
- `InvokeJavaVmPerformWithEnv(...)`

这层原生逻辑负责几件关键的事：

- 查询当前线程的 `JNIEnv*`
- 必要时附加到 Java VM
- 确保 Java Hook 相关环境已经初始化
- 在一个带作用域的 JNI 环境上下文里执行 JS 回调

也就是说，Nook 的 `Java.perform` 不是简单“调一下 JS 函数”，而是把“等待时机 + 准备 JNI 环境 + 再执行回调”这一整套流程包起来了。

### Frida 里怎么处理 `Java.perform`

本地 `frida` 目录里和 Java 相关的直接入口在：

- [`E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts`](E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts)

这一层本身很薄，只是把 `frida-java-bridge` 导出来。  
真正的 `Java.perform` 实现在上游 `frida-java-bridge` 里。

Frida 的核心思路和 Nook 类似，也是两段式：

- 先等应用 Java 运行时和主 ClassLoader 准备好
- 再通过 VM 层把线程 attach 到 JVM，拿到 `JNIEnv*` 后执行回调

区别在于，Frida 这一套是围绕 `frida-java-bridge` 的 `Runtime` / `VM` / `ClassFactory` 组织起来的，内部会维护待执行的 VM 操作队列，并结合 `ActivityThread`、应用生命周期等路径判断什么时候可以放行。

所以从语义上说，Nook 和 Frida 在 `Java.perform` 上其实是对齐的：  
都不是“立即执行”，而是“等 Java 世界可用之后再执行”。

### `Java.use` 做了什么

`Java.use('com.xxx.ClassName')` 的本质也不是“直接返回一个真实 Java 类对象”，而是：

- 根据类名构造一个脚本侧包装器
- 后续在访问字段、方法、构造函数时，再按需解析

也就是说，它返回的是一个“延迟解析的类代理”。

### Nook 里怎么处理 `Java.use`

Nook 侧 `Java.use(...)` 的原生入口同样在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp)，对应函数是 `JsJavaUse()`。

这层入口本身做的事很少：

- 读入类名参数
- 调用 `CreateJavaUseWrapper(ctx, class_name)` 构造 JS 包装对象

真正关键的是 `CreateJavaUseWrapper()`。  
它会生成一个 JS 工厂，返回形如 `new Proxy(target, { get(...) { ... } })` 的代理对象。

这个包装器里会先挂好一批元数据和缓存，例如：

- `$className`
- `__nookJavaReceiverHandle`
- `__nookJavaLoaderHandle`
- `__nookMethodCache`
- `__nookFieldCache`

后面每次脚本访问 `SomeClass.xxx` 时，不是一次性把整个类全展开，而是通过代理的 `get` 逻辑按顺序处理：

- 先看是不是包装器自身已有属性
- `class` 这种特殊属性走 `__nookJavaGetClassWrapper`
- 字段走 `__nookJavaResolveField`
- 其他情况按方法处理，动态构造 method wrapper

所以 Nook 的 `Java.use` 更像是在脚本层先搭好一个“类代理壳子”，真正解析字段和方法发生在第一次访问它们的时候。

### Frida 里怎么处理 `Java.use`

Frida 侧顶层 `Java.use(...)` 会进一步落到 `ClassFactory.use(...)`。  
它的总体语义和 Nook 一样，也是返回一个 JS 包装类，而不是把原始 JNI `jclass` 直接交给脚本。

可以把两者的差异简单理解成：

- Nook：更明显地把这套代理逻辑写成了启动脚本里的 `Proxy + 懒解析`
- Frida：由 `ClassFactory` 统一管理类包装、成员解析、重载选择等行为

但从使用者角度看，两边在 `Java.use` 上暴露的核心能力是一致的：

- 先拿到类包装器
- 再在访问成员时解析方法/字段
- 最终把 Java 世界映射成脚本里可操作的对象模型

### `overload(...)` 做了什么

`overload(...)` 的作用不是执行方法，而是从“同名方法集合”里挑出一个参数签名唯一确定的方法包装器。

比如：

- `MainActivity.get_random.overload()`
- `MainActivity.check.overload("int", "int")`

这两句的含义都是：先别直接调用 `get_random` / `check` 这个名字对应的方法组，而是先把目标重载明确下来，后面无论是调用还是挂 `implementation`，都只针对这一条签名。

也就是说，`overload(...)` 本质上是“方法分派收窄器”。

### Nook 里怎么处理 `overload(...)`

Nook 这部分逻辑仍然在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp) 里生成的那段 JS runtime 代码中。

在 `makeMethod(methodName, signature, overloadTypeNames, isStatic)` 里，每一个方法包装器都会自带一个：

- `method.overload = function () { ... }`

这层逻辑做的事情很直接：

1. 取出传进来的参数类型名列表，比如 `["int", "int"]`
2. 用这个类型列表生成缓存 key
3. 先查 `method.__nookOverloadCache`
4. 如果缓存里没有，就调用 `__nookJavaResolveOverloadSignature(className, methodName, typeNames, loaderHandle)`
5. 拿到解析结果后，再重新 `makeMethod(...)` 生成一个“已绑定唯一签名”的 method wrapper
6. 放进缓存，后续重复使用

所以 `overload(...)` 返回的不是原来的 method 对象本身，而是一个新的、更具体的方法包装器。

这个新包装器会带上更明确的元数据，比如：

- `$methodName`
- `$signature`
- `$overloadTypeNames`
- `$isStatic`

后面你再去做：

- `overload(...)(args...)`
- `overload(...).implementation = fn`

实际依赖的就是这组更具体的元数据。

### Nook 原生侧怎么解析重载

`method.overload(...)` 里调用的 `__nookJavaResolveOverloadSignature(...)`，对应的原生入口是：

- `JsJavaResolveOverloadSignature`

它会把 JS 传来的：

- 类名
- 方法名
- 参数类型名数组
- 可选的 loader handle

转换成原生侧参数，然后调用 `ResolveJavaMethodSignature(...)` 去解析真正的方法签名。

这层解析逻辑的关键点有几个：

- 先按实例方法去找
- 找不到再按静态方法去找
- 会遍历反射拿到的方法集合，而不是只看一个名字
- 会把脚本里传进来的类型名列表转换成期望的参数描述
- 对候选方法逐个做参数可赋值性匹配
- 如果匹配到多个，还会继续用 `ResolveMostSpecificJavaOverload(...)` 选最具体的那个

如果最后：

- 一个都匹配不上，就报 no match
- 匹配出多个且无法唯一收敛，就报 ambiguous
- 只剩一个唯一结果，就返回它的 JNI signature 和 `isStatic`

所以从实现上讲，Nook 的 `overload(...)` 不是简单拼字符串查表，而是走了一次“反射候选收集 + 参数匹配 + 最具体重载决议”的完整解析流程。

### `overload(...)` 和直接调用的关系

如果脚本只是写：

- `MainActivity.check(...)`

那底层会尝试根据实参做一次自动重载推断。  
但只要同名方法不止一个，或者参数推断不够稳定，就容易出现二义性。

这时候显式写：

- `MainActivity.check.overload("int", "int")`

本质上就是提前把“该走哪一个重载”固定下来。  
这样后面的调用、Hook 安装、`implementation` 赋值都会更稳，也更接近 Frida 用户平时的脚本写法。

### Frida 里 `overload(...)` 的思路

Frida 这边整体语义和 Nook 是一致的：  
`Java.use(...).someMethod` 先拿到的是一个“方法分派器”，它代表的是同名方法集合；调用 `.overload(...)` 后，才会收敛成某一个具体重载。

根据 `frida-java-bridge` 的实现组织方式，可以把它理解成：

- `ClassFactory` 先维护同名方法的候选集合
- 如果方法名对应多个重载，很多操作都要求你先 `.overload(...)`
- `.overload(...)` 会按你给的参数类型名选择一个具体 overload wrapper
- 后续的 `implementation`、调用、属性访问，都是针对这个具体 overload wrapper 进行

也就是说，Frida 和 Nook 在这里的核心模型其实是一样的：

- 先有“方法组”
- 再由 `overload(...)` 选中“唯一方法”
- 最后在这个唯一方法上安装实现或发起调用

### `implementation = function (...) {}` 做了什么

当脚本写下：

```js
check.implementation = function (left, right) {
  console.log("lab:frida-0x1:hit:check:left=" + left + ":right=" + right);
  return this.check.callOriginal(left, right);
};
```

这行代码做的不是“单纯给一个 JS 对象赋属性”，而是：

- 把这个 JS 函数登记成某个 Java 方法的替换实现
- 触发底层 Java Hook 安装
- 让后续目标方法命中时，控制流先进入脚本回调

也就是说，`implementation` 在这里是一个带副作用的属性 setter，不是普通字段。

### Nook 里怎么处理 `implementation` 赋值

Nook 这层逻辑还是在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp) 生成的 method wrapper 里。

`makeMethod(...)` 给每个方法包装器都定义了：

```js
Object.defineProperty(method, 'implementation', {
  get() {
    return this.__nookImplementation;
  },
  set(fn) {
    if (typeof fn !== 'function') {
      throw new TypeError(...);
    }
    this.__nookJavaHookId = __nookJavaInstallImplementation(this, fn);
    this.__nookImplementation = fn;
  }
});
```

所以赋值瞬间实际发生的是：

1. 校验右边必须是函数
2. 调 `__nookJavaInstallImplementation(this, fn)`
3. 返回一个 `hookId`
4. 把这个 `hookId` 挂到当前 method wrapper 的 `__nookJavaHookId`
5. 再把 JS 函数本身保存到 `__nookImplementation`

这意味着 method wrapper 从这一刻开始，不只是“描述某个 Java 方法”，而是已经和一个真实安装好的 Hook 绑定起来了。

### `__nookJavaInstallImplementation(...)` 背后做了什么

对应的原生入口是：

- `JsJavaInstallImplementation`

它会先从 method wrapper 上把安装 Hook 所需的元数据取出来，包括：

- `$className`
- `$methodName`
- `$signature`
- `$isStatic`
- `__nookJavaLoaderHandle`

然后拼出一个 `JavaJsHookRequest`：

- `class_name`
- `method_name`
- `signature`
- `loader_handle`
- `is_static`
- `deferred = true`

这里 `deferred = true` 很关键。  
它表示这次安装走的是 deferred hook 流程，也就是允许目标方法还没完全 ready 时先注册，后面再由底层时机成熟后完成接管。

随后 `JsJavaInstallImplementation` 会调用：

- `InstallJavaJsHook(request, state.java_hook_installer_dependencies, &record, &error_message)`

如果安装成功，它还会把这个 JS 回调函数保存到当前脚本的运行时回调表里：

- `state.java_hook_callbacks[state.current_script_id][record.hook_id] = fn`

所以这一层做了两件事：

- 把 Hook 安装到 Java Hook 子系统
- 把 JS 回调和 `hookId` 关联起来，供后续命中时回调

### Nook 底层 Java Hook 是怎么真正装上的

`InstallJavaJsHook(...)` 在 [`src/agent_runtime/nook_java_js_bridge.cpp`](./src/agent_runtime/nook_java_js_bridge.cpp) 里。

它的大体流程是：

1. 校验请求是否合法
2. 生成新的 `hook_id`
3. 规范化一些特殊方法名，比如把 `$init` 转成 `<init>`
4. 调具体安装逻辑
5. 把 `JavaJsHookRecord` 存进全局注册表

在 Android 默认实现里，真正落到：

- `DefaultInstallJavaJsHook(...)`

而这层最终调用的是：

- `NookJavaHookHookDeferredWithLoader(...)`

也就是说，`implementation = fn` 赋值最终会走到 Nook 底层已有的 Java Hook 能力，把某个类、某个方法、某个签名真正挂上。

安装成功后，Nook 会得到：

- `installed_hook_id`
- `callback_slot`
- `hook_id`

这些信息会一起保存在 `JavaJsHookRecord` 里，后面调用原方法、卸载 Hook、转发回调时都要用到。

### 目标方法命中后怎么回到这段 JS

目标 Java 方法被调用时，底层 Java Hook 不会直接执行你写的 JS 函数。  
中间还会经过一层“hook id -> script callback”的分发。

大体链路是：

- 底层 Java Hook 命中
- 通过 callback slot 找到对应 `hook_id`
- 进入 `DispatchJavaHookInvocationToRuntime(...)`
- 在运行时里找到 `state.java_hook_callbacks[script_id][hook_id]`
- 再把参数封装成 JS 值，真正调用你写的 `implementation`

所以 `implementation = fn` 的本质，不只是“替换方法逻辑”，而是把：

- 底层 Java Hook
- 运行时回调分发表
- 当前 method wrapper

这三者串成了一条完整调用链。

### Frida 里 `implementation` 的思路

Frida 这边语义也是一样的：  
给某个具体 overload wrapper 赋 `implementation`，本质上是在告诉 `frida-java-bridge`：

- 这个方法以后不要直接走原始实现
- 命中时先桥接到 JS/NativeCallback
- 在桥里再由用户脚本决定是否调用原方法

Frida 的实现组织在 `ClassFactory` / method wrapper 这一套里。  
它同样把 `implementation` 做成一个有副作用的属性，而不是普通字段。

可以把两边对齐理解成：

- `overload(...)` 负责“选中哪一个方法”
- `implementation = fn` 负责“把这个方法真正接管掉”

区别主要在实现组织方式：

- Nook：你现在能直接看到 `implementation` setter 调 `__nookJavaInstallImplementation`，再接到底层 Java Hook 安装函数
- Frida：由 `frida-java-bridge` 内部统一管理具体 overload 的替换、回调桥接和清理

### `callOriginal(...)` 做了什么

当脚本里写：

```js
return this.check.callOriginal(left, right);
```

这句的含义不是“再通过普通 Java 调用走一遍 `check`”，而是：

- 在当前 Hook 回调上下文里
- 直接调用这个被 Hook 方法对应的原始实现
- 并且绕过当前这层 JS 替换逻辑，避免再次进入同一个 `implementation`

所以 `callOriginal(...)` 的本质不是普通方法调用，而是“从当前 Hook 上下文回到原始实现”的专用入口。

### Nook 里 `callOriginal(...)` 是怎么挂进去的

这个能力不是一开始就长期挂在 `Java.use(...)` 返回的方法包装器上的。  
Nook 是在每次 Java Hook 命中、准备进入 JS 回调时，动态构造一个当前回调专用的 receiver。

对应逻辑在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp) 的：

- `CreateJavaHookCallbackReceiver(...)`

它做的事情是：

1. 先为当前 `thiz` 构造一个新的 Java wrapper
2. 取出当前被 Hook 的方法对象
3. 用当前 `hook_id` 生成一个 `callOriginal` 函数
4. 把这个函数挂到方法对象上

这里的关键代码路径是：

- `JS_NewCFunctionData(ctx, JsJavaCallOriginal, ..., &hook_id_value)`
- `JS_SetPropertyStr(ctx, method, "callOriginal", call_original)`

同时它还会给当前 receiver 和 method 打上一些活动上下文元数据，比如：

- `__nookActiveHookMethodName`
- `__nookActiveHookSignature`
- `__nookJavaHookId`

所以你在 `implementation` 里写的 `this.check.callOriginal(...)`，其实拿到的是“当前这次 Hook 回调专用”的那个 `callOriginal`，而不是一个全局静态函数。

### `JsJavaCallOriginal` 背后做了什么

`callOriginal(...)` 对应的原生入口是：

- `JsJavaCallOriginal`

它会先从 `func_data` 里取出当前绑定的 `hook_id`，然后把 JS 参数逐个转成 `JavaJsValue`，最后调用：

- `CallOriginalJavaJsHook(hook_id, args, arg_count, ...)`

所以 `callOriginal(...)` 的本质可以理解成：

- JS 层只是一个薄封装
- 真正的“回原方法”逻辑在 `CallOriginalJavaJsHook(...)`

### 为什么 `callOriginal(...)` 只能在当前 Hook 回调里调用

这点很关键。  
Nook 默认实现里，`callOriginal` 依赖的是“当前线程正在处理哪一次 Java Hook 调用”这个活动上下文。

在 Java Hook 命中时，Nook 会先创建一个：

- `ActiveJavaJsInvocation`

里面会记录：

- `hook_id`
- `installed_hook_id`
- `JNIEnv*`
- `thiz`
- 当前方法签名信息

然后把它压到当前线程的活动调用栈里：

- `PushActiveJavaJsInvocationForCurrentThread(invocation)`

等 JS 回调执行完，再：

- `PopActiveJavaJsInvocationForCurrentThread()`

所以 `callOriginal(...)` 并不是随时都能调。  
它必须发生在“当前线程、当前 Hook 回调、当前活动调用还在栈上”的时刻。

这也是为什么 `DefaultCallOriginalJavaJsHook(...)` 一上来就检查：

- 当前线程是否存在活动调用
- `active_invocation.hook_id` 是否和当前 `record.hook_id` 一致

如果不在这个上下文里，就会报：

- `java callOriginal must run during the matching hook callback`

### Nook 底层怎么真正回到原始 Java 方法

`CallOriginalJavaJsHook(...)` 会先通过 `hook_id` 找到对应的 `JavaJsHookRecord`，然后走默认实现：

- `DefaultCallOriginalJavaJsHook(...)`

这层会做几件关键的事：

1. 读取当前线程上的 `ActiveJavaJsInvocation`
2. 拿到当前 `installed_hook_id`
3. 确认参数个数和当前签名匹配
4. 把 JS 参数按当前 Java 签名转换成 `NookJavaHookValue`
5. 调底层：
   - `nook::java_hook_internal::CallOriginalNow(...)`
6. 把原始返回值再转回 `JavaJsValue`
7. 最后回到 JS

也就是说，真正执行原方法的是：

- `CallOriginalNow(...)`

而不是再去走一次普通 `Java.use(...).method(...)` 调用流程。

这是避免递归重入的关键。  
如果它只是“再普通调用一遍当前方法”，那很可能又会命中当前 Hook，再次进入 `implementation`，形成死循环。

### 和普通方法调用的区别

普通调用像这样：

```js
MainActivity.check(1, 2)
```

它走的是：

- method wrapper
- `__nookJavaInvoke(...)`
- Java 方法调用分发

而 `callOriginal(...)` 走的是：

- 当前 Hook 上下文
- `JsJavaCallOriginal`
- `CallOriginalJavaJsHook(...)`
- `CallOriginalNow(...)`

两条路径看起来都像“调用方法”，但底层语义完全不同：

- 普通调用：进入当前可见的方法入口
- `callOriginal`：绕过 Hook，直接进原始实现

### Frida 里 `callOriginal` 的对应语义

Frida 在 Java Hook 里的对应语义也是一样的。  
你在 `implementation` 里常见写法是：

```js
return this.check(arg0, arg1);
```

或者在某些包装语义下回到原重载实现。  
核心目标都一致：

- 当前 Hook 已经接管了方法入口
- 但脚本仍然需要一个“回原始实现”的路径
- 而且这条路径必须避免再次进入当前 JS 替换逻辑

Nook 这里把这件事显式做成了 `callOriginal(...)`，实现上更直白，也更容易从代码里看出“这是专门给当前 Hook 回调使用的原方法通道”。

### `frida-0x1` 这个脚本的完整执行流

如果把前面几层拼起来，[`tests/Test_Lab/nook-frida-labs/frida-0x1/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x1/script.js) 的运行过程可以按时间顺序理解成下面这样。

### 第 1 步：脚本先进入 `Java.perform(...)`

脚本加载后，最外层先执行：

```js
Java.perform(function () { ... });
```

这一步不会盲目立刻进回调，而是先等：

- Java VM 可用
- 当前线程有可用 `JNIEnv*`
- App 的 ClassLoader ready

等条件满足后，Nook 才真正执行 `perform` 里的这段回调。

### 第 2 步：`Java.use(...)` 拿到 `MainActivity` 的类包装器

进入 `perform` 回调后，先执行：

```js
var MainActivity = Java.use("com.ad2001.frida0x1.MainActivity");
```

这一步还没有真正去调用 `MainActivity` 的任何方法。  
它只是构造了一个脚本侧类代理，后面访问：

- `MainActivity.get_random`
- `MainActivity.check`

时，才会按需解析对应成员。

### 第 3 步：`overload(...)` 把两个目标方法收窄成唯一重载

接着脚本执行：

```js
var getRandom = MainActivity.get_random.overload();
var check = MainActivity.check.overload("int", "int");
```

这一步的含义是：

- 对 `get_random`，选中“无参数”的那个具体重载
- 对 `check`，选中参数为 `(int, int)` 的那个具体重载

到这里，`getRandom` 和 `check` 已经不再只是“方法名”，而是两个绑定了唯一签名的方法包装器。

### 第 4 步：给两个具体重载安装 `implementation`

然后脚本执行：

```js
getRandom.implementation = function () { return 5; };
check.implementation = function (left, right) {
  return this.check.callOriginal(left, right);
};
```

这一步是整个脚本真正把 Hook 装上的时刻。

对于每个 `implementation = fn`：

- method wrapper 的 setter 会触发 `__nookJavaInstallImplementation(this, fn)`
- 原生侧读取 `$className / $methodName / $signature / $isStatic`
- 组装 `JavaJsHookRequest`
- 调 `InstallJavaJsHook(...)`
- 最终落到 `NookJavaHookHookDeferredWithLoader(...)`

同时，JS 回调函数也会被按 `hook_id` 存进当前脚本的回调表。

所以到这一步结束时，实际上已经发生了两件事：

- 底层 Java Hook 已经注册
- 对应的 JS 回调也已经和 `hook_id` 建立关联

### 第 5 步：脚本打印 `installed`

两个 Hook 都安装完后，才执行：

```js
console.log("lab:frida-0x1:installed");
```

所以 `lab:frida-0x1:installed` 这个日志本质上表示：

- `get_random` 的 Hook 已挂好
- `check(int, int)` 的 Hook 已挂好

它不是“只是脚本跑到了这里”，而是“目标接管关系已经建立完成”。

### 第 6 步：App 运行到 `get_random()` 时先命中 JS Hook

后面当 App 正常执行到：

- `MainActivity.get_random()`

时，不会直接先走原始 Java 实现，而是会先进入 Nook 底层安装的 Java Hook。

这时的大体流程是：

- 底层 Hook 命中
- 根据 callback slot 找到 `hook_id`
- 构造当前回调专用 receiver
- 把命中参数封装成 JS 值
- 分发到当前脚本注册的 `getRandom.implementation`

于是脚本里的：

```js
getRandom.implementation = function () {
  console.log("lab:frida-0x1:hit:get_random");
  console.log("lab:frida-0x1:result:forced-random=5:expected-input=14");
  return 5;
};
```

会被执行。

### 第 7 步：`get_random()` 的返回值被直接改成 `5`

这时脚本没有调用 `callOriginal()`，而是直接：

```js
return 5;
```

所以这次 `get_random()` 调用的原始 Java 实现不会继续执行，返回值直接被 JS Hook 改成了 `5`。

也正因为这里把随机数强制改成了 `5`，后面用户只要输入 `14`，就能满足目标逻辑里依赖的校验条件。

### 第 8 步：后面执行到 `check(5, 14)` 时，再次命中第二个 Hook

当 App 后续执行到：

- `check(int, int)`

时，又会进入第二个 Hook。

此时脚本里的实现：

```js
check.implementation = function (left, right) {
  console.log("lab:frida-0x1:hit:check:left=" + left + ":right=" + right);
  return this.check.callOriginal(left, right);
};
```

先打印参数，所以你会看到：

- `lab:frida-0x1:hit:check:left=5:right=14`

这里的 `left=5`，正是前面 `get_random()` 被强制改写后的结果。

### 第 9 步：`this.check.callOriginal(left, right)` 回到原始 `check`

`callOriginal(...)` 不是普通方法调用，而是：

- 基于当前 Hook 的活动上下文
- 直接调用底层保存的原始 Java 方法实现

所以这一步不会再次进入当前 `check.implementation`，而是绕过 Hook，直接执行原始 `check(int, int)`。

于是这个 Hook 的行为就变成了：

- 先观察参数
- 再把控制流交还给原方法

也就是说，它做的是“观察 + 放行”，不是“完全替换”。

### 第 10 步：整个例子体现出的 Frida-like 工作流

把 `frida-0x1` 整个串起来看，它其实完整覆盖了一条很典型的 Frida Java Hook 工作流：

1. `Java.perform(...)` 解决 Java 运行时时机问题
2. `Java.use(...)` 拿到类包装器
3. `overload(...)` 选中唯一目标方法
4. `implementation = fn` 接管方法入口
5. 直接 `return` 可实现完全替换
6. `callOriginal(...)` 可实现“观察后放行”

这也是为什么这个例子特别适合当成“Nook 从 Hook 框架走向 Frida-like 工具”的起点。  
它不只是证明 Nook 能 Hook Java 方法，而是已经具备了 Frida 风格脚本工作流中最核心的一整套交互模型。

### `frida-0x2` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x2/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x2/script.js) 和 `frida-0x1` 最大的不同在于：

- `0x1` 的重点是 Hook Java 方法
- `0x2` 的重点是主动调用 Java 静态方法

脚本内容非常短：

```js
function hook(){
  var MainActivity = Java.use("com.ad2001.frida0x2.MainActivity");
  MainActivity.get_flag(4919);
}

function main(){
  Java.perform(function (){
      hook();
  })
}

setImmediate(main);
```

这里没有 `.implementation`，也没有 Hook。  
它验证的是另一类很关键的 Frida-like 能力：

- 脚本层主动触发 App 里本来不会主动执行的 Java 逻辑

### 这个例子想解决的问题

按照上游 Frida Labs 的题意，`MainActivity` 里有一个：

- `get_flag(int)`

它本身不会自动被调用，但只要传对参数 `4919`，就会走到 flag 逻辑。

所以这个题的关键不是“拦截某个现成执行流”，而是：

- 先拿到类
- 再直接调用静态方法
- 主动把目标逻辑跑起来

这正好能区分两种能力：

- 会 Hook 的框架：擅长改已有执行流
- Frida-like 工具：还能主动驱动目标代码执行

### `frida-0x2` 的完整执行流

按顺序看，这个脚本的执行过程很简单：

1. `setImmediate(main)` 先把入口排到事件循环里
2. `main()` 里进入 `Java.perform(...)`
3. 等 Java VM / `JNIEnv*` / ClassLoader ready
4. 执行 `hook()`
5. `Java.use("com.ad2001.frida0x2.MainActivity")` 拿到类包装器
6. 直接执行 `MainActivity.get_flag(4919)`
7. 底层完成 Java 静态方法调用
8. 目标 App 内部 flag 逻辑被主动跑起来

所以这个例子核心不是“等某个函数命中”，而是“我从脚本里主动把它调起来”。

### Nook 里 `MainActivity.get_flag(4919)` 是怎么跑起来的

这里的 `MainActivity.get_flag(4919)` 看起来像普通 JS 函数调用，但本质上调用的是 method wrapper。

在 Nook 的 JS runtime 里，`makeMethod(...)` 生成的方法包装器本体大致是：

```js
function method() {
  return __nookJavaInvoke.apply(null, [method].concat(args));
}
```

也就是说，当脚本执行：

```js
MainActivity.get_flag(4919);
```

时，真正进入的是：

- `__nookJavaInvoke(...)`

对应的原生入口是：

- `JsJavaInvoke`

### `JsJavaInvoke` 做了什么

`JsJavaInvoke` 主要做三件事：

1. 从 method wrapper 上解析方法元数据
2. 把 JS 实参转换成 `JavaJsValue`
3. 如果签名还没明确，就先做重载解析
4. 最后调用 `InvokeJavaMethod(...)`

这里要注意一点：

- `frida-0x2` 里没有显式写 `.overload(...)`

所以 Nook 需要根据实参 `4919` 自动推断应该调用哪个重载。

它会先给这个参数生成一组候选类型，比如 `4919` 这种整数会尝试：

- `int`
- `long`
- `float`
- `double`
- `java.lang.Integer`
- `java.lang.Long`
- `java.lang.Number`
- `java.lang.Object`

然后通过 `ResolveJavaMethodSignatureFromCandidates(...)` 一层层尝试，直到解析出一个可用的唯一签名。

如果当前方法包装器最开始不是静态方法，而当前 receiver 又是空句柄，它还会再补一次：

- 把 `record.is_static = true`
- 再尝试按静态方法解析

这也是为什么 `MainActivity.get_flag(4919)` 这种静态调用可以直接工作。

### 最后怎么真正调用到 Java 方法

当签名解析完成后，`JsJavaInvoke` 会调用：

- `InvokeJavaMethod(record, receiver_handle, ...)`

而在 Android 默认实现下，这层会继续落到真正的 Java 方法调用逻辑里。

所以从调用链上看，`frida-0x2` 验证的是：

- `Java.use(...)` 能拿到类包装器
- method wrapper 能像函数一样直接调用
- Nook 能自动做 Java 重载解析
- Nook 能区分并支持静态方法调用
- 最终能把脚本层的“主动调用”落到真实 Java 执行

### 为什么 `frida-0x2` 很重要

这个题表面上很简单，但它代表的能力非常关键。  
因为一旦工具具备了“主动调用 Java 方法”的能力，分析方式就会立刻变化：

- 不用再等 App 自己走到某条路径
- 不用只能被动观察
- 可以自己准备参数
- 可以自己触发隐藏逻辑
- 可以把 App 里内部函数直接当成脚本 API 使用

从这个角度看，`frida-0x2` 已经不只是“支持一段 Frida 脚本”这么简单，  
它其实是在证明 Nook 的 Java 侧脚本运行时已经开始具备 Frida 那种“把 App 内部逻辑转成可交互对象”的味道了。

### `frida-0x2` 再往里一层：Nook 对静态方法是怎么处理的

`frida-0x2` 最值得细讲的一点，其实是：

- 脚本里只是写了 `MainActivity.get_flag(4919)`
- 但 Nook 最后需要把它识别成一次“静态 Java 方法调用”

而这件事并不是在 `Java.use(...)` 那一步就完全决定好的。

### 类包装器上的方法默认并不是先天静态的

在 Nook 的 `CreateJavaUseWrapper()` 里，访问一个还没缓存的方法名时，会直接构造：

```js
method = makeMethod(canonicalMethodName, undefined, undefined, false);
```

注意最后那个 `false`。  
也就是说，像：

```js
MainActivity.get_flag
```

第一次被取出来时，得到的是一个：

- `$signature` 还没定
- `$isStatic` 先是 `false`
- `__nookJavaReceiverHandle` 来自当前 wrapper

的方法包装器。

而 `Java.use("com.ad2001.frida0x2.MainActivity")` 返回的是“类 wrapper”，不是实例 wrapper。  
它的 `currentReceiverHandle` 本身就是：

- `'0x0'`

所以 `MainActivity.get_flag` 这个 method wrapper 初始状态其实可以理解成：

- 这是一个挂在类 wrapper 上的方法
- 但还没有通过签名解析正式确认它是不是静态方法

### Nook 怎么从“类 wrapper 上的方法”推断成静态调用

当脚本真正执行：

```js
MainActivity.get_flag(4919)
```

时，会进入 `JsJavaInvoke`。

这时原生侧先从 method wrapper 上解析出：

- `record.class_name = "com.ad2001.frida0x2.MainActivity"`
- `record.method_name = "get_flag"`
- `record.signature = ""`
- `record.is_static = false`
- `receiver_handle = 0`

这里的关键状态是：

- `record.is_static` 还是 `false`
- 但 `receiver_handle == 0`

也就是说，从脚本层元数据看，它暂时还像“未定的实例方法包装器”；  
但从调用位置看，它又明显不是一个实例对象在调方法，因为根本没有 receiver。

### `JsJavaInvoke` 的静态回退逻辑

这时 `JsJavaInvoke` 会先尝试按当前记录去做重载解析。  
如果因为方法本来就是静态的而没解析成功，它会触发一段非常关键的回退逻辑：

```cpp
if (!resolved && receiver_handle == 0u && !record.is_static) {
    JavaJsMethodRecord static_record = record;
    static_record.is_static = true;
    resolved = ResolveJavaMethodSignatureFromCandidates(static_record, ...);
    if (resolved) {
        record.is_static = true;
        record.signature = static_record.signature;
    }
}
```

这段逻辑的意思很明确：

- 先按实例方法试一次
- 如果失败，而且当前没有 receiver
- 那就再按静态方法试一次
- 一旦静态解析成功，就把当前 method record 正式翻成静态方法

所以对 Nook 来说，`MainActivity.get_flag(4919)` 这种写法并不是“类 wrapper 上的方法天然静态”，而是：

- 先创建一个通用方法包装器
- 在实际调用那一刻根据上下文和解析结果收敛成静态调用

这是一个很务实的设计。

### 为什么这个设计对 Frida-like 体验很重要

从脚本作者的角度，大家更在意的是：

- 能不能像 Frida 一样直接写 `Java.use(...).method(...)`
- 而不是先手动声明“这是静态方法”

Nook 这里为了保证脚本体验足够接近 Frida，没有要求脚本作者先显式区分：

- `MainActivity.get_flag(...)` 是静态
- `someInstance.foo(...)` 是实例

而是把这个判断尽量留到调用分发阶段自动处理。

这意味着 Nook 在接口层更偏向：

- 先让脚本写法自然
- 再在桥接层补上静态/实例判定

### 最终 JNI 调用时静态和实例是怎么真正分叉的

当 `JsJavaInvoke` 已经把 `record.is_static` 和 `record.signature` 解析好之后，会进入：

- `InvokeJavaMethod(...)`

Android 默认实现里，最终落到 `DefaultInvokeJavaMethod(...)`。

这层对静态和实例的分叉非常直接：

1. 如果不是静态方法，就要求必须有 `receiver_handle`
2. 如果是静态方法，就不需要 receiver
3. 解析出 `jclass` 和 `jmethodID`
4. 根据返回值类型走不同 JNI 调用

最关键的是这里的调用分支：

- 静态方法：
  - `CallStaticVoidMethodA`
  - `CallStaticIntMethodA`
  - `CallStaticObjectMethodA`
  - 等等
- 实例方法：
  - `CallVoidMethodA`
  - `CallIntMethodA`
  - `CallObjectMethodA`
  - 等等

也就是说，Nook 对静态方法的处理不是抽象层面“逻辑上认为它是静态”，  
而是最终在 JNI 调用点上真的分到了 `CallStatic*MethodA` 这条路径。

### `frida-0x2` 里参数 `4919` 还触发了什么

因为脚本没有显式写：

- `.overload("int")`

所以 Nook 还得顺手做一次自动重载推断。

对 `4919` 这种 JS 数字，Nook 会生成一组候选类型，比如：

- `int`
- `long`
- `float`
- `double`
- `java.lang.Integer`
- `java.lang.Long`
- `java.lang.Number`
- `java.lang.Object`

然后逐个尝试去匹配 Java 方法签名。  
在 `frida-0x2` 这种场景里，最终会收敛到目标静态方法 `get_flag(int)`。

所以这个例子表面只是一句：

```js
MainActivity.get_flag(4919);
```

但 Nook 背后其实同时完成了两次判断：

- 这是静态方法还是实例方法
- 这个数字该对应哪一种 Java 参数类型

### Frida 里对静态方法的处理语义

Frida 用户视角下，对静态 Java 方法的调用也是同样自然的：

```js
var MainActivity = Java.use("com.ad2001.frida0x2.MainActivity");
MainActivity.get_flag(4919);
```

脚本作者通常不需要额外写一个“static invoke”专用 API。  
`Java.use(...)` 返回的类包装器本身就同时承担了：

- 类级别静态成员访问
- 对象构造与实例方法入口的桥接

本地 `frida` 目录里对应 Java 入口只有一层很薄的导出：

- [`E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts`](E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts)

真正实现是在 `frida-java-bridge` 里。  
结合它的整体模型，可以把 Frida 这边理解成：

- `Java.use(...)` 返回的是 `ClassFactory` 产出的类包装器
- 同名成员在 wrapper 上会区分静态与实例语义
- 真正调用时也会解析 overload，并选定具体 method wrapper
- 对静态方法，不需要实例 receiver，就可以直接发起调用

所以在“调用静态方法”这件事上，Nook 和 Frida 的外部语义其实是对齐的：

- 都允许直接在类包装器上调用
- 都会在桥接层处理重载和静态/实例分发
- 都把底层 Java 方法变成脚本里像普通对象一样可调用的成员

### `frida-0x2` 真正证明的能力

如果把这题讲细一点，它证明的其实不是一句“支持静态方法调用”，而是 Nook 已经具备下面这一整套能力组合：

- 类 wrapper 和实例 wrapper 的统一对象模型
- 方法包装器的延迟解析
- 调用时自动判断静态/实例语义
- JS 数字到 Java 参数类型的候选推断
- 重载解析后落到真实 JNI 静态调用

这几层一起成立，`MainActivity.get_flag(4919)` 才会表现得像一句很自然的 Frida 脚本。  
而这种“看起来简单，背后桥接链路完整”的能力，正是 Frida-like 工具最有价值的地方之一。

### 为什么 Hook 框架里静态方法和普通方法需要额外区分

对 Hook 框架来说，静态方法和实例方法不是“语法长得不一样”这么简单，而是底层调用模型就不一样。

最核心的区别是：

- 实例方法调用时，一定有一个 `this` / receiver
- 静态方法调用时，没有对象实例，只有类本身

这会直接影响三件事：

1. Hook 安装时如何定位目标方法  
   同名方法里，静态和实例方法可能同时存在，不能只靠方法名判断。

2. 调用时怎么传参  
   实例方法底层需要 `receiver`；静态方法不需要。

3. 最终 JNI/ART 调用入口怎么选  
   实例方法走 `Call<Type>MethodA`，静态方法走 `CallStatic<Type>MethodA`。

所以如果一个 Hook 框架不额外处理“这个方法到底是不是静态”，那就会出现几类典型问题：

- 该传 receiver 时没传，调用失败
- 不该传 receiver 却按实例方法处理，调用失败
- 重载解析选错目标
- Hook 安装到了同名但语义不同的方法上

### Nook 怎么判断是不是静态方法

Nook 不是在 `Java.use(...)` 那一步就把所有方法的静态/实例属性完全定死。  
它更偏向“延迟判断”。

大致分两种情况：

1. 显式重载解析时  
   比如 `.overload(...)` 或枚举 declared methods 时，会直接走反射，读取 Java `Method` 的 modifiers，再用 `Modifier.isStatic(...)` 判断。

2. 直接调用时  
   比如 `MainActivity.get_flag(4919)` 这种没显式 `.overload(...)` 的调用，Nook 会在调用时结合：
   - 当前 `receiver_handle` 是不是 `0`
   - 当前按实例方法解析是否失败
   
   来做一次静态回退判断：
   - 先按实例方法试
   - 如果失败且没有 receiver
   - 再按静态方法试
   - 如果成功，就把当前调用正式认定为静态方法

所以 Nook 的判断逻辑不是“只看一个标志位”，而是：

- 反射信息
- 当前 wrapper 所处上下文
- 当前是否存在 receiver
- 当前签名解析是否成立

这几层一起决定的。

### Nook 底层原理是什么

底层原理其实很朴素，就是把脚本层的“像普通对象调用一样”的写法，翻译成 Java 世界里真正不同的两类调用模型。

在 Nook 里，method wrapper 上会带：

- `$isStatic`
- `__nookJavaReceiverHandle`
- `$signature`

调用时先解析这些元数据，形成 `JavaJsMethodRecord`。  
如果签名还没定，就先做一次重载解析；如果当前没有 receiver 且实例解析失败，就再按静态方法解析一次。

一旦 `record.is_static` 最终确定下来，后面的底层调用就分叉了：

- 实例方法：要求必须有 receiver，并走 `Call<Type>MethodA`
- 静态方法：不需要 receiver，并走 `CallStatic<Type>MethodA`

所以从本质上说，所谓“额外处理静态方法”，其实就是在桥接层补上 Java 语义里本来就存在的那条分叉：

- 一条是“对象上的行为”
- 一条是“类上的行为”

Nook 做的事情，就是把这条分叉从底层 JNI/ART 一直向上托举到脚本层，让你最终可以用 Frida 风格的写法自然地操作它。
### `frida-0x3` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x3/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x3/script.js) 这一题的重点，从方法调用切到了字段访问。

脚本是：

```js
Java.performNow(function () {
  var Checker = Java.use("com.ad2001.frida0x3.Checker");
  var before = Checker.code.value;
  Checker.code.value = 512;
  console.log("lab:frida-0x3:result:code-before=" + before + ":code-after=" + Checker.code.value);
  console.log("lab:frida-0x3:installed");
});
```

它验证的不是：

- Hook 方法
- 调用方法

而是：

- 读取 Java 静态字段
- 写入 Java 静态字段

也就是说，`0x3` 证明的是 Nook 的 Java 桥接已经不只覆盖“可调用成员”，还开始覆盖“可读写状态”。

### `Java.performNow(...)` 在这里的含义

这个例子没有用 `Java.perform(...)`，而是用了：

- `Java.performNow(...)`

它的语义可以理解成：

- 直接在当前可用的 Java 环境里立刻执行
- 不再像 `perform(...)` 那样先挂 ready 队列等待 ClassLoader/lifecycle 条件

因为 `0x3` 做的事情只是直接操作一个静态字段，不依赖复杂的 App 生命周期回调路径，所以这里更适合用 `performNow(...)`。

### `Checker.code.value` 为什么长这样

Frida 风格里，Java 字段通常不会直接暴露成一个普通 JS 原始值，而是先暴露成一个字段包装器，然后通过：

- `.value`

去读写真实字段值。

所以：

```js
Checker.code.value
```

不是“对象里套了一个 `value` 字段”这么简单，而是：

- `Checker.code` 先得到一个 JavaField wrapper
- 再通过这个 wrapper 的 `get value()` / `set value(...)` 去触发真实的字段读写

### Nook 里字段是怎么被包装出来的

在 `CreateJavaUseWrapper()` 生成的 Proxy 里，当脚本访问：

- `Checker.code`

时，Nook 会先走：

- `__nookJavaResolveField(className, fieldName, isStaticGuess, loaderHandle)`

如果字段存在，就调用：

- `makeField(fieldName, signature, isStatic)`

生成一个字段包装器。

这个字段包装器大致长这样：

```js
{
  $className,
  $fieldName,
  $signature,
  $isStatic,
  __nookJavaReceiverHandle,
  __nookJavaLoaderHandle,
  get value() { return __nookJavaReadField(this); },
  set value(nextValue) { __nookJavaWriteField(this, nextValue); }
}
```

所以对 Nook 来说：

- `Checker.code` 是字段 wrapper
- `Checker.code.value` 才是真正的字段值

### Nook 怎么判断 `code` 是静态字段

和方法类似，字段也不能只靠名字判断。  
Nook 在解析字段时会显式带上一个“静态猜测”：

- 如果当前 wrapper 的 `receiverHandle === '0x0'`
- 就先按静态字段去解析

在 `CreateJavaUseWrapper()` 的 Proxy 逻辑里，可以直接看到：

```js
__nookJavaResolveField(className, resolvedFieldName, currentReceiverHandle === '0x0', loaderHandle)
```

也就是说：

- 类 wrapper 上访问字段时，默认先按静态字段解析
- 实例 wrapper 上访问字段时，默认先按实例字段解析

到了原生侧，`ResolveJavaField(...)` / `ResolveJavaFieldByName(...)` 会走反射，再用：

- `Modifier.isStatic(...)`

确认这个字段到底是不是静态字段，并把结果写回 `record.is_static`。

所以 `Checker.code` 这条链里，Nook 最后会把它收敛成：

- 类 `com.ad2001.frida0x3.Checker`
- 字段 `code`
- 签名 `I`
- `isStatic = true`

### 读取 `Checker.code.value` 背后发生了什么

当脚本执行：

```js
var before = Checker.code.value;
```

实际触发的是字段 wrapper 的 getter：

- `__nookJavaReadField(this)`

原生入口是：

- `JsJavaReadField`

这层会：

1. 从字段 wrapper 解析出 `JavaJsFieldRecord`
2. 取出：
   - `$className`
   - `$fieldName`
   - `$signature`
   - `$isStatic`
   - `__nookJavaReceiverHandle`
3. 调 `ReadJavaField(record, receiver_handle, ...)`

Android 默认实现下，最终会走：

- `DefaultReadJavaField(...)`

如果是静态字段，就不需要 receiver，并最终走 JNI：

- `GetStaticIntField`
- `GetStaticBooleanField`
- `GetStaticObjectField`
- 等等

`Checker.code` 的签名是 `I`，所以这里会落到：

- `GetStaticIntField(...)`

于是 `before` 读到的是 `0`。

### 写入 `Checker.code.value = 512` 背后发生了什么

当脚本执行：

```js
Checker.code.value = 512;
```

实际触发的是字段 wrapper 的 setter：

- `__nookJavaWriteField(this, 512)`

原生入口是：

- `JsJavaWriteField`

它会：

1. 解析字段元数据
2. 把 JS 值 `512` 转成 `JavaJsValue`
3. 调 `WriteJavaField(record, receiver_handle, value, ...)`

默认实现下最终进入：

- `DefaultWriteJavaField(...)`

然后根据字段签名和 `record.is_static` 分叉。

对于 `Checker.code` 这种 `static int`，最后走的是：

- `SetStaticIntField(...)`

也就是说，这句脚本最后真的是在 JNI 层把 `Checker.code` 这个静态整型字段改成了 `512`。

### `0x3` 里还顺手验证了“写后读”

这个脚本不是只写不读，它还做了：

```js
console.log(... + Checker.code.value)
```

也就是：

- 先读一次 `before`
- 再写 `512`
- 再读一次确认现在的值确实已经变成 `512`

所以它其实同时验证了两条链：

- 静态字段读取链
- 静态字段写入链

这比只做单向赋值更能证明字段桥接已经打通。

### 为什么 `0x3` 很重要

如果说：

- `0x1` 证明 Nook 能 Hook Java 方法
- `0x2` 证明 Nook 能主动调用 Java 静态方法

那 `0x3` 证明的就是：

- Nook 已经能把 Java 静态状态也映射进脚本层

这件事非常关键，因为真实分析里，很多判断条件根本不藏在方法里，而是藏在：

- 静态字段
- 单例状态
- 配置开关
- 计数器
- 缓存变量

只要工具能稳定读写这类字段，很多原本要绕很大弯的逻辑都能被直接改掉。  
从 Frida-like 工具形态看，这一步非常重要，因为它意味着脚本层已经不只是“能调函数”，而是开始能直接操控 Java 世界里的状态。

### Frida 在 `0x3` 这种字段场景里是怎么做的，和 Nook 类似吗

结论先说：  
在用户可见语义上，Frida 和 Nook 这里是很像的。

在 Frida 里，Java 字段同样不是直接当成普通 JS 属性值裸露出来，而是通过字段包装语义来访问。  
官方 Android 示例里就直接用了这种写法：

- `this._m.value = 0`

这说明 Frida 的 Java bridge 里，字段访问同样是：

- 先拿到字段成员
- 再通过 `.value` 读写真实字段值

所以从脚本写法上看，Nook 的：

- `Checker.code.value`

和 Frida 这一套是对齐的，不是 Nook 自己发明了一种完全不同的字段模型。

### Frida 对静态字段的外部语义

结合官方 `JavaScript API` 对 `Java.use(...)` 的说明，可以确定一件事：

- `Java.use(className)` 返回的是类 wrapper
- 这个 wrapper 同时暴露 static 和 non-static 成员能力

所以在 Frida 里，对静态字段也同样是直接在类 wrapper 上访问，例如：

```js
var Checker = Java.use("com.ad2001.frida0x3.Checker");
Checker.code.value = 512;
```

脚本作者不需要再写一个单独的“static field API”。  
这一点和前面静态方法调用的设计取向是一致的：

- 类级别成员就在类 wrapper 上直接暴露
- 实例级别成员就在对象 wrapper 上暴露

### Frida 和 Nook 在这里的共同点

如果只看 `0x3` 这种字段读写场景，两边的共同点非常明显：

- 都把 Java 字段包装成脚本层可操作对象
- 都通过 `.value` 完成真实字段读写
- 都允许直接从类 wrapper 上操作静态字段
- 都把 Java 世界里的“字段状态”映射成脚本层可以直接改的东西

也就是说，站在脚本作者角度，`0x3` 这题里 Frida 和 Nook 的使用体验是高度一致的。

### Frida 和 Nook 在实现组织上的差别

差别主要不在外部接口，而在内部实现组织方式。

Nook 这边你现在能直接看到完整链路：

- `CreateJavaUseWrapper()` 里的 Proxy
- `makeField(...)`
- `get value()` / `set value(...)`
- `__nookJavaReadField(...)`
- `__nookJavaWriteField(...)`
- 最终到 JNI 的 `GetStatic*Field` / `SetStatic*Field`

而 Frida 这边，在你本地仓库里真正暴露出来的 Java 入口只有：

- [`E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts`](E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-tools/bridges/java.ts)

它只是：

- `import Java from "frida-java-bridge";`
- `export default Java;`

也就是说，本地这个仓库并没有把 `frida-java-bridge` 的内部字段实现直接展开给你看。  
从架构上可以确定的是，Frida 这部分逻辑由 `frida-java-bridge` 的类包装体系统一管理，而不是像 Nook 这样把整段桥接代码直接写在你自己的 runtime 里。

### 能明确说到哪一步

基于目前能直接核对到的官方资料，可以明确说：

1. Frida 官方文档明确支持在 `Java.use(...)` 返回的 wrapper 上访问 Java 成员
2. 官方 Android 示例明确表明字段读写走 `.value`
3. 因而在 `0x3` 这种“改静态字段”的使用模型上，Frida 和 Nook 是对齐的

但如果要继续往下讲到：

- Frida 内部字段 wrapper 的具体类结构
- 静态字段和实例字段在内部是如何编码、缓存、分发的
- 最终 JNI 调用在 `frida-java-bridge` 里的源码路径

那就需要直接读上游 `frida-java-bridge` 源码，而不是仅靠你本地这个 `frida-tools` 目录。

### 对博客写法的建议

如果你在文章里讲 `0x3`，这里最稳的表述方式可以是：

- Frida 和 Nook 在字段访问语义上是对齐的，都是通过字段 wrapper 的 `.value` 来读写
- Nook 的优势是你可以直接从自己项目代码里把这条桥接链完整讲出来
- Frida 在本地仓库里这里只能看到桥接入口，内部细节要进一步追到 `frida-java-bridge`

这样写既准确，也不会把“语义等价”和“内部实现完全相同”混成一件事。

### Frida 具体是怎么做字段和静态字段处理的

这次直接看上游 `frida-java-bridge` 的源码，可以把 Frida 在 `0x3` 这种场景里的做法概括成两层：

1. `class-model.js` 负责把 Java 类成员建模出来
2. `class-factory.js` 负责把这些成员暴露成脚本层 wrapper

### Frida 先怎么知道一个成员是字段还是方法、是静态还是实例

在 [`lib/class-model.js`](https://github.com/frida/frida-java-bridge/blob/main/lib/class-model.js) 里，Frida 会先枚举类的 declared methods / declared fields，或者在不同运行时上直接从运行时内部结构里把成员信息读出来。

源码里能直接看到两件关键事：

1. `model_add_method(...)` 会把方法记录成：
   - `m:<type>0x...`

2. `model_add_field(...)` 会把字段记录成：
   - `f:<type>0x...`

其中这个 `<type>` 不是返回值类型，而是：

- `s` 表示 static
- `i` 表示 instance

也就是说，Frida 在建模阶段就已经把成员区分成了：

- 方法还是字段
- 静态还是实例

这和 Nook 那种“调用时再做一部分静态/实例回退判断”的风格不完全一样。  
Frida 更偏向先把成员元信息模型建好，再交给 wrapper 层消费。

### Frida 怎么处理字段名和方法名冲突

`class-model.js` 里还有一个很关键的细节：

- 如果字段名本身以 `$` 开头，会先转成带下划线的名字
- 如果字段名和已有成员冲突，会继续在前面补下划线

源码里 `model_add_field(...)` 的逻辑就是不断检查冲突，然后：

- `a` 可能保留成 `a`
- 如果和方法重名，就变成 `_a`
- 还冲突就继续 `_` 前缀

这正好和官方文档、以及 issue #44 里的例子对上了：

- `Test._a.value`

所以 Frida 里之所以有时候要写 `_a.value`，不是随便约定的，而是它的字段建模阶段就在主动避开字段/方法同名冲突。

### Frida 怎么把成员暴露成脚本属性

在 [`lib/class-factory.js`](https://github.com/frida/frida-java-bridge/blob/main/lib/class-factory.js) 里，Frida 的类包装器本体是一个 `Proxy`。

关键逻辑可以直接概括成：

- `wrapperHandler.get(...)`
- 调 `target.$find(property)`
- 如果找到了，就执行 `unwrap(receiver)`

也就是说，脚本访问：

- `Checker.code`

时，并不是简单地从一个普通 JS 对象字典里拿值，而是：

1. 先走 Proxy 的 `get`
2. 再让 wrapper 去查成员模型
3. 查到后返回一个解包后的成员 wrapper

而这个成员模型本身正是前面 `ClassModel.build(...)` 建出来的。

所以 Frida 的整体思路是：

- `ClassModel` 负责“这个类有哪些成员，它们分别是什么”
- `ClassFactory` 负责“把这些成员按脚本对象的方式暴露出来”

### `.value` 是怎么来的

官方文档和 Android 示例已经明确说明：

- Java 字段访问需要通过 `.value`
- 如果字段和方法同名，就用 `_fieldName.value`

这说明 Frida 的字段 wrapper 不是把字段直接展开成普通 JS primitive，而是提供了一个专门的字段对象，再通过 `.value` 来做真实读写。

从源码结构上结合前面的成员建模逻辑，可以把它理解成：

- `Checker.code` 拿到的是字段 wrapper
- `Checker.code.value` 才是字段当前值
- `Checker.code.value = 512` 才是字段写入动作

这一点和 Nook 在外部语义上是对齐的。

### Frida 对静态字段和实例字段的底层区分

从 `class-model.js` 的成员编码方式可以看出，Frida 在成员建模时就已经记录了：

- 这是字段还是方法
- 这是 static 还是 instance

所以对 Frida 来说，像：

- `f:s0x...`

这种成员，本质上就已经是“静态字段”；  
而：

- `f:i0x...`

则是“实例字段”。

这意味着 Frida 在后面的字段 wrapper 读写阶段，不需要像 Nook 某些调用场景那样再通过“receiver 是否为空”去猜一次。  
它更像是：

- 建模阶段先定性
- wrapper 阶段按这个定性执行

### Frida 和 Nook 在这里到底像不像

相同点：

- 都把字段和方法区分开处理
- 都把静态和实例区分开处理
- 都给字段提供了 `.value` 这种读写语义
- 都允许在类 wrapper 上直接操作静态字段

不同点：

- Nook：很多语义你现在能在一个 runtime 里顺着桥接代码一路看到，字段 wrapper、字段读写、JNI 分发都很直接
- Frida：更强调“先建模，再包装”
  - `class-model.js` 先把成员编码成 `m:` / `f:`，以及 `s` / `i`
  - `class-factory.js` 再通过 Proxy 和 `$find()` 把成员解包成脚本对象

所以如果你要一句话概括两者差异，可以写成：

- Nook 更像“运行时即时桥接”
- Frida 更像“先做成员模型，再按模型生成脚本接口”

### 对 `0x3` 这题最直接的结论

对于：

```js
var Checker = Java.use("com.ad2001.frida0x3.Checker");
Checker.code.value = 512;
```

Frida 背后至少已经明确完成了这几件事：

1. 在类成员建模阶段发现 `code` 是一个 field
2. 同时发现它是 `static`
3. 把它编码成字段成员模型而不是方法成员模型
4. 通过 class wrapper 的 Proxy 暴露给脚本
5. 用字段 wrapper 的 `.value` 语义完成真实读写

所以从实现思路上说，Frida 和 Nook 不是“完全一样”，但在 `0x3` 这个场景里，二者都已经实现了同一种 Frida-like 外部能力：

- 把 Java 静态字段变成脚本里可以直接读写的对象成员

### `frida-0x4` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x4/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x4/script.js) 这题的重点，是从：

- 类 wrapper 上直接调用静态成员

进一步走到：

- 构造一个 Java 对象实例
- 再通过这个实例去调用实例方法

脚本是：

```js
Java.perform(function () {
  var Check = Java.use("com.ad2001.frida0x4.Check");
  var instance = Check.$new();
  var flag = instance.get_flag(1337);
  console.log("lab:frida-0x4:instance:" + instance.$className);
  console.log("lab:frida-0x4:result:" + String(flag));
});
```

所以这一题验证的是两层新增能力：

1. `Java.use(...)` 返回的类 wrapper 不只是能调静态成员，还能负责构造对象
2. 构造出的对象 wrapper 会持有真实 Java 实例句柄，后续方法调用自动切到实例语义

### `Check.$new()` 在 Nook 里是怎么做的

Nook 在 `CreateJavaUseWrapper()` 生成的类 wrapper 里直接挂了一个：

- `$new()`

实现逻辑很清楚：

```js
$new: function () {
  const args = Array.prototype.slice.call(arguments);
  const constructorTarget = {
    $className: className,
    $methodName: '<init>',
    $signature: ...,
    $isStatic: false,
    __jptr: '0x0',
    __nookJavaReceiverHandle: '0x0',
    __nookJavaLoaderHandle: loaderHandle
  };
  return __nookJavaInvoke.apply(null, [constructorTarget].concat(args));
}
```

所以 `$new()` 的本质不是某个特殊 native API 单独处理，而是：

- 先构造一个“伪方法包装器”
- 把方法名设成 `<init>`
- 把它交给 `__nookJavaInvoke(...)`

也就是说，在 Nook 眼里，构造对象这件事本质上被统一进了“调用 Java 成员”的桥接模型里，只不过目标方法是构造函数 `<init>`。

### `$new()` 最后怎么真正创建 Java 对象

`$new()` 最终会进入：

- `JsJavaInvoke`

再进入：

- `InvokeJavaMethod(...)`

Android 默认实现里落到：

- `DefaultInvokeJavaMethod(...)`

这层一开始就会先判断：

- 当前 `record.method_name` 是不是构造函数

如果是，就走专门的 constructor 分支：

1. `ResolveJavaClass(...)` 拿到 `jclass`
2. `GetMethodID(clazz, "<init>", signature)` 拿到构造函数 ID
3. 把 JS 参数转成 JNI 参数
4. 调：
   - `env->NewObjectA(...)`
5. 把新创建的对象转成全局引用：
   - `NewGlobalRef(instance)`
6. 返回一个 `JavaJsValueKind::kObject`

所以 `$new()` 最终不是“模拟 new”，而是真的走 JNI 的：

- `NewObjectA(...)`

把 Java 对象构造出来。

### 为什么返回值最后又变成了一个 wrapper

`DefaultInvokeJavaMethod(...)` 返回构造结果时，本质上先返回的是：

- 一个携带 `object_handle` 的 `JavaJsValue`

但在 `JsJavaInvoke` 里，如果结果是 Java 对象，并且有有效 handle，就不会直接把它当普通值吐给脚本，而是进一步：

- `CreateJavaUseWrapper(ctx, wrapper_class_name, result.object_handle, record.loader_handle, true)`

也就是说，构造函数返回的不是“裸 jobject 指针”，而是一个新的对象 wrapper。

这个 wrapper 和类 wrapper 最大的区别在于：

- 它的 `currentReceiverHandle` 不再是 `'0x0'`
- 而是新构造出来那个 Java 对象的 handle

这一步非常关键，因为它决定了后面访问成员时语义已经完全变了。

### 为什么 `instance.get_flag(1337)` 这时就是实例调用了

当脚本继续执行：

```js
var flag = instance.get_flag(1337);
```

此时 `instance` 已经不是类 wrapper，而是对象 wrapper。  
所以后面 Proxy 再创建 `get_flag` 这个 method wrapper 时，会把：

- `__nookJavaReceiverHandle = currentReceiverHandle`

也就是把真实对象句柄带进去。

于是调用阶段 `ParseJavaMethodMetadata(...)` 解析出来的就不再是：

- `receiver_handle = 0`

而是：

- `receiver_handle = <真实实例句柄>`

这时 `JsJavaInvoke` 再去调用这个方法，就天然会按实例方法那条路径走，不会再像静态方法场景那样做“没有 receiver 的静态回退”。

### `0x4` 和 `0x2` 的本质区别

表面看：

- `0x2` 是 `MainActivity.get_flag(4919)`
- `0x4` 是 `instance.get_flag(1337)`

都像是在“调一个方法”。

但底层模型其实完全不同：

- `0x2`：
  - 类 wrapper
  - `receiver_handle = 0`
  - 需要判断是不是静态方法
  - 最终走 static invoke

- `0x4`：
  - 对象 wrapper
  - `receiver_handle = 实例句柄`
  - 天然就是实例方法调用模型
  - 最终走 instance invoke

所以 `0x4` 真正新增的不是“又调用了一个方法”，而是：

- Nook 已经开始支持“对象生命周期”
- 能从类 wrapper 进入对象 wrapper
- 能把真实 Java 对象实例持有在脚本运行时里

### `0x4` 这题说明了什么

如果说：

- `0x2` 证明 Nook 能主动调用静态方法
- `0x3` 证明 Nook 能读写静态字段

那 `0x4` 证明的就是：

- Nook 已经能把“构造对象 -> 拿到实例 -> 调实例方法”这条最基本的 Java 对象交互链打通

这一步非常重要。  
因为只要对象实例交互通了，后面很多更接近真实 App 逻辑的操作才可能成立，比如：

- 访问实例字段
- 调实例重载方法
- Hook 构造函数
- 持有并复用某个对象

从 Frida-like 工具视角看，这一步相当于从“能动类级别逻辑”进入了“能动对象级别逻辑”。

### `frida-0x5` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x5/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x5/script.js) 这一题的重点，不再是：

- 自己构造一个对象

而是：

- 找出 App 里“已经存在”的对象实例
- 直接在这个活对象上调用实例方法

脚本是：

```js
Java.performNow(function () {
  var fired = false;

  Java.choose("com.ad2001.frida0x5.MainActivity", {
    onMatch: function (instance) {
      if (fired) {
        return;
      }
      fired = true;
      console.log("lab:frida-0x5:match:" + instance.$className);
      instance.flag(1337);
      console.log("lab:frida-0x5:result:invoked:flag(1337)");
    },
    onComplete: function () {
      console.log("lab:frida-0x5:complete");
    }
  });
});
```

所以 `0x5` 真正新增的能力是：

- 不再自己制造对象
- 而是从运行中的 Java 世界里把现成对象“捞出来”

这一步非常像 Frida 真正常用的对象操作方式。

### 为什么这里不能简单沿用 `0x4` 的 `$new()`

上游题解里这个点其实很关键：  
`MainActivity` 这类 Android 组件不是普通 POJO，不能想当然直接：

```js
var a = Java.use("com.ad2001.frida0x5.MainActivity");
var main_act = a.$new();
main_act.flag(1337);
```

因为 `Activity` 依赖的不只是构造函数本身，还依赖：

- Android 生命周期
- 上下文环境
- 主线程 / Looper
- 系统对组件实例的管理

所以就算技术上能 `new` 出一个 Java 对象，也不代表这个对象已经处在系统期望的有效状态里。  
这也是为什么 `0x5` 这题会从：

- “自己 new 一个对象”

切换到：

- “找到系统已经创建好的那个对象”

这就是 `Java.choose(...)` 的意义。

### `Java.choose(...)` 在 Nook 里做了什么

Nook 侧 `Java.choose(...)` 的原生入口在：

- `JsJavaChoose`

这一层主要做的事是：

1. 读入类名字符串
2. 读入 callbacks 对象
3. 校验 `onMatch` / `onComplete` 都是函数
4. 调：
   - `EnumerateJavaObjects(class_name, loader_handle, ...)`
5. 把每个匹配到的对象包装成脚本实例，逐个回调 `onMatch`
6. 最后执行 `onComplete`

所以 `Java.choose(...)` 在 Nook 里不是一个“帮你找一下对象”的模糊接口，而是一条完整的：

- 对象枚举
- 对象包装
- 回调分发

链路。

### Nook 底层怎么枚举活对象

Android 默认实现里，`EnumerateJavaObjects(...)` 最终会走：

- `DefaultEnumerateJavaObjects(...)`

这层的关键点非常明确，它直接用了：

- `dalvik.system.VMDebug`
- `getInstancesOfClasses([Class], boolean)`

也就是说，Nook 并不是靠“自己维护对象注册表”来模拟 `Java.choose(...)`，而是直接利用 Android / ART 提供的调试枚举能力，从 VM 里把某个类当前活着的对象实例抓出来。

大体流程是：

1. `FindClass` 找到目标类
2. 找到 `dalvik.system.VMDebug`
3. 拿到：
   - `getInstancesOfClasses([Ljava/lang/Class;Z)[[Ljava/lang/Object;`
4. 构造只包含目标类的 `Class[]`
5. 调 `VMDebug.getInstancesOfClasses(...)`
6. 遍历返回的对象数组
7. 把每个命中的实例都转成全局引用
8. 再描述其 className，封装成 `JavaJsValue`

所以 `Java.choose("com.ad2001.frida0x5.MainActivity", ...)` 的本质就是：

- 直接向 VM 询问：“当前内存里有哪些 `MainActivity` 实例还活着？”

### 为什么 `onMatch(instance)` 里的 `instance` 能直接调用方法

枚举出来的命中对象，Nook 不会把它们直接以裸 handle 交给脚本。  
在 `JsJavaChoose` 里，每个 match 最终都会变成：

- `MakeJavaJsValue(...)`

或者在有 loader handle 的情况下进一步：

- `CreateJavaUseWrapper(ctx, wrapper_class_name, match.object_handle, loader_handle)`

也就是说，`onMatch(instance)` 收到的不是一个普通数据结构，而是一个真正的对象 wrapper。  
这个 wrapper 自带：

- `$className`
- `__nookJavaReceiverHandle`
- 方法解析能力
- 字段解析能力

所以后面写：

```js
instance.flag(1337)
```

时，就和 `0x4` 里的对象实例调用模型一样了：

- 这是对象 wrapper
- 它带着真实 receiver handle
- 后续方法调用天然走实例方法路径

### `0x5` 和 `0x4` 的本质区别

这两题表面看都在“对象上调实例方法”，但来源完全不同：

- `0x4`
  - 对象是脚本自己通过 `$new()` 创建的
  - 适合普通 Java 类

- `0x5`
  - 对象是系统已经创建好的活对象
  - 更适合 `Activity`、`Service`、View、单例、回调对象这类运行时对象

这也是 `Java.choose(...)` 这么重要的原因。  
很多逆向场景里，你真正想操作的对象根本不适合自己构造，只能：

- 找到现有实例
- 再在它上面调方法 / 改字段 / 安装 hook

### `0x5` 这题说明了什么

如果说：

- `0x4` 证明 Nook 能构造对象并操作实例

那 `0x5` 进一步证明的是：

- Nook 已经能进入“运行时对象发现”这条能力链

这一步比单纯 `$new()` 更贴近真实 Frida 使用方式。  
因为在实际分析里，很多时候你真正想拿到的不是“一个同类新对象”，而是：

- 当前界面的 `Activity`
- 当前存活的单例实例
- 某个已经被系统初始化好的管理器对象
- 某个真实回调对象

而这些对象最常见的获取方式，就是：

- `Java.choose(...)`

从工具形态上看，到了 `0x5`，Nook 已经不只是能“自己造对象”，而是开始能“读运行时现场”。这比前几题又更像真正的 Frida 风格工作流了。

### `frida-0x6` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x6/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x6/script.js) 这一题的重点，是把前面几题的能力真正组合起来：

- 自己构造一个 Java 对象
- 修改这个对象的实例字段
- 再把这个对象作为参数，传给另一个活对象的方法

脚本是：

```js
Java.performNow(function () {
  var Checker = Java.use("com.ad2001.frida0x6.Checker");
  var fired = false;

  Java.choose("com.ad2001.frida0x6.MainActivity", {
    onMatch: function (instance) {
      if (fired) {
        return;
      }
      fired = true;
      var checker = Checker.$new();
      checker.num1.value = 1234;
      checker.num2.value = 4321;
      console.log("lab:frida-0x6:match:" + instance.$className);
      console.log("lab:frida-0x6:checker:num1=" + checker.num1.value + ":num2=" + checker.num2.value);
      instance.get_flag(checker);
      console.log("lab:frida-0x6:result:invoked:get_flag(checker)");
    },
    onComplete: function () {
      console.log("lab:frida-0x6:complete");
    }
  });
});
```

所以 `0x6` 不是在验证某一个单点 API，而是在验证：

- 对象构造
- 实例字段写入
- 活对象发现
- 对象参数传递

这四层能不能连成一条完整链。

### `0x6` 相比前几题多出来的关键点

前面几题分别是：

- `0x4`：自己构造对象并调实例方法
- `0x5`：找到活对象并调实例方法

而 `0x6` 新增的是：

- “把一个脚本里构造好的 Java 对象，作为参数传给另一个 Java 对象的方法”

这一步很重要。  
因为这意味着脚本层不只是能调用零参数/基础类型参数的方法，而是开始能参与更真实的 Java 对象图交互。

### `Checker.$new()` 和字段赋值这部分，其实是前面能力复用

这题前半段：

```js
var checker = Checker.$new();
checker.num1.value = 1234;
checker.num2.value = 4321;
```

本质上就是前面几题能力的组合：

- `$new()`：沿用 `0x4` 的构造函数调用链
- `num1.value / num2.value`：沿用 `0x3` 的字段 wrapper 读写链

也就是说，到这一步结束时，Nook 已经在脚本里持有了一个真实 Java `Checker` 对象，并且它的：

- `num1 == 1234`
- `num2 == 4321`

已经通过实例字段写入真正改到了对象内部状态上。

### `instance.get_flag(checker)` 这一步真正难在哪里

表面上它只是：

```js
instance.get_flag(checker)
```

但这里和前面的基础类型参数调用完全不一样。  
这次传进去的不是：

- `int`
- `string`
- `boolean`

而是一个：

- Java 对象 wrapper

所以桥接层必须回答两个问题：

1. 这个 JS 值到底是不是一个 Java 对象
2. 如果是，它内部对应的 `jobject` / handle 到底是什么

只有把这两件事处理对了，Nook 才能把这个参数正确传回 Java 世界。

### Nook 怎么把对象 wrapper 解析成“Java 对象参数”

这一步首先发生在：

- `ParseJavaJsValue(...)`

当它看到传进来的 JS 值是一个对象时，会检查这个对象上有没有：

- `__nookJavaReceiverHandle`

如果没有，再看：

- `__jptr`

只要能从这两个属性里取到有效句柄，就会把它解析成：

- `JavaJsValueKind::kObject`

并记录：

- `object_handle`
- `object_class_name`

所以对 Nook 来说，脚本里的：

- `checker`

并不是一个普通 JS object。  
它之所以能当 Java 参数传递，正是因为它内部包着真实 Java 对象句柄。

### 后面怎么按目标方法签名把它转回 JNI 对象

当 `instance.get_flag(checker)` 进入 `JsJavaInvoke` 之后，参数会先都被解析成 `JavaJsValue`。  
此时 `checker` 这个参数已经是：

- `kind = kObject`
- `object_handle = <Checker 实例句柄>`

随后在真正发起 Java 调用前，Nook 会根据目标方法签名，对每个参数执行：

- `ConvertJavaJsValueToNookJavaHookValue(...)`

对于对象参数，这层会把：

- `value.object_handle`

直接转换成：

- `jobject`

也就是：

- `out_value->l = reinterpret_cast<jobject>(value.object_handle);`

这一步非常关键。  
它意味着脚本侧传进去的不是“某种序列化后的对象描述”，而是直接把对应 Java 对象实例本身传回给了目标方法。

### 所以 `0x6` 最终调用链是什么

把整题串起来，其实就是下面这条链：

1. `Java.choose(...)` 找到活着的 `MainActivity`
2. `Checker.$new()` 构造一个新的 `Checker`
3. `checker.num1.value = 1234`
4. `checker.num2.value = 4321`
5. `instance.get_flag(checker)` 进入实例方法调用
6. `checker` 被 `ParseJavaJsValue(...)` 识别成 Java 对象参数
7. `ConvertJavaJsValueToNookJavaHookValue(...)` 把它转成 JNI `jobject`
8. `MainActivity.get_flag(Checker)` 真正收到这个对象实例
9. Java 侧检查：
   - `A.num1 == 1234`
   - `A.num2 == 4321`
10. 条件成立，继续执行 flag 逻辑

所以 `0x6` 最核心证明的事情是：

- Nook 已经能把“脚本里构造和修改过的 Java 对象”，重新送回 Java 世界参与真实业务逻辑

### 为什么这一步特别像真正的 Frida 工具

很多逆向和动态分析场景，真正有价值的不是：

- 改一个返回值
- 调一个无参函数

而是：

- 自己拼一个对象
- 把内部字段改成想要的状态
- 再把这个对象喂给目标逻辑

这类能力一旦成立，工具的味道就明显不再只是“会 Hook”，而开始像一个：

- 可交互运行时实验平台

### `frida-0x7` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x7/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x7/script.js) 这一题开始正式讲：

- Hook 构造函数

脚本是：

```js
Java.perform(function () {
  var Checker = Java.use("com.ad2001.frida0x7.Checker");

  Checker.$init.implementation = function (param) {
    console.log("lab:frida-0x7:ctor-hit:" + String(param));
    this.$init(600, 600);
    console.log("lab:frida-0x7:ctor-rewrite:num1=600:num2=600");
  };
});
```

这题的核心不是“调一个构造函数”，而是：

- 把构造函数本身接管掉
- 在对象被创建的那一刻改写它的初始化参数

这意味着 Nook 已经开始支持一个更底层的能力：

- 在对象生命周期最开始，就介入对象初始化过程

### 为什么构造函数 Hook 和普通方法 Hook 不一样

构造函数和普通方法最大的不同是：

- 普通方法是在对象已经存在之后被调用
- 构造函数是在对象创建过程中被调用

所以构造函数 Hook 的时机更敏感。  
如果脚本晚了一点，目标对象可能早就被创建完了，Hook 就错过了最关键的初始化瞬间。

这也是为什么上游题解会强调：

- 要预加载
- 要在应用启动前把 hook 装进去

而不是等界面已经出来之后再动手。

### `Checker.$init.implementation` 在 Nook 里怎么工作

Nook 在方法包装器里对构造函数做了统一处理。  
`$init` 其实就是构造函数 `<init>` 的脚本侧名字映射。

在 `CreateJavaHookCallbackReceiver(...)` 里，Nook 会把记录里的：

- `record.method_name == "<init>"`

映射回脚本侧的：

- `$init`

同时给这个构造函数包装器挂上：

- `$className`
- `$methodName = "$init"`
- `$signature`
- `$isStatic = false`
- `__nookJavaHookId`
- `callOriginal`

所以 `Checker.$init.implementation = function (param) { ... }` 这句，实际上和普通方法 Hook 一样，最终还是落到了同一套 Java Hook 安装链里：

- `__nookJavaInstallImplementation(...)`
- `InstallJavaJsHook(...)`
- `DefaultInstallJavaJsHook(...)`
- `NookJavaHookHookDeferredWithLoader(...)`

只不过这次目标方法不是普通成员，而是构造函数 `<init>`。

### 为什么 `$init` 的 hook 会在对象创建时触发

因为构造函数本来就是在 `new` 过程中执行的。  
一旦 `Checker.$new(...)` 被调用，Nook 会在底层把 `<init>` 视为一个可 hook 的 Java 方法入口。

当这个构造函数命中时，Nook 会像普通 Java Hook 一样：

- 先进入回调上下文
- 再把当前 hook 的 receiver、签名、hook id 等信息挂好
- 然后执行你写的 `implementation`

所以从脚本视角看，`$init.implementation` 并不是“晚点再修改对象”，而是：

- 在对象生成那一瞬间，直接把初始化逻辑截住

### `this.$init(600, 600)` 为什么不会无限递归

这句是构造函数 Hook 里最关键的一行：

```js
this.$init(600, 600);
```

它的语义不是“再去执行一次当前 hook 自己”，而是：

- 在当前 hook 回调上下文里
- 调用原始构造函数实现
- 但把参数改成 `600, 600`

Nook 的 `callOriginal` 机制在这里同样发挥作用。  
构造函数命中时，Nook 会给当前回调 receiver 上挂一个专用的 `callOriginal` 入口。  
对构造函数来说，`$init(...)` 在脚本层就是这个原始构造逻辑的入口映射。

所以 `this.$init(600, 600)` 的本质就是：

- 不让对象按原参数初始化
- 而是强制改成新的构造参数

这就完成了“改写对象初始化状态”。

### 为什么这题必须在应用启动前预加载

题解里特别强调了 `-l` / spawn，这一点非常重要。  
原因很简单：

- `Checker` 的实例是在应用启动流程里创建的
- 构造函数 hook 必须在实例创建之前已经就位

如果你等 App 都跑完了再注入，`Checker` 可能已经被构造完了，那就错过了这次初始化时机。

所以这题真正体现的是：

- 构造函数 hook 的时机问题
- 预加载脚本的重要性

### `0x7` 相比 `0x4` 的提升在哪里

`0x4` 只是：

- 你自己调用 `$new()`
- 自己获得一个对象
- 再对这个对象调用实例方法

`0x7` 则更进一步：

- 对象不是你自己手动构造后再用
- 而是你直接接管构造函数
- 在对象被系统创建时就改写它的初始化参数

所以 `0x7` 证明的已经不是“会构造对象”，而是：

- 能在对象生命周期最早期介入
- 能改变对象从出生那一刻起的状态

### `frida-0x8` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x8/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x8/script.js) 这一题开始正式切到 Native 侧。

脚本是：

```js
(function () {
  var strcmpAdr = Module.getExportByName("libc.so", "strcmp");
  console.log("lab:frida-0x8:installed:target=" + String(strcmpAdr));

  Interceptor.attach(strcmpAdr, {
    onEnter: function (args) {
      try {
        if (args[0].isNull() || args[1].isNull()) {
          return;
        }

        var left = args[0].readCString();
        var right = args[1].readCString();
        if (left.indexOf("Hello") !== -1) {
          console.log("lab:frida-0x8:hit:input=" + left + ":secret=" + right);
        }
      } catch (error) {
        console.log("lab:frida-0x8:error:" + String(error));
      }
    }
  });
})();
```

这题的重点不再是：

- Java 方法调用
- Java 字段读写
- Java 对象生命周期

而是：

- 定位 Native 导出符号
- 对 Native 函数做 inline/attach hook
- 在运行时观察它的参数

这标志着 Nook 已经开始把能力从 Java 世界推向 Native 世界。

### 这题为什么先 hook `strcmp`

题解里已经把关键逻辑拆出来了：

- 用户输入会进入 `strcmp`
- 另一个参数是内部真正的 flag 字符串
- 只要比较结果命中，就能得到目标信息

所以最直接的分析路径不是先逆完整个 native 算法，而是：

- 直接 hook `strcmp`
- 观察它的两个参数

这就是 Frida 风格 native 分析最经典的一类用法。

### `Module.getExportByName(...)` 在这里做了什么

脚本第一步是：

```js
var strcmpAdr = Module.getExportByName("libc.so", "strcmp");
```

这表示：

- 去 `libc.so` 的导出表里找 `strcmp`
- 直接拿到它的运行时地址

这一步在 Native 分析里非常关键，因为大多数 hook 都要先解决“我要 hook 的函数在哪”。

题解里也顺带讲了 Frida 常见的几种拿地址方式：

- `Module.enumerateExports()`
- `Module.getExportByName()`
- `Module.findExportByName()`
- `Module.getBaseAddress() + offset`
- `Module.enumerateImports()`

但对这题来说，最直接的就是：

- `Module.getExportByName("libc.so", "strcmp")`

### `Interceptor.attach(...)` 在这里做了什么

拿到 `strcmp` 地址后，脚本马上：

```js
Interceptor.attach(strcmpAdr, {
  onEnter: function (args) { ... }
});
```

这表示：

- 在目标函数入口打上 hook
- 函数每次被调用时，先进入 `onEnter`

所以这里不是“改返回值”，而是先“观察入参”。

题解里只 hook `strcmp` 的原因也很明确：

- 用户输入会作为一个参数进入
- 内部 flag 字符串会作为另一个参数进入
- 只要看到 `Hello` 相关的输入，就能把 flag 侧参数打印出来

### 为什么要先做参数过滤

应用里并不只有一处会调用 `strcmp`。  
所以如果你直接无脑打印，会刷很多次日志。

题解里很标准地先做了过滤：

```js
var left = args[0].readCString();
if (left.indexOf("Hello") !== -1) {
  ...
}
```

这说明 native hook 里一个很重要的习惯是：

- 先缩小触发面
- 再打印你真正关心的参数

否则日志很快就会被噪音淹没。

### `0x8` 这题把什么能力补上了

如果把前面的题串起来：

- `0x1` 到 `0x7`：Java 侧的 Hook、调用、字段、对象、构造函数、对象发现、对象参数传递
- `0x8`：开始直接进入 Native 函数分析和 Native Hook

这一步补的是完全不同的一层能力：

- 不再只是把 Java 代码映射成脚本对象
- 而是把 `.so` 里的函数地址、调用参数、入口时机变成脚本可观察、可拦截的对象

这也是 Frida-like 工具很重要的一部分：

- Java 层和 Native 层都能用同一套脚本思维去分析

### Nook 和 Frida 在这题上的关系

在 `0x8` 这种 Native hook 场景里，Nook 和 Frida 的外部用法已经非常接近：

- 都能用 `Module.getExportByName(...)` 找地址
- 都能用 `Interceptor.attach(...)` 挂 hook
- 都能在 `onEnter` 里直接读参数
- 都能通过字符串过滤把你关心的调用筛出来

所以这题真正说明的是：

- Nook 不再只是 Java-like
- 它已经开始具备 Frida 那种完整的 Native 分析工作流

### `0x8` 里再往下一层：`Module.getExportByName(...)` 背后是怎么做的

这类 API 表面看只是：

```js
Module.getExportByName("libc.so", "strcmp")
```

但它背后真正做的是：

- 去已加载模块的符号表里定位一个导出符号
- 把这个符号的运行时地址变成脚本里的 NativePointer

#### Nook 里怎么做

Nook 侧入口在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp)：

- `JsModuleFindExportByName`
- `JsModuleGetExportByName`

流程非常直接：

1. 解析 JS 传进来的：
   - `module_name`
   - `symbol_name`
2. 调：
   - `FindNativeJsExportByName(module_name, symbol_name, &target_address, ...)`
3. 如果成功，就用：
   - `MakeNativePointer(ctx, target_address)`
   返回给脚本

而 `FindNativeJsExportByName(...)` 在 [`src/agent_runtime/nook_native_js_bridge.cpp`](./src/agent_runtime/nook_native_js_bridge.cpp) 里，核心逻辑是：

- 先拿一个“已加载符号解析器”
- 再让它去解析当前进程里某个已加载模块的符号地址

也就是说，Nook 对 `getExportByName` 的实现本质上是：

- “已加载模块名 + 导出符号名 -> 运行时地址”

并不是靠脚本自己遍历 ELF 结构来凑，而是落到了底层的加载符号解析器。

#### Frida 里怎么做

Frida 这类 Native API 不属于 `frida-tools` 层，而属于：

- `frida-gumjs`
- 底层再依赖 `frida-gum`

你本地仓库里能直接确认这一点，因为构建里启用了：

- `gumjs=enabled`

而且 devkit 资产示例里也直接把：

- `Module.getExportByName(...)`
- `Interceptor.attach(...)`

当成 gumjs 运行时提供的脚本 API 来使用。

所以从分层上看：

- Nook：你自己在 `js_runtime.cpp` 里把 `Module.getExportByName` 映射到本地符号解析器
- Frida：由 `gumjs` 暴露 `Module` API，底层再由 `gum` 去做模块/符号解析

两边的外部语义一样，但内部承载层不同。

### `0x8` 里再往下一层：`Interceptor.attach(...)` 背后是怎么做的

脚本里写的是：

```js
Interceptor.attach(strcmpAdr, {
  onEnter: function (args) { ... }
});
```

但这背后不是“给某个 JS 函数加回调”那么简单，而是：

- 在目标地址安装一个真正的 native hook
- 函数运行到该地址时，先跳到 bridge/trampoline
- bridge 再把寄存器/栈上的参数整理成脚本可访问的 `args`
- 然后回调 JS 的 `onEnter`

#### Nook 里怎么做

Nook 侧入口在：

- `JsInterceptorAttach`

它做的事情大致是：

1. 读取 callbacks 对象里的：
   - `onEnter`
   - `onLeave`
2. 解析 hook target  
   可以是：
   - 直接传进来的 pointer
   - 或者模块名 + 符号名这种目标描述
3. 组装一个 `NativeJsHookRequest`
4. 调：
   - `InstallNativeHookForCurrentScript(...)`
5. 再进一步落到：
   - `InstallNativeJsHook(...)`

而 `InstallNativeJsHook(...)` 在 [`src/agent_runtime/nook_native_js_bridge.cpp`](./src/agent_runtime/nook_native_js_bridge.cpp) 里最终会走：

- `InstallInlineHookWithDefaultAdapter(...)`

也就是说，Nook 的 `Interceptor.attach(...)` 本质上真的是：

- 安装一个 inline hook

并不是“纯解释层模拟 attach”。

安装成功后，Nook 会把：

- `hook_id`
- `target_address`
- `module_name`
- `symbol_name`
- `snapshots`
- `hook_handle`

这些信息记进 native hook 注册表。  
后面函数命中时，再根据 `hook_id` 回调到当前脚本注册的 `onEnter/onLeave`。

#### Frida 里怎么做

Frida 的 `Interceptor.attach(...)` 同样属于：

- `frida-gumjs` 脚本层 API

底层真正执行 hook 的则是：

- `frida-gum`

从 Frida 的整体架构理解，它背后做的也是同一种事：

- 在目标地址安装 native interceptor
- 生成 trampoline / 过桥逻辑
- 在函数进入和离开时，把上下文封装给 JS

所以从实现模型上看，Frida 和 Nook 在这里其实是同类系统：

- 都不是“解释器层回调”
- 都是“真实 native hook + JS bridge”

### 这两个 API 配起来之后，`0x8` 才成立

把这两层放在一起看，`0x8` 里的：

```js
var strcmpAdr = Module.getExportByName("libc.so", "strcmp");
Interceptor.attach(strcmpAdr, { ... });
```

本质上就是：

1. 先靠模块/符号解析，拿到运行时真实地址
2. 再在这个地址上安装一个 native inline hook
3. 函数每次进入时，把参数桥接到 JS
4. 脚本层再去做过滤和打印

所以这题真正体现的不是“会用两个 API”，而是：

- Nook 已经具备了和 Frida 相同形态的 Native 脚本桥接链

也就是：

- 模块符号解析
- hook 安装
- 参数桥接
- JS 回调观察

### `frida-0x9` 的重点是什么

[`tests/Test_Lab/nook-frida-labs/frida-0x9/script.js`](./tests/Test_Lab/nook-frida-labs/frida-0x9/script.js) 这一题是在 `0x8` 的基础上再往前推进一步：

- `0x8`：观察 Native 参数
- `0x9`：直接改 Native 返回值

脚本是：

```js
(function () {
  var target = Module.getExportByName(
    "liba0x9.so",
    "Java_com_ad2001_a0x9_MainActivity_check_1flag"
  );

  console.log("lab:frida-0x9:installed:target=" + String(target));

  Interceptor.attach(target, {
    onLeave: function (retval) {
      console.log("lab:frida-0x9:hit:original-ret=" + String(retval));
      retval.replace(1337);
      console.log("lab:frida-0x9:result:forced-ret=1337");
    }
  });
})();
```

所以这题的核心不是“看到了什么”，而是：

- 让目标 Native 函数继续正常执行
- 等它返回之后
- 再把返回值强行改掉

这意味着 Nook 的 Native hook 已经不只是观察器，而是开始具备真正的执行流改写能力。

### 这题为什么直接 hook JNI 导出函数

和 `0x8` 不同，这题并没有 hook 一个通用 libc 函数，而是直接 hook：

- `Java_com_ad2001_a0x9_MainActivity_check_1flag`

也就是 JNI 导出的真正目标函数。

这样做的好处很直接：

- 命中点更准
- 不需要再从大量通用库调用里做过滤
- 一上来就站在目标逻辑的返回点上

这也是 Native 分析里很常见的一种取舍：

- 如果你只是想观察中间秘密参数，hook `strcmp` 之类的通用函数很方便
- 如果你想直接改逻辑结果，hook 目标 JNI 函数本身通常更直接

### `onLeave(retval)` 在这里到底是什么

脚本里最关键的一行是：

```js
retval.replace(1337);
```

这里的 `retval` 不是一个普通 JS number。  
在 Nook 里，leave 回调收到的是一个“返回值包装对象”。

在 [`src/agent_runtime/js_runtime.cpp`](./src/agent_runtime/js_runtime.cpp) 里，Nook 会专门构造这个返回值对象：

- 先把当前原始返回值包装成一个 NativePointer 风格对象
- 再在这个对象上挂一个：
  - `replace(...)`

也就是说，`retval` 既能被打印、读当前值，又能通过 `replace()` 把底层返回值标记为“需要覆盖”。

所以这里的：

- `String(retval)`：是在读原始返回值
- `retval.replace(1337)`：是在请求“把真正返回给调用方的值改成 1337”

### Nook 里 `retval.replace(...)` 背后怎么工作

Nook 这部分实现可以拆成两层：

1. JS runtime 层  
   为 `onLeave` 构造一个带 `replace()` 的返回值 wrapper

2. Native dispatch 层  
   在 leave callback 返回后，检查脚本有没有提交“返回值覆盖”

在 `js_runtime.cpp` 里可以直接看到：

- `BuildNativeHookReturnValue(...)`
- `UpdateNativeHookReturnValue(...)`

以及围绕：

- `retval.replace requires a pointer value`

这类错误信息的实现逻辑。

这说明 `replace()` 不是普通 JS helper，而是和当前 native hook 调用上下文绑在一起的。

### leave 回调结束后，修改后的返回值怎么真正生效

真正关键的一层在 [`src/agent_runtime/nook_native_js_bridge.cpp`](./src/agent_runtime/nook_native_js_bridge.cpp)。

Nook 的 inline hook dispatch 流程大致是：

1. 进入 hook
2. 先跑 `onEnter`
3. 调原始函数，拿到 `return_value`
4. 构造 leave 事件
5. 跑 `onLeave`
6. 如果脚本在 `onLeave` 里提交了返回值覆盖
7. 就把 `return_value` 改成覆盖后的值
8. 最后把这个新值返回给原始调用方

源码里关键逻辑就是：

```cpp
if (leave_result.has_return_value_override) {
    return_value = leave_result.return_value;
}
```

所以 `retval.replace(1337)` 最终不是“改了一个 JS 对象的属性”，而是：

- 真正改掉 native dispatch 最后返回给调用者的那个寄存器值/返回值

这就是 `0x9` 真正有杀伤力的地方。

### `0x9` 和 `0x8` 的本质差别

两题表面都在用：

- `Module.getExportByName(...)`
- `Interceptor.attach(...)`

但行为层次完全不同：

- `0x8`
  - 目标：看参数
  - 重点：观察
  - 改动：没有改执行结果

- `0x9`
  - 目标：改返回值
  - 重点：篡改执行结果
  - 改动：直接影响后续业务逻辑判断

所以 `0x9` 对 Nook 的意义不在于“又支持了一个新 API”，而在于它证明：

- Native hook 的离开阶段已经能稳定拿到返回值
- 脚本层对返回值的修改能真正回灌到底层执行流

### 为什么这一步很重要

真实逆向里，很多时候你不想完整还原 native 算法，只想：

- 把校验函数结果改成成功
- 把比较结果改成通过
- 把某个条件分支需要的返回值直接伪造出来

这时候工具值不值钱，很大程度上就看：

- 能不能稳定改返回值

而 `0x9` 恰好证明了 Nook 在 Native 侧已经开始具备这类“简单但高频”的动态改写能力。

这几层已经串起来了。


这比单纯 `$new()` 更接近真实 Frida 使用里最有杀伤力的那一类场景。

因为你已经不只是观察和拦截，而是在主动构造 Java 世界里的输入对象，驱动程序执行你想看的路径。

### `0x5` 里的 `Java.performNow(...)` 是什么意思

`0x5` 里最容易被忽略、但其实很关键的一点是它没有写：

- `Java.perform(...)`

而是写了：

- `Java.performNow(...)`

这不是简单的换个名字，而是在表达一种不同的时机语义。

### Nook 里 `performNow` 和 `perform` 的区别

Nook 的 JS runtime 里，这两个函数定义得非常直接：

```js
Java.performNow = function (fn) {
  return Java.vm.perform(fn);
};

Java.perform = function (fn) {
  if (Java._isClassLoaderReady()) {
    return Java.vm.perform(fn);
  }
  return Java.ready(function () {
    return Java.vm.perform(fn);
  });
};
```

也就是说：

- `Java.performNow(fn)`：
  - 只做一件事：确保当前线程进入 `Java.vm.perform(fn)`
  - 不额外等待 ClassLoader ready
  - 不走 ready callback 队列

- `Java.perform(fn)`：
  - 如果 ClassLoader 已经 ready，就立即执行
  - 如果还没 ready，就先排队，等 ready 之后再执行

所以在 Nook 里，`performNow` 的核心语义可以压成一句话：

- “我现在就要进入 Java VM 执行，不额外等应用类加载时机”

### 为什么 `0x5` 更适合用 `performNow`

`0x5` 的脚本核心是：

```js
Java.choose("com.ad2001.frida0x5.MainActivity", { ... })
```

这类操作的前提不是“我要立刻拿某个 app class 做 `Java.use(...)` 并访问其静态成员”，而是：

- 当前 VM 已经可用
- 当前线程已经 attach 到 Java VM
- 然后直接去枚举运行时里现成对象

因为 `Java.choose(...)` 本身就是一种运行时对象枚举操作，所以这里更重要的是：

- 赶紧进 VM
- 立刻去扫当前活对象

而不是额外再等一层 `Java.perform(...)` 的“应用 ClassLoader ready 后再说”。

从 `0x5` 的场景看，这样写有两个好处：

1. 语义更贴近真实需求  
   这个脚本要做的是“现在去找活着的 `MainActivity`”，不是“等以后某个类加载完再操作”。

2. 减少不必要的等待层  
   既然对象已经活在 VM 里，额外排 ready 队列反而会让时机更绕。

### Frida 官方对 `performNow` 的定义

Frida 官方文档对这两个 API 的区分写得很明确：

- `Java.perform(fn)`：
  - 确保当前线程 attach 到 VM
  - 如果 app 的 class loader 还不可用，会延迟执行

- `Java.performNow(fn)`：
  - 确保当前线程 attach 到 VM
  - 立即执行
  - 适用于“不需要访问 app’s classes”的场景

官方原话里最关键的一点就是：

- `Use Java.performNow() if access to the app’s classes is not needed.`

来源：
- [Frida JavaScript API](https://frida.re/docs/javascript-api/)

### 这里为什么看起来又访问了类名

这个点容易让人困惑：  
`0x5` 明明写了：

```js
Java.choose("com.ad2001.frida0x5.MainActivity", ...)
```

看起来不是也在用 app class 吗？

这里要区分两件事：

1. `Java.use(...)` 这种场景  
   往往依赖“应用类加载器已经 ready，并能给你稳定产出 class wrapper”

2. `Java.choose(className, ...)` 这种场景  
   本质是运行时对象枚举，底层可以直接从 VM / ART 的活对象里做匹配和遍历

所以 `performNow` 不是说“脚本里完全不能出现 app class 名字”，而是说：

- 不一定需要走 `perform(...)` 那层“等应用类加载器 ready”的工作流

`0x5` 正是这种典型场景。

### 对 `0x5` 这题更准确的理解

如果把 `performNow` 放回 `0x5` 的语境里，它表达的其实是：

- 我现在不想排队等 Java.ready
- 我只要保证当前线程可以安全进入 VM
- 然后立刻去枚举当前已经活着的 `MainActivity` 对象

所以这题里：

- `Java.choose(...)` 负责“找活对象”
- `Java.performNow(...)` 负责“立刻进入 VM 执行这次对象枚举”

两者配合起来，才构成这道题完整的 Frida-like 工作流。
## 0xA：`NativeFunction(...)` 背后做了什么，`Process.attachModuleObserver(...)` 又是怎么配合它工作的

`0xA` 这题和前面的 `0x8`、`0x9` 不一样。

前面两题的重点是：
- 找到一个 Native 函数
- 在它被别人调用时拦住它

而 `0xA` 的重点变成了：
- 自己在脚本里主动拿到一个 Native 函数地址
- 把它包装成一个可调用对象
- 然后直接从 JS 里把它调起来

脚本核心是这几步：

```js
var existing = Process.findModuleByName("libfrida0xa.so");

Process.attachModuleObserver({
  onAdded: function (module) {
    if (module.name === "libfrida0xa.so") {
      var getFlagAddr = module.base.add(0x1DD60);
      var get_flag = new NativeFunction(getFlagAddr, "void", ["int", "int"]);
      get_flag(1, 2);
    }
  }
});
```

所以这一题实际上可以拆成两半来看：

1. `Process.findModuleByName(...)` / `Process.attachModuleObserver(...)`
   负责“等目标 so 出现，并拿到它的基址”
2. `new NativeFunction(...)`
   负责“把一个 Native 地址包装成 JS 里可直接调用的函数对象”

---

### 先看脚本语义：为什么这里要等模块加载

这一题里真正要调的是：

```js
module.base.add(0x1DD60)
```

也就是说，脚本拿到的不是导出符号名，而是：
- 先拿模块基址 `module.base`
- 再加上静态偏移 `0x1DD60`

那前提就很明确了：
- `libfrida0xa.so` 必须已经被装载进当前进程
- 否则你连 `base` 都拿不到
- 更别说去构造 `base + offset` 这个函数地址

所以脚本才会先：

```js
Process.findModuleByName("libfrida0xa.so")
```

如果这时模块已经在内存里了，就立刻调用；
如果还没加载，就注册：

```js
Process.attachModuleObserver({
  onAdded(module) { ... }
})
```

等这个 so 真正进入进程地址空间时，再去算地址、构造 `NativeFunction` 并调用。

这一步的本质不是 Hook，而是：
- 监听模块加载时机
- 确保“地址可用”之后再主动 call

---

### Nook 里 `Process.findModuleByName(...)` 是怎么做的

Nook 在 JS 层把这个 API 绑到了：

- `JsProcessFindModuleByName(...)`

它的流程很直接：

1. 从 JS 拿到模块名字符串
2. 调 `CollectLoadedNativeModules(...)` 枚举当前进程已加载模块
3. 用 `FindLoadedModuleByName(...)` 按名字匹配
4. 找到就 `MakeModuleObject(...)` 包成 JS `module` 对象返回
5. 找不到就返回 `null`

所以它不是“预测未来会不会加载”，而只是：
- 查询“此时此刻进程里已经有哪些 so”

这就是为什么脚本要写成：

```js
var existing = Process.findModuleByName("libfrida0xa.so");
if (existing !== null) {
  scheduleInvoke(existing);
  return;
}
```

先查一次，是为了覆盖“脚本注入时模块已经加载完成”的情况。

---

### Nook 里 `Process.attachModuleObserver(...)` 是怎么做的

Nook 对这个 API 的落地点是：

- `JsProcessAttachModuleObserver(...)`

它做的事情可以理解成“给当前脚本注册一组模块事件回调”。

主要流程是：

1. 校验参数必须是对象
2. 取出其中的 `onAdded` / `onRemoved`
3. 要求至少有一个是函数
4. 以 `current_script_id` 为 key，存入 `state.module_observers`

这里有个很关键的细节：  
注册完成后，Nook **不是只等未来事件**，而是如果你提供了 `onAdded`，它会立刻：

1. 再次 `CollectLoadedNativeModules(...)`
2. 枚举当前已经加载的所有模块
3. 对每个模块都主动调用一次 `onAdded(module)`

这意味着 Nook 这里的语义其实是：
- 先把“当前已经存在的模块”补发一遍 `onAdded`
- 之后再继续接收未来新加载模块的事件

所以这和脚本开头那段：

```js
var existing = Process.findModuleByName(...)
```

在效果上有一点重叠。  
脚本这样写主要是为了更贴近 Frida 社区里常见的写法，也更直观：
- 已加载：立即处理
- 未加载：等 `onAdded`

---

### 模块真正加载时，Nook 怎么把事件推回 JS

Native 侧模块加载后，Nook 这边最终会走到：

- `NotifyModuleObserverModuleLoaded(const char* module_path, ...)`

它做的事情是：

1. 取出当前所有 `module_observers`
2. 再次枚举已加载模块
3. 根据 `module_path` 找到对应的 `NativeModuleRecord`
4. 调 `EnqueueModuleEventLocked(...)`

注意这里不是在任意 Native 线程里直接硬调 JS 回调，而是：
- 先把模块事件排入运行时队列
- 再由 JS 运行时在安全上下文里分发给脚本

这样做的原因很实际：
- QuickJS 运行时不是随便哪个线程都能直接闯进去执行的
- 模块加载事件来自 Native 侧，不一定正处于脚本执行栈里
- 所以需要先排队，再交给运行时统一投递

从架构上看，这和 Nook 处理很多异步事件的思路是一致的：
- Native 侧发现事件
- 转成 runtime event
- 回到 JS 层触发回调

---

### `new NativeFunction(addr, retType, argTypes)` 在 Nook 里做了什么

这个 API 在 Nook 里的入口是：

- `JsNativeFunctionConstructor(...)`

也就是 JS 里执行：

```js
new NativeFunction(getFlagAddr, "void", ["int", "int"])
```

时，Nook 会做这几件事：

1. 解析第一个参数 `addr`
   - 必须是一个非 0 指针
2. 解析第二个参数 `returnType`
   - 这里是 `"void"`
3. 解析第三个参数 `argTypes`
   - 这里是 `["int", "int"]`
4. 调 `CreateNativeFunctionValue(...)`
   - 生成一个“看起来像 JS 函数，但内部带着 Native 调用元数据”的对象

所以 `NativeFunction` 的本质不是“立刻调用一次”，而是：
- 创建一个 callable JS object
- 并把目标地址、返回类型、参数类型挂在这个对象身上

---

### Nook 是怎么把它变成“可调用函数对象”的

核心在：

- `CreateNativeFunctionValue(...)`

它做了两层事：

1. 用 `JS_NewCFunctionData(...)` 创建一个 JS 可调用对象
   - 真正被调用时会落到 `JsNativeFunctionInvoke(...)`
2. 把元数据挂到这个函数对象上
   - target address
   - return type
   - arg types

也就是说，脚本里拿到的 `get_flag` 虽然看起来像普通 JS 函数：

```js
get_flag(1, 2)
```

但实际上它背后是一个“带闭包数据的宿主函数”：
- 代码入口固定是 `JsNativeFunctionInvoke(...)`
- 真正调用谁，由构造时保存下来的地址元数据决定

这点和 Java 侧的 method wrapper 思路很像：
- JS 层拿到的是一个“代理出来的可调用对象”
- 真正执行时再根据内部元数据走到底层桥接逻辑

---

### 真正调用时，Nook 走哪条链路

当脚本执行：

```js
get_flag(1, 2)
```

会进入：

- `JsNativeFunctionInvoke(...)`

它的执行流程可以概括成：

1. 先从函数对象的闭包数据里取出：
   - `target_address`
   - `return_type`
   - `expected_argc`
   - `arg_types`
2. 检查传入参数个数是否匹配
3. 逐个参数调用 `ParseJsNativeCallValue(...)`
   - 把 JS 值转成 Native 可调用的内部表示 `NativeCallValue`
4. 如果目标地址其实对应的是 Nook 注册过的 `NativeCallback` 或 replace hook
   - 先走回调/替换逻辑
5. 否则走真正的原生函数调度：
   - `DispatchTypedNativeFunction(...)`
6. 把返回值再转回 JS：
   - `NativeCallValueToJs(...)`

所以这不是“把两个 JS number 生硬塞给一个函数指针”那么简单，Nook 中间做了一整层类型桥接。

---

### `ParseJsNativeCallValue(...)` 做了什么

这是 `NativeFunction` 能工作的关键一步。

在 `0xA` 里我们传的是：

```js
new NativeFunction(addr, "void", ["int", "int"]);
get_flag(1, 2);
```

所以它要把 JS 里的 `1`、`2` 转成 Native 可调用参数。

Nook 的 `ParseJsNativeCallValue(...)` 支持的核心类型包括：

- `bool`
- `int8/int16/int32/int64`
- `uint8/uint16/uint32/uint64`
- `float/double`
- `pointer`

它会按声明的参数类型去解析 JS 值。  
比如这里两个 `"int"` 最终就会落成整数参数，写进 `NativeCallValue`。

这一步的意义是：
- JS 里的 number 只是动态值
- Native 调用时必须明确知道按什么 ABI / 什么宽度 / 什么寄存器规则传参

所以 `NativeFunction` 必须要求你显式写出：

```js
"void", ["int", "int"]
```

否则底层根本不知道该怎么 call。

---

### Nook 底层怎么真正 call 这个 Native 函数

最终落到：

- `DispatchTypedNativeFunction(...)`

这块实现非常值得讲，因为它体现了 Native 主动调用和 Native Hook 完全不是一回事。

`DispatchTypedNativeFunction(...)` 会先判断：
- 返回值是否用了浮点 ABI
- 参数里是否有 `float` / `double`

如果没有浮点类型，它会走比较直接的 raw 调用路径：

- `CallNativeFunctionRawVoid(...)`
- `CallNativeFunctionRawU64(...)`

本质上就是把目标地址强转成函数指针，然后调用。

但如果涉及浮点参数或浮点返回值，它不能再偷懒地全按 `uint64_t` 传，因为：
- 浮点参数和整数参数在 ABI 层可能走不同寄存器
- 返回值位置也可能不同

所以 Nook 又提供了另一层 typed dispatch：

- `DispatchTypedNativeFunctionWithAbi(...)`
- `InvokeTypedNativeFunction0/1/2(...)`
- `DispatchTypedNativeFunction1(...)`
- `DispatchTypedNativeFunction2(...)`

也就是说，它会根据参数类型组合，挑出合适的函数签名去调。

这里顺便能看出当前 Nook 实现的一个边界：
- typed dispatch 目前重点覆盖了 0、1、2 参数场景
- raw 路径能处理到 4 个参数

而 `0xA` 这里的 `get_flag(int, int)` 正好是一个最典型、最稳妥的 2 参数整数函数，所以非常适合拿来做 `NativeFunction` 入门示例。

---

### 这一题里，`NativeFunction` 和 `Interceptor.attach` 的定位区别是什么

这个对理解 Nook/Frida 的 Native 能力边界很重要。

`Interceptor.attach(addr, {...})` 是：
- 你不主动调函数
- 只是等别人调到这里时，你去观察/改参数/改返回值

`new NativeFunction(addr, ...)` 则是：
- 你自己主动把这个地址包装成一个 callable
- 然后直接从脚本里发起一次 Native 调用

所以 `0x8`、`0x9` 是“拦截别人调用”，  
`0xA` 是“自己调用别人”。

这也是为什么 `0xA` 对 Frida-like 能力很关键：
- 只有能主动 call Native 函数，脚本才不只是 Hook 工具
- 它开始具备了更强的运行时操控能力

---

### Frida 在这里大体也是同一类思路吗

是，语义上非常接近。

Frida 里的：

```js
new NativeFunction(ptr, retType, argTypes)
```

本质上也是：
- 你提供一个 Native 地址
- 再提供签名信息
- Frida 返回一个 JS 可调用对象
- 调这个对象时，底层按签名把参数编组后发起真正的 Native 调用

而：

```js
Process.attachModuleObserver(...)
```

本质也是：
- 观察模块何时进入进程
- 在模块可用时再做后续动作

所以在 `0xA` 这一题上，Nook 和 Frida 的外部语义已经是非常像的：
- 模块未加载时等待
- 模块已加载时拿 base
- `base + offset` 算出函数地址
- 用 `NativeFunction` 包成可调用对象
- 再主动调用

差别主要在底层实现细节：
- Frida 这套能力底座来自 Gum / GumJS
- Nook 则是在自己的 JS runtime + Native 桥接层里把这套能力补出来

---

### 把 `0xA` 压成一句话

如果把这一题的本质压成一句话，那就是：

- `Process.findModuleByName(...)` / `Process.attachModuleObserver(...)` 负责“等模块、拿基址”
- `new NativeFunction(...)` 负责“把一个 Native 地址包装成 JS 可直接调用的函数”
- `get_flag(1, 2)` 则是通过 Nook 的类型桥接和 typed dispatch，真正完成一次从 JS 主动发起的 Native 调用

所以 `0xA` 标志着 Nook 这类 Frida-like 工具不再只是“拦截已有调用”，而是已经能“从脚本主动驱动 Native 逻辑执行”。
## 0xB：`Memory.patchCode(...)` 背后做了什么，为什么这题不是 `Interceptor`

`0xB` 这题和 `0x8`、`0x9`、`0xA` 都不一样。

这次目标不是：
- 去 Hook 一个函数入口
- 也不是主动 call 一个 Native 函数

而是：
- 直接把目标 so 里的某条机器指令改掉
- 让原本一定会走失败分支的控制流，变成继续往下执行

Nook 里这题脚本是：

```js
var branch = module.base.add(0x15248);

Memory.patchCode(branch, 4, function (code, size) {
  code.writeByteArray([0x1f, 0x20, 0x03, 0xd5]);
});
```

这里写进去的 `1f 20 03 d5`，在 ARM64 上就是一条：

- `NOP`

也就是说，这题的本质是：
- 把 `b.ne` 那条条件跳转指令覆盖掉
- 让它“不再跳走”
- 程序自然继续执行后面的解码逻辑

---

### 为什么这题不是 `Interceptor.attach(...)`

这是理解 Native 能力边界时一个很关键的区分。

`Interceptor.attach(...)` 的思路是：
- 函数照常执行
- 你在函数入口/返回点插进去观察、改参、改返回值

但 `0xB` 这题的问题不在“函数入口参数不对”，而在：
- 函数内部某条分支指令把后面的逻辑直接跳过去了

也就是说，问题发生在：
- 函数体内部
- 某一条具体机器指令上
- 而不是入口参数或最终返回值上

所以这题最直接的思路不是：
- Hook 入口再想办法模拟后续逻辑

而是：
- 直接改掉那条分支指令本身

这就是典型的“inline patch / instruction patch”场景。

---

### 这题脚本整体在做什么

Nook 的 `0xB` 脚本流程很简单：

1. 先 `Process.findModuleByName("libfrida0xb.so")`
2. 如果 so 已经加载，立刻 patch
3. 如果还没加载，就 `Process.attachModuleObserver(...)`
4. 等模块出现后，取：

```js
module.base.add(0x15248)
```

5. 对这 4 字节做 `Memory.patchCode(...)`
6. 把原来的 `b.ne` 改成 `NOP`

这里仍然沿用了 `0xA` 的模块等待思路，因为：
- 没加载时你拿不到基址
- 拿不到基址就算不出真正的指令地址

所以 `0xB` 的前半段和 `0xA` 很像，差别在后半段：
- `0xA` 是 `new NativeFunction(...)` 然后 call
- `0xB` 是 `Memory.patchCode(...)` 直接改内存里的指令字节

---

### 为什么 Nook 这里用 `Memory.patchCode(...)`，而不是 `Arm64Writer`

Frida 的原始写法里，常见路线是：
- `X86Writer`
- `Arm64Writer`

也就是：
- 让 Writer 按架构帮你编码指令
- 再把编码结果写回目标地址

但 Nook 当前没有把这套 Writer API 暴露出来。  
所以在这个仓库里，`0xB` 采用的是“等价效果路径”：

- 直接用 `Memory.patchCode(...)`
- 把 ARM64 的 `NOP` 指令字节手工写进去

也就是说，这里的差别主要在“前端 API 形式”：

Frida 更像是：
- 给你一个汇编写入器
- 你告诉它“写一条 NOP”

Nook 当前则是：
- 给你一段可 patch 的临时代码缓冲区
- 你自己把最终字节写进去

两者最终落地效果是一样的：
- 把目标地址处的机器码改掉

---

### `Memory.patchCode(address, size, apply)` 在 Nook 里怎么做

Nook 对这个 API 的实现入口是：

- `JsMemoryPatchCode(...)`

这个函数的语义其实很清楚：

1. 你给它一个目标地址
2. 给它一个 patch 大小
3. 再给它一个 `apply(code, size)` 回调
4. Nook 先准备一块可写的临时缓冲区
5. 让你在回调里修改这块缓冲区
6. 然后它再把修改后的字节安全地拷回真实代码页

所以 `Memory.patchCode(...)` 不是“你直接拿目标 `.text` 去写”，而是：
- 先改临时副本
- 再统一提交到真实目标地址

---

### Nook 在 `patchCode` 里做了哪些步骤

`JsMemoryPatchCode(...)` 主要做这几步：

1. 校验参数
   - 地址必须是非 0 指针
   - size 必须大于 0
   - apply 必须是函数

2. 检查目标内存是否可读
   - `IsReadableMemoryRange(...)`

3. 分配一块 `scratch` 临时缓冲区
   - `malloc(size)`

4. 先把目标地址当前那段字节拷进 `scratch`
   - `memcpy(scratch, target, size)`

5. 把 `scratch` 指针包装成 JS 可操作的指针值
   - 作为 `apply(code, size)` 里的 `code`

6. 调 JS 回调
   - 让脚本在这块临时副本上执行：

```js
code.writeByteArray([0x1f, 0x20, 0x03, 0xd5]);
```

7. 读取目标原始页权限
   - `TryGetUniformProtectionForRange(...)`

8. 推导出可写权限版本
   - `TryMakeWritableProtectionString(...)`

9. 对目标页做页对齐
   - `ComputePageAlignedProtectionRange(...)`

10. 临时改页权限
   - Android/Linux 下最终走 `mprotect(...)`

11. 把 `scratch` 内容拷回真实目标地址
   - `memcpy(target, scratch, size)`

12. 刷指令缓存
   - `FlushInstructionCacheForRange(...)`

13. 恢复原始页权限

14. 释放 `scratch`

所以这整套 API 的核心价值是：
- 帮你处理代码页写保护
- 帮你处理 patch 后的 icache 刷新
- 把“可执行内存补丁”这件事收敛成一个稳定 API

---

### 为什么代码补丁一定要考虑页权限和指令缓存

这一步是所有 Native patch 框架都绕不过去的。

原因有两个：

1. `.text` 通常默认不可写  
   可执行代码页一般是 `r-x`，不是 `rwx`。  
   你如果直接往里面写，通常会崩。

2. 改完字节不代表 CPU 立刻按新指令执行  
   CPU / 内核可能还缓存着旧指令，所以改完后还需要：
   - flush instruction cache

这就是为什么简单的：

```js
ptr(...).writeByteArray(...)
```

往往不够稳。  
真正可用的 patch API，必须把：
- 权限切换
- 指令缓存刷新

一起处理掉。

Nook 的 `Memory.patchCode(...)` 正是在做这件事。

---

### 这一题里 `size = 4` 为什么刚好

因为当前仓库的 `0xB` 目标是 ARM64。

ARM64 固定长度指令：
- 每条指令 4 字节

而这里 patch 的就是一条：
- `b.ne`

所以：

```js
Memory.patchCode(branch, 4, ...)
```

刚好覆盖一条完整指令。  
再写入一条 4 字节的 `NOP`，就能把原分支完整替掉。

这和上游 x86 版本不一样。  
x86 指令是变长的，所以 Frida 原文里才会出现：
- 一条 `JNZ` 需要多个 `NOP` 去覆盖

而在这个 ARM64 版本里，问题简单很多：
- 一条旧指令
- 一条新指令
- 都是 4 字节

---

### 这里的 `code.writeByteArray(...)` 背后是什么

`Memory.patchCode(...)` 回调里给你的 `code`，本质上是一块临时内存的指针。  
Nook 在运行时里已经给指针对象挂了写内存能力，所以脚本可以直接：

```js
code.writeByteArray([...])
```

也就是说：
- 你写的不是原始 `.text`
- 而是 `scratch`
- Nook 在回调结束后再把 `scratch` 提交到真实代码地址

这点和很多人直觉里理解的“回调里直接在目标地址写”不完全一样。  
Nook 这里更像是：
- staged patch
- apply callback 负责生成最终字节
- runtime 负责安全提交

---

### 这题为什么说是“partial”

仓库里的 `README.md` 把 `0xB` 标成了：

- `partial`

核心原因不是“patch 不生效”，而是：
- Frida 原始解法强调的是 `X86Writer` / `Arm64Writer`
- Nook 目前没有完整暴露这套 writer 风格 API

所以 Nook 现在支持的是：
- 通过 `Memory.patchCode(...)` 达到等价效果

也就是说，能力层面已经能完成这题，但 API 形态还没有 1:1 对齐 Frida 那套“汇编 Writer”体验。

这也是一个很适合在博客里讲的点：
- 从“能做”到“API 做得像 Frida”
- 中间其实还有一层产品化 / 抽象层补齐工作

---

### Frida 在这里通常是怎么做的

Frida 这题常见有两种表达：

1. 旧一点的写法
   - `X86Writer`
   - `Arm64Writer`

2. 新一点、更通用的写法
   - `Memory.patchCode(...)`
   - 回调里再配合对应 Writer，或者直接写字节

所以从语义上说，Frida 和 Nook 在这题上并不冲突：
- 两边都支持“对某段代码做原地补丁”
- 只是 Frida 的架构级 writer 暴露得更完整
- Nook 目前走的是更偏底层但足够实用的 `patchCode + byte patch` 路线

---

### 把 `0xB` 压成一句话

如果把这一题压成一句话，那就是：

- `0xB` 不是 Hook 某个函数，而是直接 patch so 里的分支指令
- Nook 通过 `Memory.patchCode(...)` 完成“生成补丁字节 -> 临时改页权限 -> 写回代码页 -> 刷新指令缓存 -> 恢复权限”这整套流程
- 所以它已经具备了 Frida-like 的 Native inline patch 能力，只是当前 API 更接近“直接补字节”，还没有完全对齐 Frida 的 `X86Writer/Arm64Writer` 体验
## Hook `libc.so!strcmp` 这种高频函数时，Nook 为什么一开始会卡，后来又是怎么收口的

这个问题本质上不是“`strcmp` 难 Hook”，而是：

- `strcmp` 是一个极高频的基础函数
- 一旦 Hook 引擎热路径里有多余开销
- 它会被 `strcmp` 这种函数成百上千次地放大出来

所以 Hook `strcmp` 特别容易暴露：
- 递归重入问题
- 热路径锁竞争
- 每次调用都进 JS 的同步阻塞问题

这也是为什么项目里会专门拿 `libc.so!strcmp` 来观察 Hook 引擎本身的成本。

---

### 一开始为什么会有明显卡顿

早期 Nook 的 native inline hook 路径，本质上是：

1. 命中 trampoline
2. 进 `DispatchInlineHookSlot(...)`
3. 取 slot / hook 信息
4. 构造 `HookEvent`
5. 同步进入 QuickJS 执行 `onEnter`
6. 调原函数
7. 再同步进入 QuickJS 执行 `onLeave`

`docs/step8.md` 里其实已经把这个问题点得很直接了：  
对于 `strcmp` 这种高频函数，最可怕的不是单次成本大，而是：

- 每一次 `strcmp`
- 都会完整走一遍 Hook 分发
- 而 JS callback 内部、日志、字符串处理、运行时辅助逻辑，又很可能继续触发新的 `strcmp`

于是很容易出现两类放大：

1. 高频同步跨语言调用放大  
   一次普通 `strcmp`，被放大成：
   - native hook 分发
   - JS runtime 进入
   - callback 执行
   - 再回 native
   - leave 阶段再来一次

2. 重入递归放大  
   如果 JS callback 或 runtime 内部又间接调用到 `strcmp`，  
   那么原本只是“正在处理 `strcmp` Hook”，会再次命中同一个 Hook。

这时用户看到的外部现象就是：
- 明显卡顿
- UI 发僵
- 某些场景下甚至像“白屏”或“冻住几秒”

---

### 最核心的问题其实是“重入”

`strcmp` 这种函数和普通业务函数不一样。

比如你 Hook 某个 JNI 导出函数：
- 它可能只在点按钮时触发一次
- JS callback 里就算成本高一点，也未必立刻看得出来

但 `strcmp` 是 libc 基础函数：
- 系统库会调
- 运行时会调
- 你的脚本日志和字符串相关路径也可能调

所以它特别容易形成这种局面：

```text
hooked strcmp
  -> 进入 JS onEnter
     -> JS/runtime/日志路径内部再次触发 strcmp
        -> 又命中 hooked strcmp
           -> 再次进入 JS
```

如果没有“当前线程正在 hook 分发中，后续同类触发直接 bypass”的保护，  
那这个开销会非常夸张。

---

### Nook 后来是怎么解决这个问题的

Nook 当前实际收口，主要靠三层。

---

### 第一层：当前线程递归保护，直接 bypass 到 original

这层是最关键的。

现在 `src/agent_runtime/nook_native_js_bridge.cpp` 里有一个每线程状态：

```cpp
struct NativeJsInlineHookThreadState {
    uint32_t dispatch_depth = 0u;
    uint32_t ignore_level = 0u;
};
```

然后在 `DispatchInlineHookSlot(...)` 开头先判断：

```cpp
if (GetNativeJsInlineHookThreadState().dispatch_depth > 0u ||
    IsNativeJsInlineHookIgnoredOnCurrentThread()) {
    if (slot.original_function != nullptr) {
        return original(...);
    }
    return 0;
}
```

这段逻辑的意义非常直接：

- 如果当前线程已经在执行 hook 分发
- 或者当前线程被显式标记为忽略 hook
- 那么这次命中不再进 JS
- 直接跳 original

也就是说，现在 Nook 已经有了一个真正的“热路径快速逃生口”。

这点对 `strcmp` 尤其关键，因为：
- 它能挡住 JS callback 内部再次触发 `strcmp` 的重入
- 也能挡住 runtime/日志/字符串辅助逻辑造成的二次命中

---

### 第二层：进入 JS callback 前，临时把当前线程标记为 ignore

光有 `dispatch_depth` 还不够，Nook 还补了一层更明确的 suppress 机制。

现在代码里有：

- `PushNativeJsInlineHookIgnore()`
- `PopNativeJsInlineHookIgnore()`
- `ScopedNativeJsInlineHookIgnore`

并且在真正同步调用 JS callback 之前，会这样包一层：

```cpp
ScopedNativeJsInlineHookIgnore ignore_scope;
JsRuntime::InvokeNativeHookCallbackSync(...)
```

enter 和 leave 两边都这样做了。

这意味着：
- 一旦开始执行当前 Hook 的 JS callback
- 当前线程的 `ignore_level` 就会先加 1
- callback 期间如果又撞到 `strcmp`
- 直接走 bypass，不再继续进 JS

所以从现在的实现看，Nook 对 `strcmp` 卡顿的第一性修复，不是“把 JS 变快”，而是：
- 先把最致命的递归重入切掉

这也是 `docs/step8.md` 里优先级最高的那个点：
- 先做 thread-local guard / ignore

---

### 第三层：把热路径上的 slot 读取改成 runtime snapshot

早期设计文档里提到过一个问题：
- 每次触发都去锁表、读 slot、拼装状态

现在 Nook 已经往这个方向收了一步。

在 `ActivateInlineHookSlot(...)` 里，安装 hook 成功后会把热路径需要的最小信息整理到：

- `slot_runtime_snapshots`
- `slot_runtime_in_use`

也就是：

```cpp
std::array<std::atomic<bool>, kMaxNativeJsInlineHookSlots> slot_runtime_in_use = {};
std::array<NativeJsInlineHookRuntimeSnapshot, kMaxNativeJsInlineHookSlots> slot_runtime_snapshots = {};
```

而 `DispatchInlineHookSlot(...)` 走的是：

- `GetInlineHookRuntimeSnapshot(...)`

这条路径只用原子位判断 slot 是否可用，再读预先整理好的 runtime snapshot，  
不再像更早的思路那样每次都走完整的带锁查询。

这不能说已经到 Frida 那种极致程度，但至少说明 Nook 已经在做一件很明确的事：
- 把“安装期状态”和“触发期热路径状态”拆开
- 触发期尽量只读快照

对 `strcmp` 这种函数来说，这种优化是必须的，因为它不是被调用几十次，而是可能在短时间内被打上千次。

---

### 但 Nook 现在并不是完全没有成本

虽然递归和锁竞争已经明显收住了，但 Nook 当前热路径仍然比 Frida 重。

从 `DispatchInlineHookSlot(...)` 当前代码看，它每次触发仍然会：

1. 构造 `HookEvent enter_event`
2. 填参数、thread id、return address、sp/fp/lr/pc
3. 可选做 snapshot
4. blocking 模式下同步进 JS
5. 调 original
6. 再构造 `HookEvent leave_event`
7. 再进一次 JS

也就是说，Nook 现在解决的是：
- “一开始那种明显卡到不能用”的问题

但它并没有把热路径压到 Frida 那么薄。

尤其是如果你对 `strcmp` 这种函数还做：
- `onLeave`
- 大量日志
- `Thread.backtrace(...)`
- `DebugSymbol.fromAddress(...)`

那仍然会很贵。

---

### 所以 Nook 还有一条现实层面的收口：`blocking: false`

除了 native 热路径保护，Nook 后来还加了：

- `blocking: false`

这在 `docs/architecture.md` 和 `docs/code_review.md` 里都专门强调过。

它的语义是：
- JS callback 还是会跑
- 但被 Hook 的目标线程不再同步等 JS 跑完
- 因此这次调用不能再依赖 `args.replace(...)` / `retval.replace(...)` 去改变真实调用

也就是说：

- `blocking: true`
  适合真的要改参数/改返回值
- `blocking: false`
  适合 observer-only 场景，尽量别卡住目标线程

这个机制最初是为 `Thread.backtrace(...) + DebugSymbol.fromAddress(...)` 这种重观察脚本收口的，  
但它对 `strcmp` 这种高频函数同样成立：

- 如果你只是想看调用、采样、做观测
- 那就不该让目标线程每次都同步等待 JS

所以 Nook 现在对“高频 Hook 可用性”的思路其实是两条腿：

1. native 热路径里做 guard / bypass / snapshot
2. 脚本语义上提供 `blocking: false`，把 observer 场景从同步链路里拆出去

---

### Frida 底层是怎么做的

Frida 在这件事上更成熟，而且它的核心优化点正好和 Nook 对上了。

在 `frida-gum/gum/guminterceptor.c` 里，`_gum_function_context_begin_invocation(...)` 一进来就先做：

1. `gum_interceptor_guard_key` 检查
2. `ignore_level` 检查
3. 决定这次是否真的要 `invoke_listeners`
4. 如果不需要，直接 `goto bypass`

最关键的入口逻辑是：

```c
if (gum_tls_key_get_value (gum_interceptor_guard_key) == interceptor)
{
  *next_hop = function_ctx->on_invoke_trampoline;
  goto bypass;
}
gum_tls_key_set_value (gum_interceptor_guard_key, interceptor);
```

意思就是：
- 当前线程如果已经在这个 interceptor 上下文里
- 就不要再重入 listener
- 直接走 bypass

这和 Nook 现在的：
- `dispatch_depth > 0`
- `ignore_level > 0`
- 直接调 original

本质上是同一类思路。

然后 Frida 还有明确的每线程忽略计数：

```c
gum_interceptor_ignore_current_thread(...)
gum_interceptor_unignore_current_thread(...)
```

底层就是在维护：

- `interceptor_ctx->ignore_level`

当 `ignore_level > 0` 时，`invoke_listeners` 会被关掉。

---

### Frida 为什么在 `strcmp` 这种场景下更稳

因为它的“别进 listener”这件事发生得更早、更底层、更彻底。

Frida 的优势主要有几条：

1. 入口就有 TLS guard  
   不是进了一半才发现不该继续，而是 trampoline 进来后很快就能 bypass。

2. 有每线程 `ignore_level`  
   运行时可以显式告诉 interceptor：当前线程先别再触发 listener。

3. `will_trap_on_leave` 是按需决定的  
   如果没有 `onLeave` 需求，就不一定走完整 leave trap。

4. invocation stack / listener 调度模型更成熟  
   它不是简单“构造两个事件对象然后同步进 JS 两次”的模型，而是围绕 invocation context / stack 做的。

所以 Frida Hook `strcmp` 之类高热函数时，并不是“没有成本”，而是它很早就把最致命的成本：

- 重入
- 无意义 listener 进入
- 不必要的 leave trap

压掉了。

---

### Nook 和 Frida 在这件事上的关系

如果把这一段压成一句话：

- Nook 一开始 Hook `strcmp` 会明显卡，是因为高频 native 调用把“同步进 JS + 重入递归 + 热路径状态读取”这些成本全部放大了
- 后来 Nook 通过每线程 `dispatch_depth`、`ignore_level`、`ScopedNativeJsInlineHookIgnore`、runtime snapshot，以及 `blocking: false` observer 模式，把这个问题收到了可用范围
- Frida 则是在 Gum interceptor 入口层就用 `gum_interceptor_guard_key`、`ignore_level`、`bypass` 和按需 leave trap，把这类高频 Hook 的热路径做得更薄、更稳

所以从演进视角看，Nook 在这里其实就是：
- 先被 `strcmp` 这种高频函数逼出了 Hook 引擎热路径问题
- 再一点点往 Frida/Gum 那种“入口就快速判定、能 bypass 就 bypass”的架构方向靠
## 从原来的 Hook 框架视角看，Nook 整体多出来了哪些部分

如果只把早期 Nook 看成一个“Hook 框架”，那它的核心其实只有三块：

- Java Hook
- PLT Hook
- Inline Hook

也就是说，它更像是：
- 一套你编进自己 payload / so 里的 Hook 能力库
- 重点在“能 Hook”
- 还不是真正意义上的“工具链”

而 Nook 后来往 Frida-like 方向走时，新增的东西其实远不止几个 API，  
而是补出了一整套“让 Hook 能被动态使用起来”的外围系统。

可以从下面几层去看。

---

### 1. 多了 Host / Server / Agent 三层结构

这是最大的变化。

原来的 Hook 框架更像：
- 你的代码直接链接 Hook 库
- 然后在目标进程里自己干活

Nook 后来变成了：

- Host
  - CLI / Python SDK / REPL
- Device Server
  - `nook-server`
- Target Agent
  - 注入到目标进程里的运行时

这意味着项目从“库”变成了“分层系统”。

它带来的不是表面复杂度，而是两个根本能力：

1. 远程控制  
   你可以在 PC 侧控制设备上的目标进程，而不是把逻辑都编死进 payload。
2. 会话化工作流  
   attach / spawn / load script / unload / resume / detach 这些动作开始成为一整套流程。

---

### 2. 多了通信层和协议层

一个纯 Hook 框架不需要太在乎“消息协议”，  
但 Frida-like 工具必须有控制平面。

所以 Nook 后来补了：

- transport
  - TCP / Unix socket / ADB forward 这类传输
- protocol
  - frame / tlv / message types
- session
  - 会话管理、请求响应关联、消息分发
- server handlers
  - 处理 host 发来的脚本、RPC、resume、detach 等请求

这部分的意义是：
- Hook 本身不再是孤立的本地动作
- 而是被纳入一个可远程操控的协议系统

没有这层，项目再强也只是“可嵌入的 Hook 库”，不是“动态分析工具”。

---

### 3. 多了进程管理和注入工作流

原来的 Hook 框架一般默认：
- 代码已经在目标进程里了
- 或者你自己想办法把 so 加进去

Nook 后来补的是“工具如何把运行时送进去”。

这块新增的核心是：

- process enumerate / app enumerate
- attach
- spawn
- injector
- server 侧进程/会话管理

也就是说，Nook 不再假设：
- “payload 已经存在”

而是自己开始负责：
- 找进程
- 起进程
- 注入 agent
- 建立控制连接
- 挂起/恢复目标进程

这一步非常关键，因为它把项目从：
- “Hook 能力”

推进成了：
- “Hook 工作流”

---

### 4. 多了脚本运行时

这也是一个本质变化。

原来的框架是：
- 写 C/C++
- 编译
- 注入
- 运行

Nook 后来引入了：

- QuickJS runtime
- script registry
- script runtime bridge

于是工作模型变成了：

- `script.js`
- 动态加载
- 热更新
- 卸载
- post message / rpc 调用

这意味着 Nook 不再只是“编译期 Hook”，而开始支持：
- 运行时脚本化
- 动态试错
- 交互式调试

这个变化非常像 Frida 的核心体验来源：
- 不是只会 Hook
- 而是能用脚本在运行时驱动 Hook

---

### 5. 多了 JS Bridge，把底层 Hook 能力变成脚本 API

脚本运行时本身还不够，还得把底层能力暴露成可用 API。

所以 Nook 后来新增了整套 bridge：

- Java JS bridge
- Native JS bridge
- Script runtime bridge

这层做的事是把底层 C/C++ 能力翻译成脚本接口，比如：

- `Java.perform(...)`
- `Java.use(...)`
- `Interceptor.attach(...)`
- `Module.getExportByName(...)`
- `Memory.patchCode(...)`
- `NativeFunction(...)`

原来的 Hook 框架关注的是：
- “底层有没有这个能力”

Nook 后来还要再做一层：
- “用户能不能像 Frida 一样在脚本里用这个能力”

这就是从框架到工具时必然新增的一层抽象。

---

### 6. 多了 Process / Module / Memory / Thread 这些运行时对象模型

早期 Hook 框架里的很多能力是函数式的、底层的：
- 给个地址
- 装个 hook
- 改个返回值

但 Frida-like 工具需要的是一套更高层的对象模型。

所以 Nook 后来新增了：

- `Process`
- `Module`
- `Memory`
- `Thread`
- `DebugSymbol`
- `NativePointer`

这些东西的价值在于：
- 把“底层能力”组织成“运行时世界观”

换句话说，以前只是：
- 我能 Hook

后来变成：
- 我能从脚本里理解当前进程、模块、内存、线程、符号
- 并在这套模型上继续做 Hook、patch、call、scan、backtrace

这已经不是单一 Hook 库的范畴了。

---

### 7. 多了 deferred install / observer 这类“等时机成熟再装”的机制

原来的 Hook 框架很多时候是：
- 目标在那
- 直接装

但真实动态分析里，经常会遇到：
- so 还没加载
- class 还没 ready
- app 还没走到那个阶段

所以 Nook 后来新增了：

- native module observer
- deferred inline hook install
- Java 侧 deferred install / ready callback 机制
- `Process.attachModuleObserver(...)`
- `Module.attachExport(...)`

这部分很重要，因为它让 Nook 从：
- “只适合打现成地址”

进化到：
- “能跟着运行时生命周期走，等目标出现再 install”

这对工具化非常关键。

---

### 8. 多了脚本与目标进程之间的交互能力

一个纯 Hook 框架更偏“单向介入”：
- 我把东西 Hook 上去

但 Nook 后来补的是更完整的交互能力：

- `console.log` 回传 host
- post / recv 消息
- RPC exports
- Host 调 agent
- Agent 主动推事件

这意味着脚本不再只是：
- 在目标进程里孤立执行

而是：
- 可以和主机端形成双向通信

这也是 Frida-like 工具体验的核心之一。

---

### 9. 多了 spawn 语义和早期插桩能力

这是另一条非常大的新增线。

原来的 Hook 框架通常更偏：
- 进程已经起来了
- 我再 attach 或自己注入

但 Frida-like 工具真正难的是：
- 在应用很早期就插进去

所以 Nook 后来花了很多工程量补：

- spawn controller
- server 侧挂起 / 恢复
- zygote 路线
- legacy ncore / symbi / strict zygote-control 等方案

这部分不是“多一个命令”那么简单，而是新增了：
- 一整套进程生命周期控制逻辑

所以从项目整体看，spawn 能力几乎可以看成：
- Nook 从 Hook 框架走向工具的真正分水岭

---

### 10. 多了运行时状态管理、资源清理和生命周期治理

框架和工具还有一个很大的差别：

- 框架只要这次 Hook 成了就行
- 工具要长期活着、处理反复 attach/unload/reload/detach

所以 Nook 后来还新增了大量“不是功能 API，但非常工具化”的东西：

- script registry
- hook registry
- pending hook registry
- session registry
- detach / detachAll / revert / unload cleanup
- runtime shutdown cleanup
- deferred install status tracking

这些东西平时不显眼，但没有它们，项目就会变成：
- 演示时能跑
- 一旦多次重载、多脚本、多会话就乱

这也是工具化和单纯框架化最容易被忽略的差别。

---

### 11. 多了性能收口和 observer 语义

纯 Hook 框架早期更关注：
- 功能做出来

但 Nook 后来开始遇到：
- `strcmp`
- backtrace-heavy hook
- 高频 native observer

于是又新增了一层“运行时治理能力”：

- `blocking: false`
- hook 热路径递归保护
- ignore/bypass 思路
- runtime snapshot
- `DebugSymbol` cache

这说明项目新增的已经不只是功能模块，而是：
- 真正在为“高频、长期、交互式使用”补引擎级约束

这也是工具化阶段才会被逼出来的东西。

---

### 12. 多了打包、发布和部署模型

最后一个容易被忽略，但对“项目整体”很重要的新增点是：

- 单文件 `nook-server`
- 内嵌 agent / ncore / helper blob
- `pip install` 暴露 `nook-cli`
- 面向 GitHub 发布的仓库整理

也就是说，Nook 后来不只是：
- 开发仓库里能 build

而是开始考虑：
- 用户怎么拿到
- 怎么 push 到设备
- 怎么最小化部署资产
- 怎么发布成一个别人能直接上手的项目

这一步意味着它从：
- 研究代码

进一步走向：
- 可分发工具

---

### 压成一句话

如果把这个问题压成一句话，那就是：

- 早期 Hook 框架只有“Hook 能力本身”
- 而 Nook 后来在它外面补出了 Host/Server/Agent 三层、通信协议、注入与 spawn 工作流、脚本运行时、JS bridge、运行时对象模型、deferred install、双向交互、生命周期治理、性能收口，以及部署发布模型
- 也正因为补齐了这些东西，Nook 才开始从“一个 Hook 框架”变成“一个 Frida-like 动态 Hook 工具”
## 从一个例子来理解一次 Hook 背后完整的工作流程

如果只看脚本，Hook 往往像一句话那么简单：

```js
Java.perform(function () {
  var MainActivity = Java.use("com.ad2001.frida0x1.MainActivity");
  MainActivity.get_random.implementation = function () {
    return 1;
  };
});
```

但在 Nook 里，这句脚本背后其实会穿过很多层。  
如果要选一个最适合讲“完整工作流”的例子，我会选：

- `tests/Test_Lab/nook-frida-labs/frida-0x1/script.js`

因为它足够简单，但又刚好能把：

- host
- server
- agent
- JS runtime
- Java bridge
- 真正的 hook install
- 回调触发

这一整条链都串起来。

---

### 先把这次 Hook 压成一句话

这次 Hook 的本质是：

- 主机端把脚本送进目标进程
- 目标进程里的 QuickJS 执行脚本
- `Java.perform(...)` 把执行切进 Java VM
- `Java.use(...)` 找到目标类并构造方法 wrapper
- `.implementation = fn` 把 JS 函数登记成 Java hook 回调
- 底层安装真正的 Java hook
- 当目标方法被调用时，再从 Java/Native 层回调回 JS

也就是说，表面上是“一句 implementation”，  
实际上是“控制面 + 运行时 + 桥接层 + Hook 引擎”协同完成的一次链式动作。

---

### 第 1 步：Host 侧发起这次操作

用户真正做的事情通常是：

```powershell
nook-cli -U com.ad2001.frida0x1 -l .\tests\Test_Lab\nook-frida-labs\frida-0x1\script.js
```

这里 Host 侧承担的职责是：

1. 解析命令行参数
2. 连接设备上的 `nook-server`
3. 选择 attach 或 spawn 工作流
4. 建立一个面向目标进程的 session
5. 把 `script.js` 内容作为脚本加载请求发给 server

这一步和早期“把 Hook 库编进 payload”最大的区别就在于：
- Hook 不再是编译期动作
- 而是一次会话里的动态请求

这也是工具化和框架化的第一道分水岭。

---

### 第 2 步：Server 负责把 agent 放进目标进程，并建立控制连接

`nook-server` 接到 Host 请求后，不是自己去执行 Hook。  
它真正负责的是：

- 找到目标进程
- 或拉起目标进程
- 注入 `libnook-agent.so`
- 等 agent 在目标进程内初始化完成
- 建立 Host ↔ Server ↔ Agent 的控制通道

所以 server 更像是：
- 调度者
- 控制器
- 路由器

它把“我想在这个进程里执行一段脚本”这件事，变成一个真的能在目标进程里落地的运行时环境。

没有这层，脚本根本到不了目标进程。

---

### 第 3 步：Agent 启动 QuickJS 运行时，并接收脚本

agent 进入目标进程后，会初始化自己的运行时环境：

- QuickJS runtime
- script registry
- native / java / rpc / message bridge

接着 server 把 `SCRIPT_LOAD` 这类请求转发给 agent，  
agent 再把脚本内容交给 `JsRuntime::Evaluate(...)` 一类入口去执行。

这一步非常关键，因为它意味着：
- 脚本已经不是“外部描述”
- 而是真正成为目标进程内部的一段运行时代码

从这里开始，后面的所有 `Java.*`、`Module.*`、`Interceptor.*` 才有机会工作。

---

### 第 4 步：JS 运行时先执行 `Java.perform(...)`

脚本一加载，首先跑到的是：

```js
Java.perform(function () { ... });
```

这一步表面语义很简单：
- “在 Java 可用的时候执行回调”

但对 Nook 来说，它实际做了两件事：

1. 确保当前执行上下文能安全进入 Java VM
2. 在 Java ready / class loader ready 的语义下安排这个回调

也就是说，这一步不是 Hook 本身，  
而是在给后面的 `Java.use(...)` 和 `implementation = fn` 铺路。

如果连 Java VM 都没切进去，后面的 Java bridge 根本没法工作。

---

### 第 5 步：`Java.use(...)` 不是直接拿类，而是先构造 wrapper

接下来脚本会执行：

```js
var MainActivity = Java.use("com.ad2001.frida0x1.MainActivity");
```

这一步很多人容易直觉理解成：
- “已经拿到了真实 Java 类”

但无论是 Nook 还是 Frida，语义都更接近：
- 构造了一个 JS 层的 class wrapper / proxy

这个 wrapper 里会延迟解析：

- 方法
- 字段
- 重载信息
- static / instance 元数据

所以 `Java.use(...)` 更准确的理解不是“立刻把一切都找好”，而是：
- 先把“如何操作这个类”的入口对象建出来

这样脚本后面写：

```js
MainActivity.get_random
```

时，才会进一步落到方法 wrapper 的生成和解析上。

---

### 第 6 步：`.implementation = fn` 才是真正的“安装 Hook 请求”

真正把这次 Hook 变成底层 install 动作的，是这一句：

```js
MainActivity.get_random.implementation = function () {
  return 1;
};
```

对脚本作者来说，这像是在“给方法换实现”。  
但 Nook 背后实际在做的是：

1. 取到这个方法 wrapper 对应的类名、方法名、签名等元数据
2. 把右侧 JS 函数登记成一个 runtime 持有的 callback
3. 构造一个 Java hook install request
4. 走到底层 Java hook 安装逻辑

也就是说，这一刻做的不是“直接执行替换”，而是：
- 把脚本层意图翻译成底层 Hook 引擎能够理解的安装请求

这和原来的 Hook 框架很不一样。  
早期框架里你通常是：

- 自己写 native callback
- 自己调 hook API 安装

现在则是：
- 脚本层一条赋值语句
- 由 bridge 自动完成这一整层翻译

---

### 第 7 步：底层真正安装 Java Hook

当 `implementation = fn` 落到 native bridge 后，  
Nook 会继续把请求传给 Java hook 子系统。

这里发生的核心动作是：

- 找到目标类和目标方法
- 确认签名
- 准备 callback receiver
- 安装 Java hook

如果是 deferred 场景，还会先登记 pending hook，  
等类或时机满足后再真正 install。

这一步才是传统意义上“Hook 引擎真的动起来”的地方。

所以从项目分层看：

- 前面几步在做“把脚本送到正确上下文”
- 这里才进入“底层 Hook 实现”

这也是为什么 Nook 后来多了那么多外围层。  
因为只有把前面所有控制面和运行时层补齐，底层 Hook 才能被脚本稳定驱动起来。

---

### 第 8 步：目标方法真正被调用时，回调链路再反向跑回来

安装成功后，这次 Hook 还没有结束。  
真正的价值要等目标方法实际执行时才体现。

当 app 运行到：

- `MainActivity.get_random()`

时，底层 Java hook 会先截获这次调用，  
然后再把控制流送回 Nook runtime 持有的 JS callback，也就是：

```js
function () {
  return 1;
}
```

于是完整链路变成：

1. Java 方法被调用
2. 底层 Java hook 命中
3. 回到 Nook 的 callback receiver
4. 再桥接回 JS runtime
5. 执行用户脚本里的实现函数
6. 把 JS 返回值再转回 Java / JNI 可接受的值
7. 最终把这个值作为真实方法返回值送回上层

这一步很关键，因为它说明：
- Nook 不只是“能 install”
- 而是真的形成了“目标进程调用 -> Hook 命中 -> JS 执行 -> 返回值回写”的闭环

这才叫一次完整的 Hook 工作流。

---

### 把整条链压成一个流程图

如果把这个例子压成一条线，大概就是：

```text
Host CLI
  -> Server
    -> inject agent
      -> QuickJS runtime 初始化
        -> load script.js
          -> Java.perform(...)
            -> Java.use(...)
              -> method wrapper
                -> implementation = fn
                  -> Java hook install
                    -> 目标方法后续真正执行
                      -> hook 命中
                        -> 回调回 JS
                          -> return 1
                            -> 写回真实返回值
```

看起来只是一个 demo，  
但它其实已经把 Nook 从 Hook 框架变成 Frida-like 工具时新增的所有关键层都带出来了：

- 控制层
- 注入层
- 通信层
- 运行时层
- bridge 层
- Hook 引擎层
- 回调回流层

---

### 这个例子最适合拿来说明什么

如果要用这一节服务博客正文，我觉得它最适合说明的是：

- 一次 Hook 从来不只是“底层装一个 Hook”
- 真正的工具化工作量，大量都花在了 Hook 之外
- Nook 后来新增的 Host/Server/Agent、脚本运行时、bridge、deferred install、session 与通信层，都是为了让这条链真的闭环

所以这个例子很适合放在“从框架到工具”的文章中间位置。  
它能把前面讲的那些抽象架构，压回到一个读者能直观看懂的具体场景里。

---

### 压成一句话

如果把这节再压成一句话，那就是：

- 表面上看，一次 Hook 只是脚本里的一句 `.implementation = fn`
- 但在 Nook 里，它背后其实要经过 Host 发起、Server 注入、Agent 建 runtime、JS bridge 翻译意图、底层 Hook 安装，以及方法真正触发后再回调回 JS 这一整条链
- 也正因为这条链被补齐了，Nook 才从一个 Hook 框架，开始变成一个真正可交互、可脚本化的 Frida-like 工具
