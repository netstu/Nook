#include "verify_password_replacement.h"

#include "../common/nook_runtime_loader.h"

#include <android/log.h>
#include <jni.h>

namespace {

constexpr char kTag[] = "NookVerifyInline";
constexpr char kTargetModule[] = "libnative-lib.so";
constexpr char kTargetSymbol[] = "Java_com_demo_target_LoginFragment_verifyPasswordNative";
void* g_original_verify = nullptr;
void* g_hook_handle = nullptr;

extern "C" jboolean hooked_verify_password(JNIEnv* env, jobject thiz, jstring password) {
    (void)env;
    (void)thiz;
    (void)password;
    __android_log_print(ANDROID_LOG_INFO, kTag, "hooked verifyPasswordNative => JNI_TRUE");
    return NookTestVerifyPasswordAlwaysTrue() ? JNI_TRUE : JNI_FALSE;
}

void register_deferred_hook() {
    NookExampleRuntimeLoader::NookInlineApi api = {};
    if (!NookExampleRuntimeLoader::ResolveNookInlineApi(kTag, &api)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "ResolveNookInlineApi failed");
        return;
    }

    const NookStatus init_status = api.initialize();
    __android_log_print(ANDROID_LOG_INFO, kTag, "NookInlineHookInitialize status=%d", init_status);
    if (init_status != NOOK_STATUS_OK) {
        return;
    }

    const NookStatus hook_status =
            api.hook_symbol_deferred(kTargetModule,
                                     kTargetSymbol,
                                     reinterpret_cast<void*>(hooked_verify_password),
                                     &g_original_verify,
                                     &g_hook_handle);
    __android_log_print(ANDROID_LOG_INFO,
                        kTag,
                        "NookInlineHookSymbolDeferred status=%d original=%p handle=%p",
                        hook_status,
                        g_original_verify,
                        g_hook_handle);
}

}  // namespace

__attribute__((constructor(200))) static void on_library_loaded() {
    register_deferred_hook();
}
