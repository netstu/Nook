#include "agent_runtime/nook_script_runtime_bridge.h"

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/nook_native_js_bridge.h"
#include "agent_runtime/script_registry.h"
#include "framework/NookCommInternal.h"
#include "java_hook/JVM.h"
#include "java_hook/deferred/java_hook_loader_resolver.h"
#include "nook/NookComm.h"

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace nook {
namespace agent_runtime {
namespace {

std::mutex g_bridge_mutex;
ScriptRegistry g_script_registry;
bool g_bridge_installed = false;
std::condition_variable g_native_hook_dispatch_cv;
std::thread g_native_hook_dispatch_thread;
bool g_native_hook_dispatch_requested = false;
bool g_native_hook_dispatch_stop_requested = false;

#if defined(__ANDROID__)
constexpr const char* kBridgeTag = "NookCommApi";
#define NOOK_BRIDGE_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kBridgeTag, __VA_ARGS__))
#define NOOK_BRIDGE_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, kBridgeTag, __VA_ARGS__))
#else
#define NOOK_BRIDGE_LOGI(...) ((void)0)
#define NOOK_BRIDGE_LOGE(...) ((void)0)
#endif

void SignalNativeHookDispatch() {
    {
        std::lock_guard<std::mutex> lock(g_bridge_mutex);
        g_native_hook_dispatch_requested = true;
    }
    g_native_hook_dispatch_cv.notify_one();
}

void NativeHookDispatchThreadMain() {
    std::unique_lock<std::mutex> lock(g_bridge_mutex);
    while (!g_native_hook_dispatch_stop_requested) {
        g_native_hook_dispatch_cv.wait(lock, []() {
            return g_native_hook_dispatch_stop_requested || g_native_hook_dispatch_requested;
        });
        if (g_native_hook_dispatch_stop_requested) {
            break;
        }

        g_native_hook_dispatch_requested = false;
        lock.unlock();

        std::string error_message;
        if (!JsRuntime::DispatchPendingNativeHookEvents(&error_message)) {
            NOOK_BRIDGE_LOGE("dispatch native hook events failed error=%s", error_message.c_str());
        }

        lock.lock();
    }
}

bool WaitForSpawnGateLifecycleReadyAndSync(uint32_t script_id, std::string* error_message) {
    auto sync_java_ready = [&](const char* reason, int attempt) -> bool {
        if (!JsRuntime::DispatchJavaReadyCallbacks(error_message)) {
            NOOK_BRIDGE_LOGE("script load java-ready dispatch failed script_id=%u reason=%s attempt=%d error=%s",
                             script_id,
                             reason,
                             attempt,
                             error_message != nullptr ? error_message->c_str() : "");
            return false;
        }
        if (!JsRuntime::PumpPendingTasks(error_message)) {
            NOOK_BRIDGE_LOGE("script load pending task pump failed script_id=%u reason=%s attempt=%d error=%s",
                             script_id,
                             reason,
                             attempt,
                             error_message != nullptr ? error_message->c_str() : "");
            return false;
        }
        return true;
    };

    constexpr int kMaxAttempts = 20;
    constexpr auto kRetryDelay = std::chrono::milliseconds(50);
    for (int attempt = 0; attempt <= kMaxAttempts; ++attempt) {
        JavaEnv jenv;
        const bool lifecycle_ready =
            !jenv.isNull() &&
            JavaHookLoaderResolver::IsApplicationLifecycleReady(jenv.get());
        if (lifecycle_ready) {
            if (attempt == 0) {
                if (!sync_java_ready("immediate", attempt)) {
                    return false;
                }
                NOOK_BRIDGE_LOGI("script load synchronized java-ready while spawn gate held script_id=%u",
                                 script_id);
            } else {
                if (!sync_java_ready("wait", attempt)) {
                    return false;
                }
                NOOK_BRIDGE_LOGI("script load lifecycle ready after wait while spawn gate held script_id=%u attempt=%d",
                                 script_id,
                                 attempt);
            }
            return true;
        }

        if (attempt == 0) {
            NOOK_BRIDGE_LOGI("script load deferred java-ready while spawn gate held script_id=%u",
                             script_id);
        }
        if (attempt == kMaxAttempts) {
            if (error_message != nullptr) {
                *error_message = "spawn gate lifecycle ready wait timed out";
            }
            NOOK_BRIDGE_LOGE("script load timed out waiting for java-ready while spawn gate held script_id=%u",
                             script_id);
            return false;
        }
        std::this_thread::sleep_for(kRetryDelay);
    }

    if (error_message != nullptr) {
        *error_message = "spawn gate lifecycle ready wait exhausted";
    }
    return false;
}

NookStatus OnCreate(const char* name, const char* source, uint32_t* script_id) {
    std::string error_message;
    NOOK_BRIDGE_LOGI("script create request name=%s source_len=%zu source=%s",
                     name != nullptr ? name : "(null)",
                     source != nullptr ? std::char_traits<char>::length(source) : 0u,
                     source != nullptr ? source : "(null)");
    if (!g_script_registry.CreateScript(name != nullptr ? name : "",
                                        source != nullptr ? source : "",
                                        script_id,
                                        &error_message)) {
        framework::SetPendingScriptCallbackError(error_message);
        NOOK_BRIDGE_LOGE("script create failed name=%s error=%s",
                         name != nullptr ? name : "(null)",
                         error_message.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    NOOK_BRIDGE_LOGI("script create ok name=%s script_id=%u",
                     name != nullptr ? name : "(null)",
                     script_id != nullptr ? *script_id : 0u);
    return NOOK_STATUS_OK;
}

NookStatus OnLoad(uint32_t script_id) {
    std::string error_message;
    if (!g_script_registry.LoadScript(script_id, &error_message)) {
        framework::SetPendingScriptCallbackError(error_message);
        NOOK_BRIDGE_LOGE("script load failed script_id=%u error=%s",
                         script_id,
                         error_message.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    if (framework::IsCurrentProcessStrictLifecycleSpawnGateHeld()) {
        if (!WaitForSpawnGateLifecycleReadyAndSync(script_id, &error_message)) {
            framework::SetPendingScriptCallbackError(error_message);
            NOOK_BRIDGE_LOGE("script load spawn-gate java-ready wait failed script_id=%u error=%s",
                             script_id,
                             error_message.c_str());
            return NOOK_STATUS_INTERNAL_ERROR;
        }
    }

    NOOK_BRIDGE_LOGI("script load ok script_id=%u", script_id);
    return NOOK_STATUS_OK;
}

NookStatus OnUnload(uint32_t script_id) {
    std::string error_message;
    if (!g_script_registry.UnloadScript(script_id, &error_message)) {
        framework::SetPendingScriptCallbackError(error_message);
        NOOK_BRIDGE_LOGE("script unload failed script_id=%u error=%s",
                         script_id,
                         error_message.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    NOOK_BRIDGE_LOGI("script unload ok script_id=%u", script_id);
    return NOOK_STATUS_OK;
}

nook::comm::RpcResponse OnRpc(const nook::comm::RpcRequest& request) {
    nook::comm::RpcResponse response;
    response.script_id = request.script_id;

    std::string result_json;
    std::string error_message;
    if (!JsRuntime::CallRpc(request.script_id,
                            request.method,
                            request.args_json,
                            &result_json,
                            &error_message)) {
        response.success = false;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INTERNAL_ERROR);
        response.error.message = error_message.empty() ? "rpc call failed" : error_message;
        NOOK_BRIDGE_LOGE("rpc call failed script_id=%u method=%s error=%s",
                         request.script_id,
                         request.method.c_str(),
                         response.error.message.c_str());
        return response;
    }

    response.success = true;
    response.result_json = result_json;
    NOOK_BRIDGE_LOGI("rpc call ok script_id=%u method=%s result=%s",
                     request.script_id,
                     request.method.c_str(),
                     response.result_json.c_str());
    return response;
}

bool SendToHost(const std::string& json, const std::vector<uint8_t>& data) {
    const uint8_t* payload = data.empty() ? nullptr : data.data();
    return NookCommSendMessage(json.c_str(), payload, data.size()) == NOOK_STATUS_OK;
}

void OnMessage(uint32_t script_id,
               const char* type,
               const char* message_json,
               const uint8_t* data,
               size_t data_len) {
    if (type == nullptr || std::string(type) != "script-post") {
        return;
    }

    std::vector<uint8_t> payload;
    if (data != nullptr && data_len > 0) {
        payload.assign(data, data + data_len);
    }

    std::string error_message;
    if (!JsRuntime::DispatchMessage(script_id,
                                    message_json != nullptr ? message_json : "{}",
                                    payload,
                                    &error_message)) {
        NOOK_BRIDGE_LOGE("dispatch script post failed script_id=%u error=%s",
                         script_id,
                         error_message.c_str());
    }
}

#if defined(__ANDROID__) && !defined(_WIN32)
__attribute__((constructor(210))) static void AutoInitializeBridge() {
    // NookComm owns eager agent initialization so zygote-loaded agents can
    // defer child activation until after specialization.
}
#endif

}  // namespace

NookStatus NookScriptRuntimeBridgeInitialize() {
    std::lock_guard<std::mutex> lock(g_bridge_mutex);
    if (g_bridge_installed) {
        return NOOK_STATUS_OK;
    }

    NOOK_BRIDGE_LOGI("bridge init: set send callback");
    JsRuntime::SetSendCallback(SendToHost);
    NOOK_BRIDGE_LOGI("bridge init: set send callback ok");

    NOOK_BRIDGE_LOGI("bridge init: register message callback");
    const NookStatus message_status = NookCommSetMessageCallback(OnMessage);
    if (message_status != NOOK_STATUS_OK) {
        NOOK_BRIDGE_LOGE("register message callback failed status=%d", message_status);
        return message_status;
    }
    NOOK_BRIDGE_LOGI("bridge init: register message callback ok");

    NOOK_BRIDGE_LOGI("bridge init: register create callback");
    const NookStatus create_status = NookCommSetScriptCreateCallback(OnCreate);
    if (create_status != NOOK_STATUS_OK) {
        NOOK_BRIDGE_LOGE("register script create callback failed status=%d", create_status);
        return create_status;
    }
    NOOK_BRIDGE_LOGI("bridge init: register create callback ok");

    NOOK_BRIDGE_LOGI("bridge init: register load callback");
    const NookStatus load_status = NookCommSetScriptLoadCallback(OnLoad);
    if (load_status != NOOK_STATUS_OK) {
        NOOK_BRIDGE_LOGE("register script load callback failed status=%d", load_status);
        return load_status;
    }
    NOOK_BRIDGE_LOGI("bridge init: register load callback ok");

    NOOK_BRIDGE_LOGI("bridge init: register unload callback");
    const NookStatus unload_status = NookCommSetScriptUnloadCallback(OnUnload);
    if (unload_status != NOOK_STATUS_OK) {
        NOOK_BRIDGE_LOGE("register script unload callback failed status=%d", unload_status);
        return unload_status;
    }
    NOOK_BRIDGE_LOGI("bridge init: register unload callback ok");

    g_native_hook_dispatch_requested = false;
    g_native_hook_dispatch_stop_requested = false;
    NOOK_BRIDGE_LOGI("bridge init: set native hook notifier");
    SetNativeJsHookEventNotifier(&SignalNativeHookDispatch);
    NOOK_BRIDGE_LOGI("bridge init: start native hook dispatch thread");
    g_native_hook_dispatch_thread = std::thread(&NativeHookDispatchThreadMain);
    NOOK_BRIDGE_LOGI("bridge init: start native hook dispatch thread ok");

    NOOK_BRIDGE_LOGI("bridge init: register internal rpc wildcard");
    framework::SetInternalRpcRequestHandler(OnRpc);
    NOOK_BRIDGE_LOGI("bridge init: register internal rpc wildcard ok");

    g_bridge_installed = true;
    NOOK_BRIDGE_LOGI("script runtime bridge initialized (lazy runtime init)");
    return NOOK_STATUS_OK;
}

void NookScriptRuntimeBridgeShutdown() {
    {
        std::lock_guard<std::mutex> lock(g_bridge_mutex);
        g_native_hook_dispatch_stop_requested = true;
        g_native_hook_dispatch_requested = true;
    }
    g_native_hook_dispatch_cv.notify_one();
    if (g_native_hook_dispatch_thread.joinable()) {
        g_native_hook_dispatch_thread.join();
    }

    std::lock_guard<std::mutex> lock(g_bridge_mutex);
    ResetNativeJsHookEventNotifier();
    g_script_registry.Clear();
    framework::SetInternalRpcRequestHandler({});
    JsRuntime::Shutdown();
    g_native_hook_dispatch_requested = false;
    g_native_hook_dispatch_stop_requested = false;
    g_bridge_installed = false;
}

}  // namespace agent_runtime
}  // namespace nook
