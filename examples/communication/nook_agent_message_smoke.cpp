#include "nook/NookComm.h"

#include <android/log.h>

namespace {

constexpr const char* kSmokeTag = "NookAgentSmoke";

__attribute__((constructor(210))) void SendSmokeMessage() {
    const char* json = "{\"type\":\"send\",\"payload\":\"hello-from-agent\"}";
    const NookStatus status = NookCommSendMessage(json, nullptr, 0);
    __android_log_print(ANDROID_LOG_INFO, kSmokeTag, "NookCommSendMessage status=%d", status);
}

}  // namespace
