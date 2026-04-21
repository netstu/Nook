#include <android/log.h>

namespace {

constexpr char kTag[] = "NookInlineProbe";

__attribute__((constructor)) void OnProbeLoaded() {
    __android_log_print(ANDROID_LOG_INFO, kTag, "inline observer probe loaded");
}

}  // namespace
