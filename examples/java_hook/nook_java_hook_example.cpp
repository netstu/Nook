#include "nook/NookJavaHook.h"
#include "JVM.h"

#include <android/log.h>
#include <thread>
#include <unistd.h>

#define TAG "NookJavaHookExample"

static JavaVM* g_jvm = nullptr;
static bool g_hook_installed = false;
static bool g_initialized = false;
static bool g_init_started = false;

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

static bool install_hook_now() {
    if (g_hook_installed) {
        return true;
    }

    int hook_id = NookJavaHookHook(
        "cn/n1ng/hooktest/JavaHookTest",
        "get_num_from_java_method",
        "()I",
        0,
        hook_get_num_from_java_method);

    if (hook_id >= 0) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Hook installed successfully: %d", hook_id);
        g_hook_installed = true;
        return true;
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG, "Hook installation failed: %d", hook_id);
    return false;
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

    for (int attempt = 0; attempt < 5; ++attempt) {
        if (install_hook_now()) {
            return;
        }
        sleep(1);
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
