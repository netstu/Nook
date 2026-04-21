#pragma once

#include <cstddef>

#include "nook/Nook.h"

namespace NookInlineHookInternal {

using InstallPendingInlineSymbolHookFn = NookStatus (*)(const char* module_path,
                                                        const char* symbol_name,
                                                        void* replacement,
                                                        void** original,
                                                        void** hook_handle,
                                                        void* context);

struct PendingInlineHookRequest {
    const char* module_name = nullptr;
    const char* symbol_name = nullptr;
    void* replacement = nullptr;
    void** original = nullptr;
    void** hook_handle = nullptr;
};

struct PendingInlineHookInstallerDependencies {
    InstallPendingInlineSymbolHookFn install_symbol_hook = nullptr;
    void* context = nullptr;
};

bool RegisterPendingInlineHook(const PendingInlineHookRequest& request);
size_t TryInstallPendingInlineHooksForModule(
        const char* module_path,
        const PendingInlineHookInstallerDependencies& dependencies);
size_t TryInstallPendingInlineHooksForModules(
        const char* const* module_paths,
        size_t module_count,
        const PendingInlineHookInstallerDependencies& dependencies);

size_t GetPendingInlineHookCountForTesting(void);
size_t GetInstalledPendingInlineHookCountForTesting(void);
void ResetPendingInlineHookRegistryForTesting(void);

}  // namespace NookInlineHookInternal
