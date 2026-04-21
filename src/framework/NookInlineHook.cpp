#include "nook/NookInlineHook.h"

#include "native_hook/core/native_hook_symbol_resolver.h"
#include "native_hook/inline_hook/inline_hook_impl.h"
#include "native_hook/inline_hook/inline_hook_module_observer.h"
#include "native_hook/inline_hook/pending_inline_hook_registry.h"

#include <cstdarg>

#if defined(__ANDROID__)
#include <android/log.h>
#else
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
#endif

namespace {

bool g_inline_hook_initialized = false;
constexpr char kInlineFrameworkTag[] = "NookInlineDeferred";

enum class InlineHookSymbolAttemptResult {
    kInstalled,
    kResolveMiss,
    kInstallFailed,
};

void LogDeferredEvent(int priority, const char* format, ...);

InlineHookSymbolAttemptResult TryInstallInlineHookSymbolNow(const char* module_name,
                                                           const char* symbol_name,
                                                           void* replacement,
                                                           void** original,
                                                           void** hook_handle,
                                                           NookStatus* failure_status) {
    if (failure_status != nullptr) {
        *failure_status = NOOK_STATUS_OK;
    }

    void* target_address = nullptr;
    if (!NookNativeInternal::ResolveSymbolAddress(module_name, symbol_name, &target_address)) {
        if (failure_status != nullptr) {
            *failure_status = NOOK_STATUS_INTERNAL_ERROR;
        }
        return InlineHookSymbolAttemptResult::kResolveMiss;
    }

    const NookStatus status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
    if (status != NOOK_STATUS_OK) {
        if (failure_status != nullptr) {
            *failure_status = status;
        }
        return InlineHookSymbolAttemptResult::kInstallFailed;
    }

    return InlineHookSymbolAttemptResult::kInstalled;
}

NookStatus InstallPendingInlineHookSymbol(const char* module_path,
                                         const char* symbol_name,
                                         void* replacement,
                                         void** original,
                                         void** hook_handle,
                                         void*) {
    void* target_address = nullptr;
    NookStatus status = NOOK_STATUS_INTERNAL_ERROR;
    if (NookNativeInternal::ResolveSymbolAddressInLoadedModule(module_path,
                                                               symbol_name,
                                                               &target_address)) {
        status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
    }
    LogDeferredEvent(status == NOOK_STATUS_OK ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                     "pending install module=%s symbol=%s status=%d",
                     module_path,
                     symbol_name,
                     status);
    return status;
}

void LogDeferredEvent(int priority, const char* format, ...) {
#if defined(__ANDROID__)
    va_list args;
    va_start(args, format);
    __android_log_vprint(priority, kInlineFrameworkTag, format, args);
    va_end(args);
#else
    (void)priority;
    (void)format;
#endif
}

}  // namespace

extern "C" {

NookStatus NookInlineHookInitialize(void) {
    g_inline_hook_initialized = true;
    return NOOK_STATUS_OK;
}

NookStatus NookInlineHookIsAvailable(int* available) {
    if (available == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    *available = 1;
    return NookInlineHookInitialize();
}

NookStatus NookInlineHookAddress(void* target_address,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle) {
    if (target_address == nullptr || replacement == nullptr ||
        original == nullptr || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    if (!g_inline_hook_initialized) {
        const NookStatus status = NookInlineHookInitialize();
        if (status != NOOK_STATUS_OK) {
            return status;
        }
    }

    return NookInlineHookInternal::InstallInlineHook(target_address, replacement, original, hook_handle)
                   ? NOOK_STATUS_OK
                   : NOOK_STATUS_INTERNAL_ERROR;
}

NookStatus NookInlineHookSymbol(const char* module_name,
                                const char* symbol_name,
                                void* replacement,
                                void** original,
                                void** hook_handle) {
    if (module_name == nullptr || module_name[0] == '\0' ||
        symbol_name == nullptr || symbol_name[0] == '\0' ||
        replacement == nullptr || original == nullptr || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    NookStatus failure_status = NOOK_STATUS_INTERNAL_ERROR;
    const InlineHookSymbolAttemptResult result =
            TryInstallInlineHookSymbolNow(module_name,
                                          symbol_name,
                                          replacement,
                                          original,
                                          hook_handle,
                                          &failure_status);
    return result == InlineHookSymbolAttemptResult::kInstalled ? NOOK_STATUS_OK : failure_status;
}

NookStatus NookInlineHookSymbolDeferred(const char* module_name,
                                        const char* symbol_name,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle) {
    if (module_name == nullptr || module_name[0] == '\0' ||
        symbol_name == nullptr || symbol_name[0] == '\0' ||
        replacement == nullptr || original == nullptr || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    if (!g_inline_hook_initialized) {
        const NookStatus status = NookInlineHookInitialize();
        if (status != NOOK_STATUS_OK) {
            return status;
        }
    }

    LogDeferredEvent(ANDROID_LOG_INFO,
                     "deferred hook register pending module=%s symbol=%s",
                     module_name,
                     symbol_name);

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
    if (observer_status != NOOK_STATUS_OK) {
        LogDeferredEvent(ANDROID_LOG_ERROR,
                         "deferred hook observer async schedule failed module=%s symbol=%s status=%d",
                         module_name,
                         symbol_name,
                         observer_status);
        return observer_status;
    }
    LogDeferredEvent(ANDROID_LOG_INFO,
                     "deferred hook observer async scheduled module=%s symbol=%s",
                     module_name,
                     symbol_name);
    return NOOK_STATUS_OK;
}

NookStatus NookInlineUnhook(void* hook_handle) {
    if (hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    return NookInlineHookInternal::UninstallInlineHook(hook_handle)
                   ? NOOK_STATUS_OK
                   : NOOK_STATUS_INTERNAL_ERROR;
}

}  // extern "C"
