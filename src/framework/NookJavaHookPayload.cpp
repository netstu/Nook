#include "nook/NookJavaHookMacros.h"

#include <atomic>
#include <cstdarg>

#if defined(__ANDROID__)
#include "../java_hook/JVM.h"
#include "NookJavaHookInternal.h"
#include <android/log.h>
#endif

static constexpr unsigned int kMaxDecls = 64;
static const NookJavaHookDecl* g_java_decls[kMaxDecls] = {};
static unsigned int g_java_decl_count = 0;
static std::atomic<bool> g_started{false};

#if defined(__ANDROID__)
static void NookPayloadLog(const char* tag, int priority, const char* format, ...) {
    if (tag == nullptr || format == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    __android_log_vprint(priority, tag, format, args);
    va_end(args);
}
#endif

extern "C" void NookPayloadRegisterJavaHook(const NookJavaHookDecl* decl) {
    if (decl == nullptr) {
        return;
    }

    unsigned int index = __atomic_fetch_add(&g_java_decl_count, 1u, __ATOMIC_RELAXED);
    if (index >= kMaxDecls) {
        return;
    }
    g_java_decls[index] = decl;
}

extern "C" __attribute__((weak)) const NookJavaHookPayloadConfig* NookPayloadGetConfig(void) {
    static const NookJavaHookPayloadConfig config = {"NOOK", 5, 200};
    return &config;
}

extern "C" void NookPayloadSetJavaVM(JavaVM* vm) {
#if defined(__ANDROID__)
    JavaEnv::SetJavaVM(vm);
#else
    (void)vm;
#endif
}

extern "C" __attribute__((weak)) JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    NookPayloadSetJavaVM(vm);
    return JNI_VERSION_1_6;
}

static void NookPayloadRegisterHooks(void) {
    const NookJavaHookPayloadConfig* config = NookPayloadGetConfig();
#if defined(__ANDROID__)
    const char* log_tag = config != nullptr && config->log_tag != nullptr ? config->log_tag : "NOOK";
    const int retry_interval_ms =
        config != nullptr && config->retry_interval_ms > 0 ? config->retry_interval_ms : 200;
    const int requested_retry_count =
        config != nullptr && config->retry_count > 0 ? config->retry_count : 5;
    nook::java_hook_internal::EnsureDeferredInitialize(requested_retry_count, retry_interval_ms);
#endif

    unsigned int total = __atomic_load_n(&g_java_decl_count, __ATOMIC_ACQUIRE);
    if (total > kMaxDecls) {
        total = kMaxDecls;
    }

    for (unsigned int index = 0; index < total; ++index) {
        const NookJavaHookDecl* decl = g_java_decls[index];
        if (decl == nullptr) {
            continue;
        }

        const int request_id = NookJavaHookHookDeferred(
            decl->class_name,
            decl->method_name,
            decl->signature,
            decl->is_static,
            decl->callback);

#if defined(__ANDROID__)
        NookPayloadLog(log_tag,
                       ANDROID_LOG_INFO,
                       "registered deferred hook class=%s method=%s request=%d",
                       decl->class_name != nullptr ? decl->class_name : "<null>",
                       decl->method_name != nullptr ? decl->method_name : "<null>",
                       request_id);
#else
        (void)config;
#endif
    }
}

extern "C" void NookPayloadStart(void) {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }

    NookPayloadRegisterHooks();
}

extern "C" void NookPayloadStop(void) {
    NookJavaHookUnhookAll();
}
