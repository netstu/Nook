#include "nook/NookJavaHook.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include "../java_hook/JavaHook.h"
#include "../java_hook/deferred/java_hook_class_observer.h"
#include "../java_hook/deferred/pending_java_hook_registry.h"
#include "NookJavaHookInternal.h"
#endif

#if defined(__ANDROID__)
namespace {

std::atomic<int> g_java_hook_init_state{0};

int NormalizeRetryIntervalMs(int retry_interval_ms) {
    return retry_interval_ms > 0 ? retry_interval_ms : 100;
}

int NormalizeRetryCount(int retry_count) {
    return retry_count > 40 ? retry_count : 40;
}

void DeferredInitializeWorker(int retry_count, int retry_interval_ms) {
    const int interval_ms = NormalizeRetryIntervalMs(retry_interval_ms);
    const int max_attempts = NormalizeRetryCount(retry_count);

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (NookJavaHookInitialize() == NOOK_STATUS_OK) {
            JavaHookClassObserver::EnsureInstalled();
            JavaHookClassObserver::SchedulePendingRetry(nullptr, 0);
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    g_java_hook_init_state.store(0, std::memory_order_release);
}

}  // namespace
#endif

extern "C" {

NookStatus NookJavaHookInitialize(void) {
#if defined(__ANDROID__)
    if (JavaHook::Init()) {
        g_java_hook_init_state.store(2, std::memory_order_release);
        return NOOK_STATUS_OK;
    }
    return NOOK_STATUS_INTERNAL_ERROR;
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
    if (!nook::java_hook_internal::IsInitialized()) {
        return static_cast<int>(NOOK_STATUS_INTERNAL_ERROR);
    }
    return nook::java_hook_internal::InstallNow(
        class_name, nullptr, method_name, signature, is_static, callback);
#else
    (void)class_name;
    (void)method_name;
    (void)signature;
    (void)is_static;
    (void)callback;
    return static_cast<int>(NOOK_STATUS_NOT_IMPLEMENTED);
#endif
}

int NookJavaHookHookWithLoader(JNIEnv* env,
                               jobject loader,
                               const char* class_name,
                               const char* method_name,
                               const char* signature,
                               int is_static,
                               NookJavaHookCallback callback) {
#if defined(__ANDROID__)
    (void)env;
    if (!nook::java_hook_internal::IsInitialized()) {
        return static_cast<int>(NOOK_STATUS_INTERNAL_ERROR);
    }
    return nook::java_hook_internal::InstallNow(
        class_name, loader, method_name, signature, is_static, callback);
#else
    (void)env;
    (void)loader;
    (void)class_name;
    (void)method_name;
    (void)signature;
    (void)is_static;
    (void)callback;
    return static_cast<int>(NOOK_STATUS_NOT_IMPLEMENTED);
#endif
}

int NookJavaHookHookDeferred(const char* class_name,
                             const char* method_name,
                             const char* signature,
                             int is_static,
                             NookJavaHookCallback callback) {
#if defined(__ANDROID__)
    return NookJavaHookHookDeferredWithLoader(
        nullptr, nullptr, class_name, method_name, signature, is_static, callback);
#else
    (void)class_name;
    (void)method_name;
    (void)signature;
    (void)is_static;
    (void)callback;
    return static_cast<int>(NOOK_STATUS_NOT_IMPLEMENTED);
#endif
}

int NookJavaHookHookDeferredWithLoader(JNIEnv* env,
                                       jobject loader,
                                       const char* class_name,
                                       const char* method_name,
                                       const char* signature,
                                       int is_static,
                                       NookJavaHookCallback callback) {
#if defined(__ANDROID__)
    (void)env;
    if (class_name == nullptr || method_name == nullptr || signature == nullptr || callback == nullptr) {
        return static_cast<int>(NOOK_STATUS_INVALID_ARGUMENT);
    }

    if (nook::java_hook_internal::IsInitialized()) {
        const int hook_id = nook::java_hook_internal::InstallNow(
            class_name, loader, method_name, signature, is_static, callback);
        if (hook_id >= 0) {
            return hook_id;
        }
    }

    const int request_id = PendingJavaHookRegistry::Instance().Register(
        class_name,
        method_name,
        signature,
        is_static,
        reinterpret_cast<uint64_t>(loader),
        callback);
    if (request_id < 0) {
        return request_id;
    }

    if (nook::java_hook_internal::IsInitialized()) {
        JavaHookClassObserver::EnsureInstalled();
        JavaHookClassObserver::SchedulePendingRetry(class_name, 0);
    } else {
        nook::java_hook_internal::EnsureDeferredInitialize(40, 100);
    }
    return request_id;
#else
    (void)env;
    (void)loader;
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
    int installed_hook_id = -1;
    if (hook_id >= 0x40000000 && PendingJavaHookRegistry::Instance().RemoveRequest(hook_id, &installed_hook_id)) {
        if (installed_hook_id >= 0) {
            return JavaHook::Unhook(installed_hook_id) ? NOOK_STATUS_OK : NOOK_STATUS_INTERNAL_ERROR;
        }
        return NOOK_STATUS_OK;
    }
    return JavaHook::Unhook(hook_id) ? NOOK_STATUS_OK : NOOK_STATUS_INTERNAL_ERROR;
#else
    (void)hook_id;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

void NookJavaHookUnhookAll(void) {
#if defined(__ANDROID__)
    PendingJavaHookRegistry::Instance().Clear();
    JavaHookClassObserver::Reset();
    JavaHook::UnhookAll();
#endif
}

jclass NookJavaHookFindClass(JNIEnv* env, const char* class_name) {
#if defined(__ANDROID__)
    return JavaHook::FindClassWithLoader(env, nullptr, class_name);
#else
    (void)env;
    (void)class_name;
    return nullptr;
#endif
}

jclass NookJavaHookFindClassWithLoader(JNIEnv* env, jobject loader, const char* class_name) {
#if defined(__ANDROID__)
    return JavaHook::FindClassWithLoader(env, loader, class_name);
#else
    (void)env;
    (void)loader;
    (void)class_name;
    return nullptr;
#endif
}

}

#if defined(__ANDROID__)
namespace nook::java_hook_internal {

bool IsInitialized() {
    return g_java_hook_init_state.load(std::memory_order_acquire) == 2;
}

void EnsureDeferredInitialize(int retry_count, int retry_interval_ms) {
    if (IsInitialized()) {
        return;
    }

    int expected = 0;
    if (!g_java_hook_init_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        return;
    }

    std::thread(DeferredInitializeWorker, retry_count, retry_interval_ms).detach();
}

int InstallNow(const char* class_name,
               jobject loader,
               const char* method_name,
               const char* signature,
               int is_static,
               NookJavaHookCallback callback) {
    if (class_name == nullptr || method_name == nullptr || signature == nullptr || callback == nullptr) {
        return static_cast<int>(NOOK_STATUS_INVALID_ARGUMENT);
    }

    JavaHookClassObserver::ScopedSuppression suppression;
    return JavaHook::HookMethodWithLoader(
        class_name,
        loader,
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
}

int InstallNow(const char* class_name,
               const char* method_name,
               const char* signature,
               int is_static,
               NookJavaHookCallback callback) {
    return InstallNow(class_name, nullptr, method_name, signature, is_static, callback);
}

void ProcessPendingRequests(const char* class_name) {
    const std::vector<int> pending_request_ids =
        PendingJavaHookRegistry::Instance().CollectPendingRequestIds(class_name);
    for (int request_id : pending_request_ids) {
        PendingJavaHookRegistry::Request request = {};
        if (!PendingJavaHookRegistry::Instance().TryBeginInstall(request_id, &request)) {
            continue;
        }

        const int hook_id = InstallNow(request.class_name.c_str(),
                                       reinterpret_cast<jobject>(request.loader_handle),
                                       request.method_name.c_str(),
                                       request.signature.c_str(),
                                       request.is_static,
                                       request.callback);
        if (hook_id >= 0) {
            std::string resolved_signature;
            if (JavaHook::GetHookSignature(hook_id, &resolved_signature) &&
                !resolved_signature.empty()) {
                PendingJavaHookRegistry::Instance().SetResolvedSignature(request_id, resolved_signature);
            }
        }
        PendingJavaHookRegistry::Instance().FinishInstall(request_id, hook_id);
    }
}

bool CallOriginalNow(int installed_hook_id,
                     JNIEnv* env,
                     jobject thiz,
                     NookJavaHookValue* args,
                     size_t arg_count,
                     NookJavaHookValue* result) {
    if (installed_hook_id < 0 || env == nullptr || result == nullptr) {
        return false;
    }
    LOGI("CallOriginalNow: enter installed_hook_id=%d arg_count=%zu", installed_hook_id, arg_count);
    const bool ok = JavaHook::InvokeOriginalMethod(installed_hook_id,
                                                   env,
                                                   thiz,
                                                   reinterpret_cast<HookValue*>(args),
                                                   arg_count,
                                                   reinterpret_cast<HookValue*>(result));
    LOGI("CallOriginalNow: exit installed_hook_id=%d ok=%d", installed_hook_id, ok ? 1 : 0);
    return ok;
}

bool ResolveInstalledHookId(int request_id, int* installed_hook_id) {
    if (installed_hook_id == nullptr) {
        return false;
    }

    if (request_id >= 0 && request_id < 0x40000000) {
        *installed_hook_id = request_id;
        return true;
    }

    return PendingJavaHookRegistry::Instance().TryGetInstalledHookId(request_id, installed_hook_id);
}

bool ResolveInstalledHookSignature(int request_id, std::string* signature) {
    if (signature == nullptr) {
        return false;
    }

    if (request_id >= 0 && request_id < 0x40000000) {
        return JavaHook::GetHookSignature(request_id, signature);
    }

    return PendingJavaHookRegistry::Instance().TryGetResolvedSignature(request_id, signature);
}

}
#endif
