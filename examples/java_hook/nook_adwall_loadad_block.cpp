#include "nook/NookJavaHook.h"
#include "JVM.h"

#include <android/log.h>
#include <thread>
#include <unistd.h>

#define TAG "NookAdWallHook"

static JavaVM* g_jvm = nullptr;
static bool g_hook_installed = false;
static bool g_initialized = false;
static bool g_init_started = false;

static void log_jstring(JNIEnv* env, const char* label, jobject value) {
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

static bool install_hook_now() {
    if (g_hook_installed) {
        return true;
    }

    int load_ad_hook_id = NookJavaHookHook(
        "com/demo/target/AdWallFragment",
        "loadAd",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        0,
        hook_load_ad);

    if (load_ad_hook_id < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "loadAd hook installation failed: %d", load_ad_hook_id);
        return false;
    }

    int item_count_hook_id = NookJavaHookHook(
        "com/demo/target/AdWallFragment$ContentAdapter",
        "getItemCount",
        "()I",
        0,
        hook_content_adapter_get_item_count);

    if (item_count_hook_id < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "getItemCount hook installation failed: %d", item_count_hook_id);
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO,
                        TAG,
                        "Hooks installed successfully: loadAd=%d getItemCount=%d",
                        load_ad_hook_id,
                        item_count_hook_id);
    g_hook_installed = true;
    return true;
}

static void init_and_install_hook_in_thread() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;

    NookStatus status = NookJavaHookInitialize();
    __android_log_print(ANDROID_LOG_INFO, TAG, "NookJavaHookInitialize status=%d", status);
    if (status != NOOK_STATUS_OK) {
        return;
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (install_hook_now()) {
            return;
        }
        usleep(200000);
    }
}

__attribute__((constructor))
static void on_library_loaded() {
    if (!g_init_started) {
        g_init_started = true;
        std::thread([]() {
            usleep(100000);
            init_and_install_hook_in_thread();
        }).detach();
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_jvm = vm;
    JavaEnv::SetJavaVM(g_jvm);

    if (!g_init_started) {
        g_init_started = true;
        std::thread([]() {
            init_and_install_hook_in_thread();
        }).detach();
    }

    return JNI_VERSION_1_6;
}

__attribute__((destructor))
static void on_library_unloaded() {
    NookJavaHookUnhookAll();
}
