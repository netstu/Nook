// C API 实现 - 连接公共 C 接口和内部 C++ 实现
#include "../../JavaHook.h"  // 公共 API - 定义 JavaHookCallback, JavaHookValue
#include "JavaHook.h"         // 内部 API - 定义 JavaHook 类
#include <functional>
#include <android/log.h>

extern "C" {

// 转换函数：将 C 回调转换为 C++ 回调
int JavaHook_Hook(const char* className, const char* methodName,
                  const char* signature, int isStatic,
                  JavaHookCallback callback) {
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "→ JavaHook_Hook() called: %s.%s", className, methodName);

    // 将 C 函数指针转换为 C++ std::function
    int result = JavaHook::HookMethod(className, methodName, signature,
                                isStatic != 0,
                                [callback](JNIEnv* env, jobject thiz,
                                          HookValue* args, size_t argCount,
                                          HookValue* pRet) -> bool {
        // HookValue 和 JavaHookValue 应该是相同的类型
        return callback(env, thiz,
                        reinterpret_cast<JavaHookValue*>(args),
                        argCount,
                        reinterpret_cast<JavaHookValue*>(pRet));
    });

    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "← JavaHook_Hook() result: %d", result);
    return result;
}

int JavaHook_Initialize(void) {
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "→ JavaHook_Initialize() ENTRY");

    bool result = JavaHook::Init();

    if (result) {
        __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓ JavaHook::Init() SUCCESS");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ JavaHook::Init() FAILED");
    }

    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "← JavaHook_Initialize() EXIT result=%d", result ? 0 : -1);
    return result ? 0 : -1;
}

int JavaHook_Unhook(int hookId) {
    return JavaHook::Unhook(hookId) ? 0 : -1;
}

void JavaHook_UnhookAll(void) {
    JavaHook::UnhookAll();
}

jclass JavaHook_FindClass(JNIEnv* env, const char* className) {
    return JavaHook::FindClass(env, className);
}

}  // extern "C" - 重要：保持这个闭合
