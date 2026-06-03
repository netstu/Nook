# 从0到1构建一个Hook工具之 Native Hook篇（二）：Nook 的 Inline Hook 原理、实现与工程化落地

## 前言

上一篇把 `PLT Hook` 跑通之后，整个 `Nook` 的 native hook 能力其实已经能覆盖一批很常见的场景了。但很快就会碰到一个更现实的问题：

> 并不是所有 native 函数调用都会经过导入表，也不是所有我们想接管的目标，都适合用 `PLT Hook` 去处理。

`PLT Hook` 改的是导入调用链，准确地说，是“调用方通过 `PLT/GOT` 去找目标函数”这条路径。  
可如果目标函数根本不是一个导入函数，或者它在模块内部是直接跳转，甚至我们就是想改掉这个函数本体的执行入口，那么 `PLT Hook` 就到头了。

这时候真正该上场的，就是 `Inline Hook`。

这一篇我想讲的，不是一个抽象的“inline hook 原理介绍”，也不是简单把几段 patch 代码贴出来，而是结合 `Nook` 当前项目里的真实实现，把下面这几件事讲清楚：

1. `Inline Hook` 和 `PLT Hook` 的根本区别到底在哪里。
2. `Nook` 当前这套 `arm64 Inline Hook` 是怎么一步步落地的。
3. 为什么真正难的地方往往不是“改几条机器码”，而是“什么时候改，改完以后原逻辑怎么接回来”。
4. deferred install、module observer、probe 这些看起来额外复杂的东西，为什么在工程上反而是必须的。

如果只用一句话概括这一篇，那就是：

> `Nook` 的 `Inline Hook` 并不是一个“能跳过去就算成功”的 demo，而是一套把 patch、trampoline、instruction relocation、deferred install、observer 和 unhook 全部串成闭环的实现。

---

## 一、为什么 `PLT Hook` 不够，必须继续做 `Inline Hook`

如果只从“都能 hook native 函数”这个表象去看，`PLT Hook` 和 `Inline Hook` 好像只是两种不同写法。 
但如果深入到它们真正修改的位置，就会发现两者根本不是一回事。

`PLT Hook` 改的是调用链中的导入槽位：

```text
caller
  -> PLT
  -> GOT slot
  -> target function
```

而 `Inline Hook` 改的是目标函数本体入口：

```text
caller
  -> target function entry
  -> patch jump
  -> replacement
```

这意味着两者的控制点完全不同。

`PLT Hook` 适合的是：

1. 调用方通过导入表访问外部符号。
2. 我们关心的是“谁调用了这个外部函数”。
3. 想在导入层统一截获某个外部 API。

而 `Inline Hook` 更适合的是：

1. 目标函数根本不是导入函数。
2. 目标函数在模块内部是直接调用，不走 `PLT/GOT`。
3. 想直接接管某个 JNI 导出函数。
4. 想在函数入口插入自己的控制逻辑，同时又保留原始执行流继续可走。

这也是 `Nook` 最后必须继续补 `Inline Hook` 的根本原因。  
因为如果只停在 `PLT Hook`，你拿到的只是“导入调用链级别的劫持能力”；但真正想把 native hook 做成一个通用框架，最终一定会走到“直接改函数入口”这一步。

---

## 二、`Inline Hook` 到底改了什么

很多人第一次接触 `Inline Hook`，很容易把它理解成一句很模糊的话：

> 把函数前几条指令改掉，跳到自己的函数里。

这句话不算错，但不够精确。  
一个真正能工作的 `Inline Hook`，至少要同时解决两个问题：

1. 如何把目标函数入口改到 replacement。
2. 如何让原函数被覆盖掉的前几条指令还能继续正确执行。

在 `Nook` 当前的实现里，整个过程可以抽象成三步：

1. 先把目标函数开头固定长度的一段原始指令备份出来。
2. 在原函数入口写入一段绝对跳转 patch，让执行流先进入 replacement。
3. 另外构造一块 trampoline，把刚才被覆盖掉的原始指令以“重写后仍然语义等价”的方式放进去，再在尾部跳回原函数剩余部分。

所以 hook 之后，执行流不再是：

```text
caller -> target
```

而变成：

```text
caller -> patched target entry -> replacement
```

如果 replacement 还想继续执行原始逻辑，那么它实际上走的是：

```text
replacement -> original -> trampoline -> target + patched_length
```

这里有个非常重要的认知点，后面整个实现都会围绕它展开：

> 对 `Inline Hook` 来说，所谓 `original`，通常并不是“原函数入口地址”，而是“继续执行原始逻辑的入口”，也就是 trampoline。

---

## 三、先看 `Nook` 里的对外接口：三种 Hook 模型已经分开了

先看 `include/nook/NookInlineHook.h`，当前公开的接口是：

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

表面上只是几个函数，但它其实已经把 `Inline Hook` 的三种使用场景拆开了：

### 1. `NookInlineHookAddress`

这是最底层的模式。  
调用者已经知道目标函数地址，框架只负责真正的 patch 安装。

### 2. `NookInlineHookSymbol`

这是“先解析符号地址，再走 address hook”的封装。  
对调用者来说更方便，但本质上并没有引入新的 patch 技术。

### 3. `NookInlineHookSymbolDeferred`

这是整套实现里最有工程价值的入口。  
因为在 Android 真实场景里，payload 往往很早就被注入了，但目标 so 这时候未必已经加载。  
这时如果强行做 symbol resolve，大概率直接失败。

所以 deferred hook 的思路不是“现在就安装”，而是：

1. 先声明“我以后想 hook 谁”。
2. 框架内部把这个需求登记起来。
3. 等目标模块真正进入合适的生命周期节点，再做真实安装。

### 4. `NookInlineUnhook`

它不是可有可无的清理接口，而是让整个 inline hook 生命周期真正闭环的重要组成部分。

---

## 四、真正的入口层：`NookInlineHook.cpp` 在做什么

对外 API 只是门面，真正把三种场景分发出去的是 `src/framework/NookInlineHook.cpp`。

这一层本身并不复杂，但它非常重要，因为它把整个模块的使用模型梳理得很清楚。

### 1. `NookInlineHookInitialize()`

当前实现的初始化很轻，只是把一个全局状态置为已初始化。  
这说明 inline hook 内核本身并没有复杂的全局资源初始化动作，至少现在还没有。

### 2. `NookInlineHookAddress()`

它做的事情非常直接：

1. 参数检查。
2. 确保初始化。
3. 调用内部 `InstallInlineHook(...)`。

也就是说，真正的 direct inline patch 主线是从这里进入 `inline_hook_impl.cpp` 的。

### 3. `NookInlineHookSymbol()`

这条线只是先走：

```text
ResolveSymbolAddress(...)
```

把 `module + symbol` 变成真实地址，然后继续走：

```text
NookInlineHookAddress(...)
```

所以 symbol hook 本质上没有发明新的 hook 机制，它只是“符号定位 + 地址 hook”。

### 4. `NookInlineHookSymbolDeferred()`

这是当前 `Nook` 里最值得关注的入口。

它不试图立刻安装，而是做下面三件事：

1. 把 `module_name + symbol_name + replacement + original + hook_handle` 封装成一个 `PendingInlineHookRequest`。
2. 调用 `RegisterPendingInlineHook(...)` 把请求登记到 pending registry。
3. 调用 `EnsureInlineHookModuleObserverAsync()` 异步确保模块观察器已经启动。

这一层的设计取向非常明确：

> deferred hook 的职责不是“现在能不能装上”，而是“先把 hook 需求记录下来，并把未来安装的观察链路搭好”。

这也是 `Nook` 当前实现比较成熟的一个信号：  
它不再让业务侧自己轮询，不再让 payload 自己猜时机，而是把“何时安装 hook”这件事收回到了框架内部。

---

## 五、真正的 patch 内核：`InstallInlineHook()` 到底做了什么

真正的核心实现位于 `src/native_hook/inline_hook/inline_hook_impl.cpp`。

先看两个非常关键的常量：

```cpp
constexpr size_t kArm64InlineHookPatchWords = 5u;
constexpr size_t kArm64InlineHookPatchSize = kArm64InlineHookPatchWords * sizeof(uint32_t);
```

也就是说，当前这套 `arm64 Inline Hook` 默认要覆盖目标函数前 `5` 条指令，总共 `20` 字节。

这 20 字节不是拍脑袋选的，而是因为当前入口 patch 模板正好就是 5 个 `uint32_t`。

### 1. 先准备好“可写代码页”和“清指令缓存”这两个基础动作

无论任何运行时 patch，本质上都绕不过两件事：

1. 目标代码页原本通常不可写，patch 前必须改权限。
2. patch 完成后 CPU 可能还缓存着旧指令，必须清 instruction cache。

所以 `inline_hook_impl.cpp` 里有两类基础辅助函数：

1. `SetPatchWritable(...)`
2. `ClearInstructionCache(...)`

在 Android/Linux 下，分别走的是：

1. `mprotect`
2. `__builtin___clear_cache`

这说明当前实现保持了一个很直接的思路：不额外包装太多层，而是清楚地把 patch 前后的必要动作放在内核里。

### 2. 入口 patch 实际长什么样

`WriteAbsoluteJumpPatch(...)` 写入的 patch 模板如下：

```cpp
patch[0] = 0x58000051u;  // LDR X17, #8
patch[1] = 0x14000003u;  // B #12
patch[2] = low32(target);
patch[3] = high32(target);
patch[4] = 0xD61F0220u;  // BR X17
```

如果用逻辑语义去解释，它干的事情是：

1. 通过 `LDR X17, #8` 把后面的 64 位目标地址读到寄存器里。
2. 通过一个短跳绕过这段嵌在指令流中的地址字面量。
3. 最后 `BR X17` 跳到 replacement。

为什么不直接用 `B replacement`？

因为普通 `B/BL` 的跳转距离是有限的，而 replacement 地址未必刚好落在它的 immediates 可编码范围内。  
如果要做一个通用、稳定、与相对距离无关的 patch，最省事的方法就是：

> 把 64 位地址直接塞进 patch，再通过寄存器间接跳转。

所以这一段 patch，本质上就是一个 ARM64 上的绝对跳转桩。

### 3. `InstallInlineHook()` 的完整执行链路

顺着源码往下看，这个函数的流程其实很工整。

#### 第一步：参数检查和输出清空

先确保所有关键参数都不为空，并先把：

```cpp
*original = nullptr;
*hook_handle = nullptr;
```

这一步的意义很简单：  
即便后续某一步失败，调用者也不会拿到半初始化状态的脏输出。

#### 第二步：创建 `InlineHookHandle`

当前实现里的 `hook_handle` 实际上对应：

```cpp
struct InlineHookHandle {
    InlineHookRecord record;
    TrampolineAllocation trampoline;
};
```

也就是说，一次 inline hook 的句柄，内部至少包含：

1. 这次 hook 的所有上下文记录。
2. 这次 hook 使用的 trampoline 内存块。

后面 unhook 能不能恢复现场，全靠这个 handle。

#### 第三步：先把目标函数前 5 条指令读出来

```cpp
uint32_t original_words[kArm64InlineHookPatchWords] = {};
std::memcpy(original_words, target_address, sizeof(original_words));
```

此时还没有 patch，只是在备份即将被覆盖的原始入口区域。

#### 第四步：先估算 trampoline 需要多大

这里是很多人第一次看 inline hook 时最容易忽略的点。

代码逻辑大致是：

1. 先为 trampoline 末尾的 jump back 预留 5 个 word。
2. 再逐条计算原始前 5 条指令在 relocation 后会占多大空间。

为什么要这么做？

因为原始 ARM64 指令虽然每条都是 4 字节，但搬到 trampoline 后未必还是 4 字节：

1. 普通无关指令可能原样保留。
2. `B/BL` 可能会展开成 20 字节。
3. `B.cond`、`CBZ`、`TBZ` 这类条件跳转甚至可能展开成 24 字节。
4. 一些 literal load / SIMD literal 指令还会更长。

所以 trampoline 大小不是固定 20 字节，而是“重写后长度之和 + jump back”。

#### 第五步：分配可执行 trampoline

这一步会调用 `AllocateExecutableTrampoline(...)`。

其实现位于 `trampoline_allocator.cpp`，逻辑也很直接：

1. 按页对齐向上取整。
2. 调用 `mmap(..., PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, ...)`。
3. 得到一块 RWX 内存。

这块内存后面就是 trampoline 的载体。

#### 第六步：把前 5 条指令重定位到 trampoline

这一层调用的是：

```cpp
RelocateArm64InstructionSequence(...)
```

它的作用不是简单复制前 20 字节，而是：

1. 识别每条原始指令的类型。
2. 判断它是否依赖原始 PC。
3. 如果依赖，就改写成在 trampoline 位置仍然语义等价的新序列。
4. 最终把整个前导块重写进 trampoline。

这一步就是整个 inline hook 的技术核心之一。

#### 第七步：在 trampoline 尾部补一个 jump back

这里的目标地址是：

```cpp
target_address + kArm64InlineHookPatchSize
```

也就是原函数第 6 条指令开始的位置。

它的逻辑含义很清楚：

> trampoline 先负责执行被覆盖掉的前 20 字节语义，执行完以后再跳回原函数剩余部分。

#### 第八步：记录 hook 现场

调用 `ActivateInlineHookRecord(...)`，把下面这些信息写进 record：

1. target address
2. replacement address
3. trampoline address
4. patch 长度
5. original code bytes

#### 第九步：真正覆盖目标函数入口

最后才调用：

```cpp
WriteAbsoluteJumpPatch(target_address, replacement)
```

到这一步，目标函数入口才真正被换成“跳到 replacement”的 patch。

#### 第十步：返回 `original` 和 `hook_handle`

非常关键的一句是：

```cpp
*original = handle->trampoline.address;
```

这再次说明：

> 对 `Inline Hook` 来说，`original` 不是原函数入口，而是 trampoline。

因为原函数入口已经被你 patch 掉了。  
如果 replacement 再去直接调原函数入口，只会重新命中自己，最后形成递归环路。

---

## 六、为什么 trampoline 才是 `Inline Hook` 真正的核心

很多人一提到 `Inline Hook`，第一反应就是：

1. 覆盖前几条指令
2. 跳到 replacement

这当然没错，但只解决了一半问题。  
另一半更麻烦的问题是：

> 被你覆盖掉的那几条原始指令怎么办？

如果没有 trampoline，那么 replacement 一旦想继续走原始逻辑，就根本找不到一个正确入口。  
因为原函数前导已经被改掉了，原始执行链断了。

而 trampoline 的存在，恰好就是为了把这条链接回来。

在 `Nook` 里，trampoline 可以抽象成：

```text
relocated original prologue
    + jump back to target + 20
```

也就是：

1. 先把被覆盖掉的前 5 条指令，搬到新的可执行内存中。
2. 但搬过去不是直接 memcpy，而是按 relocation 规则重写。
3. 最后在尾部补一个跳回原函数剩余部分的绝对跳转。

所以 replacement 中如果去调用 `original(...)`，控制流实际走的是：

```text
replacement
    -> trampoline
    -> 被重写后的原函数前 5 条指令
    -> 原函数第 6 条指令以后
```

这也是为什么我一直觉得，真正的 inline hook 核心不该描述成“改入口”，而应该描述成：

> 改入口，同时保住原执行流。

---

## 七、为什么不能把前 20 字节原样拷到 trampoline：ARM64 指令重定位是绕不过去的

如果只是把原函数前 20 字节拷贝到 trampoline，再在后面跳回原函数，看起来是不是就已经够了？

答案是：很多时候完全不够。

原因在于 ARM64 里有大量 PC-relative 指令。  
这些指令原本是在目标函数入口处执行的，它们解释 immediates 时依赖的是“原位置的 PC”。  
一旦被搬到 trampoline：

1. 指令字节虽然没变
2. 但执行位置已经变了
3. 所以算出来的目标地址也会跟着变
4. 语义就错了

这也是为什么 `Nook` 专门实现了一个 `arm64_instruction_relocator.cpp`。

---

## 八、`arm64_instruction_relocator.cpp` 在做什么

这个文件的整体思路很清晰：

1. 先识别当前指令属于哪一类。
2. 根据指令类型，估算它重写后需要多长。
3. 再按语义把它改写成新的指令序列。

当前实现识别并处理的类型包括：

1. `B`
2. `BL`
3. `B.cond`
4. `ADR`
5. `ADRP`
6. `LDR literal`
7. `LDRSW literal`
8. SIMD literal load
9. `CBZ/CBNZ`
10. `TBZ/TBNZ`

这说明作者并不是在写一个完整 ARM64 反汇编器，而是在写一个“围绕 inline hook 场景所需的实用 relocator”。

### 1. 为什么要先算每条重写后的长度

因为不是每条指令重写后都还是 4 字节。

例如：

1. `B/BL` 会被改写成绝对跳转/绝对调用序列。
2. `ADR/ADRP` 会被改成“加载绝对地址”的序列。
3. 条件跳转类会被展开成更长的“条件判断 + 绝对跳转桩”。

所以 trampoline 大小必须预估，而不能拍脑袋写死。

### 2. `B/BL` 是怎么重写的

当前实现没有尝试重新编码新的近跳，而是直接统一改成一段绝对跳转或绝对调用桩。

这样做的好处很明显：

1. 不用再管新旧地址之间的距离范围。
2. 不用重新考虑 immediates 是否还能编码进去。
3. 逻辑和入口 patch 方案保持一致。

### 3. `ADR/ADRP` 是怎么重写的

原始语义是在当前位置附近构造一个地址。  
搬到 trampoline 后，原来的“当前位置”已经失效。

所以当前做法是直接把目标地址算出来，再改写成“加载绝对地址字面量”的形式。

### 4. `B.cond`、`CBZ/CBNZ`、`TBZ/TBNZ` 为什么更麻烦

因为这些不是简单跳不跳的问题，而是“在保留条件语义的前提下，还要确保跳到正确地址”。

所以当前实现通常会展开成：

1. 一个短的条件判断分支
2. 一个绝对跳转桩

本质思路是：

1. 先保住条件判断逻辑
2. 再用更通用的绝对跳转去完成目标跳转

### 5. `LDR literal` 之类为什么也必须重写

因为它们原本也是基于 PC-relative 方式取字面量。  
搬迁后，原 PC 已经不在了。

所以当前实现的思路是：

1. 先加载绝对地址
2. 再从这个绝对地址位置真正取值

### 6. 为什么还要做块内地址映射

这里有一个更细但很重要的实现点：

> 被覆盖掉的前 5 条指令内部，自己也可能存在跳转到这 5 条内部其他位置的情况。

如果 relocation 只会“把外部目标地址改对”，但不会处理“这 5 条指令内部彼此跳转关系”，那 trampoline 一样可能失真。

所以 `Nook` 这里还有一个 `TranslateAddressIfNeeded(...)`，它会做一件事：

1. 如果某个跳转目标落在原始被搬迁块内部
2. 那就把它映射到 relocated block 中对应的新地址

这说明 `Nook` 当前的 relocation 已经不是“改单条指令”这么简单，而是在努力保持整个被搬迁代码块内部的控制流关系不变。

---

## 九、为什么 `Inline Hook` 真正难的地方常常不是 patch，而是 deferred install

如果只在一个已经加载好的 so 上，拿到一个明确的目标地址去做 patch，那 direct inline hook 的问题相对可控。  
真正落到 Android 场景里，更麻烦的问题往往是：

> 目标 so 在 payload 初始化时，根本还没加载。

这时候如果你直接在 constructor 里调用：

```cpp
NookInlineHookSymbol(...)
```

很可能立刻失败。  
失败的原因不是 patch 不会写，而是符号根本还不存在于当前进程映射里。

这就是为什么 `Nook` 专门做了 `NookInlineHookSymbolDeferred()`，并为它配套实现了：

1. pending registry
2. module observer
3. probe

这几部分从代码量上看比 direct patch 还重，但从工程价值上看，它们反而更核心。

因为它们解决的是：

> 什么时候装，在哪里等，等到了怎么知道“现在可以装了”。

---

## 十、Pending Registry：先把“我要 hook 谁”这件事记下来

先看 `pending_inline_hook_registry.cpp`。

这一层的职责很纯粹：  
在目标模块还没加载的时候，先把 hook 请求登记起来。

内部存储的 entry 很简单：

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

这说明 registry 本身并不负责 patch，只负责两件事：

1. 存请求
2. 在某个模块加载事件到来时，找出有哪些请求命中了这个模块

### 它真正解决了什么问题

有了 registry 以后，payload 的职责就被简化成了：

> 我只负责声明“以后我要 hook 这个模块里的这个符号”。

至于：

1. 目标模块什么时候出现
2. 模块一出现时如何匹配到对应请求
3. 安装成功后如何避免重复安装

这些都交给框架内部处理。

这比让每个 payload 自己开线程轮询、自己维护状态，要整洁得多。

---

## 十一、为什么 observer 没有停在 `dlopen`，而是继续往 linker 里走了一层

理论上，如果目标模块还没加载，一个很自然的想法是：

1. 轮询 `/proc/self/maps`
2. 或者 hook `dlopen/android_dlopen_ext`

但这些方案都有明显问题。

轮询的问题在于：

1. payload 自己承担时机判断
2. wakeup 和延迟都不优雅
3. 很像“为了跑通而跑通”的过渡方案

而单纯盯 `dlopen` 的问题在于：

1. 看到“开始加载”并不等于“现在已经适合安装 patch”
2. 模块还处于 linker 处理流程中时，很多状态未必稳定
3. 在错误时机做过多操作，轻则 hook 无效，重则进程异常

所以 `Nook` 当前最终选择的观察点，不是表层 loader API，而是 linker 内部的：

```cpp
soinfo::call_constructors()
```

这一步的思路非常关键：

> 不再猜模块什么时候合适，而是把自己放到 linker 真正处理模块生命周期的关键节点上。

---

## 十二、observer 的实现思路：先把 linker 的 `call_constructors()` 自己也 inline hook 掉

`inline_hook_module_observer.cpp` 里的逻辑可以概括成：

1. 找到 `linker` / `linker64`
2. 解析 `soinfo::call_constructors()` 的符号地址
3. 用当前这套 inline hook 内核，把它也 hook 掉
4. 从此以后，每次某个 so 进入构造阶段，observer 都能先收到通知

这里有个很有意思的点：

> observer 自己也是建立在这套 inline hook 内核之上的。

也就是说，deferred install 这条线并不是另起一套 patch 技术，它是复用 direct inline patch 内核，再往上搭出来的一层时机控制系统。

---

## 十三、为什么还需要 probe：`soinfo` 偏移不能硬编码

即使已经 hook 到了 `call_constructors()`，问题也还没结束。

因为 observer 拿到的参数只是一个 `soinfo*`，而 `soinfo` 是 linker 内部结构，不是稳定公开 ABI。  
不同 Android 版本里，它的字段布局未必完全一致。

所以如果你直接硬编码：

1. 第几个偏移是 `name`
2. 第几个偏移是 `load_bias`
3. 第几个偏移是 `constructors_called`

这套方案的兼容性会非常脆。

这就是 probe 存在的意义。

当前 `Nook` 额外带了一个很小的 probe so，作用不是业务功能，而是给 observer 提供一个“已知样本模块”。

做法是：

1. observer 初始化时先确定 probe so 的路径。
2. `dlopen` 一次 probe。
3. probe 被 linker 处理时，observer 会收到它对应的 `soinfo*`。
4. 而 probe 的模块名、`dlpi_phdr`、`dlpi_phnum`、`dli_fbase` 等信息又是已知的。
5. 于是 observer 就能扫描这块 `soinfo` 内存，反推出关键字段偏移。

要识别的字段主要包括：

1. `phdr`
2. `phnum`
3. `load_bias`
4. `name`
5. `constructors_called`

这套方案的价值很高，因为它让 observer 不再是“绑死某个 Android 版本 linker 布局”的硬编码方案，而是一种有一定运行时自适应能力的方案。

---

## 十四、真正收到模块通知之后，deferred hook 是怎么装上的

当 observer 已经成功 hook 到 `call_constructors()`，而且 probe 也已经帮它识别出 `soinfo` 关键偏移之后，后面的事情就顺了。

每次某个模块进入构造阶段时，observer 会：

1. 从 `soinfo` 中取出当前模块路径。
2. 调用 `NotifyModuleLoaded(module_path)`。

而 `NotifyModuleLoaded(...)` 的核心逻辑是：

1. 构造安装依赖，把“真正怎么安装一个 pending inline hook”封成回调。
2. 调用：

```cpp
TryInstallPendingInlineHooksForModule(module_path, dependencies)
```

3. registry 遍历所有还没安装的挂起请求。
4. 用模块路径匹配规则找出命中的请求。
5. 对每个命中的请求，执行：

```text
resolve symbol in loaded module
    -> NookInlineHookAddress(...)
```

6. 安装成功后把这条 request 标记成 `installed = true`

所以 deferred hook 的本质并不是另一种 patch，而是：

> 先挂起，等模块时机成熟后，再回到“符号解析 + address hook”这条主线。

---

## 十五、symbol resolver 在整个 inline hook 链路里的位置

虽然这一篇重点是 inline hook，但 `native_hook_symbol_resolver.cpp` 其实是非常关键的支撑模块。

它要解决的问题很直接：

> 给定 `module + symbol`，怎么把它稳定地变成运行时真实地址？

当前 resolver 的策略大致是：

1. 优先从“已加载模块 + 对应 ELF 文件”的视角解析。
2. 如果失败，再 fallback 到 `xdl` 或 `dlopen/dlsym`。

其中比较关键的一步是：

```text
runtime_symbol_address = runtime_bias + symbol_value
```

也就是说，它会先：

1. 从 `/proc/self/maps` 里拿到模块基址和真实映射路径
2. 用 `ELFIO` 解析模块文件里的动态符号值
3. 再把“文件视角下的符号值”转换成“运行时视角下的真实地址”

这和前面 `PLT Hook` 那篇里反复强调的“文件视角”和“运行时视角”分离，是一脉相承的。

另外它还做了两件很实用的事情：

1. 特判 GNU IFUNC
2. 提供“这个符号是否适合 inline hook”的安全检查

比如如果符号大小小于当前 patch 长度，或者当前拿到的其实是 IFUNC resolver 本体，那么直接 inline hook 就不一定安全。

这说明当前 `Nook` 的 inline hook 内核不是“地址一拿到就无脑 patch”，而是开始考虑一些更现实的安全边界。

---

## 十六、unhook 是怎么恢复现场的

`Inline Hook` 如果只有安装，没有恢复，那本质上还是半成品。  
`NookInlineUnhook()` 最终走到 `UninstallInlineHook()`，它做的事情很明确：

1. 根据 `hook_handle` 找到 `InlineHookHandle`
2. 确认这次 hook 当前仍然 active
3. 把 `InlineHookRecord` 里保存的原始机器码写回目标函数入口
4. 清 instruction cache
5. 释放 trampoline
6. 清空 record，删除 handle

这里也再次说明了为什么安装时一定要保存：

1. target address
2. trampoline allocation
3. original code bytes

没有这些上下文，你根本不可能完整恢复现场。

所以 `hook_handle` 在 `Nook` 当前实现里的语义并不是“随便给调用者一个句柄”，而是：

> 这次 hook 的完整运行时现场凭证。

---

## 十七、把整条调用链收束起来：`Nook` 当前的 `Inline Hook` 到底是怎么工作的

如果把前面所有部分重新压缩成一条完整链路，大概就是这样。

### 场景一：已知地址，直接 inline hook

```text
NookInlineHookAddress(...)
    -> InstallInlineHook(...)
        -> 备份前 5 条指令
        -> 估算 trampoline 大小
        -> 分配 trampoline
        -> relocation
        -> 追加 jump back
        -> 记录 hook record
        -> patch 目标函数入口
        -> original = trampoline
```

### 场景二：已知 `module + symbol`

```text
NookInlineHookSymbol(...)
    -> ResolveSymbolAddress(...)
    -> NookInlineHookAddress(...)
```

### 场景三：目标模块尚未加载

```text
NookInlineHookSymbolDeferred(...)
    -> RegisterPendingInlineHook(...)
    -> EnsureInlineHookModuleObserverAsync(...)
        -> hook linker soinfo::call_constructors()
        -> dlopen probe
        -> 扫描识别 soinfo 偏移

目标模块加载时
    -> observer 拿到 module_path
    -> NotifyModuleLoaded(module_path)
    -> registry 找到匹配请求
    -> resolve symbol in loaded module
    -> NookInlineHookAddress(...)
```

### 场景四：取消 hook

```text
NookInlineUnhook(hook_handle)
    -> 恢复原始入口机器码
    -> 释放 trampoline
    -> 删除 handle
```

到这里就能看出来，当前 `Nook` 的 inline hook 实现虽然模块不少，但整体架构其实非常收敛：

1. patch 内核只有一套
2. trampoline 机制只有一套
3. relocation 机制只有一套
4. direct / symbol / deferred 三种入口最后都会汇聚到同一个 patch 内核

差别只在于：

1. 地址从哪里来
2. 什么时候安装

---

## 十八、这套实现当前最值得注意的几个点

如果从“做成一个真正可用框架”的角度看，当前 `Nook` 这套 inline hook 有几个我觉得很值得单独点出来的地方。

### 1. `original` 的语义定义得很正确

它没有把 `original` 伪装成“原函数入口地址”，而是明确让它指向 trampoline。  
这才符合 inline hook 的真实执行流。

### 2. relocation 不是装饰，而是完整考虑了块内控制流

它不仅处理了外部跳转目标，也考虑了被搬迁块内部的地址映射。  
这一点很关键。

### 3. deferred hook 没有停在轮询

最终选择下沉到 linker 生命周期观察，而不是让 payload 自己轮询模块加载状态，这一点非常工程化。

### 4. probe 机制让 observer 更稳

`soinfo` 偏移不是写死的，而是通过已知样本模块动态识别出来。  
这说明作者在有意识地减轻平台版本差异带来的风险。

### 5. observer 已经被更高层能力复用

当前 `inline_hook_module_observer.cpp` 里不只是服务 pending inline hooks，还顺手通知了 `native js hook` 桥接层。  
这说明 `Inline Hook` 在 `Nook` 里已经开始承担“底层 patch 基础设施”的角色。

---

## 十九、当前实现的边界和风险

写到这里，也有必要把当前边界说清楚，不然文章就容易变成只讲成功路径。

### 1. 当前核心实现明显是面向 `arm64`

无论 patch 模板还是 relocator，当前都是 ARM64 指令级实现。

### 2. 默认 patch 长度固定为前 5 条指令

这是一种工程折中，并不意味着所有函数前导都天然适合这么处理。

### 3. relocator 处理的是一批常见 PC-relative 指令

它已经很实用，但不是完整 ARM64 指令语义模拟器。

### 4. observer 依赖 linker 内部实现

虽然 probe 已经让它比硬编码偏移稳很多，但它本质上还是在和系统私有内部结构打交道，因此兼容性始终需要持续验证。

### 5. 当前统一 native hook 总入口还主要偏向 `PLT Hook`

从项目结构上看，`NookNativeHookHookSymbol()` 现在默认还是走 `PLT Hook`。  
也就是说，inline hook 虽然已经形成了独立能力，但在总入口层面还没有完全并入“统一默认选择”里。

这些都属于后续还能继续打磨的地方。

---

## 结语

把 `Nook` 这一轮的 `Inline Hook` 做下来之后，我最大的感受其实不是“指令重定位有多难”，而是另一件更本质的事情：

> 在 Android 上做一个真正能稳定工作的 `Inline Hook`，难点往往不是 patch 本身，而是怎么把 patch 放到正确的运行时节点上，同时还把原始逻辑完整接回来。

如果只会“改前几条机器码，再跳到 replacement”，那最多只是个 demo。  
真正想把它做成框架，最终一定要把下面这些问题一起解决：

1. patch 怎么写
2. trampoline 怎么构造
3. relocation 怎么保证语义不变
4. 目标 so 什么时候出现
5. 什么时机安装最稳
6. 安装失败怎么排查
7. unhook 怎么恢复现场

从当前源码看，`Nook` 已经把这套闭环的主干搭出来了：

1. 有独立的 `Inline Hook` API
2. 有独立的 patch 内核
3. 有 trampoline 和 unhook 生命周期管理
4. 有 ARM64 relocator
5. 有 pending registry
6. 有 linker observer
7. 有 probe 驱动的 `soinfo` 偏移识别
8. 还有被更高层 hook 系统继续复用的趋势

所以如果要用一句话概括当前这套实现，我会这样说：

> `Nook` 的 `Inline Hook` 已经不再只是“能改入口”的代码片段，而是一套把 patch、原逻辑保留、模块时机控制和恢复流程真正串成闭环的 native hook 内核。
