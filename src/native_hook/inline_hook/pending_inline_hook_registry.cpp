#include "native_hook/inline_hook/pending_inline_hook_registry.h"

#include "native_hook/core/module_match.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace NookInlineHookInternal {

namespace {

struct PendingInlineHookEntry {
    std::string module_name;
    std::string symbol_name;
    void* replacement = nullptr;
    void** original = nullptr;
    void** hook_handle = nullptr;
    bool installed = false;
};

struct PendingInstallCandidate {
    size_t index = 0u;
    std::string module_path;
    std::string symbol_name;
    void* replacement = nullptr;
    void** original = nullptr;
    void** hook_handle = nullptr;
};

std::mutex g_pending_inline_hook_registry_mutex;
std::vector<PendingInlineHookEntry> g_pending_inline_hook_registry;

bool IsValidPendingInlineHookRequest(const PendingInlineHookRequest& request) {
    return request.module_name != nullptr && request.module_name[0] != '\0' &&
           request.symbol_name != nullptr && request.symbol_name[0] != '\0' &&
           request.replacement != nullptr && request.original != nullptr &&
           request.hook_handle != nullptr;
}

size_t CountEntriesByInstalledState(bool installed) {
    size_t count = 0u;
    for (const PendingInlineHookEntry& entry : g_pending_inline_hook_registry) {
        if (entry.installed == installed) {
            ++count;
        }
    }
    return count;
}

}  // namespace

bool RegisterPendingInlineHook(const PendingInlineHookRequest& request) {
    if (!IsValidPendingInlineHookRequest(request)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
    for (const PendingInlineHookEntry& entry : g_pending_inline_hook_registry) {
        if (entry.module_name == request.module_name &&
            entry.symbol_name == request.symbol_name &&
            entry.replacement == request.replacement &&
            entry.original == request.original &&
            entry.hook_handle == request.hook_handle) {
            return true;
        }
    }

    PendingInlineHookEntry entry = {};
    entry.module_name = request.module_name;
    entry.symbol_name = request.symbol_name;
    entry.replacement = request.replacement;
    entry.original = request.original;
    entry.hook_handle = request.hook_handle;
    entry.installed = false;
    g_pending_inline_hook_registry.push_back(std::move(entry));
    return true;
}

size_t TryInstallPendingInlineHooksForModule(
        const char* module_path,
        const PendingInlineHookInstallerDependencies& dependencies) {
    const char* module_paths[] = {module_path};
    return TryInstallPendingInlineHooksForModules(module_paths, 1u, dependencies);
}

size_t TryInstallPendingInlineHooksForModules(
        const char* const* module_paths,
        size_t module_count,
        const PendingInlineHookInstallerDependencies& dependencies) {
    if (module_paths == nullptr || module_count == 0u ||
        dependencies.install_symbol_hook == nullptr) {
        return 0u;
    }

    std::vector<PendingInstallCandidate> candidates;
    {
        std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
        for (size_t index = 0u; index < g_pending_inline_hook_registry.size(); ++index) {
            const PendingInlineHookEntry& entry = g_pending_inline_hook_registry[index];
            if (entry.installed) {
                continue;
            }

            const char* matched_module_path = nullptr;
            for (size_t module_index = 0u; module_index < module_count; ++module_index) {
                const char* module_path = module_paths[module_index];
                if (module_path == nullptr || module_path[0] == '\0') {
                    continue;
                }
                if (ElfHooker::module_path_matches(module_path, entry.module_name.c_str())) {
                    matched_module_path = module_path;
                    break;
                }
            }
            if (matched_module_path == nullptr) {
                continue;
            }

            PendingInstallCandidate candidate = {};
            candidate.index = index;
            candidate.module_path = matched_module_path;
            candidate.symbol_name = entry.symbol_name;
            candidate.replacement = entry.replacement;
            candidate.original = entry.original;
            candidate.hook_handle = entry.hook_handle;
            candidates.push_back(std::move(candidate));
        }
    }

    size_t installed_count = 0u;
    for (const PendingInstallCandidate& candidate : candidates) {
        const NookStatus status =
                dependencies.install_symbol_hook(candidate.module_path.c_str(),
                                                 candidate.symbol_name.c_str(),
                                                 candidate.replacement,
                                                 candidate.original,
                                                 candidate.hook_handle,
                                                 dependencies.context);
        if (status != NOOK_STATUS_OK) {
            continue;
        }

        std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
        if (candidate.index < g_pending_inline_hook_registry.size()) {
            PendingInlineHookEntry& entry = g_pending_inline_hook_registry[candidate.index];
            if (!entry.installed) {
                entry.installed = true;
                ++installed_count;
            }
        }
    }

    return installed_count;
}

size_t GetPendingInlineHookCountForTesting(void) {
    std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
    return CountEntriesByInstalledState(false);
}

size_t GetInstalledPendingInlineHookCountForTesting(void) {
    std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
    return CountEntriesByInstalledState(true);
}

void ResetPendingInlineHookRegistryForTesting(void) {
    std::lock_guard<std::mutex> lock(g_pending_inline_hook_registry_mutex);
    g_pending_inline_hook_registry.clear();
}

}  // namespace NookInlineHookInternal
