#include "nook/Nook.h"
#include "native_hook/core/native_hook_dispatcher.h"

#include <string>

namespace {

struct FakeState {
    bool module_info_result = true;
    void* module_base = reinterpret_cast<void*>(0x1000);
    std::string module_path = "/data/app/libtarget.so";
    bool primary_result = false;
    bool fallback_result = false;
    int module_info_calls = 0;
    int primary_calls = 0;
    int fallback_calls = 0;
};

bool FakeGetModuleInfo(pid_t,
                       const char*,
                       void** module_base,
                       std::string* module_path,
                       void* context) {
    FakeState* state = static_cast<FakeState*>(context);
    ++state->module_info_calls;
    if (!state->module_info_result) {
        return false;
    }
    *module_base = state->module_base;
    *module_path = state->module_path;
    return true;
}

bool FakePrimaryHook(const NookNativeHookInternal::ResolvedHookTarget&, void* context) {
    FakeState* state = static_cast<FakeState*>(context);
    ++state->primary_calls;
    return state->primary_result;
}

bool FakeFallbackHook(const NookNativeHookInternal::ResolvedHookTarget&, void* context) {
    FakeState* state = static_cast<FakeState*>(context);
    ++state->fallback_calls;
    return state->fallback_result;
}

int ExpectPrimaryShortCircuit() {
    FakeState state;
    state.primary_result = true;

    void* original = reinterpret_cast<void*>(0x9999);
    const NookNativeHookInternal::FallbackHookDependencies deps = {
            &FakeGetModuleInfo,
            &FakePrimaryHook,
            &FakeFallbackHook,
            &state};

    const NookStatus status = NookNativeHookInternal::HookSymbolWithFallback(
            "libtarget.so",
            "strcmp",
            reinterpret_cast<void*>(0x1234),
            &original,
            deps);

    if (status != NOOK_STATUS_OK) {
        return 1;
    }
    if (state.module_info_calls != 1 || state.primary_calls != 1 || state.fallback_calls != 0) {
        return 1;
    }
    return 0;
}

int ExpectFallbackAfterPrimaryFailure() {
    FakeState state;
    state.primary_result = false;
    state.fallback_result = true;

    void* original = nullptr;
    const NookNativeHookInternal::FallbackHookDependencies deps = {
            &FakeGetModuleInfo,
            &FakePrimaryHook,
            &FakeFallbackHook,
            &state};

    const NookStatus status = NookNativeHookInternal::HookSymbolWithFallback(
            "libtarget.so",
            "strcmp",
            reinterpret_cast<void*>(0x1234),
            &original,
            deps);

    if (status != NOOK_STATUS_OK) {
        return 1;
    }
    if (state.module_info_calls != 1 || state.primary_calls != 1 || state.fallback_calls != 1) {
        return 1;
    }
    return 0;
}

int ExpectFailureWhenTargetCannotBeResolved() {
    FakeState state;
    state.module_info_result = false;

    void* original = reinterpret_cast<void*>(0x9999);
    const NookNativeHookInternal::FallbackHookDependencies deps = {
            &FakeGetModuleInfo,
            &FakePrimaryHook,
            &FakeFallbackHook,
            &state};

    const NookStatus status = NookNativeHookInternal::HookSymbolWithFallback(
            "libtarget.so",
            "strcmp",
            reinterpret_cast<void*>(0x1234),
            &original,
            deps);

    if (status != NOOK_STATUS_INTERNAL_ERROR) {
        return 1;
    }
    if (state.module_info_calls != 1 || state.primary_calls != 0 || state.fallback_calls != 0) {
        return 1;
    }
    return 0;
}

int ExpectFailureWhenBothStrategiesFail() {
    FakeState state;
    state.primary_result = false;
    state.fallback_result = false;

    void* original = nullptr;
    const NookNativeHookInternal::FallbackHookDependencies deps = {
            &FakeGetModuleInfo,
            &FakePrimaryHook,
            &FakeFallbackHook,
            &state};

    const NookStatus status = NookNativeHookInternal::HookSymbolWithFallback(
            "libtarget.so",
            "strcmp",
            reinterpret_cast<void*>(0x1234),
            &original,
            deps);

    if (status != NOOK_STATUS_INTERNAL_ERROR) {
        return 1;
    }
    if (state.module_info_calls != 1 || state.primary_calls != 1 || state.fallback_calls != 1) {
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (ExpectPrimaryShortCircuit() != 0) {
        return 1;
    }
    if (ExpectFallbackAfterPrimaryFailure() != 0) {
        return 1;
    }
    if (ExpectFailureWhenTargetCannotBeResolved() != 0) {
        return 1;
    }
    if (ExpectFailureWhenBothStrategiesFail() != 0) {
        return 1;
    }
    return 0;
}
