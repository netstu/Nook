#include "java_hook_loader_resolver.h"

#include "../JVM.h"

#include <cstdlib>
#include <mutex>

namespace {

std::mutex g_application_class_loader_mutex;
jobject g_application_class_loader = nullptr;
std::mutex g_application_lifecycle_mutex;
bool g_application_lifecycle_ready = false;
bool g_require_application_lifecycle_ready = false;

void ClearException(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void DeleteCachedApplicationClassLoaderGlobalRefLocked(JNIEnv* env) {
    if (env == nullptr || g_application_class_loader == nullptr) {
        return;
    }

    env->DeleteGlobalRef(g_application_class_loader);
    ClearException(env);
    g_application_class_loader = nullptr;
}

jobject GetCachedApplicationClassLoader(JNIEnv* env) {
    if (env == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_application_class_loader_mutex);
    if (g_application_class_loader == nullptr) {
        return nullptr;
    }

    jobject local_ref = env->NewLocalRef(g_application_class_loader);
    if (local_ref == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return nullptr;
    }
    return local_ref;
}

}

namespace JavaHookLoaderResolver {

std::string NormalizeSlashClassName(const char* class_name) {
    if (class_name == nullptr) {
        return {};
    }

    std::string result;
    for (const char* current = class_name; *current != '\0'; ++current) {
        result.push_back(*current == '.' ? '/' : *current);
    }
    return result;
}

std::string NormalizeDotClassName(const char* class_name) {
    if (class_name == nullptr) {
        return {};
    }

    std::string result;
    for (const char* current = class_name; *current != '\0'; ++current) {
        result.push_back(*current == '/' ? '.' : *current);
    }
    return result;
}

jobject GetCurrentApplication(JNIEnv* env) {
    if (env == nullptr) {
        return nullptr;
    }

    jclass activity_thread_class = env->FindClass("android/app/ActivityThread");
    if (activity_thread_class == nullptr) {
        ClearException(env);
        return nullptr;
    }

    jmethodID current_application_method =
        env->GetStaticMethodID(activity_thread_class,
                               "currentApplication",
                               "()Landroid/app/Application;");
    if (current_application_method == nullptr) {
        env->DeleteLocalRef(activity_thread_class);
        ClearException(env);
        return nullptr;
    }

    jobject application =
        env->CallStaticObjectMethod(activity_thread_class, current_application_method);
    env->DeleteLocalRef(activity_thread_class);
    if (application == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return nullptr;
    }

    return application;
}

jobject GetApplicationClassLoader(JNIEnv* env) {
    jobject cached_class_loader = GetCachedApplicationClassLoader(env);
    if (cached_class_loader != nullptr) {
        return cached_class_loader;
    }

    jobject application = GetCurrentApplication(env);
    if (application == nullptr) {
        return nullptr;
    }

    jclass application_class = env->GetObjectClass(application);
    if (application_class == nullptr) {
        env->DeleteLocalRef(application);
        ClearException(env);
        return nullptr;
    }

    jmethodID get_class_loader_method =
        env->GetMethodID(application_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(application_class);
    if (get_class_loader_method == nullptr) {
        env->DeleteLocalRef(application);
        ClearException(env);
        return nullptr;
    }

    jobject class_loader = env->CallObjectMethod(application, get_class_loader_method);
    env->DeleteLocalRef(application);
    if (class_loader == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return nullptr;
    }

    UpdateApplicationClassLoader(env, class_loader);
    return class_loader;
}

bool UpdateApplicationClassLoader(JNIEnv* env, jobject class_loader) {
    if (env == nullptr || class_loader == nullptr) {
        return false;
    }

    jobject global_ref = env->NewGlobalRef(class_loader);
    if (global_ref == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return false;
    }

    jobject previous_global_ref = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_application_class_loader_mutex);
        previous_global_ref = g_application_class_loader;
        g_application_class_loader = global_ref;
    }

    if (previous_global_ref != nullptr) {
        env->DeleteGlobalRef(previous_global_ref);
    }
    return true;
}

bool IsApplicationClassLoaderReady(JNIEnv* env) {
    jobject cached_class_loader = GetCachedApplicationClassLoader(env);
    if (cached_class_loader != nullptr) {
        env->DeleteLocalRef(cached_class_loader);
        return true;
    }

    jobject class_loader = GetApplicationClassLoader(env);
    if (class_loader == nullptr) {
        return false;
    }

    env->DeleteLocalRef(class_loader);
    return true;
}

bool IsCurrentApplicationReady(JNIEnv* env) {
    bool require_lifecycle_ready = false;
    {
        std::lock_guard<std::mutex> lock(g_application_lifecycle_mutex);
        require_lifecycle_ready = g_require_application_lifecycle_ready;
    }
    if (require_lifecycle_ready) {
        return IsApplicationLifecycleReady(env);
    }

    jobject application = GetCurrentApplication(env);
    if (application == nullptr) {
        return false;
    }

    env->DeleteLocalRef(application);
    return true;
}

void MarkApplicationLifecycleReady(JNIEnv* env, jobject application) {
    if (env == nullptr || application == nullptr) {
        return;
    }

    jobject class_loader = nullptr;
    jclass application_class = env->GetObjectClass(application);
    if (application_class != nullptr && !env->ExceptionCheck()) {
        jmethodID get_class_loader_method =
            env->GetMethodID(application_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (get_class_loader_method != nullptr && !env->ExceptionCheck()) {
            class_loader = env->CallObjectMethod(application, get_class_loader_method);
            if (class_loader == nullptr || env->ExceptionCheck()) {
                ClearException(env);
                class_loader = nullptr;
            }
        } else {
            ClearException(env);
        }
        env->DeleteLocalRef(application_class);
    } else {
        ClearException(env);
    }

    if (class_loader != nullptr) {
        UpdateApplicationClassLoader(env, class_loader);
        env->DeleteLocalRef(class_loader);
    }

    std::lock_guard<std::mutex> lock(g_application_lifecycle_mutex);
    g_application_lifecycle_ready = true;
}

bool IsApplicationLifecycleReady(JNIEnv* env) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_application_lifecycle_mutex);
    return g_application_lifecycle_ready;
}

void SetRequireApplicationLifecycleReady(bool required) {
    std::lock_guard<std::mutex> lock(g_application_lifecycle_mutex);
    g_require_application_lifecycle_ready = required;
}

void ResetInheritedApplicationLoaderState() {
    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    {
        std::lock_guard<std::mutex> lock(g_application_class_loader_mutex);
        DeleteCachedApplicationClassLoaderGlobalRefLocked(env);
    }
    {
        std::lock_guard<std::mutex> lock(g_application_lifecycle_mutex);
        g_application_lifecycle_ready = false;
        g_require_application_lifecycle_ready = false;
    }
}

jclass FindLoadedClassWithLoader(JNIEnv* env, jobject class_loader, const char* class_name) {
    if (env == nullptr || class_loader == nullptr || class_name == nullptr) {
        return nullptr;
    }

    jclass class_loader_class = env->GetObjectClass(class_loader);
    if (class_loader_class == nullptr) {
        ClearException(env);
        return nullptr;
    }

    jmethodID find_loaded_class_method =
        env->GetMethodID(class_loader_class, "findLoadedClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (find_loaded_class_method == nullptr) {
        env->DeleteLocalRef(class_loader_class);
        ClearException(env);
        return nullptr;
    }

    const std::string dot_name = NormalizeDotClassName(class_name);
    jstring class_name_string = env->NewStringUTF(dot_name.c_str());
    if (class_name_string == nullptr) {
        env->DeleteLocalRef(class_loader_class);
        ClearException(env);
        return nullptr;
    }

    jobject loaded_class = env->CallObjectMethod(class_loader, find_loaded_class_method, class_name_string);
    env->DeleteLocalRef(class_name_string);
    env->DeleteLocalRef(class_loader_class);
    if (loaded_class == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return nullptr;
    }

    return reinterpret_cast<jclass>(loaded_class);
}

jclass LoadClassWithLoader(JNIEnv* env, jobject class_loader, const char* class_name) {
    if (env == nullptr || class_loader == nullptr || class_name == nullptr) {
        return nullptr;
    }

    jclass class_loader_class = env->GetObjectClass(class_loader);
    if (class_loader_class == nullptr) {
        ClearException(env);
        return nullptr;
    }

    jmethodID load_class_method =
        env->GetMethodID(class_loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (load_class_method == nullptr) {
        env->DeleteLocalRef(class_loader_class);
        ClearException(env);
        return nullptr;
    }

    const std::string dot_name = NormalizeDotClassName(class_name);
    jstring class_name_string = env->NewStringUTF(dot_name.c_str());
    if (class_name_string == nullptr) {
        env->DeleteLocalRef(class_loader_class);
        ClearException(env);
        return nullptr;
    }

    jobject loaded_class = env->CallObjectMethod(class_loader, load_class_method, class_name_string);
    env->DeleteLocalRef(class_name_string);
    env->DeleteLocalRef(class_loader_class);
    if (loaded_class == nullptr || env->ExceptionCheck()) {
        ClearException(env);
        return nullptr;
    }

    return reinterpret_cast<jclass>(loaded_class);
}

}
