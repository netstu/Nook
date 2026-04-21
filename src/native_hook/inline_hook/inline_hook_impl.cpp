#include "native_hook/inline_hook/inline_hook_impl.h"

#include "native_hook/inline_hook/arm64_instruction_relocator.h"

#include <cstring>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace NookInlineHookInternal {

namespace {

constexpr size_t kArm64InlineHookPatchWords = 5u;
constexpr size_t kArm64InlineHookPatchSize = kArm64InlineHookPatchWords * sizeof(uint32_t);

static void ClearInstructionCache(void* address, size_t size) {
#if defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), address, size);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(address),
                            reinterpret_cast<char*>(address) + size);
#endif
}

static bool SetPatchWritable(void* address, size_t size, unsigned long* old_protect) {
#if defined(_WIN32)
    DWORD previous = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return false;
    }
    if (old_protect != nullptr) {
        *old_protect = previous;
    }
    return true;
#else
    (void)old_protect;
    const long page_size = sysconf(_SC_PAGESIZE);
    const uintptr_t start = reinterpret_cast<uintptr_t>(address) &
                            ~static_cast<uintptr_t>(page_size - 1);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + page_size - 1) &
                          ~static_cast<uintptr_t>(page_size - 1);
    return mprotect(reinterpret_cast<void*>(start),
                    static_cast<size_t>(end - start),
                    PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
#endif
}

static void RestorePatchProtection(void* address, size_t size, unsigned long old_protect) {
#if defined(_WIN32)
    DWORD ignored = 0;
    VirtualProtect(address, size, static_cast<DWORD>(old_protect), &ignored);
#else
    (void)address;
    (void)size;
    (void)old_protect;
#endif
}

static bool WriteAbsoluteJumpPatch(void* target_address, void* replacement) {
    uint32_t patch[kArm64InlineHookPatchWords] = {};
    const uintptr_t target = reinterpret_cast<uintptr_t>(replacement);
    patch[0] = 0x58000051u;  // LDR X17, #8
    patch[1] = 0x14000003u;  // B #12
    patch[2] = static_cast<uint32_t>(target & 0xffffffffu);
    patch[3] = static_cast<uint32_t>(target >> 32u);
    patch[4] = 0xD61F0220u;  // BR X17

    unsigned long old_protect = 0u;
    if (!SetPatchWritable(target_address, sizeof(patch), &old_protect)) {
        return false;
    }
    std::memcpy(target_address, patch, sizeof(patch));
    ClearInstructionCache(target_address, sizeof(patch));
    RestorePatchProtection(target_address, sizeof(patch), old_protect);
    return true;
}

static bool RestoreOriginalCode(void* target_address, const InlineHookRecord& record) {
    if (target_address == nullptr || record.original_code.empty()) {
        return false;
    }

    unsigned long old_protect = 0u;
    if (!SetPatchWritable(target_address, record.original_code.size(), &old_protect)) {
        return false;
    }
    std::memcpy(target_address, record.original_code.data(), record.original_code.size());
    ClearInstructionCache(target_address, record.original_code.size());
    RestorePatchProtection(target_address, record.original_code.size(), old_protect);
    return true;
}

}  // namespace

size_t GetArm64InlineHookPatchSize(void) {
    return kArm64InlineHookPatchSize;
}

bool InstallInlineHook(void* target_address,
                       void* replacement,
                       void** original,
                       void** hook_handle) {
    if (target_address == nullptr || replacement == nullptr ||
        original == nullptr || hook_handle == nullptr) {
        return false;
    }

    *original = nullptr;
    *hook_handle = nullptr;

    auto* handle = new (std::nothrow) InlineHookHandle();
    if (handle == nullptr) {
        return false;
    }

    uint32_t original_words[kArm64InlineHookPatchWords] = {};
    std::memcpy(original_words, target_address, sizeof(original_words));

    size_t trampoline_words_required = kArm64InlineHookPatchWords;
    for (size_t i = 0; i < kArm64InlineHookPatchWords; ++i) {
        trampoline_words_required += GetArm64RelocatedInstructionLength(original_words[i]) /
                                     sizeof(uint32_t);
    }

    if (!AllocateExecutableTrampoline(trampoline_words_required * sizeof(uint32_t),
                                      &handle->trampoline)) {
        delete handle;
        return false;
    }

    auto* trampoline_words = reinterpret_cast<uint32_t*>(handle->trampoline.address);
    size_t rewritten_word_count = 0u;
    if (!RelocateArm64InstructionSequence(original_words,
                                          kArm64InlineHookPatchWords,
                                          reinterpret_cast<uintptr_t>(target_address),
                                          reinterpret_cast<uintptr_t>(handle->trampoline.address),
                                          trampoline_words,
                                          trampoline_words_required,
                                          &rewritten_word_count)) {
        FreeExecutableTrampoline(&handle->trampoline);
        delete handle;
        return false;
    }

    const uintptr_t return_address =
            reinterpret_cast<uintptr_t>(target_address) + kArm64InlineHookPatchSize;
    if (rewritten_word_count + kArm64InlineHookPatchWords > trampoline_words_required) {
        FreeExecutableTrampoline(&handle->trampoline);
        delete handle;
        return false;
    }

    trampoline_words[rewritten_word_count + 0u] = 0x58000051u;
    trampoline_words[rewritten_word_count + 1u] = 0x14000003u;
    trampoline_words[rewritten_word_count + 2u] = static_cast<uint32_t>(return_address & 0xffffffffu);
    trampoline_words[rewritten_word_count + 3u] = static_cast<uint32_t>(return_address >> 32u);
    trampoline_words[rewritten_word_count + 4u] = 0xD61F0220u;
    ClearInstructionCache(handle->trampoline.address,
                          (rewritten_word_count + kArm64InlineHookPatchWords) * sizeof(uint32_t));

    ActivateInlineHookRecord(&handle->record,
                             target_address,
                             replacement,
                             handle->trampoline.address,
                             kArm64InlineHookPatchSize,
                             reinterpret_cast<const uint8_t*>(original_words),
                             sizeof(original_words));

    if (!WriteAbsoluteJumpPatch(target_address, replacement)) {
        FreeExecutableTrampoline(&handle->trampoline);
        delete handle;
        return false;
    }

    *original = handle->trampoline.address;
    *hook_handle = handle;
    return true;
}

bool UninstallInlineHook(void* hook_handle) {
    if (hook_handle == nullptr) {
        return false;
    }

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
    return true;
}

}  // namespace NookInlineHookInternal
