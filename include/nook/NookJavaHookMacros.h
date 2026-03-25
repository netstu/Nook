#pragma once

#include "nook/NookJavaHook.h"

#if !defined(JNIEXPORT)
#define JNIEXPORT
#endif

#if !defined(JNICALL)
#define JNICALL
#endif

#if !defined(JNI_VERSION_1_6)
#define JNI_VERSION_1_6 0x00010006
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NookJavaHookDecl {
    const char* class_name;
    const char* method_name;
    const char* signature;
    int is_static;
    NookJavaHookCallback callback;
} NookJavaHookDecl;

typedef struct NookJavaHookPayloadConfig {
    const char* log_tag;
    int retry_count;
    int retry_interval_ms;
} NookJavaHookPayloadConfig;

static inline jobject NookJavaThisObject(JNIEnv* env, jobject thiz) {
    if (env == nullptr || thiz == nullptr) {
        return nullptr;
    }
#if defined(__has_include)
#if __has_include(<jni.h>)
    return env->NewLocalRef(thiz);
#else
    return thiz;
#endif
#else
    return thiz;
#endif
}

static inline jobject NookJavaArgObject(JNIEnv* env, NookJavaHookValue* args, size_t index) {
    if (env == nullptr || args == nullptr) {
        return nullptr;
    }
#if defined(__has_include)
#if __has_include(<jni.h>)
    return env->NewLocalRef(reinterpret_cast<jobject>(args[index].l));
#else
    return reinterpret_cast<jobject>(args[index].l);
#endif
#else
    return reinterpret_cast<jobject>(args[index].l);
#endif
}

void NookPayloadRegisterJavaHook(const NookJavaHookDecl* decl);
const NookJavaHookPayloadConfig* NookPayloadGetConfig(void);
void NookPayloadStart(void);
void NookPayloadSetJavaVM(JavaVM* vm);
void NookPayloadStop(void);

#define NOOK_USED __attribute__((used))
#define NOOK_CTOR(priority) __attribute__((constructor(priority)))
#define NOOK_DTOR(priority) __attribute__((destructor(priority)))
#define NOOK_JOIN2(a, b) a##b
#define NOOK_JOIN(a, b) NOOK_JOIN2(a, b)

#define NOOK_PAYLOAD_CONFIG(tag, retry_count, retry_interval_ms)                          \
    extern "C" const NookJavaHookPayloadConfig* NookPayloadGetConfig(void) {              \
        static const NookJavaHookPayloadConfig config = {(tag), (retry_count), (retry_interval_ms)}; \
        return &config;                                                                   \
    }                                                                                     \
    static void NOOK_USED NOOK_CTOR(200) NookPayloadAutoStart(void) {                     \
        NookPayloadStart();                                                               \
    }                                                                                     \
    extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {            \
        (void)reserved;                                                                   \
        NookPayloadSetJavaVM(vm);                                                         \
        NookPayloadStart();                                                               \
        return JNI_VERSION_1_6;                                                           \
    }                                                                                     \
    static void NOOK_USED NOOK_DTOR(200) NookPayloadAutoStop(void) {                      \
        NookPayloadStop();                                                                \
    }

#define NOOK_JAVA_HOOK(name, class_name, method_name, signature, is_static)               \
    static int name(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result); \
    static const NookJavaHookDecl NOOK_USED NOOK_JOIN(nook_java_decl_, __LINE__) = {      \
        (class_name), (method_name), (signature), (is_static), name                       \
    };                                                                                     \
    static void NOOK_USED NOOK_CTOR(101) NOOK_JOIN(nook_java_reg_, __LINE__)(void) {      \
        NookPayloadRegisterJavaHook(&NOOK_JOIN(nook_java_decl_, __LINE__));               \
    }                                                                                      \
    static int name(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result)

#define NOOK_JAVA_BLOCK(class_name, method_name, signature, is_static)                     \
    NOOK_JAVA_BLOCK_IMPL(__LINE__, class_name, method_name, signature, is_static)

#define NOOK_JAVA_BLOCK_IMPL(line, class_name, method_name, signature, is_static)         \
    NOOK_JAVA_HOOK(NOOK_JOIN(nook_java_block_, line), class_name, method_name, signature, is_static) { \
        (void)env;                                                                         \
        (void)thiz;                                                                        \
        (void)args;                                                                        \
        (void)arg_count;                                                                   \
        (void)result;                                                                      \
        return 0;                                                                          \
    }

#define NOOK_JAVA_REPLACE_BOOL(class_name, method_name, signature, is_static, value)      \
    NOOK_JAVA_REPLACE_BOOL_IMPL(__LINE__, class_name, method_name, signature, is_static, value)

#define NOOK_JAVA_REPLACE_BOOL_IMPL(line, class_name, method_name, signature, is_static, value) \
    NOOK_JAVA_HOOK(NOOK_JOIN(nook_java_rpl_bool_, line), class_name, method_name, signature, is_static) { \
        (void)env;                                                                         \
        (void)thiz;                                                                        \
        (void)args;                                                                        \
        (void)arg_count;                                                                   \
        result->z = (value) ? 1 : 0;                                                       \
        return 0;                                                                          \
    }

#define NOOK_JAVA_REPLACE_INT(class_name, method_name, signature, is_static, value)       \
    NOOK_JAVA_REPLACE_INT_IMPL(__LINE__, class_name, method_name, signature, is_static, value)

#define NOOK_JAVA_REPLACE_INT_IMPL(line, class_name, method_name, signature, is_static, value) \
    NOOK_JAVA_HOOK(NOOK_JOIN(nook_java_rpl_int_, line), class_name, method_name, signature, is_static) { \
        (void)env;                                                                         \
        (void)thiz;                                                                        \
        (void)args;                                                                        \
        (void)arg_count;                                                                   \
        result->i = static_cast<long long>(value);                                         \
        return 0;                                                                          \
    }

#define NOOK_JAVA_REPLACE_LONG(class_name, method_name, signature, is_static, value)      \
    NOOK_JAVA_REPLACE_LONG_IMPL(__LINE__, class_name, method_name, signature, is_static, value)

#define NOOK_JAVA_REPLACE_LONG_IMPL(line, class_name, method_name, signature, is_static, value) \
    NOOK_JAVA_HOOK(NOOK_JOIN(nook_java_rpl_long_, line), class_name, method_name, signature, is_static) { \
        (void)env;                                                                         \
        (void)thiz;                                                                        \
        (void)args;                                                                        \
        (void)arg_count;                                                                   \
        result->j = static_cast<long long>(value);                                         \
        return 0;                                                                          \
    }

#define NOOK_JAVA_THIS_OBJECT(env, thiz) NookJavaThisObject((env), (thiz))
#define NOOK_JAVA_ARG_OBJECT(env, args, index) NookJavaArgObject((env), (args), (index))

#ifdef __cplusplus
}
#endif
