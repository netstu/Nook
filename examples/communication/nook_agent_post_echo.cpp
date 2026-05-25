#include "nook/NookComm.h"

#include <android/log.h>

namespace {

constexpr const char* kSmokeTag = "NookAgentSmoke";

void OnMessage(uint32_t script_id,
               const char* type,
               const char* message_json,
               const uint8_t* data,
               size_t data_len) {
    __android_log_print(ANDROID_LOG_INFO,
                        kSmokeTag,
                        "callback script_id=%u type=%s json=%s data_len=%zu",
                        script_id,
                        type != nullptr ? type : "(null)",
                        message_json != nullptr ? message_json : "(null)",
                        data_len);

    const char* response = "{\"type\":\"send\",\"payload\":\"script-post-received\"}";
    const NookStatus status = NookCommSendMessage(response, data, data_len);
    __android_log_print(ANDROID_LOG_INFO, kSmokeTag, "echo NookCommSendMessage status=%d", status);
}

__attribute__((constructor(210))) void RegisterSmokeCallback() {
    const NookStatus status = NookCommSetMessageCallback(OnMessage);
    __android_log_print(ANDROID_LOG_INFO, kSmokeTag, "NookCommSetMessageCallback status=%d", status);
}

}  // namespace
