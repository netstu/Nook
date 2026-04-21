#include "target_replacement.h"

#include "../common/nook_runtime_loader.h"

#include <android/log.h>

namespace {

constexpr const char* kTag = "NookInlineTest";

using InlineTargetFunction = int (*)(int);

InlineTargetFunction g_original_inline_target = nullptr;
void* g_inline_hook_handle = nullptr;

extern "C" __attribute__((noinline)) int NookInlineTargetFunction(int value) {
    return value + 1;
}

int CallInlineTarget(int value) {
    volatile InlineTargetFunction function = &NookInlineTargetFunction;
    return function(value);
}

extern "C" int NookInlineReplacementFunction(int value) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "replacement called with value=%d", value);
    if (g_original_inline_target == nullptr) {
        return -1000;
    }

    return g_original_inline_target(value) + 100;
}

}  // namespace

void RunNookInlineSelfTest(void) {
    NookExampleRuntimeLoader::NookInlineApi api = {};
    if (!NookExampleRuntimeLoader::ResolveNookInlineApi(kTag, &api)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "ResolveNookInlineApi failed");
        return;
    }

    const int before_hook = CallInlineTarget(1);
    __android_log_print(ANDROID_LOG_INFO, kTag, "before hook result=%d", before_hook);

    void* original = nullptr;
    const NookStatus hook_status = api.hook_address(reinterpret_cast<void*>(&NookInlineTargetFunction),
                                                    reinterpret_cast<void*>(&NookInlineReplacementFunction),
                                                    &original,
                                                    &g_inline_hook_handle);

    __android_log_print(ANDROID_LOG_INFO,
                        kTag,
                        "inline hook status=%d original=%p handle=%p",
                        static_cast<int>(hook_status),
                        original,
                        g_inline_hook_handle);

    g_original_inline_target = reinterpret_cast<InlineTargetFunction>(original);

    const int after_hook = CallInlineTarget(1);
    __android_log_print(ANDROID_LOG_INFO, kTag, "after hook result=%d", after_hook);

    const NookStatus unhook_status = api.unhook(g_inline_hook_handle);
    __android_log_print(ANDROID_LOG_INFO, kTag, "inline unhook status=%d", static_cast<int>(unhook_status));

    g_inline_hook_handle = nullptr;
    g_original_inline_target = nullptr;

    const int after_unhook = CallInlineTarget(1);
    __android_log_print(ANDROID_LOG_INFO, kTag, "after unhook result=%d", after_unhook);
}
