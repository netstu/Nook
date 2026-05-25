#include "native_hook_dispatcher.h"

namespace NookNativeHookInternal {

NookStatus HookSymbolWithFallback(const char* module_name,
                                  const char* symbol_name,
                                  void* replacement,
                                  void** original,
                                  const FallbackHookDependencies& dependencies) {
    if (dependencies.get_module_info == nullptr || dependencies.primary_hook == nullptr) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    void* module_base = nullptr;
    std::string module_path;
    if (!dependencies.get_module_info(0, module_name, &module_base, &module_path, dependencies.context) ||
        module_base == nullptr || module_path.empty()) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    const ResolvedHookTarget target = {
            module_name,
            symbol_name,
            module_base,
            module_path,
            replacement,
            original};

    if (dependencies.primary_hook(target, dependencies.context)) {
        return NOOK_STATUS_OK;
    }

    if (dependencies.fallback_hook != nullptr &&
        dependencies.fallback_hook(target, dependencies.context)) {
        return NOOK_STATUS_OK;
    }

    return NOOK_STATUS_INTERNAL_ERROR;
}

}  // namespace NookNativeHookInternal
