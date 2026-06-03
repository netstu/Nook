# 从0到1构建一个Hook工具之Native Hook篇（二）：Nook 的 Inline Hook 原理、实现与踩坑复盘

## 前言

前一篇我已经把 `PLT Hook` 跑通了，但很快就会遇到一个现实问题：`PLT Hook` 并不能覆盖所有 native 场景。

原因很简单，`PLT Hook` 劫持的是“导入调用链”，也就是某个模块通过 `GOT/PLT` 去调用外部函数的那条路。如果目标函数根本不是通过导入表调用的，或者我就是想直接改掉目标函数本体的执行流，那 `PLT Hook` 就不够了，这时候必须进入更麻烦的部分：`Inline Hook`。

这篇文章就只讲 `Inline Hook`。默认你已经看过前面的注入篇和 `PLT Hook` 篇，知道 so 注入、模块加载、ELF、符号解析这些基本概念。因此这篇不会再重复讲基础，而是重点讲三件事：

1. `Nook` 的 arm64 `Inline Hook` 最终是怎么落地的
2. 为什么最难的部分其实不是“改指令”，而是“等目标 so 加载”
3. 这一路上踩过哪些坑，最后为什么收敛到了现在这套方案

---

## 为什么 PLT Hook 不够，必须继续做 Inline Hook

`PLT Hook` 和 `Inline Hook` 的区别，不是都在 Hook native 函数这么简单，而是两者修改的层次完全不同。

`PLT Hook` 改的是导入槽位：

```text
call site
  -> PLT
  -> GOT slot
  -> target function
```

`Inline Hook` 改的是目标函数入口的机器码：

```text
target function prologue
  -> patch jump
  -> replacement
```

所以 `Inline Hook` 能覆盖更多场景：

1. 目标函数不是导入函数
2. 同模块内部直接跳转到该函数
3. 想直接接管某个 JNI 导出函数
4. 想保留原始执行流，同时在入口处插入 trampoline

这也是这次测试里最后选中这个目标的原因：

```cpp
extern "C" JNIEXPORT jboolean JNICALL
Java_com_demo_target_LoginFragment_verifyPasswordNative(
        JNIEnv* env,
        jobject /* thiz */,
        jstring password)
```

对这种函数，`PLT Hook` 不适合，`Inline Hook` 才是正路。

---

## 这次要解决的目标

这次在 `Nook` 里做的是第一版可用的 arm64 `Inline Hook`，边界很明确：

1. 只做 `arm64`
2. 提供独立于 `PLT Hook` 的公开 API
3. 支持按地址 Hook
4. 支持按 `module + symbol` Hook
5. 支持“目标 so 尚未加载时先注册，等加载后再安装”
6. 支持 `unhook`

对应的公开头文件是 `include/nook/NookInlineHook.h`：

```cpp
NookStatus NookInlineHookInitialize(void);
NookStatus NookInlineHookIsAvailable(int* available);
NookStatus NookInlineHookAddress(void* target_address,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle);
NookStatus NookInlineHookSymbol(const char* module_name,
                                const char* symbol_name,
                                void* replacement,
                                void** original,
                                void** hook_handle);
NookStatus NookInlineHookSymbolDeferred(const char* module_name,
                                        const char* symbol_name,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle);
NookStatus NookInlineUnhook(void* hook_handle);
```

这里多出来的 `NookInlineHookSymbolDeferred()`，其实就是后面整篇文章最关键的入口。

---

## Inline Hook 真正难的地方，不只是跳板

很多人一提 `Inline Hook`，第一反应就是：

1. 改前几条指令
2. 申请一块 trampoline
3. 把原始指令搬过去
4. 在原函数头部跳到 replacement

这当然没错，但这只解决了“怎么改入口”的问题，没有解决“什么时候改入口”的问题。

对 Android 场景来说，真正麻烦的是下面这两个条件经常同时出现：

1. payload 很早就被注入进进程了
2. 目标 so 这时候还没有加载

这就会导致一个经典失败场景：

```text
payload constructor 执行了
  -> 想 hook libnative-lib.so::Java_xxx
  -> 但是 libnative-lib.so 还没进内存
  -> resolve 失败
  -> hook 不生效
```

所以做 `Inline Hook`，至少要同时解决两件事：

1. 地址 patch 和 trampoline 的正确性
2. 目标模块加载时机的正确性

前者是汇编和内存改写问题，后者是运行时生命周期问题。实际做下来，第二个问题往往更折磨人。

---

## 第一版为什么不稳定

这部分其实就是这次做 `Inline Hook` 时最有价值的复盘。

### 1. 直接按符号安装，最先遇到的是“目标 so 还没加载”

如果直接走：

```cpp
NookInlineHookSymbol("libnative-lib.so",
                     "Java_com_demo_target_LoginFragment_verifyPasswordNative",
                     replacement,
                     &original,
                     &handle);
```

那它只适合目标 so 已经在内存里的场景。否则符号解析一定失败。

### 2. 轮询能用，但边界明显不对

后面一度改成 payload 自己开线程，循环检查目标 so 是否加载，加载后再去装 hook。

这个方案不是完全不能用，甚至在测试里一度是成功的，但问题也很明显：

1. payload 自己承担了时机控制
2. 每个 payload 都要自己写一份轮询逻辑
3. 有额外 wakeup 和延迟
4. 不够优雅，也不够像一个真正的框架

说白了，轮询更像是“为了验证 inline patch 本身是通的”而存在的过渡方案，不应该成为最终设计。

### 3. 只盯着 `dlopen` 一类入口，也不够稳

后面继续往前走时，会很自然地想到：既然轮询不好，那就去观察模块加载事件。

一开始最直观的做法，就是盯 `dlopen` / `android_dlopen_ext` 这类公开加载入口。但实际在 Android 上做下来，这条路并不总是稳：

1. 加载路径很多，时机也很敏感
2. 你看到“开始加载”不等于现在就适合安装 hook
3. 如果在错误的时间点做过多事情，轻则 hook 不生效，重则页面白屏、进程卡住

这也是为什么后面 `Nook` 最终没有停在“hook `dlopen`”这一层，而是继续往 linker 内部走了一步。

---

## 最终收敛出来的方案

在参考 `shadowhook/android-inline-hook`、`Dobby`、`GirlHook`、`ReZeroHook` 这些项目之后，`Nook` 最后收敛到的是一套更接近成熟框架的做法：

1. payload 不再轮询，只负责“声明我要 hook 谁”
2. 框架内部把请求注册成 pending hook
3. 框架异步安装一个 module observer
4. observer 不再只看 `dlopen`，而是直接观察 linker 的 `soinfo::call_constructors()`
5. 当目标 so 进入构造阶段时，再去尝试解析符号并安装 hook

可以把它理解成：

```text
payload constructor
  -> register pending hook
  -> start observer

linker load target so
  -> call soinfo::call_constructors()
  -> Nook observer notified
  -> match pending request
  -> resolve symbol in loaded module
  -> install inline hook
```

这个思路的核心变化是：不再“猜它什么时候会加载”，而是在 linker 真正处理目标模块时出手。

---

## 从测试 payload 开始看调用链

先看这次测试用的 payload：`examples/native_hook/nook_native_verify_password_inline_test/payload.cpp`

```cpp
constexpr char kTargetModule[] = "libnative-lib.so";
constexpr char kTargetSymbol[] = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

extern "C" jboolean hooked_verify_password(JNIEnv* env, jobject thiz, jstring password) {
    (void)env;
    (void)thiz;
    (void)password;
    __android_log_print(ANDROID_LOG_INFO, kTag, "hooked verifyPasswordNative => JNI_TRUE");
    return NookTestVerifyPasswordAlwaysTrue() ? JNI_TRUE : JNI_FALSE;
}

__attribute__((constructor(200))) static void on_library_loaded() {
    register_deferred_hook();
}
```

这里没有再开轮询线程，而是在构造函数里直接注册一个 deferred hook。

真正调用的是：

```cpp
api.hook_symbol_deferred(kTargetModule,
                         kTargetSymbol,
                         reinterpret_cast<void*>(hooked_verify_password),
                         &g_original_verify,
                         &g_hook_handle);
```

这一步非常重要，因为它把“立即安装”变成了“先登记，等时机成熟再安装”。

---

## `NookInlineHookSymbolDeferred()` 做了什么

入口在 `src/framework/NookInlineHook.cpp`。

先看核心逻辑：

```cpp
const NookInlineHookInternal::PendingInlineHookRequest request = {
        module_name,
        symbol_name,
        replacement,
        original,
        hook_handle};
if (!NookInlineHookInternal::RegisterPendingInlineHook(request)) {
    return NOOK_STATUS_INTERNAL_ERROR;
}

const NookStatus observer_status = NookInlineHookInternal::EnsureInlineHookModuleObserverAsync();
```

这层其实只做了三件事：

1. 参数检查和初始化
2. 把请求注册到 pending registry
3. 异步确保 module observer 已经启动

注意这里当前代码已经不再做“先立即尝试一次，再失败转 pending”的混合逻辑了，而是直接进入 deferred 流程。这也是前面几轮调试后刻意收敛出来的结果，目的就是减少时机判断上的不确定性。

---

## Pending Registry 是怎么工作的

内部注册表在 `src/native_hook/inline_hook/pending_inline_hook_registry.cpp`。

它存的并不是复杂对象，而是一条条很朴素的记录：

```cpp
struct PendingInlineHookEntry {
    std::string module_name;
    std::string symbol_name;
    void* replacement = nullptr;
    void** original = nullptr;
    void** hook_handle = nullptr;
    bool installed = false;
};
```

也就是说，payload 说的那句“我要 hook `libnative-lib.so` 里的 `Java_xxx`”，最后会被框架内部保存成这样一条待安装记录。

等 observer 上报一个模块路径后，注册表会把这个路径和所有 pending 记录做匹配：

```cpp
if (ElfHooker::module_path_matches(module_path, entry.module_name.c_str())) {
    matched_module_path = module_path;
    break;
}
```

而 `src/native_hook/core/module_match.cpp` 的匹配规则也比较实用：

1. 完整路径相等
2. basename 相等
3. 子串匹配

这样 payload 既可以传完整路径，也可以只传 `libnative-lib.so`。

一旦匹配成功，注册表就回调安装器：

```cpp
dependencies.install_symbol_hook(candidate.module_path.c_str(),
                                 candidate.symbol_name.c_str(),
                                 candidate.replacement,
                                 candidate.original,
                                 candidate.hook_handle,
                                 dependencies.context);
```

如果安装成功，就把这一条 pending 标记为 `installed = true`，后面不再重复安装。

---

## 为什么 observer 要盯 `soinfo::call_constructors()`

这一部分是整个方案最关键、也最像成熟框架的地方。

实现文件是 `src/native_hook/inline_hook/inline_hook_module_observer.cpp`。

### 1. 为什么不是继续轮询

因为轮询本质上是在猜。

### 2. 为什么不是只看 `dlopen`

因为“开始 load”不等于“现在已经到了适合 patch 的时刻”。

### 3. 为什么选 `call_constructors`

因为走到这里时，有几个条件同时成立：

1. 目标 ELF 已经映射到进程里了
2. 模块的 load bias、phdr、name 等信息已经成形
3. 符号解析已经有现实基础
4. 模块生命周期正处于一个比较稳定的节点

所以当前 `Nook` 直接 hook 了 linker 里的 `soinfo::call_constructors()`：

```cpp
constexpr char kLinkerCallConstructorsSymbolLower[] = "__dl__ZN6soinfo17call_constructorsEv";
constexpr char kLinkerCallConstructorsSymbolUpper[] = "__dl__ZN6soinfo16CallConstructorsEv";
```

然后安装 observer：

```cpp
const bool observer_installed =
        TryInstallObserverHook(call_constructors_address,
                               reinterpret_cast<void*>(HookedLinkerCallConstructors),
                               &g_original_linker_call_constructors,
                               &g_linker_call_constructors_handle);
```

这意味着之后每个 so 进入构造阶段时，`Nook` 都能先收到通知。

---

## probe 为什么存在

如果只是能 hook 到 `call_constructors()`，其实还不够，因为你拿到的参数是 `soinfo*`，问题马上变成：

> 不同 Android 版本下，`soinfo` 结构体内部字段偏移并不稳定，我怎么从这个指针里把模块路径、load bias 这些信息拿出来？

这就是 probe 的意义。

`Nook` 现在额外带了一个很小的 so：`examples/native_hook/common/nook_inline_observer_probe.cpp`

```cpp
__attribute__((constructor)) void OnProbeLoaded() {
    __android_log_print(ANDROID_LOG_INFO, kTag, "inline observer probe loaded");
}
```

它本身不做 hook，作用是给 observer 提供一个“已知样本”。

observer 初始化时，会先根据当前 payload 路径拼出 probe 路径：

```cpp
g_probe_module_path = JoinSiblingPath(payload_info.dli_fname, kProbeLibraryName);
```

然后临时 `dlopen` probe，让 linker 去处理它。这样当前 `call_constructors()` 收到的 `soinfo*` 就是 probe 对应的 `soinfo`。而 probe 的模块名、`dlpi_phdr`、`dlpi_phnum`、`dli_fbase` 这些信息我们又是已知的，于是就可以反向扫描 `soinfo` 内存，找出对应字段偏移：

```cpp
if (g_soinfo_offset_phdr == SIZE_MAX &&
    value_0 == reinterpret_cast<uintptr_t>(probe_info.dlpi_phdr) &&
    value_1 == static_cast<uintptr_t>(probe_info.dlpi_phnum)) {
    g_soinfo_offset_phdr = offset;
    g_soinfo_offset_phnum = offset + sizeof(uintptr_t);
}
```

再比如 `load_bias`、`name`、`constructors_called` 也是同理。

等 probe 这轮扫描跑通后，`Nook` 就知道了当前系统上 `soinfo` 的关键偏移，之后别的模块再进入 `call_constructors()`，就可以直接取出模块路径：

```cpp
const char* module_path =
        *(reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(soinfo) +
                                               g_soinfo_offset_name));
```

这就是现在这套方案里 probe 存在的根本原因：不是为了业务功能，而是为了动态识别 linker 内部结构布局。

---

## observer 收到模块通知后，怎么完成真正安装

当 `HookedLinkerCallConstructors()` 收到某个模块的 `soinfo` 后，会先尝试拿到它的路径，然后触发：

```cpp
(void)NotifyModuleLoaded(module_path);
```

`NotifyModuleLoaded()` 的本质就是：

1. 拿着这个模块路径去 pending registry 里找匹配项
2. 对每个匹配项执行真正的符号解析和 inline 安装

对应代码：

```cpp
PendingInlineHookInstallerDependencies dependencies = {};
dependencies.install_symbol_hook = &InstallPendingInlineSymbolHook;
const size_t installed = TryInstallPendingInlineHooksForModule(module_path, dependencies);
```

到这里为止，才真正从“等待时机”进入“安装 hook 本体”的阶段。

---

## 符号是怎么定位到真实地址的

真正的符号定位在 `src/native_hook/core/native_hook_symbol_resolver.cpp`。

这里做了两层区分：

### 1. 模块已经加载时，优先解析当前进程里的真实模块

`ResolveSymbolAddressInLoadedModule()` 先通过 `/proc/self/maps` 拿到模块的运行时基址和真实磁盘路径：

```cpp
if (!ElfHooker::get_module_info(0, module_name, &module_base, &module_path) ||
    module_base == nullptr || module_path.empty()) {
    return false;
}
```

然后再用 `ELFIO` 去解析这个模块文件里的动态符号值：

```cpp
ElfHooker::ElfioImageParser parser;
parser.LoadFromFile(module_path);
parser.FindDynamicSymbolValue(symbol_name, &symbol_value);
parser.ComputeRuntimeBias(reinterpret_cast<uintptr_t>(module_base), &runtime_bias);
*symbol_address = reinterpret_cast<void*>(runtime_bias + static_cast<uintptr_t>(symbol_value));
```

也就是：

```text
runtime address = runtime_bias + symbol_value
```

这和前面 `PLT Hook` 里“文件视角”和“内存视角”分开处理的思路是一样的。

### 2. 普通立即解析时，优先用 XDL，再 fallback 到 `dlopen/dlsym`

`ResolveSymbolAddress()` 里对 Android 先走 `xdl_open/xdl_sym`，失败再退回 `dlopen/dlsym`。

这样做的目的就是尽量优先在“已加载模块”语义下定位符号，降低误差。

---

## 真正的 Inline Patch 是怎么做的

安装核心在 `src/native_hook/inline_hook/inline_hook_impl.cpp`。

当前 arm64 版本的 patch 大体流程很直接：

1. 读取目标函数前 `5` 条指令
2. 计算这些指令重定位后需要多少 trampoline 空间
3. 申请一块可执行内存
4. 把原始指令重写进 trampoline
5. 在 trampoline 尾部补一段跳回原函数后续地址的绝对跳转
6. 在目标函数入口写一段跳到 replacement 的绝对跳转
7. 保存原始字节，供 unhook 恢复

当前 patch 长度定义在：

```cpp
constexpr size_t kArm64InlineHookPatchWords = 5u;
constexpr size_t kArm64InlineHookPatchSize = kArm64InlineHookPatchWords * sizeof(uint32_t);
```

目标入口写入的是一段固定格式的绝对跳转：

```cpp
patch[0] = 0x58000051u;  // LDR X17, #8
patch[1] = 0x14000003u;  // B #12
patch[2] = low32(target);
patch[3] = high32(target);
patch[4] = 0xD61F0220u;  // BR X17
```

可以把它理解成：

```text
先把 replacement 地址字面量读进 X17
再 BR X17
```

这样不依赖近跳范围，比较适合做通用 patch。

安装成功后，`original` 返回的不是原函数地址本身，而是 trampoline 地址。也就是说，replacement 里如果还想继续走原始逻辑，调的是 trampoline。

---

## trampoline 里为什么一定要做指令重定位

如果只是把原函数前 20 字节原样拷贝到 trampoline，很多情况下会立刻出错。

原因是 arm64 里有大量 PC-relative 指令，比如：

1. `B` / `BL`
2. `B.cond`
3. `ADR` / `ADRP`
4. `LDR literal`
5. `CBZ` / `CBNZ`
6. `TBZ` / `TBNZ`

这些指令的目标地址是“相对于当前位置”计算出来的。你把它搬到 trampoline 后，“当前位置”变了，语义自然就错了。

所以 `Nook` 专门做了一个 arm64 relocator：`src/native_hook/inline_hook/arm64_instruction_relocator.cpp`

比如对 `B/BL`，当前做法不是硬算一个新的近跳，而是直接改写成一段绝对跳转：

```cpp
return EmitAbsoluteBranch(
        output, output_capacity_words, target, type == Arm64InstructionType::kBl, output_word_count);
```

对 `ADR/ADRP`，则改写成加载绝对地址字面量。

对 `B.cond`、`CBZ`、`TBZ` 这类条件跳转，当前做法也是展开成更长的序列，先保留条件语义，再在需要时跳向绝对地址。

这也是为什么 `Inline Hook` 不是“拷 20 字节再跳走”这么简单，而必须有一个真正可用的重定位器。

---

## hook record 和 unhook 是怎么做的

安装成功后，框架会把这次 hook 的关键信息记到 `src/native_hook/inline_hook/inline_hook_record.h` 里：

```cpp
struct InlineHookRecord {
    void* target_address = nullptr;
    void* replacement_address = nullptr;
    void* trampoline_address = nullptr;
    size_t patched_length = 0u;
    bool active = false;
    std::vector<uint8_t> original_code;
};
```

它解决的是两个问题：

1. `unhook` 时知道要恢复哪一段原始字节
2. 生命周期结束时能释放 trampoline

对应的 opaque handle 实际上就是：

```cpp
struct InlineHookHandle {
    InlineHookRecord record;
    TrampolineAllocation trampoline;
};
```

`NookInlineUnhook()` 最终走到 `UninstallInlineHook()`，把原始代码写回去，清缓存，释放 trampoline，删除 handle。

这一套虽然不复杂，但这是把“能 Hook”变成“能完整安装和卸载”的关键一步。

---

## 这一路上踩过的几个典型坑

这部分我觉得比“代码怎么写”更重要，因为 inline hook 真正难的地方，很多都藏在这些坑里。

### 1. hook 没生效，不一定是 patch 失败，可能只是时机错了

前面已经说过，最常见的问题其实不是汇编 patch 错，而是目标 so 还没加载，或者加载时机还不稳定。

这个坑如果不先想明白，后面所有调试都会非常乱。

### 2. 轮询能成功，不代表它是正确架构

轮询方案一度是有效的，所以很容易让人误以为“这就够了”。但那只是说明 patch 部分大概率没问题，不能说明框架设计已经正确。

### 3. 只看表面 loader 入口，很容易出现白屏、卡住或漏装

这一轮做下来，最大的体会就是：Android 上 native 模块加载的稳定观察点，不一定是表层 API。很多时候你真正该盯的是 linker 内部生命周期。

### 4. app 内直接 `System.loadLibrary()` 测 payload 时，要处理运行时依赖

这个坑在测试里也遇到过。payload 如果依赖 `libnook.so`，而当前 namespace 又找不到它，就会直接出现类似：

```text
dlopen failed: library "libnook.so" not found
```

所以 app 内直加载测试时，必须先把运行时依赖准备好，或者像当前例子一样通过 runtime loader 明确去加载：

1. `/data/local/tmp/Ninjector/libc++_shared.so`
2. `/data/local/tmp/Ninjector/libnook.so`

这也是 `examples/native_hook/common/nook_runtime_loader.h` 这层存在的原因。

### 5. 测试环境里如果残留了同名 so，很容易把现象看乱

这一轮调试里还遇到过一个很实际的问题：目标 app 自己目录里残留了测试 so，导致 observer 收到的模块通知、实际生效的 so、你以为注入进去的 so 三者不是一回事，结果看起来就像“hook 没生效”或者“app 一进去就卡住”。

这种问题不属于框架逻辑本身，但在调试阶段杀伤力很大，必须保持测试环境干净。

---

## 和 shadowhook 这类方案相比，Nook 现在处在什么位置

如果把当前 `Nook` 和 `shadowhook/android-inline-hook` 放在一起看，它们在思路上其实已经有不少相似点了。

### 相同点

1. 都把“等待目标 so 加载”作为框架内部能力，而不是交给 payload 轮询
2. 都会维护一份 pending hook 请求
3. 都会观察 linker 生命周期，而不只停留在简单的 `dlopen`
4. 都需要一个额外的辅助 so，来帮助完成观察或布局识别
5. 都提供按地址和按符号的 inline hook 能力

### 不同点

1. `Nook` 当前只先完成了 `arm64`
2. `Nook` 现在的 API 和代码路径更小、更直接，便于自己读透
3. `Nook` 还没有做到 `shadowhook` 那种成熟度，比如更多架构、更多边界处理、更多工程化细节
4. `Nook` 现在更像“自己实现一套能跑通、能继续扩展的 first-party inline hook 内核”，而不是直接集成第三方成熟框架

换句话说，`Nook` 现在的目标不是和 `shadowhook` 比谁功能全，而是先把架构、原理、控制权都掌握在自己手里。

---

## 当前这套方案的整体调用链

把整条链路再压缩一遍，当前 `Nook` 的 `Inline Hook` 可以概括成下面这几个步骤：

```text
1. payload 被注入
2. payload constructor 调用 NookInlineHookSymbolDeferred()
3. Nook 把请求注册到 pending registry
4. Nook 异步安装 linker call_constructors observer
5. observer 通过 probe 识别当前系统 soinfo 关键偏移
6. 目标 so 加载时，observer 取出 module path
7. pending registry 找到匹配记录
8. symbol resolver 解析出目标函数真实地址
9. inline_hook_impl 写 trampoline 并 patch 目标入口
10. replacement 生效，original 指向 trampoline
```

如果只看表面，用户侧可能只写了这一句：

```cpp
api.hook_symbol_deferred("libnative-lib.so",
                         "Java_com_demo_target_LoginFragment_verifyPasswordNative",
                         replacement,
                         &original,
                         &handle);
```

但内部其实已经把“模块时机问题”和“入口 patch 问题”一起解决掉了。

---

## 结语

这次把 `Nook` 的 arm64 `Inline Hook` 做下来之后，我最大的感受其实不是“指令重定位有多难”，而是：

> 在 Android 上做一个真正能稳定工作的 inline hook，难点往往不是 patch 本身，而是怎么把 patch 放到正确的运行时节点上。

如果只停在“会写 trampoline”，那充其量只是一个 demo。真正要把它做成框架，就必须继续解决：

1. 目标 so 什么时候出现
2. 在什么节点安装最稳
3. 安装失败怎么继续排查
4. payload、runtime、injector 三者怎么配合

当前 `Nook` 的这套方案，至少把第一版最关键的骨架搭起来了：

1. `PLT Hook` 和 `Inline Hook` API 已经分开
2. `arm64 Inline Hook` 已经可用
3. deferred install 已经从轮询收敛到了 linker observer
4. probe 机制把 `soinfo` 偏移识别也补上了

后面再继续往下做，剩下的工作就会更明确：

1. 补 `arm32`
2. 继续打磨 observer 的兼容性
3. 补更多测试样例
4. 继续收紧 API 和交付形态

但不管后面怎么扩展，至少到这里，`Nook` 已经不再只是“能 hook 一下”的 demo 了，而是有了一个真正可以继续长大的 native hook 内核。
