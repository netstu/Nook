#ifndef JAVAHOOK_JVM_H
#define JAVAHOOK_JVM_H

#include <jni.h>
#include <string>

namespace tool {
    // 分配可执行内存
    void* allocate_exec_mem(size_t size);
    // 释放可执行内存
    bool free_exec_mem(void* addr, size_t size);
}

class JavaEnv {
public:
    JavaEnv();
    ~JavaEnv();

    JNIEnv* get() const;
    JNIEnv* operator->() const;
    JavaVM* getJVM() const;
    bool isNull() const;
    static JavaVM* GetJavaVM();

    // 设置全局 JavaVM（从 JNI_OnLoad 调用）
    static void SetJavaVM(JavaVM* vm);

private:
    JNIEnv* env = nullptr;
    JavaVM* javaVm = nullptr;
    bool attached = false;
    bool acquired_from_cache = false;

    static JavaVM* getJavaVMInternal();
    static JavaVM* g_globalJavaVM;  // 全局 JavaVM 指针
};

#endif // JAVAHOOK_JVM_H
