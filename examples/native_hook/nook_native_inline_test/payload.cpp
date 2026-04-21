#include "target_replacement.h"

#include "../common/nook_runtime_loader.h"

#include <android/log.h>

namespace {

constexpr const char* kTag = "NookInlineTest";

__attribute__((constructor)) void InitializeInlineHookPayload(void) {
    NookExampleRuntimeLoader::NookInlineApi api = {};
    if (!NookExampleRuntimeLoader::ResolveNookInlineApi(kTag, &api)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "ResolveNookInlineApi failed");
        return;
    }

    const NookStatus status = api.initialize();
    __android_log_print(
            ANDROID_LOG_INFO, kTag, "payload init inline status=%d", static_cast<int>(status));
    RunNookInlineSelfTest();
}

}  // namespace
