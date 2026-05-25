#include "nook/NookJavaHook.h"
#include "JVM.h"

#include <android/log.h>
#include <mutex>

#define TAG "NookAdWallHook"

static constexpr int kDeferredRequestIdBase = 0x40000000;
static std::once_flag g_register_once;

static void log_jstring(JNIEnv* env, const char* label, jobject value) {
    if (env == nullptr) {
        return;
    }

    if (value == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "%s=null", label);
        return;
    }

    const char* chars = env->GetStringUTFChars(reinterpret_cast<jstring>(value), nullptr);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s=%s", label, chars != nullptr ? chars : "<null>");
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(reinterpret_cast<jstring>(value), chars);
    }
}

static int hook_load_ad(JNIEnv* env,
                        jobject thiz,
                        NookJavaHookValue* args,
                        size_t arg_count,
                        NookJavaHookValue* result) {
    (void)result;

    __android_log_print(ANDROID_LOG_INFO, TAG, "loadAd hooked: this=%p arg_count=%zu", thiz, arg_count);

    if (arg_count >= 1) {
        log_jstring(env, "arg0", reinterpret_cast<jobject>(args[0].l));
    }
    if (arg_count >= 2) {
        log_jstring(env, "arg1", reinterpret_cast<jobject>(args[1].l));
    }

    __android_log_print(ANDROID_LOG_INFO, TAG, "block AdWallFragment.loadAd");
    return 0;
}

static int hook_content_adapter_get_item_count(JNIEnv* env,
                                               jobject thiz,
                                               NookJavaHookValue* args,
                                               size_t arg_count,
                                               NookJavaHookValue* result) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;

    result->i = 0;
    __android_log_print(ANDROID_LOG_INFO, TAG, "force ContentAdapter.getItemCount() = 0");
    return 0;
}

static void log_hook_registration(const char* method_name, int hook_id) {
    if (hook_id >= kDeferredRequestIdBase) {
        __android_log_print(ANDROID_LOG_INFO,
                            TAG,
                            "Deferred hook request registered: %s request=%d",
                            method_name != nullptr ? method_name : "<null>",
                            hook_id);
    } else {
        __android_log_print(ANDROID_LOG_INFO,
                            TAG,
                            "Hook installed immediately: %s hook=%d",
                            method_name != nullptr ? method_name : "<null>",
                            hook_id);
    }
}

static void register_hooks_once() {
    const int load_ad_hook_id = NookJavaHookHookDeferred(
        "com/demo/target/AdWallFragment",
        "loadAd",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        0,
        hook_load_ad);

    if (load_ad_hook_id < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "loadAd hook installation failed: %d", load_ad_hook_id);
    } else {
        log_hook_registration("AdWallFragment.loadAd", load_ad_hook_id);
    }

    const int item_count_hook_id = NookJavaHookHookDeferred(
        "com/demo/target/AdWallFragment$ContentAdapter",
        "getItemCount",
        "()I",
        0,
        hook_content_adapter_get_item_count);

    if (item_count_hook_id < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "getItemCount hook installation failed: %d", item_count_hook_id);
    } else {
        log_hook_registration("AdWallFragment$ContentAdapter.getItemCount", item_count_hook_id);
    }
}

__attribute__((constructor))
static void on_library_loaded() {
    std::call_once(g_register_once, register_hooks_once);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JavaEnv::SetJavaVM(vm);
    std::call_once(g_register_once, register_hooks_once);

    return JNI_VERSION_1_6;
}

__attribute__((destructor))
static void on_library_unloaded() {
    NookJavaHookUnhookAll();
}
