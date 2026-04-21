#ifndef NOOK_NATIVE_HOOK_DISPATCHER_H
#define NOOK_NATIVE_HOOK_DISPATCHER_H

#include "nook/Nook.h"

#include <string>
#include <unistd.h>

namespace NookNativeInternal {

struct ResolvedHookTarget {
    const char* module_name = nullptr;
    const char* symbol_name = nullptr;
    void* module_base = nullptr;
    std::string module_path;
    void* replacement = nullptr;
    void** original = nullptr;
};

using GetModuleInfoFn = bool (*)(pid_t pid,
                                 const char* module_name,
                                 void** module_base,
                                 std::string* module_path,
                                 void* context);
using HookStrategyFn = bool (*)(const ResolvedHookTarget& target, void* context);

struct FallbackHookDependencies {
    GetModuleInfoFn get_module_info = nullptr;
    HookStrategyFn primary_hook = nullptr;
    HookStrategyFn fallback_hook = nullptr;
    void* context = nullptr;
};

NookStatus HookSymbolWithFallback(const char* module_name,
                                  const char* symbol_name,
                                  void* replacement,
                                  void** original,
                                  const FallbackHookDependencies& dependencies);

}  // namespace NookNativeInternal

#endif  // NOOK_NATIVE_HOOK_DISPATCHER_H
