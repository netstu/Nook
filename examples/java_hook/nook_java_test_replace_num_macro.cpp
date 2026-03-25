#include "nook/NookJavaHookMacros.h"

#include <android/log.h>

#define TAG "NookJavaTestHook"

NOOK_PAYLOAD_CONFIG("NookJavaTestHook", 10, 200);

NOOK_JAVA_HOOK(HookGetNumFromJavaMethod,
               "cn/n1ng/javatest/JavaHookTest",
               "get_num_from_java_method",
               "()I",
               0) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;

    __android_log_print(ANDROID_LOG_INFO, TAG, "get_num_from_java_method hooked");
    result->i = 999;
    __android_log_print(ANDROID_LOG_INFO, TAG, "replace return value with 999");
    return 0;
}
