#include "nook/NookComm.h"
#include "agent_runtime/nook_script_runtime_bridge.h"

#include <android/log.h>

namespace {

constexpr const char* kSmokeTag = "NookAgentSmoke";

__attribute__((constructor(210))) void RegisterScriptHandlers() {
    const NookStatus status = nook::agent_runtime::NookScriptRuntimeBridgeInitialize();
    __android_log_print(ANDROID_LOG_INFO,
                        kSmokeTag,
                        "NookScriptRuntimeBridgeInitialize=%d",
                        status);
}

}  // namespace
