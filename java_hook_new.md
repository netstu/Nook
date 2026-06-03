# Java Hook 新方案总结：这次到底改了什么，和旧方案差在哪

## 前言

这一篇不再讲 Java Hook 的基础原理，而是专门总结这次 `Nook` 里 Java Hook 方案的重构。

目标很明确：

1. 把现在做出的改动梳理清楚
2. 和旧版 `E:\Learn\my_program\all_my_hook\kanxue\nook备份\Nook` 做一次完整对比
3. 结合当前源码，把“新方案是怎么工作的”讲明白

如果只看结论，那就是一句话：

旧方案本质上是“payload 自己起线程，反复 sleep + 重试安装”；新方案则变成了“framework 统一接管时机，先注册 pending hook，再通过类加载观察器在目标类真正加载后完成安装”。

这两个方案都能把 Hook 装上，但工程质量、稳定性、可维护性完全不是一个层级。

---

## 一、为什么要重构

旧方案在最开始能跑通，是因为它足够直接：

1. payload 被加载
2. `NookPayloadStart()` 起一个线程
3. 先 `usleep(100000)`
4. 调 `NookJavaHookInitialize()`
5. 然后循环多次执行 `NookJavaHookHook(...)`
6. 成功就结束，失败就继续 sleep 重试

这个思路的优点是简单粗暴，早期验证非常快。

但问题也很明显：

1. Hook 安装时机由 payload 自己控制，framework 不知道外面在干什么
2. 本质上是在“猜”目标类什么时候可用，而不是在“观察”目标类什么时候真的加载
3. 每个 payload 都可能重复写一套安装时序逻辑
4. 日志、状态、取消安装、去重这些能力都很难统一
5. 如果类加载时机偏晚，固定次数轮询可能直接错过
6. 如果初始化太早，`FindClass`/`GetMethodID` 会失败

所以这次重构的核心不是“把 Hook 功能改强”，而是把“时机控制权”从 payload 手里收回到 framework。

---

## 二、旧方案到底是怎么做的

旧版本关键代码主要在：

1. `src/framework/NookJavaHook.cpp`
2. `src/framework/NookJavaHookPayload.cpp`

### 1. 旧版 `NookJavaHook.cpp` 很薄

旧版 `src/framework/NookJavaHook.cpp` 基本只是个转发层。

`NookJavaHookInitialize()` 直接调：

```cpp
return JavaHook::Init() ? NOOK_STATUS_OK : NOOK_STATUS_INTERNAL_ERROR;
```

`NookJavaHookHook()` 直接调：

```cpp
return JavaHook::HookMethod(
    class_name,
    method_name,
    signature,
    is_static != 0,
    ...);
```

也就是说，旧版 framework 不负责时序，也不负责 deferred 安装，更没有 pending registry 这种概念。

它只提供两个能力：

1. 初始化 Java Hook 运行时
2. 立即对某个类方法执行安装

至于“什么时候安装”，完全交给 payload 自己想办法。

### 2. 旧版 `NookJavaHookPayload.cpp` 自己起线程轮询

旧版 `src/framework/NookJavaHookPayload.cpp` 的核心逻辑是 `NookPayloadInstallThread()`：

```cpp
if (NookJavaHookInitialize() != NOOK_STATUS_OK) {
    return;
}

for (int attempt = 0; attempt < retry_count; ++attempt) {
    ...
    int hook_id = NookJavaHookHook(...);
    ...
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
}
```

而 `NookPayloadStart()` 会再包一层线程，并且先睡 100ms：

```cpp
std::thread([]() {
#if !defined(_WIN32)
    usleep(100000);
#endif
    NookPayloadInstallThread();
}).detach();
```

所以旧方案的真实模型其实是：

```text
payload constructor / start
    -> sleep 100ms
    -> initialize
    -> loop:
         try hook
         fail -> sleep
         try again
```

### 3. 旧方案不是“监控类加载”，而是“反复试到成功”

这一点很重要。

很多人第一次看旧方案，会误以为它是在等类加载。其实不是。

它没有监控 `ClassLoader.loadClass()`，也没有监控 `Application.attach()`，更没有维护某个“等待安装列表”。

它只是不断调用：

```cpp
JavaHook::HookMethod(...)
```

如果那个时候类已经能被找到，安装成功；如果还没准备好，就继续下一轮。

这是一种“重试式安装”，不是“事件驱动式安装”。

---

## 三、新方案增加了什么

这次重构后，Java Hook 相关新增能力主要分三层：

1. API 层：新增 deferred 接口
2. Framework 层：增加初始化状态机和 pending request 处理逻辑
3. Java Hook 层：增加类加载观察器、ClassLoader 解析器、pending 注册表

对应新增或重点变更的文件：

1. `include/nook/NookJavaHook.h`
2. `src/framework/NookJavaHook.cpp`
3. `src/framework/NookJavaHookPayload.cpp`
4. `src/framework/NookJavaHookInternal.h`
5. `src/java_hook/deferred/pending_java_hook_registry.cpp`
6. `src/java_hook/deferred/java_hook_class_observer.cpp`
7. `src/java_hook/deferred/java_hook_loader_resolver.cpp`
8. `src/java_hook/JavaHook.cpp`

---

## 四、新 API 的变化

### 1. 新增 `NookJavaHookHookDeferred`

当前 `include/nook/NookJavaHook.h` 里最关键的新接口就是：

```cpp
int NookJavaHookHookDeferred(const char* class_name,
                             const char* method_name,
                             const char* signature,
                             int is_static,
                             NookJavaHookCallback callback);
```

它和旧的 `NookJavaHookHook(...)` 的区别在于：

1. `NookJavaHookHook(...)` 要求 framework 已经初始化完成，并且目标类当前就能被找到
2. `NookJavaHookHookDeferred(...)` 允许“现在先注册，等未来合适的时机再装”

这个接口一出现，整个方案的思路就变了。

以前是：

```text
payload 负责想办法把 Hook 装上
```

现在是：

```text
payload 只负责声明“我要 Hook 谁”
framework 负责决定“什么时候真正装上”
```

---

## 五、新方案的整体链路

先看完整流程，再拆源码。

### 1. payload 启动时不再自己轮询安装

现在 `src/framework/NookJavaHookPayload.cpp` 里的 `NookPayloadRegisterHooks()` 不再自己循环调用 `NookJavaHookHook(...)`，而是：

1. 先请求 framework 做 deferred initialize
2. 遍历所有声明的 hook
3. 调 `NookJavaHookHookDeferred(...)` 注册请求

核心代码：

```cpp
nook::java_hook_internal::EnsureDeferredInitialize(requested_retry_count, retry_interval_ms);

const int request_id = NookJavaHookHookDeferred(
    decl->class_name,
    decl->method_name,
    decl->signature,
    decl->is_static,
    decl->callback);
```

所以 payload 现在只干两件事：

1. 把 Hook 声明交出去
2. 把初始化交给 framework

它自己不再做 sleep + 轮询安装。

### 2. framework 先尝试立即安装，失败再转 pending

`src/framework/NookJavaHook.cpp` 里的 `NookJavaHookHookDeferred(...)` 逻辑是：

1. 参数校验
2. 如果 framework 已经初始化，先试一次 `InstallNow(...)`
3. 如果这次就成功，直接返回 hook id
4. 如果失败，就注册到 `PendingJavaHookRegistry`
5. 如果 framework 已初始化，则确保 observer 已安装，并调度一次 pending retry
6. 如果 framework 还未初始化，则开启 deferred initialize worker

关键逻辑如下：

```cpp
if (nook::java_hook_internal::IsInitialized()) {
    const int hook_id = nook::java_hook_internal::InstallNow(...);
    if (hook_id >= 0) {
        return hook_id;
    }
}

const int request_id = PendingJavaHookRegistry::Instance().Register(...);

if (nook::java_hook_internal::IsInitialized()) {
    JavaHookClassObserver::EnsureInstalled();
    JavaHookClassObserver::SchedulePendingRetry(class_name, 0);
} else {
    nook::java_hook_internal::EnsureDeferredInitialize(40, 100);
}
```

这一步非常关键，因为它决定了新方案不是“纯粹等待”，而是“先立即尝试一次，失败再延后”。

### 3. deferred initialize 成功后安装观察器

`NookJavaHook.cpp` 里新增了初始化状态机：

1. `0` 表示未初始化
2. `1` 表示初始化中
3. `2` 表示初始化完成

后台线程 `DeferredInitializeWorker(...)` 会不断尝试：

```cpp
if (NookJavaHookInitialize() == NOOK_STATUS_OK) {
    JavaHookClassObserver::EnsureInstalled();
    JavaHookClassObserver::SchedulePendingRetry(nullptr, 0);
    return;
}
```

也就是说，初始化一旦成功，framework 会立即做两件事：

1. 安装类加载观察器
2. 主动处理一次所有 pending request

### 4. 观察器负责盯住类加载时机

`src/java_hook/deferred/java_hook_class_observer.cpp` 里，这次新增了两个内部 Hook：

1. `android/app/Application.attach`
2. `java/lang/ClassLoader.loadClass`

安装逻辑在 `JavaHookClassObserver::EnsureInstalled()`：

```cpp
int attach_hook_id = nook::java_hook_internal::InstallNow(
    "android/app/Application",
    "attach",
    "(Landroid/content/Context;)V",
    0,
    OnApplicationAttach);

int load_class_hook_id = nook::java_hook_internal::InstallNow(
    "java/lang/ClassLoader",
    "loadClass",
    "(Ljava/lang/String;)Ljava/lang/Class;",
    0,
    OnClassLoaderLoadClass);
```

它的作用分别是：

1. `Application.attach` 触发时，说明应用上下文和 class loader 环境更完整了，适合再处理一轮 pending hook
2. `ClassLoader.loadClass` 触发时，可以知道某个目标类刚刚被加载，这时只处理这个类对应的 pending hook

这就是新方案和旧方案最大的本质区别：

旧方案靠时间猜测，新方案靠类加载事件触发。

---

## 六、Pending Registry 是怎么工作的

新增的 `src/java_hook/deferred/pending_java_hook_registry.cpp` 相当于是一个“待安装 Hook 请求表”。

### 1. `Register()`：注册请求

当 deferred hook 当前装不上时，会先进入 registry：

```cpp
request.request_id = next_request_id_++;
request.class_name = normalized_class_name;
request.dot_class_name = dot_class_name;
request.method_name = method_name;
request.signature = signature;
request.is_static = is_static;
request.callback = callback;
request.hook_id = -1;
request.installed = false;
```

这里专门保存了两种类名：

1. slash 风格：`com/demo/target/AdWallFragment`
2. dot 风格：`com.demo.target.AdWallFragment`

原因很简单，JNI 和 `ClassLoader.loadClass()` 用的类名格式不同，后续匹配时两边都要兼容。

### 2. `MarkRetryScheduled()`：避免重复调度

观察器收到事件后，不会无脑并发起很多次安装线程，而是先把对应请求标记为 `retry_scheduled = true`。

这样可以避免：

1. 同一个类短时间内反复触发 `loadClass`
2. 多个线程同时处理同一批 pending request

### 3. `TryBeginInstall()` / `FinishInstall()`：控制单次安装状态

真正安装前会先：

```cpp
request.installing = true;
```

安装完成后再：

1. 成功则 `installed = true`，同时记录真实 `hook_id`
2. 失败则仅清理 `installing`，下次还可以继续尝试

所以这个 registry 不只是个 list，它实际上承担了三个角色：

1. 去重
2. 状态机
3. 请求到真实 hook id 的映射

这也是为什么 `NookJavaHookUnhook()` 现在要额外兼容 pending request id。

---

## 七、新版 `NookJavaHook.cpp` 做了哪些事情

这一版 `src/framework/NookJavaHook.cpp` 是整个新方案的调度中枢。

### 1. 初始化状态机

新增：

```cpp
std::atomic<int> g_java_hook_init_state{0};
```

配合：

1. `IsInitialized()`
2. `EnsureDeferredInitialize(...)`
3. `DeferredInitializeWorker(...)`

framework 首次真正具备了“初始化只做一次、后台异步推进”的能力。

### 2. `InstallNow(...)`

现在 framework 不再直接把所有安装逻辑散落在外部，而是通过内部统一入口：

```cpp
int InstallNow(const char* class_name,
               const char* method_name,
               const char* signature,
               int is_static,
               NookJavaHookCallback callback)
```

这个函数内部做的事情很直接：

1. 用 `JavaHookClassObserver::ScopedSuppression` 临时屏蔽观察器递归触发
2. 调 `JavaHook::HookMethod(...)`
3. 把 framework callback 适配到 `JavaHook::HookMethod(...)` 的 callback 形式

之所以要做 `ScopedSuppression`，是因为观察器本身也是通过 Java Hook 装上去的。如果在观察器回调里再触发观察器，就很容易递归或者重入。

### 3. `ProcessPendingRequests(...)`

这是新方案真正把 pending request 落地成真实 hook 的地方。

流程是：

1. 先从 registry 里拿出匹配 class 的 pending request id
2. 对每个 request 调 `TryBeginInstall(...)`
3. 成功进入安装流程后，调用 `InstallNow(...)`
4. 再把结果写回 `FinishInstall(...)`

也就是说，observer 只是“触发器”，真正安装还是在 framework 内统一收口。

---

## 八、`JavaHookClassObserver` 为什么能解决时机问题

`src/java_hook/deferred/java_hook_class_observer.cpp` 是这次 Java Hook 新方案的核心。

### 1. 观察 `Application.attach`

回调 `OnApplicationAttach(...)` 很简单：

```cpp
JavaHookClassObserver::SchedulePendingRetry(nullptr, 0);
```

意思是：应用 attach 完成后，重新尝试全部 pending request。

这一步的意义在于：

1. 很多 app 自己的 class loader、context、资源环境，在 `attach` 之后才真正稳定
2. 如果 payload 注入过早，这里能补一次“环境已经就绪”的安装机会

### 2. 观察 `ClassLoader.loadClass`

回调 `OnClassLoaderLoadClass(...)` 会：

1. 取出入参里的类名字符串
2. 判断 registry 里是否有这个类的 pending request
3. 如果有，就只调度这一类的安装

关键逻辑：

```cpp
if (PendingJavaHookRegistry::Instance().HasPendingForClass(class_name.c_str())) {
    JavaHookClassObserver::SchedulePendingRetry(class_name.c_str(), 0);
}
```

这一步把方案从“时间驱动”变成了“类加载事件驱动”。

只要目标类真的开始加载，就会触发一次精准安装。

### 3. `SchedulePendingRetry(...)`

这一层不是直接在观察器回调里安装，而是异步起线程：

```cpp
std::thread([request_ids, class_name_copy, delay_ms]() {
    ...
    nook::java_hook_internal::ProcessPendingRequests(...);
    ...
}).detach();
```

这样做的好处是：

1. 观察器回调尽量短，不把复杂安装逻辑直接塞进被 Hook 方法调用栈里
2. 避免在 `loadClass` 的执行上下文里做太重的事情
3. 方便统一加 suppression，避免重入

---

## 九、`JavaHookLoaderResolver` 和新版 `FindClass` 改了什么

这一块是这次方案里另一个关键点。

对应文件：

1. `src/java_hook/deferred/java_hook_loader_resolver.cpp`
2. `src/java_hook/JavaHook.cpp`

### 1. 新增 `JavaHookLoaderResolver`

它主要提供四类能力：

1. 类名 slash/dot 互转
2. 获取当前 `Application`
3. 获取当前应用的 `ClassLoader`
4. 用指定 loader 查找或加载类

核心函数包括：

1. `GetApplicationClassLoader(...)`
2. `FindLoadedClassWithLoader(...)`
3. `LoadClassWithLoader(...)`

### 2. 新版 `JavaHook::FindClass()` 不再激进主动加载业务类

当前 `src/java_hook/JavaHook.cpp` 里的 `FindClass()` 逻辑是：

1. 先把类名规范化
2. 如果是 bootstrap/system class，先尝试 `env->FindClass`
3. 再拿到应用 `ClassLoader`
4. 先调用 `findLoadedClass`
5. 如果是普通业务类且没加载到，直接返回 `nullptr`
6. 只有 bootstrap class 才可能继续走 `loadClass`

关键代码：

```cpp
jclass loadedClass = JavaHookLoaderResolver::FindLoadedClassWithLoader(env, class_loader, className);
if (loadedClass != nullptr) {
    ...
    return loadedClass;
}

if (!bootstrapClass) {
    ...
    return nullptr;
}
```

这个变化非常重要。

旧思路很容易变成：

```text
类没加载？
那我帮你 loadClass 一下
```

但对业务类来说，这样做常常会引入额外副作用：

1. 改变原本的类加载时机
2. 触发未预期的初始化
3. 在错误的 ClassLoader 上下文里操作
4. 让整个 Hook 时序更加不可控

现在的新方案明确收敛为：

1. 普通业务类没加载，就老老实实返回空
2. 等观察器捕获到真实加载事件后再装

这比“强行 loadClass”更稳。

---

## 十、除了 deferred 之外，这次 Java Hook 运行时还修了哪些坑

这次重构并不只是“加了 observer + pending registry”，还顺手修了之前在 Java Hook 回调链路里暴露出的一些运行时问题。

这些修复虽然不全都属于“时序方案”，但它们直接决定了新方案能不能稳定跑。

### 1. `thiz` 处理修正

现在 `hook_handler(...)` 里，实例方法的 `thiz` 会这样处理：

```cpp
if (!hookInfo.isStatic && thiz != nullptr) {
    callbackThis = env->NewLocalRef(thiz);
}
```

这比早期直接把内部引用对象裸传给回调更安全，因为 callback 收到的是标准 JNI local ref。

### 2. 对象参数不再按“栈压缩引用”错误解码

当前对象参数处理是：

```cpp
ownedLocalRefs[i] = env->NewLocalRef(reinterpret_cast<jobject>(obj_ptr));
args[i].l = ownedLocalRefs[i];
```

而不是再走早期那种容易出问题的 `create_local_ref_from_stack_ref(...)` 路径。

这个问题之前非常典型：

1. 参数明明是对象
2. 结果按错误的压缩引用/栈布局去解析
3. 最终得到非法 jobject
4. 轻则 Hook 无效，重则直接崩

### 3. `ScopedGCCriticalSection` 按真实语义使用

现在是：

```cpp
char gcScope[256] = {};
ArtInternals::SGCFn(gcScope, thread, kGcCauseDebugger, kCollectorTypeDebugger);
...
ArtInternals::DestroyGCFn(gcScope);
```

而不是把它误当成某种返回句柄的 API。

这个修正很关键，因为 ART 这类内部结构很多本来就是栈对象语义，错用后果通常不是“逻辑不对”，而是直接内存破坏。

### 4. 初始化逻辑变成“立即尝试 + 异步补偿”

相比旧版固定先 `usleep(100000)`，新方案的策略更合理：

1. 能立即初始化就立即初始化
2. 不能初始化再交给后台 worker 重试
3. 初始化一旦成功，立即安装 observer 并处理 pending

这种方式至少不会因为“固定先睡一会儿”而白白丢掉前面的时机窗口。

---

## 十一、新旧方案的源码级 diff

这一节直接按文件看差异。

### 1. `include/nook/NookJavaHook.h`

旧版只有：

1. `NookJavaHookInitialize`
2. `NookJavaHookHook`
3. `NookJavaHookUnhook`
4. `NookJavaHookUnhookAll`

新版新增：

1. `NookJavaHookHookDeferred`

这代表 API 已经从“只支持立即安装”升级成“支持声明式延迟安装”。

### 2. `src/framework/NookJavaHook.cpp`

旧版特点：

1. 很薄
2. 直接调用 `JavaHook::Init`
3. 直接调用 `JavaHook::HookMethod`
4. 不管时机

新版特点：

1. 增加初始化状态机
2. 增加 `DeferredInitializeWorker`
3. 增加 `InstallNow`
4. 增加 `ProcessPendingRequests`
5. `Unhook` 兼容 pending request id

可以说，framework 层真正开始“管理 Hook 生命周期”就是从这个文件开始的。

### 3. `src/framework/NookJavaHookPayload.cpp`

旧版：

1. payload 自己起线程
2. 先 `usleep(100000)`
3. 自己循环安装
4. 重试次数和间隔由 payload 控制

新版：

1. payload 不再自己装
2. 只做 `EnsureDeferredInitialize(...)`
3. 只做 `NookJavaHookHookDeferred(...)`
4. framework 统一接管安装时机

也就是说，payload 从“安装执行者”退化成“Hook 声明提供者”。

这是职责分层上最大的变化。

### 4. `src/java_hook/deferred/`

旧版完全没有这个目录。

新版新增整个 deferred 子系统：

1. `pending_java_hook_registry.cpp`
2. `java_hook_class_observer.cpp`
3. `java_hook_loader_resolver.cpp`

这是新方案的核心增量。

### 5. `src/java_hook/JavaHook.cpp`

这个文件的变化不只是一点点。

除了时序配套之外，还包括：

1. 接入 `JavaHookLoaderResolver`
2. `FindClass()` 改成 `findLoadedClass` 优先
3. `thiz` local ref 修正
4. 对象参数 local ref 修正
5. `ScopedGCCriticalSection` 使用修正

所以这次重构不能只理解成“新增 deferred Hook API”，它其实把 Java Hook 的运行时细节也重新打磨了一遍。

---

## 十二、新方案现在是如何监控类加载并完成安装的

这一节把整个动作链串起来。

### 第一步：payload 注册 Hook 声明

宏或手写声明最终都会通过：

```cpp
NookPayloadRegisterJavaHook(...)
```

把目标方法信息注册进 payload 自己的声明表。

### 第二步：`NookPayloadStart()` 启动

`NookPayloadStart()` 只会执行一次，然后调用：

```cpp
NookPayloadRegisterHooks();
```

### 第三步：framework 开始准备初始化

`NookPayloadRegisterHooks()` 先调：

```cpp
nook::java_hook_internal::EnsureDeferredInitialize(...);
```

如果还没初始化，就起后台线程去做。

### 第四步：每个 Hook 请求先注册成 deferred request

接着每条声明都调：

```cpp
NookJavaHookHookDeferred(...)
```

如果此时能立即装上，就直接成功；装不上，就进入 `PendingJavaHookRegistry`。

### 第五步：framework 初始化成功后装上观察器

后台 worker 一旦 `NookJavaHookInitialize()` 成功，就会：

1. `JavaHookClassObserver::EnsureInstalled()`
2. `JavaHookClassObserver::SchedulePendingRetry(nullptr, 0)`

也就是“先把观察器挂上，再主动处理一轮全部 pending”。

### 第六步：应用运行过程中持续观察

如果某些 Hook 还是没装上，后续就依赖两个事件：

1. `Application.attach`
2. `ClassLoader.loadClass`

一旦触发，observer 就会判断这次是否命中了某个 pending class。

### 第七步：命中后异步处理 pending request

observer 调：

```cpp
SchedulePendingRetry(class_name, 0);
```

然后异步调用：

```cpp
nook::java_hook_internal::ProcessPendingRequests(...)
```

最终在 framework 里执行真实的 `InstallNow(...)`。

### 第八步：安装成功后 request 转换成真实 hook

`FinishInstall(...)` 会把 request 标记为：

1. `installed = true`
2. 记录 `hook_id`

到这里，这条 deferred hook 就从“待安装请求”变成了“已生效 hook”。

---

## 十三、旧方案和新方案各自的优缺点

### 1. 旧方案优点

1. 实现简单
2. 调试门槛低
3. 对验证型 demo 很友好

### 2. 旧方案缺点

1. 轮询驱动，时机不精确
2. 安装逻辑分散在 payload
3. hook 生命周期无法统一管理
4. 重复代码多
5. 类加载偏晚时容易失效

### 3. 新方案优点

1. framework 统一接管时机
2. payload 更轻，职责更清晰
3. 支持 pending request、去重、取消、状态跟踪
4. 基于类加载事件驱动，命中更准
5. 更适合后续扩展更多 Java Hook payload

### 4. 新方案代价

1. 代码量明显增加
2. 要维护 observer、自身 suppression、registry 状态
3. 对 ClassLoader 和 JNI 细节要求更高

但从工程角度看，这个复杂度是值得的。

因为旧方案的简单，本质上是把复杂度转嫁给每一个 payload；而新方案是把复杂度收敛进 framework，一次解决，全局复用。

---

## 十四、和旧备份方案最本质的区别

如果只保留一句最关键的总结，我认为应该是这句：

旧方案是在“失败后继续重试”，新方案是在“目标真正可 Hook 时再安装”。

展开来说：

### 旧方案

```text
payload
  -> sleep
  -> init
  -> try hook
  -> fail
  -> sleep
  -> try again
```

### 新方案

```text
payload
  -> register deferred hook
framework
  -> init when possible
  -> install observer
  -> class load observed
  -> process pending request
  -> install hook
```

所以两者的差别不只是“有没有循环 sleep”，而是整套设计哲学变了：

1. 从重试驱动变成事件驱动
2. 从 payload 控制变成 framework 控制
3. 从立即安装思维变成 deferred request 思维

---

## 十五、现在这套方案适合怎么理解

现在可以把 `Nook` 的 Java Hook 方案理解成两层：

### 第一层：底层 Java Hook 引擎

也就是 `src/java_hook/JavaHook.cpp` 负责的部分：

1. 找类
2. 找方法
3. 解码 ArtMethod
4. 改入口
5. 回调分发

### 第二层：上层安装调度框架

也就是这次新增的 framework + deferred 子系统：

1. 初始化状态管理
2. pending hook 请求注册
3. 类加载观察
4. 按时机安装
5. unhook 映射处理

旧方案基本只有第一层；新方案是把第二层补齐了。

---

## 十六、结论

这次 Java Hook 重构，本质上完成了三件事：

1. 新增 `NookJavaHookHookDeferred(...)`，让 Java Hook 从“立即安装 API”升级成“声明式延迟安装 API”
2. 新增 pending registry + class observer + loader resolver，把安装时机从 sleep 轮询切到类加载事件驱动
3. 顺手修正了 `FindClass`、对象参数、`thiz`、GC critical section 等运行时细节问题

如果用一句更工程化的话来总结：

以前 `Nook` 的 Java Hook 是“能跑”；现在这套方案才开始接近“可复用的 framework”。

后面如果继续扩展更多 Java Hook payload，或者继续适配更复杂的注入场景，应该都基于现在这套 deferred + observer 方案往前走，而不是再回到旧版那种“payload 自己线程里 sleep 重试安装”的做法。
