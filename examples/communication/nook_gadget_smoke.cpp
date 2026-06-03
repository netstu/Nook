#include "nook/NookGadget.h"

#include <android/log.h>

namespace {

constexpr const char* kSmokeTag = "NookGadgetSmoke";

__attribute__((constructor(230))) void RunNookGadgetSmoke() {
    const NookStatus status = NookGadgetInitialize();
    __android_log_print(ANDROID_LOG_INFO,
                        kSmokeTag,
                        "NookGadgetInitialize=%d",
                        status);
}

}  // namespace
