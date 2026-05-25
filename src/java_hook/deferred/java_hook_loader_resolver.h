#pragma once

#include <jni.h>
#include <string>

namespace JavaHookLoaderResolver {

std::string NormalizeSlashClassName(const char* class_name);
std::string NormalizeDotClassName(const char* class_name);

jobject GetCurrentApplication(JNIEnv* env);
jobject GetApplicationClassLoader(JNIEnv* env);
bool UpdateApplicationClassLoader(JNIEnv* env, jobject class_loader);
bool IsApplicationClassLoaderReady(JNIEnv* env);
bool IsCurrentApplicationReady(JNIEnv* env);
void MarkApplicationLifecycleReady(JNIEnv* env, jobject application);
bool IsApplicationLifecycleReady(JNIEnv* env);
void SetRequireApplicationLifecycleReady(bool required);
void ResetInheritedApplicationLoaderState();
jclass FindLoadedClassWithLoader(JNIEnv* env, jobject class_loader, const char* class_name);
jclass LoadClassWithLoader(JNIEnv* env, jobject class_loader, const char* class_name);

}
