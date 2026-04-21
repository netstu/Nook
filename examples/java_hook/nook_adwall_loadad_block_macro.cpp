#include "nook/NookJavaHookMacros.h"

#include <android/log.h>

#define TAG "NookAdWallHook"

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

NOOK_PAYLOAD_CONFIG("NookAdWallHook", 10, 200);

NOOK_JAVA_HOOK(HookLoadAd,
               "com/demo/target/AdWallFragment",
               "loadAd",
               "(Ljava/lang/String;Ljava/lang/String;)V",
               0) {
    (void)result;

    __android_log_print(ANDROID_LOG_INFO, TAG, "loadAd hooked: this=%p arg_count=%zu", thiz, arg_count);

    if (arg_count >= 1) {
        jobject arg0 = NOOK_JAVA_ARG_OBJECT(env, args, 0);
        log_jstring(env, "arg0", arg0);
        if (arg0 != nullptr) {
            env->DeleteLocalRef(arg0);
        }
    }
    if (arg_count >= 2) {
        jobject arg1 = NOOK_JAVA_ARG_OBJECT(env, args, 1);
        log_jstring(env, "arg1", arg1);
        if (arg1 != nullptr) {
            env->DeleteLocalRef(arg1);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, TAG, "block AdWallFragment.loadAd");
    return 0;
}

NOOK_JAVA_HOOK(HookContentAdapterGetItemCount,
               "com/demo/target/AdWallFragment$ContentAdapter",
               "getItemCount",
               "()I",
               0) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;

    result->i = 0;
    __android_log_print(ANDROID_LOG_INFO, TAG, "force ContentAdapter.getItemCount() = 0");
    return 0;
}
