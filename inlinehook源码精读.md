# Nook Inline Hook 源码精读：把所有相关代码串起来读一遍

## 前言

前一版“源码精读”更偏实现总结，主线是清楚的，但你提的要求是对的：

> 既然叫源码精读，就不能只讲思路，还要把所有相关代码真正串起来讲。

所以这一版我不再按“概念章节”来组织，而是严格按源码文件来走。

换句话说，这篇文章的目标不是只告诉读者：

1. `Inline Hook` 的原理是什么
2. `Nook` 的实现思路是什么

而是要进一步做到：

1. 这个模块一共有哪些文件
2. 每个头文件定义了什么
3. 每个 `cpp` 里到底实现了哪些函数
4. 这些函数谁调谁
5. 整条调用链是怎么从 API 入口一路落到真正 patch 的

这一篇涉及到的核心文件如下：

1. `include/nook/NookInlineHook.h`
2. `src/framework/NookInlineHook.cpp`
3. `src/native_hook/inline_hook/inline_hook_impl.h`
4. `src/native_hook/inline_hook/inline_hook_impl.cpp`
5. `src/native_hook/inline_hook/inline_hook_record.h`
6. `src/native_hook/inline_hook/inline_hook_record.cpp`
7. `src/native_hook/inline_hook/trampoline_allocator.h`
8. `src/native_hook/inline_hook/trampoline_allocator.cpp`
9. `src/native_hook/inline_hook/arm64_instruction_relocator.h`
10. `src/native_hook/inline_hook/arm64_instruction_relocator.cpp`
11. `src/native_hook/inline_hook/pending_inline_hook_registry.h`
12. `src/native_hook/inline_hook/pending_inline_hook_registry.cpp`
13. `src/native_hook/inline_hook/inline_hook_module_observer.h`
14. `src/native_hook/inline_hook/inline_hook_module_observer.cpp`
15. `src/native_hook/core/native_hook_symbol_resolver.h`
16. `src/native_hook/core/native_hook_symbol_resolver.cpp`
17. `src/native_hook/core/module_match.cpp`
18. `src/native_hook/core/module_info.h`
19. `src/native_hook/core/module_info.cpp`

另外，`inline_hook_module_observer.cpp` 里还顺手接入了 `native js hook` 的模块加载通知逻辑，所以也会顺带点一下它为什么会出现在这里。

---

## 一、先看入口头文件：`NookInlineHook.h`

这份头文件很短，但它定义了整个 `Inline Hook` 模块对外公开的能力边界：

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

这里已经把三种安装模型拆开了：

1. `NookInlineHookAddress`
   已知真实地址，直接 hook。

2. `NookInlineHookSymbol`
   已知 `module + symbol`，框架先解析地址，再 hook。

3. `NookInlineHookSymbolDeferred`
   已知未来要 hook 谁，但目标模块当前可能还没加载，先登记请求，等模块加载后再装。

此外还有：

1. `NookInlineHookInitialize`
2. `NookInlineHookIsAvailable`
3. `NookInlineUnhook`

这一层只定义接口，不包含实现。但从接口设计上已经能看出当前模块的使用方式不是单一的“传地址 patch”，而是把 direct、symbol、deferred 和 unhook 全都独立成了完整 API。

---

## 二、第一层实现：`NookInlineHook.cpp` 负责把三种入口分流

真正的框架入口在 `src/framework/NookInlineHook.cpp`。

先看它包含了哪些头文件：

```cpp
#include "nook/NookInlineHook.h"
#include "native_hook/core/native_hook_symbol_resolver.h"
#include "native_hook/inline_hook/inline_hook_impl.h"
#include "native_hook/inline_hook/inline_hook_module_observer.h"
#include "native_hook/inline_hook/pending_inline_hook_registry.h"
```

这个 include 关系已经很能说明问题了：

1. 它自己不做底层 patch。
2. 它负责调用 symbol resolver。
3. 它负责调 inline hook 内核。
4. 它负责接 pending registry。
5. 它负责调 module observer。

也就是说，这一层是整个 `Inline Hook` 的“路由层”。

### 1. 文件开头的几个内部对象

```cpp
bool g_inline_hook_initialized = false;
constexpr char kInlineFrameworkTag[] = "NookInlineDeferred";

enum class InlineHookSymbolAttemptResult {
    kInstalled,
    kResolveMiss,
    kInstallFailed,
};
```

含义分别是：

1. `g_inline_hook_initialized`
   记录 inline hook 框架是否初始化。

2. `kInlineFrameworkTag`
   Android 日志 tag。

3. `InlineHookSymbolAttemptResult`
   把 symbol hook 当前尝试分成三类结果：
   - 成功安装
   - 符号没解析到
   - 解析到了但安装失败

### 2. `TryInstallInlineHookSymbolNow(...)`

这个内部函数是 `NookInlineHookSymbol(...)` 的真正主逻辑：

```cpp
void* target_address = nullptr;
if (!NookNativeHookInternal::ResolveSymbolAddress(module_name, symbol_name, &target_address)) {
    return InlineHookSymbolAttemptResult::kResolveMiss;
}

const NookStatus status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
if (status != NOOK_STATUS_OK) {
    return InlineHookSymbolAttemptResult::kInstallFailed;
}

return InlineHookSymbolAttemptResult::kInstalled;
```

这段代码非常重要，因为它明确告诉我们：

> immediate symbol hook 本质上就是“先解析符号地址，再调用 address hook”。

### 3. `InstallPendingInlineHookSymbol(...)`

这个内部函数是 deferred 场景下给 pending registry 用的安装回调：

```cpp
if (NookNativeHookInternal::ResolveSymbolAddressInLoadedModule(module_path,
                                                               symbol_name,
                                                               &target_address)) {
    status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
}
```

这里和 `TryInstallInlineHookSymbolNow(...)` 的区别在于：

1. immediate hook 用的是 `ResolveSymbolAddress(...)`
2. deferred hook 回调用的是 `ResolveSymbolAddressInLoadedModule(...)`

### 4. `NookInlineHookInitialize()` / `NookInlineHookIsAvailable()`

初始化当前很轻，只是把 `g_inline_hook_initialized` 置真。`IsAvailable()` 则检查参数、把 `available` 设为 `1`，再顺手初始化。

### 5. `NookInlineHookAddress()`

```cpp
if (target_address == nullptr || replacement == nullptr ||
    original == nullptr || hook_handle == nullptr) {
    return NOOK_STATUS_INVALID_ARGUMENT;
}
...
return NookInlineHookInternal::InstallInlineHook(target_address, replacement, original, hook_handle)
               ? NOOK_STATUS_OK
               : NOOK_STATUS_INTERNAL_ERROR;
```

含义就是：

1. 参数检查
2. 确保初始化
3. 真正安装交给 `InstallInlineHook(...)`

### 6. `NookInlineHookSymbol()`

它只是包装 `TryInstallInlineHookSymbolNow(...)`，也就是：

```text
resolve symbol -> address hook
```

### 7. `NookInlineHookSymbolDeferred()`

这一段是 deferred 入口的核心：

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

意思很明确：

1. 先登记请求
2. 再确保 observer 已异步启动

### 8. `NookInlineUnhook()`

直接转到 `UninstallInlineHook(...)`。

所以这一层的角色可以概括成一句话：

> 负责把 direct、symbol、deferred、unhook 四条入口链路分别接到正确的内部实现上。

---

## 三、内核句柄定义：`inline_hook_impl.h`

这份头文件很短，但非常关键：

```cpp
struct InlineHookHandle {
    InlineHookRecord record;
    TrampolineAllocation trampoline;
};

size_t GetArm64InlineHookPatchSize(void);

bool InstallInlineHook(void* target_address,
                       void* replacement,
                       void** original,
                       void** hook_handle);

bool UninstallInlineHook(void* hook_handle);
```

它告诉了我们：

1. `hook_handle` 的真实内部类型就是 `InlineHookHandle`
2. 这个 handle 同时持有：
   - `InlineHookRecord`
   - `TrampolineAllocation`
3. patch 长度通过 `GetArm64InlineHookPatchSize()` 暴露出去
4. direct patch 的安装和卸载都由这一层统一实现

---

## 四、真正的 patch 内核：`inline_hook_impl.cpp`

这一份文件是整个 `Inline Hook` 的核心中的核心。

### 1. 文件开头先定义 patch 长度

```cpp
constexpr size_t kArm64InlineHookPatchWords = 5u;
constexpr size_t kArm64InlineHookPatchSize = kArm64InlineHookPatchWords * sizeof(uint32_t);
```

这说明当前 ARM64 patch 模板固定覆盖 `5` 条 32 位指令，也就是 `20` 字节。

### 2. `ClearInstructionCache(...)`

```cpp
__builtin___clear_cache(reinterpret_cast<char*>(address),
                        reinterpret_cast<char*>(address) + size);
```

运行时代码 patch 后必须清 icache，否则 CPU 可能还执行旧缓存里的指令。

### 3. `SetPatchWritable(...)`

Linux/Android 下走的是：

```cpp
const uintptr_t start = reinterpret_cast<uintptr_t>(address) &
                        ~static_cast<uintptr_t>(page_size - 1);
const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + page_size - 1) &
                      ~static_cast<uintptr_t>(page_size - 1);
return mprotect(reinterpret_cast<void*>(start),
                static_cast<size_t>(end - start),
                PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
```

说明作者知道页保护修改必须按页对齐处理。

### 4. `WriteAbsoluteJumpPatch(...)`

```cpp
patch[0] = 0x58000051u;  // LDR X17, #8
patch[1] = 0x14000003u;  // B #12
patch[2] = static_cast<uint32_t>(target & 0xffffffffu);
patch[3] = static_cast<uint32_t>(target >> 32u);
patch[4] = 0xD61F0220u;  // BR X17
```

这是整个入口 patch 的真正模板。

逻辑是：

1. 先把 replacement 地址字面量读到 `X17`
2. 跳过中间嵌入的 64 位地址数据
3. 最后 `BR X17`

这不是近跳，而是绝对跳转桩。好处是不用受 branch immediate 编码范围限制。

### 5. `RestoreOriginalCode(...)`

```cpp
std::memcpy(target_address, record.original_code.data(), record.original_code.size());
ClearInstructionCache(target_address, record.original_code.size());
```

这是 unhook 时真正把原始入口机器码写回去的地方。

### 6. `GetArm64InlineHookPatchSize()`

只是把 `20` 字节这个 patch 长度暴露给别的模块复用。

### 7. `InstallInlineHook(...)`

这是当前 direct inline patch 的真正主线，必须按顺序看。

#### 7.1 参数检查和输出清空

```cpp
if (target_address == nullptr || replacement == nullptr ||
    original == nullptr || hook_handle == nullptr) {
    return false;
}

*original = nullptr;
*hook_handle = nullptr;
```

#### 7.2 分配 `InlineHookHandle`

```cpp
auto* handle = new (std::nothrow) InlineHookHandle();
```

后续这次 hook 的所有上下文都会挂在这里面。

#### 7.3 先读取原函数前 5 条指令

```cpp
uint32_t original_words[kArm64InlineHookPatchWords] = {};
std::memcpy(original_words, target_address, sizeof(original_words));
```

#### 7.4 先估算 trampoline 需要多大

```cpp
size_t trampoline_words_required = kArm64InlineHookPatchWords;
for (size_t i = 0; i < kArm64InlineHookPatchWords; ++i) {
    trampoline_words_required += GetArm64RelocatedInstructionLength(original_words[i]) /
                                 sizeof(uint32_t);
}
```

注意这里说明 trampoline 大小不是固定 20 字节，而是：

1. relocation 后 5 条指令的长度之和
2. 再加上结尾 jump back 预留的 5 个 word

#### 7.5 分配 trampoline 可执行内存

```cpp
if (!AllocateExecutableTrampoline(trampoline_words_required * sizeof(uint32_t),
                                  &handle->trampoline)) {
    delete handle;
    return false;
}
```

#### 7.6 把原始 5 条指令 relocation 到 trampoline

```cpp
if (!RelocateArm64InstructionSequence(original_words,
                                      kArm64InlineHookPatchWords,
                                      reinterpret_cast<uintptr_t>(target_address),
                                      reinterpret_cast<uintptr_t>(handle->trampoline.address),
                                      trampoline_words,
                                      trampoline_words_required,
                                      &rewritten_word_count)) {
    ...
}
```

这一步说明：

1. 不是简单 memcpy
2. 而是按 ARM64 语义做 relocation
3. 把语义等价的新序列写到 trampoline

#### 7.7 在 trampoline 尾部补一个 jump back

```cpp
const uintptr_t return_address =
        reinterpret_cast<uintptr_t>(target_address) + kArm64InlineHookPatchSize;

trampoline_words[rewritten_word_count + 0u] = 0x58000051u;
trampoline_words[rewritten_word_count + 1u] = 0x14000003u;
trampoline_words[rewritten_word_count + 2u] = static_cast<uint32_t>(return_address & 0xffffffffu);
trampoline_words[rewritten_word_count + 3u] = static_cast<uint32_t>(return_address >> 32u);
trampoline_words[rewritten_word_count + 4u] = 0xD61F0220u;
```

这段的目标不是 replacement，而是：

```text
target + 20
```

即原函数第 6 条指令开始的位置。

#### 7.8 记录 hook 现场

```cpp
ActivateInlineHookRecord(&handle->record,
                         target_address,
                         replacement,
                         handle->trampoline.address,
                         kArm64InlineHookPatchSize,
                         reinterpret_cast<const uint8_t*>(original_words),
                         sizeof(original_words));
```

#### 7.9 真正覆盖目标函数入口

```cpp
if (!WriteAbsoluteJumpPatch(target_address, replacement)) {
    ...
}
```

到这一步，target entry 才真的被 replacement 接管。

#### 7.10 返回 `original` 和 `hook_handle`

```cpp
*original = handle->trampoline.address;
*hook_handle = handle;
```

这句最关键，因为它明确告诉我们：

> `original` 在 inline hook 里不是原函数入口，而是 trampoline。

### 8. `UninstallInlineHook(...)`

```cpp
auto* handle = reinterpret_cast<InlineHookHandle*>(hook_handle);
if (!handle->record.active) {
    return false;
}

if (!RestoreOriginalCode(handle->record.target_address, handle->record)) {
    return false;
}

ResetInlineHookRecord(&handle->record);
FreeExecutableTrampoline(&handle->trampoline);
delete handle;
```

这就是完整的 unhook：

1. 取回 handle
2. 检查 active
3. 恢复原始入口机器码
4. 清 record
5. 释放 trampoline
6. 删除 handle

---

## 五、`inline_hook_record.h/.cpp`：保存这次 hook 的现场

先看头文件：

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

它保存的就是一次 hook 的恢复现场：

1. 被 patch 的 target address
2. replacement address
3. trampoline address
4. patch 长度
5. 被覆盖掉的原始字节

### 1. `ResetInlineHookRecord(...)`

```cpp
record->target_address = nullptr;
record->replacement_address = nullptr;
record->trampoline_address = nullptr;
record->patched_length = 0u;
record->active = false;
record->original_code.clear();
```

就是清空状态。

### 2. `ActivateInlineHookRecord(...)`

```cpp
record->target_address = target_address;
record->replacement_address = replacement_address;
record->trampoline_address = trampoline_address;
record->patched_length = patched_length;
record->active = true;
record->original_code.assign(original_code, original_code + original_code_size);
```

就是把一次 hook 的关键恢复信息完整保存下来。

---

## 六、`trampoline_allocator.h/.cpp`：给 trampoline 找到真正可执行的内存

头文件：

```cpp
struct TrampolineAllocation {
    void* address = nullptr;
    size_t size = 0u;
};

bool AllocateExecutableTrampoline(size_t size, TrampolineAllocation* allocation);
void FreeExecutableTrampoline(TrampolineAllocation* allocation);
```

### 1. `AllocateExecutableTrampoline(...)`

Linux/Android 下的核心实现：

```cpp
const long page_size = sysconf(_SC_PAGESIZE);
const size_t rounded_size =
        ((size + static_cast<size_t>(page_size) - 1u) / static_cast<size_t>(page_size)) *
        static_cast<size_t>(page_size);
void* address = mmap(nullptr,
                     rounded_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1,
                     0);
```

说明 trampoline 需要：

1. 可读
2. 可写
3. 可执行

### 2. `FreeExecutableTrampoline(...)`

```cpp
munmap(allocation->address, allocation->size);
allocation->address = nullptr;
allocation->size = 0u;
```

就是 trampoline 生命周期回收。

---

## 七、`arm64_instruction_relocator.h/.cpp`：真正把前 5 条指令变成可搬迁代码块

### 1. 头文件接口先说明职责

```cpp
size_t GetArm64RelocatedInstructionLength(uint32_t instruction);
bool RewriteArm64Instruction(...);
bool RelocateArm64InstructionSequence(...);
```

这说明它分三层：

1. 估长度
2. 改单条
3. 改整段

### 2. 指令分类：`GetInstructionType(...)`

文件开头定义了这些类型：

1. `kB`
2. `kBl`
3. `kBCond`
4. `kAdr`
5. `kAdrp`
6. `kLdrLit32`
7. `kLdrLit64`
8. `kLdrswLit`
9. `kPrfmLit`
10. `kLdrSimdLit32`
11. `kLdrSimdLit64`
12. `kLdrSimdLit128`
13. `kCbz`
14. `kCbnz`
15. `kTbz`
16. `kTbnz`
17. `kIgnored`

这说明当前 relocator 不是通用 ARM64 反汇编器，而是专门面向 inline hook 场景的实用重写器。

### 3. `TranslateAddressIfNeeded(...)`

```cpp
const uintptr_t source_block_end = source_block_start + instruction_count * sizeof(uint32_t);
if (address < source_block_start || address >= source_block_end) {
    return address;
}
```

它在解决一个经常被忽略的问题：

> 如果原始前 5 条指令内部自己就有跳转关系，搬到 trampoline 后这些内部目标地址也要一起映射过去。

### 4. `EmitAbsoluteBranch(...)`

```cpp
output[0] = 0x58000051u;
output[1] = 0x14000003u;
output[2] = low32(target_address);
output[3] = high32(target_address);
output[4] = link ? 0xD63F0220u : 0xD61F0220u;
```

这是 relocation 中最重要的基础积木：

1. `B/BL` 可以用它
2. 条件分支展开后也会用它

### 5. `RewriteWithInternalContext(...)` 是真正的单条重写核心

先把函数骨架贴出来：

```cpp
static bool RewriteWithInternalContext(uint32_t instruction,
                                       uintptr_t instruction_address,
                                       uintptr_t source_block_start,
                                       uintptr_t relocated_block_start,
                                       const size_t* relocated_instruction_lengths,
                                       size_t relocated_instruction_count,
                                       uint32_t* output,
                                       size_t output_capacity_words,
                                       size_t* output_word_count) {
    if (output == nullptr || output_word_count == nullptr) {
        return false;
    }

    const Arm64InstructionType type = GetInstructionType(instruction);
    switch (type) {
        ...
    }
}
```

也就是说，所有单条指令 relocation 最后都会落到这个 `switch` 里。下面按类型看。

#### 5.1 `B` / `BL`

```cpp
uintptr_t target = instruction_address + SignExtend64(imm26 << 2u, 28u);
target = TranslateAddressIfNeeded(target, ...);
return EmitAbsoluteBranch(output, ..., target, type == Arm64InstructionType::kBl, output_word_count);
```

也就是：

1. 先算原始 branch 目标
2. 如果目标在被搬迁块内，先重映射
3. 再改成绝对跳转或绝对调用

#### 5.2 `ADR` / `ADRP`

```cpp
output[0] = 0x58000040u | rd;  // LDR Xd, #8
output[1] = 0x14000003u;       // B #12
output[2] = low32(target);
output[3] = high32(target);
```

原本是 PC-relative 生成地址，现在直接改成加载绝对地址。

#### 5.3 `B.cond`

```cpp
output[0] = (instruction & 0xFF00001Fu) | 0x40u;  // B.<cond> #8
output[1] = 0x14000005u;                          // B #20
output[2] = 0x58000051u;                          // LDR X17, #8
output[3] = 0xD61F0220u;                          // BR X17
output[4] = low32(target);
output[5] = high32(target);
```

也就是说：

1. 条件判断逻辑本身保留
2. 真正的目标跳转改成绝对跳转桩

#### 5.4 `CBZ/CBNZ`

和 `B.cond` 一个思路：

1. 保留寄存器条件判断
2. 真正跳转走绝对桩

#### 5.5 `TBZ/TBNZ`

同样是“条件判断 + 绝对跳转”。

#### 5.6 `LDR literal` / `LDRSW literal`

```cpp
output[0] = 0x58000060u | rt;  // LDR Xt, #12
output[1] = ...                // LDR / LDRSW [Xt]
output[2] = 0x14000003u;
output[3] = low32(target);
output[4] = high32(target);
```

原来是 PC-relative 取字面量，现在改成：

1. 先加载绝对地址
2. 再从这个绝对地址位置取值

#### 5.7 `PRFM` / SIMD literal loads

```cpp
output[0] = 0xA93F47F0u;  // STP X16, X17, [SP, #-0x10]
output[1] = 0x58000091u;  // LDR X17, #16
...
output[3] = 0xF85F83F1u;  // LDR X17, [SP, #-0x8]
```

这说明作者考虑到了更复杂的访存场景，需要：

1. 临时保存寄存器
2. 做绝对地址访问
3. 再恢复寄存器

#### 5.8 默认分支

```cpp
output[0] = instruction;
*output_word_count = 1u;
```

普通指令原样保留。

### 6. `GetArm64RelocatedInstructionLength(...)`

这里是 trampoline 预估的依据：

1. `B/BL` -> 20
2. `ADR/ADRP` -> 16
3. 条件跳转类 -> 24
4. `LDR literal` / `LDRSW` -> 20
5. `PRFM` / SIMD literal loads -> 28
6. 默认 -> 4

### 7. `RelocateArm64InstructionSequence(...)`

这一段是 `InstallInlineHook(...)` 真正调用的入口：

```cpp
std::vector<size_t> rewritten_lengths(instruction_count, 0u);
for (size_t i = 0; i < instruction_count; ++i) {
    rewritten_lengths[i] = GetArm64RelocatedInstructionLength(instructions[i]);
    total_output_words += rewritten_lengths[i] / sizeof(uint32_t);
}
```

先算所有长度，再逐条重写：

```cpp
for (size_t i = 0; i < instruction_count; ++i) {
    if (!RewriteWithInternalContext(...)) {
        return false;
    }
    output_offset += rewritten_words;
}
```

到这里，trampoline 的前半部分才真正构造完成。

---

## 八、`pending_inline_hook_registry.h/.cpp`：deferred hook 的挂起请求表

头文件里先定义了安装回调类型：

```cpp
using InstallPendingInlineSymbolHookFn = NookStatus (*)(const char* module_path,
                                                        const char* symbol_name,
                                                        void* replacement,
                                                        void** original,
                                                        void** hook_handle,
                                                        void* context);
```

说明 pending registry 自己不懂 patch，它只是：

1. 维护一批待安装请求
2. 在模块出现时，通过回调让外部去真正安装

再看请求结构：

```cpp
struct PendingInlineHookRequest {
    const char* module_name = nullptr;
    const char* symbol_name = nullptr;
    void* replacement = nullptr;
    void** original = nullptr;
    void** hook_handle = nullptr;
};
```

### 1. 内部真正存储的结构

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

差异点是：

1. 字符串变成了自己持有的 `std::string`
2. 多了 `installed` 状态

### 2. `RegisterPendingInlineHook(...)`

```cpp
if (!IsValidPendingInlineHookRequest(request)) {
    return false;
}
...
if (entry.module_name == request.module_name &&
    entry.symbol_name == request.symbol_name &&
    entry.replacement == request.replacement &&
    entry.original == request.original &&
    entry.hook_handle == request.hook_handle) {
    return true;
}
```

说明：

1. 注册前先校验
2. 会做去重

最后才 push：

```cpp
entry.module_name = request.module_name;
entry.symbol_name = request.symbol_name;
entry.replacement = request.replacement;
entry.original = request.original;
entry.hook_handle = request.hook_handle;
entry.installed = false;
```

### 3. `TryInstallPendingInlineHooksForModule(...)`

只是把单模块路径转成数组，再走批量版本。

### 4. `TryInstallPendingInlineHooksForModules(...)`

这是真正核心：

#### 4.1 先筛候选

```cpp
for (size_t index = 0u; index < g_pending_inline_hook_registry.size(); ++index) {
    const PendingInlineHookEntry& entry = g_pending_inline_hook_registry[index];
    if (entry.installed) {
        continue;
    }
    ...
    if (ElfHooker::module_path_matches(module_path, entry.module_name.c_str())) {
        matched_module_path = module_path;
        break;
    }
}
```

也就是：

1. 只看还没安装的请求
2. 用模块路径匹配规则找命中项
3. 命中的收集成 candidate

#### 4.2 再真正安装

```cpp
const NookStatus status =
        dependencies.install_symbol_hook(candidate.module_path.c_str(),
                                         candidate.symbol_name.c_str(),
                                         candidate.replacement,
                                         candidate.original,
                                         candidate.hook_handle,
                                         dependencies.context);
```

registry 自己不 patch，只负责调用外部回调。

#### 4.3 安装成功后标记状态

```cpp
if (!entry.installed) {
    entry.installed = true;
    ++installed_count;
}
```

这样 deferred hook 就保证只装一次。

所以 pending registry 的本质就是：

> deferred inline hook 的任务表。

---

## 九、`inline_hook_module_observer.h/.cpp`：整个 deferred install 的关键工程模块

头文件只暴露两个函数：

```cpp
NookStatus InitializeInlineHookModuleObserver(void);
NookStatus EnsureInlineHookModuleObserverAsync(void);
```

真正复杂的东西都在 `inline_hook_module_observer.cpp`。

### 1. include 关系先说明它的定位

```cpp
#include "agent_runtime/nook_native_js_bridge.h"
#include "native_hook/core/module_info.h"
#include "native_hook/core/native_hook_symbol_resolver.h"
#include "native_hook/inline_hook/inline_hook_impl.h"
#include "native_hook/inline_hook/pending_inline_hook_registry.h"
#include "nook/NookInlineHook.h"
```

说明 observer 同时依赖：

1. 模块查询
2. 符号解析
3. inline hook patch 内核
4. pending registry
5. 更高层的 native js hook bridge

所以 observer 是 deferred install 的“桥接中枢”。

### 2. 全局状态分几类

#### 2.1 linker 目标

```cpp
constexpr char kLinkerModuleName[] = "linker64"; // 或 linker
constexpr char kLinkerCallConstructorsSymbolLower[] = "__dl__ZN6soinfo17call_constructorsEv";
constexpr char kLinkerCallConstructorsSymbolUpper[] = "__dl__ZN6soinfo16CallConstructorsEv";
```

观察点就是 linker 内部的 `soinfo::call_constructors()`。

#### 2.2 probe 相关

```cpp
constexpr char kProbeLibraryName[] = "libnook_inline_observer_probe.so";
std::string g_probe_module_path;
std::string g_probe_module_basename;
```

说明 observer 会依赖一个 probe so 来辅助识别 `soinfo` 偏移。

#### 2.3 `soinfo` 偏移缓存

```cpp
size_t g_soinfo_offset_phdr = SIZE_MAX;
size_t g_soinfo_offset_phnum = SIZE_MAX;
size_t g_soinfo_offset_load_bias = SIZE_MAX;
size_t g_soinfo_offset_name = SIZE_MAX;
size_t g_soinfo_offset_constructors_called = SIZE_MAX;
```

说明这些字段不是硬编码，而是运行时探测后缓存。

#### 2.4 初始化控制

```cpp
std::once_flag g_module_observer_once;
NookStatus g_module_observer_status = NOOK_STATUS_INTERNAL_ERROR;
std::mutex g_module_observer_async_mutex;
bool g_module_observer_async_started = false;
```

保证 observer：

1. 只初始化一次
2. 支持异步调度
3. 能复用初始化结果

#### 2.5 TLS guard

```cpp
pthread_key_t g_inline_hook_module_notification_key = 0;
bool g_inline_hook_module_notification_key_ready = false;
```

防止 observer 在模块通知期间递归重入。

### 3. `IsReadableAddress(...)`

```cpp
return ElfHooker::get_address_protection(const_cast<void*>(address), &protection) &&
       ((protection & PROT_READ) != 0);
```

后面探测 `soinfo` 字段时要用它判断某个疑似字符串指针是否真的可读。

### 4. `InstallPendingInlineSymbolHook(...)`

```cpp
if (NookNativeHookInternal::ResolveSymbolAddressInLoadedModule(module_path,
                                                               symbol_name,
                                                               &target_address)) {
    status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
}
```

这说明 observer 不自己 patch，它只是给 registry 提供 deferred 场景下真正的安装回调。

### 5. `NotifyModuleLoaded(...)`

```cpp
PendingInlineHookInstallerDependencies dependencies = {};
dependencies.install_symbol_hook = &InstallPendingInlineSymbolHook;
const size_t installed = TryInstallPendingInlineHooksForModule(module_path, dependencies);
std::string native_js_error;
const size_t native_js_installed =
        nook::agent_runtime::NotifyNativeJsHookModuleLoaded(module_path, &native_js_error);
```

这段说明：

1. observer 先驱动 pending inline hooks 安装
2. 再通知 native js hook 系统

说明当前 observer 已经不只是 inline hook 私有逻辑，而是更高层复用的模块加载事件基础设施。

### 6. `TryGetProbeModuleInfo(...)`

```cpp
void* handle = xdl_open(g_probe_module_path.c_str(), XDL_DEFAULT);
xdl_info(handle, XDL_DI_DLINFO, probe_info);
```

这里不是为了加载 probe，而是为了取 probe 的已知模块信息，给 `soinfo` 偏移探测当样本。

### 7. `ComputeProbeDynamicAddress(...)`

遍历 probe 的 program headers，找到 `PT_DYNAMIC`，计算动态段地址。

这个值会拿去和 `soinfo` 中的字段对照匹配。

### 8. `TryDiscoverSoinfoOffsets(...)`

这是 observer 最核心的一段。

它的思路是：

1. probe 的 `dlpi_phdr`、`dlpi_phnum`、`dli_fbase`、模块名、动态段地址都是已知值
2. 当前传进来的 `soinfo*` 对应的也是 probe 自己
3. 那么扫描 `soinfo` 前若干个 word，就能把这些字段的位置反推出来

典型匹配逻辑：

```cpp
if (g_soinfo_offset_phdr == SIZE_MAX &&
    value_0 == reinterpret_cast<uintptr_t>(probe_info.dlpi_phdr) &&
    value_1 == static_cast<uintptr_t>(probe_info.dlpi_phnum)) {
    g_soinfo_offset_phdr = offset;
    g_soinfo_offset_phnum = offset + sizeof(uintptr_t);
}
```

以及：

```cpp
if (g_soinfo_offset_load_bias == SIZE_MAX &&
    value_0 == reinterpret_cast<uintptr_t>(probe_info.dli_fbase) &&
    value_2 == probe_dynamic &&
    value_5 == 0u &&
    value_6 == value_0) {
    const char* candidate_name = reinterpret_cast<const char*>(value_1);
    ...
    g_soinfo_offset_load_bias = offset;
    g_soinfo_offset_name = offset + sizeof(uintptr_t);
    g_soinfo_offset_constructors_called = offset + sizeof(uintptr_t) * 5u;
}
```

这段就是整套 deferred observer 里最有逆向味的部分。

### 9. `FinalizeSoinfoOffsetDiscovery(...)`

```cpp
const int constructors_called = *(...);
if (constructors_called == 0) {
    return false;
}

g_soinfo_offsets_ready.store(true, std::memory_order_release);
```

意思是：前面只是找到疑似偏移；这里再做一次状态确认，确认 probe 的 `soinfo` 真正到了预期阶段后，才正式把偏移标记为 ready。

### 10. `IsSoinfoLoading(...)`

```cpp
return *(... + g_soinfo_offset_constructors_called) == 0;
```

这给 observer 一个判断：

> 当前模块是否仍处在构造尚未完成的加载阶段。

### 11. `GetLoadedModulePathFromSoinfo(...)`

```cpp
const char* module_path = *(... + g_soinfo_offset_name);
if (module_path == nullptr || module_path[0] == '\0' || !IsReadableAddress(module_path)) {
    return nullptr;
}
```

这里就是 observer 真正把 `soinfo*` 转成模块路径的关键一步。

### 12. `TryInstallObserverHook(...)`

```cpp
return InstallInlineHook(target_address, replacement, original, hook_handle);
```

也就是说：

> observer 自己也是通过当前这套 inline hook 内核装上去的。

### 13. `HookedLinkerCallConstructors(void* soinfo)`

这是整个 deferred install 在运行时真正被触发的入口，先看完整代码：

```cpp
extern "C" void HookedLinkerCallConstructors(void* soinfo) {
    bool scan_started = false;
    if (!g_soinfo_offsets_ready.load(std::memory_order_acquire) &&
        g_soinfo_scan_requested.load(std::memory_order_acquire)) {
        scan_started = TryDiscoverSoinfoOffsets(soinfo);
    }

    const char* module_path = nullptr;
    if (g_soinfo_offsets_ready.load(std::memory_order_acquire) && IsSoinfoLoading(soinfo)) {
        module_path = GetLoadedModulePathFromSoinfo(soinfo);
        if (module_path != nullptr && !g_probe_module_basename.empty() &&
            !EndsWith(module_path, g_probe_module_basename.c_str())) {
            (void)NotifyModuleLoaded(module_path);
        }
    }

    auto* original = reinterpret_cast<LinkerCallConstructorsFn>(g_original_linker_call_constructors);
    if (original != nullptr) {
        original(soinfo);
    }

    if (scan_started) {
        (void)FinalizeSoinfoOffsetDiscovery(soinfo);
    }
}
```

这是整个 deferred install 在运行时真正被触发的入口。

分三段：

#### 13.1 如果当前正在做 probe 扫描，就先尝试识别 `soinfo` 偏移

```cpp
if (!g_soinfo_offsets_ready &&
    g_soinfo_scan_requested.load(std::memory_order_acquire)) {
    scan_started = TryDiscoverSoinfoOffsets(soinfo);
}
```

#### 13.2 如果偏移已就绪，并且模块当前处于加载阶段，就取路径并通知系统

```cpp
if (g_soinfo_offsets_ready.load(std::memory_order_acquire) && IsSoinfoLoading(soinfo)) {
    module_path = GetLoadedModulePathFromSoinfo(soinfo);
    if (module_path != nullptr && !g_probe_module_basename.empty() &&
        !EndsWith(module_path, g_probe_module_basename.c_str())) {
        (void)NotifyModuleLoaded(module_path);
    }
}
```

probe 自己会被排除掉，避免误当成普通目标模块。

#### 13.3 最后调用原始 linker 逻辑

```cpp
auto* original = reinterpret_cast<LinkerCallConstructorsFn>(g_original_linker_call_constructors);
if (original != nullptr) {
    original(soinfo);
}
```

observer 只是插入观察窗口，不替代 linker。

### 14. `InitializeInlineHookModuleObserverOnce()`

这是 observer 初始化主线，顺着代码看：

1. 创建 TLS key
2. 用 `dladdr` 反推出当前 payload so 路径
3. 拼出 probe so 路径
4. 打开 linker / linker64
5. 解析 `call_constructors` 符号地址
6. 对它安装 inline hook
7. `dlopen(probe)` 触发一轮 `soinfo` 偏移探测
8. 根据偏移是否 ready 确定 observer 初始化最终状态

### 15. `InitializeInlineHookModuleObserver()`

```cpp
std::call_once(g_module_observer_once, &InitializeInlineHookModuleObserverOnce);
return g_module_observer_status;
```

确保只初始化一次。

### 16. `EnsureInlineHookModuleObserverAsync()`

```cpp
if (g_module_observer_async_started) {
    return g_module_observer_async_schedule_status;
}
...
pthread_create(&thread, nullptr, &InitializeInlineHookModuleObserverThreadMain, nullptr)
...
pthread_detach(thread);
```

说明 deferred hook 注册时不会同步阻塞等待 observer 完成初始化，而是异步拉起。

这个文件总结成一句话就是：

> 它把“未来某个模块加载到合适阶段，再真正安装 pending inline hook”这件事，完整工程化了。

---

## 十、`native_hook_symbol_resolver.h/.cpp`：把 `module + symbol` 变成真实运行时地址

头文件先定义可插拔解析依赖：

```cpp
using OpenSymbolModuleFn = void* (*)(const char* module_name, void* context);
using FindSymbolInModuleFn = void* (*)(void* handle, const char* symbol_name, void* context);
using CloseSymbolModuleFn = void (*)(void* handle, void* context);
```

再定义：

```cpp
struct SymbolResolverDependencies {
    OpenSymbolModuleFn open_preferred_module = nullptr;
    FindSymbolInModuleFn find_preferred_symbol = nullptr;
    ...
};
```

说明 resolver 是按“优先策略 + fallback 策略”组织的。

### 1. `InitializeResolverOutput(...)`

```cpp
*symbol_address = nullptr;
if (module_name == nullptr || module_name[0] == '\0' ||
    symbol_name == nullptr || symbol_name[0] == '\0') {
    return false;
}
```

先清输出，再验输入。

### 2. `TryResolveWithStrategy(...)`

```cpp
void* handle = open_module(module_name, context);
...
*symbol_address = find_symbol(handle, symbol_name, context);
```

这是“打开模块 -> 查符号 -> 关闭模块”的统一包装。

### 3. Android 下的 xdl 路径

```cpp
void* OpenLoadedModuleWithXdl(const char* module_name, void*) {
    return xdl_open(module_name, XDL_DEFAULT);
}

void* FindSymbolInLoadedModuleWithXdl(void* handle, const char* symbol_name, void*) {
    void* address = xdl_sym(handle, symbol_name, nullptr);
    if (address != nullptr) {
        return address;
    }
    return xdl_dsym(handle, symbol_name, nullptr);
}
```

### 4. fallback 的 `dlopen/dlsym`

```cpp
void* OpenModuleWithDlopen(const char* module_name, void*) {
    return dlopen(module_name, RTLD_NOW);
}

void* FindSymbolWithDlsym(void* handle, const char* symbol_name, void*) {
    return dlsym(handle, symbol_name);
}
```

### 5. `ResolveSymbolAddressWithDependencies(...)`

先试 preferred，再试 fallback。

### 6. `ResolveSymbolAddressInModuleFile(...)`

这一步最关键：

```cpp
ElfHooker::ElfioImageParser parser;
if (!parser.LoadFromFile(module_path)) {
    return false;
}

uint64_t symbol_value = 0;
if (!parser.FindDynamicSymbolValue(symbol_name, &symbol_value)) {
    return false;
}
```

先从 ELF 文件里拿动态符号值，然后：

```cpp
uintptr_t runtime_bias = 0u;
if (!parser.ComputeRuntimeBias(reinterpret_cast<uintptr_t>(module_base), &runtime_bias)) {
    return false;
}

const uintptr_t runtime_symbol_address = runtime_bias + static_cast<uintptr_t>(symbol_value);
```

也就是：

> 运行时地址 = runtime bias + 文件视角下的 symbol value

### 7. GNU IFUNC 特判

```cpp
if (has_symbol_type && symbol_type == kElfSymbolTypeGnuIfunc) {
    IfuncResolverFn resolver = reinterpret_cast<IfuncResolverFn>(runtime_symbol_address);
    *symbol_address = resolver();
    return *symbol_address != nullptr;
}
```

说明作者考虑到了 IFUNC 这种特殊符号。

### 8. `ResolveSymbolAddressInLoadedModule(...)`

```cpp
void* module_base = nullptr;
std::string module_path;
if (!ElfHooker::get_module_info(0, module_name, &module_base, &module_path) ||
    module_base == nullptr || module_path.empty()) {
    return false;
}

return ResolveSymbolAddressInModuleFile(module_path.c_str(),
                                        module_base,
                                        symbol_name,
                                        symbol_address);
```

说明当前 resolver 非常偏好“当前进程真实加载模块 + 对应 ELF 文件”的组合视角。

### 9. `ResolveSymbolAddress(...)`

先走：

```cpp
if (ResolveSymbolAddressInLoadedModule(module_name, symbol_name, symbol_address)) {
    return true;
}
```

再 fallback 到：

1. xdl
2. `dlopen/dlsym`

### 10. `IsSymbolInlineHookSafeInModuleFile(...)`

这一层做的不是地址解析，而是安全性判断。

#### 10.1 避免把 IFUNC resolver 本体当成 hook 目标

```cpp
if (reinterpret_cast<uintptr_t>(symbol_address) == runtime_resolver_address) {
    return false;
}
```

#### 10.2 检查 symbol size 是否至少够当前 patch 长度

```cpp
return symbol_size >= NookInlineHookInternal::GetArm64InlineHookPatchSize();
```

也就是至少要有 `20` 字节可覆盖。

### 11. `IsSymbolInlineHookSafeInLoadedModule(...)`

先取 loaded module 的 base 和 path，再继续走文件级安全检查。

所以 resolver 这一层不是只会“把地址找出来”，它还开始回答“这个地址适不适合按当前 inline hook 方案直接 patch”。

---

## 十一、`module_match.cpp`：为什么 deferred hook 不能只做完整路径匹配

文件虽然短，但很关键：

```cpp
if (std::strcmp(mapped_path, module_name) == 0) {
    return true;
}

const char* basename = std::strrchr(mapped_path, '/');
if (basename != nullptr && std::strcmp(basename + 1, module_name) == 0) {
    return true;
}

return std::strstr(mapped_path, module_name) != nullptr;
```

匹配规则就是：

1. 完整路径相等
2. basename 相等
3. 子串匹配

这让 payload 侧既可以传完整路径，也可以只传：

```text
libnative-lib.so
```

对 deferred hook 来说，这个实用性很重要。

---

## 十二、`module_info.h/.cpp`：resolver 和 observer 的共同底座

头文件定义四个能力：

```cpp
bool get_module_info(pid_t pid, const char* module, void** module_base, std::string* module_path);
void* get_module_base(pid_t pid, const char* module);
bool get_address_protection(void* address, int* protection);
void clear_cache(void* addr, size_t len);
```

### 1. `get_module_info(...)`

实现上就是读 `/proc/self/maps`：

```cpp
if (pid <= 0) {
    std::snprintf(buffer, sizeof(buffer), "/proc/self/maps");
} else {
    std::snprintf(buffer, sizeof(buffer), "/proc/%d/maps", pid);
}
```

逐行解析：

```cpp
if (std::sscanf(buffer,
                "%lx-%lx %4s %*x %*x:%*x %*d %127s",
                &map_start,
                &map_end,
                perms,
                so_name) != 4) {
    continue;
}
```

再做模块匹配：

```cpp
if (!module_path_matches(so_name, module)) {
    continue;
}
```

最终返回：

1. `module_base`
2. `module_path`

这就是 resolver 为何能把模块名还原成“当前进程真实加载模块实例”。

### 2. `get_module_base(...)`

只是 `get_module_info(...)` 的简化包装。

### 3. `get_address_protection(...)`

也是扫 `/proc/self/maps`，但目标是查某个具体地址所在映射的权限。

observer 在探测 `soinfo` 偏移时要靠它判断某个候选字符串指针是否可读。

### 4. `clear_cache(...)`

当前实现也是直接走编译器内建。

所以 `module_info.*` 这层虽然小，但它是：

1. resolver 找模块位置的底座
2. observer 判断地址可读性的底座

---

## 十三、把所有文件重新串成一条总调用链

现在可以把所有文件按真正调用关系收束起来。

### 场景一：`NookInlineHookAddress(...)`

```text
NookInlineHook.h
    -> NookInlineHook.cpp::NookInlineHookAddress(...)
        -> inline_hook_impl.cpp::InstallInlineHook(...)
            -> trampoline_allocator.cpp::AllocateExecutableTrampoline(...)
            -> arm64_instruction_relocator.cpp::RelocateArm64InstructionSequence(...)
            -> inline_hook_record.cpp::ActivateInlineHookRecord(...)
            -> inline_hook_impl.cpp::WriteAbsoluteJumpPatch(...)
```

### 场景二：`NookInlineHookSymbol(...)`

```text
NookInlineHook.cpp::NookInlineHookSymbol(...)
    -> TryInstallInlineHookSymbolNow(...)
        -> native_hook_symbol_resolver.cpp::ResolveSymbolAddress(...)
            -> module_info.cpp::get_module_info(...)
            -> ResolveSymbolAddressInModuleFile(...)
        -> NookInlineHookAddress(...)
            -> InstallInlineHook(...)
```

### 场景三：`NookInlineHookSymbolDeferred(...)`

```text
NookInlineHook.cpp::NookInlineHookSymbolDeferred(...)
    -> pending_inline_hook_registry.cpp::RegisterPendingInlineHook(...)
    -> inline_hook_module_observer.cpp::EnsureInlineHookModuleObserverAsync(...)
        -> InitializeInlineHookModuleObserverOnce(...)
            -> InstallInlineHook(call_constructors, HookedLinkerCallConstructors, ...)
            -> dlopen(probe)
            -> TryDiscoverSoinfoOffsets(...)
```

之后某个模块真的加载时：

```text
HookedLinkerCallConstructors(soinfo)
    -> GetLoadedModulePathFromSoinfo(...)
    -> NotifyModuleLoaded(module_path)
        -> TryInstallPendingInlineHooksForModule(...)
            -> InstallPendingInlineSymbolHook(...)
                -> ResolveSymbolAddressInLoadedModule(...)
                -> NookInlineHookAddress(...)
                    -> InstallInlineHook(...)
```

### 场景四：`NookInlineUnhook(...)`

```text
NookInlineHook.cpp::NookInlineUnhook(...)
    -> inline_hook_impl.cpp::UninstallInlineHook(...)
        -> RestoreOriginalCode(...)
        -> inline_hook_record.cpp::ResetInlineHookRecord(...)
        -> trampoline_allocator.cpp::FreeExecutableTrampoline(...)
```

这一条总链说明了最核心的事实：

> 所有 direct、symbol、deferred 三种入口，最终都会收敛到同一个 patch 内核 `InstallInlineHook(...)`，区别只在于目标地址从哪里来，以及什么时候才适合安装。

---

## 结语

这一版之所以比前一版更接近“源码精读”，关键不在于讲了更多原理，而在于把所有相关文件真正串起来了：

1. `NookInlineHook.h` 负责定义对外能力边界
2. `NookInlineHook.cpp` 负责分发 direct / symbol / deferred / unhook 四条入口
3. `inline_hook_impl.*` 负责真正 patch 与恢复
4. `inline_hook_record.*` 负责保存现场
5. `trampoline_allocator.*` 负责 trampoline 生命周期
6. `arm64_instruction_relocator.*` 负责把原入口前导块重写成可搬迁代码
7. `pending_inline_hook_registry.*` 负责 deferred 挂起请求表
8. `inline_hook_module_observer.*` 负责模块生命周期观察与延迟安装
9. `native_hook_symbol_resolver.*` 负责把 `module + symbol` 转成真实运行时地址
10. `module_match.cpp` 与 `module_info.*` 则是 resolver 和 observer 的公共底座

把这些文件一层层接起来之后，看到的就不再只是“一个能改入口的 patch”，而是一套真正有 API、有 patch 内核、有 relocation、有 deferred install、有 observer 的 native inline hook 系统。


