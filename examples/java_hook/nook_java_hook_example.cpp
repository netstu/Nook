#include "nook/NookJavaHook.h"
#include "JVM.h"

#include <android/log.h>
#include <mutex>

#define TAG "NookJavaHookExample"

static constexpr int kDeferredRequestIdBase = 0x40000000;

static std::once_flag g_register_once;

static int hook_get_num_from_java_method(JNIEnv* env,
                                         jobject thiz,
                                         NookJavaHookValue* args,
                                         size_t arg_count,
                                         NookJavaHookValue* result) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "Hook triggered for get_num_from_java_method");
    __android_log_print(ANDROID_LOG_INFO, TAG, "this=%p arg_count=%zu", thiz, arg_count);

    for (size_t index = 0; index < arg_count; ++index) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "arg[%zu]=%lld", index, args[index].i);
    }

    (void)env;
    result->i = 999;
    return 0;
}

static void register_hook_once() {
    const int hook_id = NookJavaHookHookDeferred(
        "cn/n1ng/hooktest/JavaHookTest",
        "get_num_from_java_method",
        "()I",
        0,
        hook_get_num_from_java_method);

    if (hook_id >= 0) {
        if (hook_id >= kDeferredRequestIdBase) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Hook deferred pending request registered: %d", hook_id);
        } else {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Hook installed successfully: %d", hook_id);
        }
        return;
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG, "Hook installation failed: %d", hook_id);
}

__attribute__((constructor))
static void on_library_loaded() {
    std::call_once(g_register_once, register_hook_once);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JavaEnv::SetJavaVM(vm);
    std::call_once(g_register_once, register_hook_once);

    return JNI_VERSION_1_6;
}

__attribute__((destructor))
static void on_library_unloaded() {
    NookJavaHookUnhookAll();
}
