#include "../../src/native_hook/inline_hook/inline_hook_impl.h"

#include "../../src/native_hook/inline_hook/trampoline_allocator.h"

#include <cstdint>

int main() {
    using NookInlineHookInternal::AllocateExecutableTrampoline;
    using NookInlineHookInternal::GetArm64InlineHookPatchSize;
    using NookInlineHookInternal::InstallInlineHook;
    using NookInlineHookInternal::TrampolineAllocation;
    using NookInlineHookInternal::UninstallInlineHook;

    void* original = nullptr;
    void* hook_handle = nullptr;
    if (InstallInlineHook(nullptr, reinterpret_cast<void*>(0x1234u), &original, &hook_handle)) {
        return 1;
    }
    if (InstallInlineHook(reinterpret_cast<void*>(0x1234u), nullptr, &original, &hook_handle)) {
        return 1;
    }
    if (InstallInlineHook(reinterpret_cast<void*>(0x1234u),
                          reinterpret_cast<void*>(0x5678u),
                          nullptr,
                          &hook_handle)) {
        return 1;
    }
    if (InstallInlineHook(reinterpret_cast<void*>(0x1234u),
                          reinterpret_cast<void*>(0x5678u),
                          &original,
                          nullptr)) {
        return 1;
    }

    TrampolineAllocation target_memory = {};
    if (!AllocateExecutableTrampoline(64u, &target_memory)) {
        return 1;
    }

    uint32_t* target_words = reinterpret_cast<uint32_t*>(target_memory.address);
    for (size_t i = 0; i < 8u; ++i) {
        target_words[i] = 0xD503201Fu;  // NOP
    }

    original = nullptr;
    hook_handle = nullptr;
    if (!InstallInlineHook(target_memory.address,
                           reinterpret_cast<void*>(0x123456789abcdef0ull),
                           &original,
                           &hook_handle)) {
        return 1;
    }
    if (original == nullptr || hook_handle == nullptr) {
        return 1;
    }
    if (target_words[0] != 0x58000051u || target_words[4] != 0xD61F0220u) {
        return 1;
    }

    if (!UninstallInlineHook(hook_handle)) {
        return 1;
    }
    for (size_t i = 0; i < GetArm64InlineHookPatchSize() / sizeof(uint32_t); ++i) {
        if (target_words[i] != 0xD503201Fu) {
            return 1;
        }
    }

    return 0;
}
