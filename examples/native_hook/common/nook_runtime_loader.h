#pragma once

#include "nook/Nook.h"

#include <android/log.h>
#include <cstdarg>
#include <dlfcn.h>

namespace NookExampleRuntimeLoader {

constexpr char kRuntimePath[] = "/data/local/tmp/Ninjector/libnook.so";
constexpr char kCxxSharedPath[] = "/data/local/tmp/Ninjector/libc++_shared.so";

using NookInlineHookInitializeFn = NookStatus (*)(void);
using NookInlineHookAddressFn = NookStatus (*)(void* target_address,
                                               void* replacement,
                                               void** original,
                                               void** hook_handle);
using NookInlineHookSymbolDeferredFn = NookStatus (*)(const char* module_name,
                                                      const char* symbol_name,
                                                      void* replacement,
                                                      void** original,
                                                      void** hook_handle);
using NookInlineUnhookFn = NookStatus (*)(void* hook_handle);
using NookPltHookInitializeFn = NookStatus (*)(void);
using NookPltHookHookSymbolFn = NookStatus (*)(const char* module_name,
                                               const char* symbol_name,
                                               void* replacement,
                                               void** original);

struct NookInlineApi {
    NookInlineHookInitializeFn initialize = nullptr;
    NookInlineHookAddressFn hook_address = nullptr;
    NookInlineHookSymbolDeferredFn hook_symbol_deferred = nullptr;
    NookInlineUnhookFn unhook = nullptr;
};

struct NookPltApi {
    NookPltHookInitializeFn initialize = nullptr;
    NookPltHookHookSymbolFn hook_symbol = nullptr;
};

inline void LogRuntimeLoader(int priority, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    __android_log_vprint(priority, tag, format, args);
    va_end(args);
}

inline void* TryOpenLibrary(const char* path, int flags, const char* tag) {
    void* handle = dlopen(path, flags);
    if (handle == nullptr) {
        LogRuntimeLoader(ANDROID_LOG_WARN, tag, "dlopen failed path=%s error=%s", path, dlerror());
    }
    return handle;
}

inline void* EnsureNookRuntimeLoaded(const char* tag) {
    static void* runtime_handle = nullptr;
    static bool attempted = false;

    if (runtime_handle != nullptr) {
        return runtime_handle;
    }
    if (attempted) {
        return nullptr;
    }
    attempted = true;

    (void)TryOpenLibrary(kCxxSharedPath, RTLD_NOW | RTLD_GLOBAL, tag);
    runtime_handle = TryOpenLibrary(kRuntimePath, RTLD_NOW | RTLD_GLOBAL, tag);
    if (runtime_handle != nullptr) {
        LogRuntimeLoader(ANDROID_LOG_INFO, tag, "loaded runtime path=%s handle=%p", kRuntimePath, runtime_handle);
    }
    return runtime_handle;
}

template <typename Fn>
inline Fn ResolveSymbol(void* runtime_handle, const char* symbol_name, const char* tag) {
    if (runtime_handle == nullptr) {
        return nullptr;
    }

    dlerror();
    void* symbol = dlsym(runtime_handle, symbol_name);
    const char* error = dlerror();
    if (symbol == nullptr || error != nullptr) {
        LogRuntimeLoader(ANDROID_LOG_ERROR,
                         tag,
                         "dlsym failed symbol=%s error=%s",
                         symbol_name,
                         error != nullptr ? error : "<null>");
        return nullptr;
    }
    return reinterpret_cast<Fn>(symbol);
}

inline bool ResolveNookInlineApi(const char* tag, NookInlineApi* api) {
    if (api == nullptr) {
        return false;
    }

    void* runtime_handle = EnsureNookRuntimeLoaded(tag);
    api->initialize = ResolveSymbol<NookInlineHookInitializeFn>(runtime_handle,
                                                                "NookInlineHookInitialize",
                                                                tag);
    api->hook_address = ResolveSymbol<NookInlineHookAddressFn>(runtime_handle,
                                                               "NookInlineHookAddress",
                                                               tag);
    api->hook_symbol_deferred =
            ResolveSymbol<NookInlineHookSymbolDeferredFn>(runtime_handle,
                                                          "NookInlineHookSymbolDeferred",
                                                          tag);
    api->unhook = ResolveSymbol<NookInlineUnhookFn>(runtime_handle, "NookInlineUnhook", tag);
    return api->initialize != nullptr && api->hook_address != nullptr &&
           api->hook_symbol_deferred != nullptr && api->unhook != nullptr;
}

inline bool ResolveNookPltApi(const char* tag, NookPltApi* api) {
    if (api == nullptr) {
        return false;
    }

    void* runtime_handle = EnsureNookRuntimeLoaded(tag);
    api->initialize = ResolveSymbol<NookPltHookInitializeFn>(runtime_handle,
                                                             "NookPltHookInitialize",
                                                             tag);
    api->hook_symbol = ResolveSymbol<NookPltHookHookSymbolFn>(runtime_handle,
                                                              "NookPltHookSymbol",
                                                              tag);
    return api->initialize != nullptr && api->hook_symbol != nullptr;
}

}  // namespace NookExampleRuntimeLoader
