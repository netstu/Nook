#pragma once

#include "nook/NookJavaHook.h"
#include <string>

namespace nook::java_hook_internal {

bool IsInitialized();

void EnsureDeferredInitialize(int retry_count, int retry_interval_ms);

int InstallNow(const char* class_name,
               const char* method_name,
               const char* signature,
               int is_static,
               NookJavaHookCallback callback);
int InstallNow(const char* class_name,
               jobject loader,
               const char* method_name,
               const char* signature,
               int is_static,
               NookJavaHookCallback callback);

bool CallOriginalNow(int installed_hook_id,
                     JNIEnv* env,
                     jobject thiz,
                     NookJavaHookValue* args,
                     size_t arg_count,
                     NookJavaHookValue* result);

bool ResolveInstalledHookId(int request_id, int* installed_hook_id);
bool ResolveInstalledHookSignature(int request_id, std::string* signature);

void ProcessPendingRequests(const char* class_name);

}
