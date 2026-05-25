#include "../../src/native_hook/inline_hook/pending_inline_hook_registry.h"

#include <cstring>
#include <string>

namespace {

using NookInlineHookInternal::GetInstalledPendingInlineHookCountForTesting;
using NookInlineHookInternal::GetPendingInlineHookCountForTesting;
using NookInlineHookInternal::PendingInlineHookInstallerDependencies;
using NookInlineHookInternal::PendingInlineHookRequest;
using NookInlineHookInternal::RegisterPendingInlineHook;
using NookInlineHookInternal::ResetPendingInlineHookRegistryForTesting;
using NookInlineHookInternal::TryInstallPendingInlineHooksForModule;
using NookInlineHookInternal::TryInstallPendingInlineHooksForModules;

struct FakeInstallerState {
    int call_count = 0;
    int fail_count = 0;
    void* original_value = reinterpret_cast<void*>(0x1111);
    void* hook_handle_value = reinterpret_cast<void*>(0x2222);
    std::string last_module_name;
};

NookStatus FakeInstallSymbolHook(const char* module_name,
                                 const char* symbol_name,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle,
                                 void* context) {
    auto* state = static_cast<FakeInstallerState*>(context);
    ++state->call_count;
    state->last_module_name = module_name != nullptr ? module_name : "";

    if (module_name == nullptr || symbol_name == nullptr || replacement == nullptr ||
        original == nullptr || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    if (state->fail_count > 0) {
        --state->fail_count;
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    *original = state->original_value;
    *hook_handle = state->hook_handle_value;
    return NOOK_STATUS_OK;
}

int ExpectInvalidRequestRejected() {
    ResetPendingInlineHookRegistryForTesting();

    PendingInlineHookRequest request = {};
    if (RegisterPendingInlineHook(request)) {
        return 1;
    }
    if (GetPendingInlineHookCountForTesting() != 0u) {
        return 1;
    }
    return 0;
}

int ExpectMatchingModuleInstallsPendingHook() {
    ResetPendingInlineHookRegistryForTesting();

    void* original = nullptr;
    void* hook_handle = nullptr;
    PendingInlineHookRequest request = {};
    request.module_name = "libnative-lib.so";
    request.symbol_name = "target_symbol";
    request.replacement = reinterpret_cast<void*>(0x3333);
    request.original = &original;
    request.hook_handle = &hook_handle;
    if (!RegisterPendingInlineHook(request)) {
        return 1;
    }

    FakeInstallerState state;
    PendingInlineHookInstallerDependencies dependencies = {};
    dependencies.install_symbol_hook = &FakeInstallSymbolHook;
    dependencies.context = &state;

    if (TryInstallPendingInlineHooksForModule("/data/app/pkg/lib/arm64/libother.so", dependencies) != 0u) {
        return 1;
    }
    if (state.call_count != 0) {
        return 1;
    }

    if (TryInstallPendingInlineHooksForModule("/data/app/pkg/lib/arm64/libnative-lib.so", dependencies) != 1u) {
        return 1;
    }
    if (state.call_count != 1) {
        return 1;
    }
    if (state.last_module_name != "/data/app/pkg/lib/arm64/libnative-lib.so") {
        return 1;
    }
    if (original != state.original_value || hook_handle != state.hook_handle_value) {
        return 1;
    }
    if (GetInstalledPendingInlineHookCountForTesting() != 1u) {
        return 1;
    }
    if (GetPendingInlineHookCountForTesting() != 0u) {
        return 1;
    }
    return 0;
}

int ExpectFailedInstallRemainsPending() {
    ResetPendingInlineHookRegistryForTesting();

    void* original = nullptr;
    void* hook_handle = nullptr;
    PendingInlineHookRequest request = {};
    request.module_name = "libnative-lib.so";
    request.symbol_name = "target_symbol";
    request.replacement = reinterpret_cast<void*>(0x4444);
    request.original = &original;
    request.hook_handle = &hook_handle;
    if (!RegisterPendingInlineHook(request)) {
        return 1;
    }

    FakeInstallerState state;
    state.fail_count = 1;
    PendingInlineHookInstallerDependencies dependencies = {};
    dependencies.install_symbol_hook = &FakeInstallSymbolHook;
    dependencies.context = &state;

    if (TryInstallPendingInlineHooksForModule("libnative-lib.so", dependencies) != 0u) {
        return 1;
    }
    if (state.call_count != 1) {
        return 1;
    }
    if (GetPendingInlineHookCountForTesting() != 1u) {
        return 1;
    }
    if (GetInstalledPendingInlineHookCountForTesting() != 0u) {
        return 1;
    }

    if (TryInstallPendingInlineHooksForModule("libnative-lib.so", dependencies) != 1u) {
        return 1;
    }
    if (state.call_count != 2) {
        return 1;
    }
    if (GetPendingInlineHookCountForTesting() != 0u) {
        return 1;
    }
    if (GetInstalledPendingInlineHookCountForTesting() != 1u) {
        return 1;
    }
    return 0;
}

int ExpectInstalledHookNotReinstalled() {
    ResetPendingInlineHookRegistryForTesting();

    void* original = nullptr;
    void* hook_handle = nullptr;
    PendingInlineHookRequest request = {};
    request.module_name = "libnative-lib.so";
    request.symbol_name = "target_symbol";
    request.replacement = reinterpret_cast<void*>(0x5555);
    request.original = &original;
    request.hook_handle = &hook_handle;
    if (!RegisterPendingInlineHook(request)) {
        return 1;
    }

    FakeInstallerState state;
    PendingInlineHookInstallerDependencies dependencies = {};
    dependencies.install_symbol_hook = &FakeInstallSymbolHook;
    dependencies.context = &state;

    if (TryInstallPendingInlineHooksForModule("libnative-lib.so", dependencies) != 1u) {
        return 1;
    }
    if (TryInstallPendingInlineHooksForModule("libnative-lib.so", dependencies) != 0u) {
        return 1;
    }
    if (state.call_count != 1) {
        return 1;
    }
    return 0;
}

int ExpectLoadedModuleSweepInstallsMatchingHook() {
    ResetPendingInlineHookRegistryForTesting();

    void* original = nullptr;
    void* hook_handle = nullptr;
    PendingInlineHookRequest request = {};
    request.module_name = "libnative-lib.so";
    request.symbol_name = "target_symbol";
    request.replacement = reinterpret_cast<void*>(0x6666);
    request.original = &original;
    request.hook_handle = &hook_handle;
    if (!RegisterPendingInlineHook(request)) {
        return 1;
    }

    const char* module_paths[] = {
            "/system/lib64/libc.so",
            "/data/app/pkg/lib/arm64/libnative-lib.so",
            "/system/lib64/libm.so"};

    FakeInstallerState state;
    PendingInlineHookInstallerDependencies dependencies = {};
    dependencies.install_symbol_hook = &FakeInstallSymbolHook;
    dependencies.context = &state;

    if (TryInstallPendingInlineHooksForModules(module_paths, 3u, dependencies) != 1u) {
        return 1;
    }
    if (state.call_count != 1) {
        return 1;
    }
    if (state.last_module_name != "/data/app/pkg/lib/arm64/libnative-lib.so") {
        return 1;
    }
    if (original != state.original_value || hook_handle != state.hook_handle_value) {
        return 1;
    }
    if (GetPendingInlineHookCountForTesting() != 0u ||
        GetInstalledPendingInlineHookCountForTesting() != 1u) {
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (ExpectInvalidRequestRejected() != 0) {
        return 1;
    }
    if (ExpectMatchingModuleInstallsPendingHook() != 0) {
        return 1;
    }
    if (ExpectFailedInstallRemainsPending() != 0) {
        return 1;
    }
    if (ExpectInstalledHookNotReinstalled() != 0) {
        return 1;
    }
    if (ExpectLoadedModuleSweepInstallsMatchingHook() != 0) {
        return 1;
    }
    return 0;
}
