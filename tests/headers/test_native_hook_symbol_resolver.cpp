#include "native_hook/core/native_hook_symbol_resolver.h"

#include <cstdint>

namespace {

struct FakeResolverState {
    void* preferred_handle = reinterpret_cast<void*>(0x1000);
    void* fallback_handle = reinterpret_cast<void*>(0x2000);
    void* preferred_symbol = nullptr;
    void* fallback_symbol = nullptr;
    int preferred_open_calls = 0;
    int preferred_find_calls = 0;
    int preferred_close_calls = 0;
    int fallback_open_calls = 0;
    int fallback_find_calls = 0;
    int fallback_close_calls = 0;
};

void* FakeOpenPreferredModule(const char*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->preferred_open_calls;
    return state->preferred_handle;
}

void* FakeFindPreferredSymbol(void*, const char*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->preferred_find_calls;
    return state->preferred_symbol;
}

void FakeClosePreferredModule(void*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->preferred_close_calls;
}

void* FakeOpenFallbackModule(const char*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->fallback_open_calls;
    return state->fallback_handle;
}

void* FakeFindFallbackSymbol(void*, const char*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->fallback_find_calls;
    return state->fallback_symbol;
}

void FakeCloseFallbackModule(void*, void* context) {
    auto* state = static_cast<FakeResolverState*>(context);
    ++state->fallback_close_calls;
}

int ExpectPreferredResolutionShortCircuitsFallback() {
    FakeResolverState state;
    state.preferred_symbol = reinterpret_cast<void*>(0x3456);

    NookNativeHookInternal::SymbolResolverDependencies deps = {};
    deps.open_preferred_module = &FakeOpenPreferredModule;
    deps.find_preferred_symbol = &FakeFindPreferredSymbol;
    deps.close_preferred_module = &FakeClosePreferredModule;
    deps.open_fallback_module = &FakeOpenFallbackModule;
    deps.find_fallback_symbol = &FakeFindFallbackSymbol;
    deps.close_fallback_module = &FakeCloseFallbackModule;
    deps.context = &state;

    void* resolved = nullptr;
    if (!NookNativeHookInternal::ResolveSymbolAddressWithDependencies(
                "libnative-lib.so",
                "Java_com_demo_target_LoginFragment_verifyPasswordNative",
                &resolved,
                deps)) {
        return 1;
    }

    if (resolved != state.preferred_symbol) {
        return 1;
    }
    if (state.preferred_open_calls != 1 || state.preferred_find_calls != 1 ||
        state.preferred_close_calls != 1) {
        return 1;
    }
    if (state.fallback_open_calls != 0 || state.fallback_find_calls != 0 ||
        state.fallback_close_calls != 0) {
        return 1;
    }
    return 0;
}

int ExpectFallbackResolutionAfterPreferredMiss() {
    FakeResolverState state;
    state.preferred_symbol = nullptr;
    state.fallback_symbol = reinterpret_cast<void*>(0x4567);

    NookNativeHookInternal::SymbolResolverDependencies deps = {};
    deps.open_preferred_module = &FakeOpenPreferredModule;
    deps.find_preferred_symbol = &FakeFindPreferredSymbol;
    deps.close_preferred_module = &FakeClosePreferredModule;
    deps.open_fallback_module = &FakeOpenFallbackModule;
    deps.find_fallback_symbol = &FakeFindFallbackSymbol;
    deps.close_fallback_module = &FakeCloseFallbackModule;
    deps.context = &state;

    void* resolved = nullptr;
    if (!NookNativeHookInternal::ResolveSymbolAddressWithDependencies(
                "libnative-lib.so",
                "Java_com_demo_target_LoginFragment_verifyPasswordNative",
                &resolved,
                deps)) {
        return 1;
    }

    if (resolved != state.fallback_symbol) {
        return 1;
    }
    if (state.preferred_open_calls != 1 || state.preferred_find_calls != 1 ||
        state.preferred_close_calls != 1) {
        return 1;
    }
    if (state.fallback_open_calls != 1 || state.fallback_find_calls != 1 ||
        state.fallback_close_calls != 1) {
        return 1;
    }
    return 0;
}

int ExpectFailureWhenBothStrategiesMiss() {
    FakeResolverState state;

    NookNativeHookInternal::SymbolResolverDependencies deps = {};
    deps.open_preferred_module = &FakeOpenPreferredModule;
    deps.find_preferred_symbol = &FakeFindPreferredSymbol;
    deps.close_preferred_module = &FakeClosePreferredModule;
    deps.open_fallback_module = &FakeOpenFallbackModule;
    deps.find_fallback_symbol = &FakeFindFallbackSymbol;
    deps.close_fallback_module = &FakeCloseFallbackModule;
    deps.context = &state;

    void* resolved = reinterpret_cast<void*>(0x1111);
    if (NookNativeHookInternal::ResolveSymbolAddressWithDependencies(
                "libnative-lib.so",
                "Java_com_demo_target_LoginFragment_verifyPasswordNative",
                &resolved,
                deps)) {
        return 1;
    }

    if (resolved != nullptr) {
        return 1;
    }
    return 0;
}

int ExpectFailureForInvalidArguments() {
    FakeResolverState state;

    NookNativeHookInternal::SymbolResolverDependencies deps = {};
    deps.open_preferred_module = &FakeOpenPreferredModule;
    deps.find_preferred_symbol = &FakeFindPreferredSymbol;
    deps.close_preferred_module = &FakeClosePreferredModule;
    deps.open_fallback_module = &FakeOpenFallbackModule;
    deps.find_fallback_symbol = &FakeFindFallbackSymbol;
    deps.close_fallback_module = &FakeCloseFallbackModule;
    deps.context = &state;

    void* resolved = reinterpret_cast<void*>(0x1111);
    if (NookNativeHookInternal::ResolveSymbolAddressWithDependencies(nullptr,
                                                                     "symbol",
                                                                     &resolved,
                                                                     deps)) {
        return 1;
    }
    if (resolved != nullptr) {
        return 1;
    }
    return 0;
}

int ExpectResolveAddressInModuleFileForExportedSymbol() {
    void* resolved = nullptr;
    const void* module_base = reinterpret_cast<void*>(0x70000000);
    if (!NookNativeHookInternal::ResolveSymbolAddressInModuleFile(
                "libs/arm64-v8a/libnook_java_hook_example.so",
                module_base,
                "JNI_OnLoad",
                &resolved)) {
        return 1;
    }
    if (resolved == nullptr || reinterpret_cast<uintptr_t>(resolved) <=
                                      reinterpret_cast<uintptr_t>(module_base)) {
        return 1;
    }
    return 0;
}

int ExpectResolveAddressInModuleFileFailsForMissingSymbol() {
    void* resolved = reinterpret_cast<void*>(0x1234);
    if (NookNativeHookInternal::ResolveSymbolAddressInModuleFile(
                "libs/arm64-v8a/libnook_java_hook_example.so",
                reinterpret_cast<void*>(0x70000000),
                "DefinitelyMissingSymbol",
                &resolved)) {
        return 1;
    }
    if (resolved != nullptr) {
        return 1;
    }
    return 0;
}

int ExpectInlineHookSafetyRejectsTooSmallSymbolInModuleFile() {
    const void* module_base = reinterpret_cast<void*>(0x74089c0000ull);
    if (NookNativeHookInternal::IsSymbolInlineHookSafeInModuleFile(
                "libs/arm64-v8a/libnook-agent.so",
                module_base,
                "JS_GetRuntime",
                reinterpret_cast<void*>(0x7408b09cb4ull))) {
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (ExpectPreferredResolutionShortCircuitsFallback() != 0) {
        return 1;
    }
    if (ExpectFallbackResolutionAfterPreferredMiss() != 0) {
        return 1;
    }
    if (ExpectFailureWhenBothStrategiesMiss() != 0) {
        return 1;
    }
    if (ExpectFailureForInvalidArguments() != 0) {
        return 1;
    }
    if (ExpectResolveAddressInModuleFileForExportedSymbol() != 0) {
        return 1;
    }
    if (ExpectResolveAddressInModuleFileFailsForMissingSymbol() != 0) {
        return 1;
    }
    if (ExpectInlineHookSafetyRejectsTooSmallSymbolInModuleFile() != 0) {
        return 1;
    }
    return 0;
}
