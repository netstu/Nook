#include "nook/NookPltHook.h"

#if defined(__ANDROID__) || defined(__linux__)
#include "native_hook/core/module_info.h"
#include "native_hook/core/native_hook_dispatcher.h"
#include "native_hook/plt_hook/plt_hook_impl.h"
#endif

#include <string>

namespace {

bool g_plt_hook_initialized = false;

#if defined(__ANDROID__) || defined(__linux__)
bool ResolveModuleInfo(pid_t pid,
                       const char* module_name,
                       void** module_base,
                       std::string* module_path,
                       void*) {
    return ElfHooker::get_module_info(pid, module_name, module_base, module_path);
}
#endif

}  // namespace

extern "C" {

NookStatus NookPltHookInitialize(void) {
    g_plt_hook_initialized = true;
    return NOOK_STATUS_OK;
}

NookStatus NookPltHookIsAvailable(int* available) {
    if (available == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    *available = 1;
    return NookPltHookInitialize();
}

NookStatus NookPltHookSymbol(const char* module_name,
                             const char* symbol_name,
                             void* replacement,
                             void** original) {
    if (module_name == nullptr || module_name[0] == '\0' ||
        symbol_name == nullptr || symbol_name[0] == '\0' ||
        replacement == nullptr || original == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    *original = nullptr;
    if (!g_plt_hook_initialized) {
        const NookStatus status = NookPltHookInitialize();
        if (status != NOOK_STATUS_OK) {
            return status;
        }
    }

#if defined(__ANDROID__) || defined(__linux__)
    const NookNativeHookInternal::FallbackHookDependencies dependencies = {
            &ResolveModuleInfo,
            &NookNativeHookInternal::TryPltHookWithElfio,
            &NookNativeHookInternal::TryPltHookWithElfReader,
            nullptr};

    return NookNativeHookInternal::HookSymbolWithFallback(
            module_name, symbol_name, replacement, original, dependencies);
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

}  // extern "C"
