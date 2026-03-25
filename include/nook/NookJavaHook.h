#pragma once

#include "nook/Nook.h"
#include <stddef.h>

#if defined(__has_include)
#if __has_include(<jni.h>)
#include <jni.h>
#else
typedef struct JNIEnv JNIEnv;
typedef struct JavaVM JavaVM;
typedef int jint;
typedef void* jobject;
typedef void* jclass;
#endif
#else
typedef struct JNIEnv JNIEnv;
typedef struct JavaVM JavaVM;
typedef int jint;
typedef void* jobject;
typedef void* jclass;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef union NookJavaHookValue {
    unsigned long long u;
    long long i;
    double d;
    float f;
    int z;
    unsigned char b;
    unsigned short c;
    unsigned short s;
    long long j;
    void* l;
} NookJavaHookValue;

typedef int (*NookJavaHookCallback)(JNIEnv* env,
                                    jobject thiz,
                                    NookJavaHookValue* args,
                                    size_t arg_count,
                                    NookJavaHookValue* result);

NookStatus NookJavaHookInitialize(void);
NookStatus NookJavaHookIsAvailable(int* available);
int NookJavaHookHook(const char* class_name,
                     const char* method_name,
                     const char* signature,
                     int is_static,
                     NookJavaHookCallback callback);
NookStatus NookJavaHookUnhook(int hook_id);
void NookJavaHookUnhookAll(void);
jclass NookJavaHookFindClass(JNIEnv* env, const char* class_name);

#ifdef __cplusplus
}
#endif
