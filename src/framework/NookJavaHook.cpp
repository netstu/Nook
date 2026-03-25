#include "nook/NookJavaHook.h"

#include <cstddef>

#if defined(__ANDROID__)
#include "../java_hook/JavaHook.h"
#endif

extern "C" {

NookStatus NookJavaHookInitialize(void) {
#if defined(__ANDROID__)
    return JavaHook::Init() ? NOOK_STATUS_OK : NOOK_STATUS_INTERNAL_ERROR;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookJavaHookIsAvailable(int* available) {
    if (available == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__)
    *available = 1;
#else
    *available = 0;
#endif
    return NOOK_STATUS_OK;
}

int NookJavaHookHook(const char* class_name,
                     const char* method_name,
                     const char* signature,
                     int is_static,
                     NookJavaHookCallback callback) {
#if defined(__ANDROID__)
    if (class_name == nullptr || method_name == nullptr || callback == nullptr) {
        return static_cast<int>(NOOK_STATUS_INVALID_ARGUMENT);
    }

    return JavaHook::HookMethod(
        class_name,
        method_name,
        signature,
        is_static != 0,
        [callback](JNIEnv* env, jobject thiz, HookValue* args, size_t arg_count, HookValue* result) -> bool {
            return callback(
                env,
                thiz,
                reinterpret_cast<NookJavaHookValue*>(args),
                arg_count,
                reinterpret_cast<NookJavaHookValue*>(result)) != 0;
        });
#else
    (void)class_name;
    (void)method_name;
    (void)signature;
    (void)is_static;
    (void)callback;
    return static_cast<int>(NOOK_STATUS_NOT_IMPLEMENTED);
#endif
}

NookStatus NookJavaHookUnhook(int hook_id) {
#if defined(__ANDROID__)
    return JavaHook::Unhook(hook_id) ? NOOK_STATUS_OK : NOOK_STATUS_INTERNAL_ERROR;
#else
    (void)hook_id;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

void NookJavaHookUnhookAll(void) {
#if defined(__ANDROID__)
    JavaHook::UnhookAll();
#endif
}

jclass NookJavaHookFindClass(JNIEnv* env, const char* class_name) {
#if defined(__ANDROID__)
    return JavaHook::FindClass(env, class_name);
#else
    (void)env;
    (void)class_name;
    return nullptr;
#endif
}

}
