#include "gadget/nook_gadget_runtime.h"

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/nook_script_runtime_bridge.h"
#include "agent_runtime/script_registry.h"
#include "framework/NookCommInternal.h"
#include "gadget/nook_gadget_config.h"
#include "gadget/nook_gadget_direct_listener.h"

#include <exception>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#include "java_hook/JVM.h"
#include "java_hook/deferred/java_hook_loader_resolver.h"
#define NOOK_GADGET_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookCommApi", __VA_ARGS__))
#define NOOK_GADGET_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookCommApi", __VA_ARGS__))
#else
#define NOOK_GADGET_LOGI(...) ((void)0)
#define NOOK_GADGET_LOGE(...) ((void)0)
#endif

namespace nook {
namespace gadget {
namespace {

std::mutex& RuntimeMutex() {
    static std::mutex mutex;
    return mutex;
}

bool g_runtime_initialized = false;
NookStatus g_runtime_terminal_failure = NOOK_STATUS_OK;
constexpr const char* kLoadConfiguredStartupRpcMethod = "nook.gadget.load-configured-startup";
ControlInitializer g_control_initializer = nullptr;
ConnectInitializer g_connect_initializer = nullptr;
BridgeInitializer g_bridge_initializer = nullptr;
ListenInitializer g_listen_initializer = nullptr;
OnLoadWaiter g_on_load_waiter = nullptr;
RuntimeWarmupInitializer g_runtime_warmup_initializer = nullptr;
RuntimeReadyNotifier g_runtime_ready_notifier = nullptr;
StartupScriptInitializer g_startup_script_initializer = nullptr;
AssetFileReader g_asset_file_reader = &ReadGadgetAssetFile;
agent_runtime::ScriptRegistry& StartupScriptRegistry() {
    static agent_runtime::ScriptRegistry registry;
    return registry;
}

std::mutex& OnLoadWaitMutex() {
    static std::mutex mutex;
    return mutex;
}

std::condition_variable& OnLoadWaitCv() {
    static std::condition_variable cv;
    return cv;
}

bool g_on_load_wait_enabled = false;
bool g_on_load_wait_pending = false;
bool g_on_load_resume_requested = false;

StartupScriptLoader g_startup_script_loader = nullptr;

#if defined(__ANDROID__)
std::atomic<bool>& LifecycleSyncRetryThreadStarted() {
    static std::atomic<bool> started{false};
    return started;
}

void PumpJavaReadyCallbacksForGadget(const char* reason) {
    if (!agent_runtime::JsRuntime::IsInitialized()) {
        return;
    }

    std::string error_message;
    if (!agent_runtime::JsRuntime::DispatchJavaReadyCallbacks(&error_message)) {
        NOOK_GADGET_LOGE("gadget java-ready dispatch failed reason=%s error=%s",
                         reason != nullptr ? reason : "(unknown)",
                         error_message.c_str());
        return;
    }
    if (!agent_runtime::JsRuntime::PumpPendingTasks(&error_message)) {
        NOOK_GADGET_LOGE("gadget java-ready pump failed reason=%s error=%s",
                         reason != nullptr ? reason : "(unknown)",
                         error_message.c_str());
        return;
    }
    NOOK_GADGET_LOGI("gadget java-ready dispatch/pump ok reason=%s",
                     reason != nullptr ? reason : "(unknown)");
}

bool TrySyncApplicationLifecycleReady(const char* reason) {
    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    if (env == nullptr) {
        return false;
    }

    jobject application = JavaHookLoaderResolver::GetCurrentApplication(env);
    if (application == nullptr) {
        return false;
    }

    JavaHookLoaderResolver::MarkApplicationLifecycleReady(env, application);
    env->DeleteLocalRef(application);
    PumpJavaReadyCallbacksForGadget(reason);
    NOOK_GADGET_LOGI("gadget lifecycle sync ok reason=%s",
                     reason != nullptr ? reason : "(unknown)");
    return true;
}

void EnsureLifecycleSyncRetryThreadStarted() {
    bool expected = false;
    if (!LifecycleSyncRetryThreadStarted().compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }

    std::thread([]() {
        constexpr int kMaxAttempts = 40;
        constexpr auto kRetryDelay = std::chrono::milliseconds(100);
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            std::this_thread::sleep_for(kRetryDelay);
            if (TrySyncApplicationLifecycleReady("gadget-retry")) {
                LifecycleSyncRetryThreadStarted().store(false, std::memory_order_release);
                return;
            }
        }
        NOOK_GADGET_LOGE("gadget lifecycle sync retry timed out");
        LifecycleSyncRetryThreadStarted().store(false, std::memory_order_release);
    }).detach();
}
#else
void PumpJavaReadyCallbacksForGadget(const char* reason) {
    (void)reason;
}

bool TrySyncApplicationLifecycleReady(const char* reason) {
    (void)reason;
    return false;
}

void EnsureLifecycleSyncRetryThreadStarted() {}
#endif

void EmitDebugLog(const GadgetConfig& config,
                  RuntimeDebugLogger debug_logger,
                  const std::string& message) {
    if (!config.debug_logging || debug_logger == nullptr) {
        return;
    }
    debug_logger(message.c_str());
}

std::string FormatBool(bool value) {
    return value ? "true" : "false";
}

std::string WrapStartupScriptForLifecycleReady(const std::string& source) {
    std::string wrapped;
    wrapped.reserve(source.size() + 1024u);
    wrapped +=
        "(function(){"
        "if (typeof Java === 'object' && Java !== null &&"
        "    typeof Java.perform === 'function' &&"
        "    typeof Java._isLifecycleReady === 'function' &&"
        "    Java.vm && typeof Java.vm.perform === 'function') {"
        "  var __nookOriginalPerform = Java.perform;"
        "  Java.perform = function(fn) {"
        "    if (typeof fn !== 'function') {"
        "      throw new TypeError('Java.perform requires a function');"
        "    }"
        "    function __nookLifecycleReadyPerform() {"
        "      try {"
        "        if (!Java._isLifecycleReady()) {"
        "          setTimeout(__nookLifecycleReadyPerform, 50);"
        "          return;"
        "        }"
        "        Java.vm.perform(fn);"
        "      } catch (e) {"
        "        send('gadget-startup-defer-error:' + String(e));"
        "      }"
        "    }"
        "    __nookLifecycleReadyPerform();"
        "  };"
        "}"
        "})();\n";
    wrapped += source;
    return wrapped;
}

std::string CanonicalInteractionType(const GadgetConfig& config) {
    return config.interaction.type.empty() ? "listen" : config.interaction.type;
}

std::string CanonicalInteractionOnLoad(const GadgetConfig& config) {
    return config.interaction.on_load.empty() ? "resume" : config.interaction.on_load;
}

bool IsConnectInteraction(const GadgetConfig& config) {
    return CanonicalInteractionType(config) == "connect";
}

bool ShouldWaitOnLoad(const GadgetConfig& config) {
    return CanonicalInteractionType(config) == "listen" &&
           CanonicalInteractionOnLoad(config) == "wait";
}

void RollBackPostBridgeRuntimeState() {
    framework::UnregisterInternalRpcRequestHandler(kLoadConfiguredStartupRpcMethod);
    framework::RefreshAgentCallbacksForInternalRpc();
    framework::ResetExternalResumeHandler();
    std::lock_guard<std::mutex> lock(OnLoadWaitMutex());
    g_on_load_wait_enabled = false;
    g_on_load_wait_pending = false;
    g_on_load_resume_requested = false;
    OnLoadWaitCv().notify_all();
}

NookStatus LoadRuntimeConfig(GadgetConfig* config) {
    if (config == nullptr) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    *config = GadgetConfig{};
    std::string config_json;
    if (g_asset_file_reader == nullptr ||
        !g_asset_file_reader("assets/nook-gadget/config.json", &config_json)) {
        return NOOK_STATUS_OK;
    }
    if (!ParseGadgetConfigJson(config_json, config)) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return NOOK_STATUS_OK;
}

#if defined(__ANDROID__)
void DefaultRuntimeDebugLogger(const char* message) {
    NOOK_GADGET_LOGI("%s", message != nullptr ? message : "");
}
#else
void DefaultRuntimeDebugLogger(const char* message) {
    (void)message;
}
#endif

NookStatus LoadConfiguredStartupScript(const GadgetConfig& config,
                                       AssetFileReader asset_reader,
                                       StartupScriptLoader script_loader,
                                       RuntimeDebugLogger debug_logger,
                                       bool force_load) {
    try {
        if (!force_load && config.startup_mode != "auto-start") {
            EmitDebugLog(config,
                         debug_logger,
                         "startup auto-load skipped startup_mode=" + config.startup_mode);
            return NOOK_STATUS_OK;
        }
        if (force_load) {
            EmitDebugLog(config,
                         debug_logger,
                         "startup manual-load requested startup_mode=" + config.startup_mode);
        }
        if (!config.startup_script.enabled) {
            EmitDebugLog(config, debug_logger, "startup auto-load skipped startup_script=disabled");
            return NOOK_STATUS_OK;
        }
        if (config.startup_script.mode != "asset" || config.startup_script.path.empty()) {
            EmitDebugLog(config,
                         debug_logger,
                         "startup auto-load invalid config mode=" + config.startup_script.mode +
                             " path=" + config.startup_script.path +
                             " required=" + FormatBool(config.startup_script.required));
            return config.startup_script.required ? NOOK_STATUS_INTERNAL_ERROR : NOOK_STATUS_OK;
        }
        if (asset_reader == nullptr || script_loader == nullptr) {
            EmitDebugLog(config,
                         debug_logger,
                         "startup auto-load missing callbacks reader=" +
                             FormatBool(asset_reader != nullptr) + " loader=" +
                             FormatBool(script_loader != nullptr) + " required=" +
                             FormatBool(config.startup_script.required));
            return config.startup_script.required ? NOOK_STATUS_INTERNAL_ERROR : NOOK_STATUS_OK;
        }

        std::string startup_source;
        if (!asset_reader(config.startup_script.path.c_str(), &startup_source)) {
            EmitDebugLog(config,
                         debug_logger,
                         "startup asset read failed path=" + config.startup_script.path +
                             " required=" + FormatBool(config.startup_script.required));
            return config.startup_script.required ? NOOK_STATUS_INTERNAL_ERROR : NOOK_STATUS_OK;
        }

        const std::string effective_source =
            force_load ? startup_source : WrapStartupScriptForLifecycleReady(startup_source);
        uint32_t script_id = 0;
        const NookStatus load_status =
            script_loader("startup.js", effective_source.c_str(), &script_id);
        if (load_status != NOOK_STATUS_OK) {
            EmitDebugLog(config,
                         debug_logger,
                         "startup script load failed status=" + std::to_string(load_status) +
                             " required=" + FormatBool(config.startup_script.required));
        }
        if (load_status != NOOK_STATUS_OK && config.startup_script.required) {
            return load_status;
        }
        EmitDebugLog(config,
                     debug_logger,
                     "startup script load ok path=" + config.startup_script.path +
                         " script_id=" + std::to_string(script_id));
        return NOOK_STATUS_OK;
    } catch (const std::exception& exception) {
        NOOK_GADGET_LOGE("gadget startup exception=%s", exception.what());
        return config.startup_script.required ? NOOK_STATUS_INTERNAL_ERROR : NOOK_STATUS_OK;
    } catch (...) {
        NOOK_GADGET_LOGE("gadget startup exception=unknown");
        return config.startup_script.required ? NOOK_STATUS_INTERNAL_ERROR : NOOK_STATUS_OK;
    }
}

NookStatus DefaultStartupScriptInitializer() {
    GadgetConfig config;
    const NookStatus config_status = LoadRuntimeConfig(&config);
    if (config_status != NOOK_STATUS_OK) {
        return config_status;
    }
    return LoadConfiguredStartupScript(config,
                                       g_asset_file_reader,
                                       g_startup_script_loader,
                                       &DefaultRuntimeDebugLogger,
                                       false);
}

NookStatus DefaultStartupScriptLoader(const char* name,
                                      const char* source,
                                      uint32_t* script_id) {
    try {
        std::string error_message;
        if (!StartupScriptRegistry().CreateScript(name != nullptr ? name : "startup.js",
                                                  source != nullptr ? source : "",
                                                  script_id,
                                                  &error_message)) {
            NOOK_GADGET_LOGE("startup script create failed error=%s",
                             error_message.c_str());
            return NOOK_STATUS_INTERNAL_ERROR;
        }
        if (!StartupScriptRegistry().LoadScript(script_id != nullptr ? *script_id : 0,
                                                &error_message)) {
            NOOK_GADGET_LOGE("startup script load failed error=%s",
                             error_message.c_str());
            return NOOK_STATUS_INTERNAL_ERROR;
        }
        return NOOK_STATUS_OK;
    } catch (const std::exception& exception) {
        NOOK_GADGET_LOGE("startup script loader exception=%s", exception.what());
        return NOOK_STATUS_INTERNAL_ERROR;
    } catch (...) {
        NOOK_GADGET_LOGE("startup script loader exception=unknown");
        return NOOK_STATUS_INTERNAL_ERROR;
    }
}

NookStatus DefaultConnectInitializer(const GadgetConfig& config) {
    std::string host = config.interaction.host;
    if (host.empty()) {
        host = config.interaction.address;
    }

    return nook::framework::EnsureOutboundControlChannelReadyForCurrentProcess(
        host.c_str(),
        config.interaction.port);
}

NookStatus DefaultListenInitializer(const GadgetConfig& config) {
    return EnsureDirectAttachListenerForCurrentProcess(config);
}

NookStatus DefaultOnLoadWaiter(const GadgetConfig& config) {
    std::unique_lock<std::mutex> lock(OnLoadWaitMutex());
    g_on_load_wait_pending = true;
    EmitDebugLog(config,
                 &DefaultRuntimeDebugLogger,
                 "interaction on_load=wait blocking until host resume");
    OnLoadWaitCv().wait(lock, []() { return g_on_load_resume_requested || !g_on_load_wait_enabled; });
    const bool resume_requested = g_on_load_resume_requested;
    g_on_load_wait_pending = false;
    g_on_load_resume_requested = false;
    g_on_load_wait_enabled = false;
    if (resume_requested) {
        EmitDebugLog(config,
                     &DefaultRuntimeDebugLogger,
                     "interaction on_load=wait resumed by host");
    }
    return NOOK_STATUS_OK;
}

NookStatus DefaultRuntimeWarmupInitializer() {
#if defined(__ANDROID__)
    std::string error_message;
    if (!agent_runtime::JsRuntime::Initialize(&error_message)) {
        NOOK_GADGET_LOGE("gadget runtime warmup failed error=%s", error_message.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_OK;
#endif
}

NookStatus HandleExternalResumeRequest(uint32_t pid, bool* handled) {
    (void)pid;
    {
        std::lock_guard<std::mutex> lock(OnLoadWaitMutex());
        if (!g_on_load_wait_enabled) {
            if (handled != nullptr) {
                *handled = false;
            }
            return NOOK_STATUS_OK;
        }
        g_on_load_resume_requested = true;
        if (handled != nullptr) {
            *handled = true;
        }
    }
    OnLoadWaitCv().notify_all();
    return NOOK_STATUS_OK;
}

NookStatus InitializeConfiguredControlChannel(const GadgetConfig& config,
                                              ControlInitializer control_initializer) {
    const std::string interaction_type = CanonicalInteractionType(config);
    if (interaction_type == "listen") {
        if (config.transport_mode != "default") {
            NOOK_GADGET_LOGE("unsupported gadget transport_mode=%s",
                             config.transport_mode.c_str());
            return NOOK_STATUS_INVALID_ARGUMENT;
        }
        (void)control_initializer;
        return NOOK_STATUS_OK;
    }
    if (interaction_type != "connect") {
        NOOK_GADGET_LOGE("unsupported gadget interaction_type=%s",
                         interaction_type.c_str());
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    if (config.transport_mode != "default") {
        NOOK_GADGET_LOGE("unsupported gadget transport_mode=%s",
                         config.transport_mode.c_str());
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    std::string host = config.interaction.host;
    if (host.empty()) {
        host = config.interaction.address;
    }
    if (host.empty() || config.interaction.port <= 0) {
        NOOK_GADGET_LOGE("invalid gadget connect endpoint host=%s port=%d",
                         host.c_str(),
                         config.interaction.port);
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    ConnectInitializer connect_initializer = g_connect_initializer;
    return connect_initializer != nullptr ? connect_initializer(config) : NOOK_STATUS_INTERNAL_ERROR;
}

nook::comm::RpcResponse HandleLoadConfiguredStartupRpc(const nook::comm::RpcRequest& request) {
    nook::comm::RpcResponse response;
    response.script_id = request.script_id;

    GadgetConfig config;
    const NookStatus config_status = LoadRuntimeConfig(&config);
    if (config_status != NOOK_STATUS_OK) {
        response.success = false;
        response.error.code = static_cast<int32_t>(config_status);
        response.error.message = "gadget config load failed";
        return response;
    }
    if (!config.startup_script.enabled) {
        response.success = false;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "configured startup script not available";
        return response;
    }

    const NookStatus load_status = LoadConfiguredStartupScript(
        config,
        g_asset_file_reader,
        g_startup_script_loader,
        &DefaultRuntimeDebugLogger,
        true);
    if (load_status != NOOK_STATUS_OK) {
        response.success = false;
        response.error.code = static_cast<int32_t>(load_status);
        response.error.message = "configured startup script load failed";
        return response;
    }

    response.success = true;
    response.result_json =
        std::string("{\"loaded\":true,\"method\":\"") + kLoadConfiguredStartupRpcMethod +
        "\",\"startup_mode\":\"" + config.startup_mode + "\"}";
    return response;
}

}  // namespace

NookStatus InitializeRuntime() {
    std::unique_lock<std::mutex> lock(RuntimeMutex());
    if (g_runtime_terminal_failure != NOOK_STATUS_OK) {
        return g_runtime_terminal_failure;
    }
    if (g_runtime_initialized) {
        return NOOK_STATUS_OK;
    }

    GadgetConfig config;
    const NookStatus config_status = LoadRuntimeConfig(&config);
    if (config_status != NOOK_STATUS_OK) {
        NOOK_GADGET_LOGE("gadget config load failed status=%d", config_status);
        return config_status;
    }

    ControlInitializer control_initializer = g_control_initializer;
    const NookStatus control_status = InitializeConfiguredControlChannel(config, control_initializer);
    if (control_status != NOOK_STATUS_OK) {
        return control_status;
    }

    BridgeInitializer initializer = g_bridge_initializer;
    const NookStatus bridge_status =
        initializer != nullptr ? initializer() : NOOK_STATUS_INTERNAL_ERROR;
    if (bridge_status != NOOK_STATUS_OK) {
        return bridge_status;
    }
    framework::RegisterInternalRpcRequestHandler(
        kLoadConfiguredStartupRpcMethod,
        [](const nook::comm::RpcRequest& request) {
            return HandleLoadConfiguredStartupRpc(request);
        });
    framework::RefreshAgentCallbacksForInternalRpc();
    if (ShouldWaitOnLoad(config)) {
        {
            std::lock_guard<std::mutex> wait_lock(OnLoadWaitMutex());
            g_on_load_wait_enabled = true;
            g_on_load_wait_pending = false;
            g_on_load_resume_requested = false;
        }
        framework::SetExternalResumeHandler(
            [](uint32_t pid, bool* handled) {
                return HandleExternalResumeRequest(pid, handled);
            });
    } else {
        framework::ResetExternalResumeHandler();
    }

    ListenInitializer listen_initializer = g_listen_initializer;
    if (!IsConnectInteraction(config) &&
        listen_initializer != nullptr &&
        config.interaction.port > 0) {
        const NookStatus listen_status = listen_initializer(config);
        if (listen_status != NOOK_STATUS_OK) {
            RollBackPostBridgeRuntimeState();
            g_runtime_terminal_failure = listen_status;
            return listen_status;
        }
    }

    RuntimeReadyNotifier runtime_ready_notifier = g_runtime_ready_notifier;
    if (IsConnectInteraction(config) && runtime_ready_notifier != nullptr) {
        const NookStatus runtime_ready_status = runtime_ready_notifier();
        if (runtime_ready_status != NOOK_STATUS_OK) {
            NOOK_GADGET_LOGE("gadget runtime-ready notify failed status=%d",
                             runtime_ready_status);
            RollBackPostBridgeRuntimeState();
            g_runtime_terminal_failure = runtime_ready_status;
            return runtime_ready_status;
        }
    }

    if (!TrySyncApplicationLifecycleReady("gadget-init")) {
        EnsureLifecycleSyncRetryThreadStarted();
    }

    if (ShouldWaitOnLoad(config)) {
        RuntimeWarmupInitializer runtime_warmup_initializer = g_runtime_warmup_initializer;
        const NookStatus warmup_status =
            runtime_warmup_initializer != nullptr
                ? runtime_warmup_initializer()
                : NOOK_STATUS_INTERNAL_ERROR;
        if (warmup_status != NOOK_STATUS_OK) {
            NOOK_GADGET_LOGE("gadget runtime warmup failed status=%d", warmup_status);
            RollBackPostBridgeRuntimeState();
            g_runtime_terminal_failure = warmup_status;
            return warmup_status;
        }
    }

    StartupScriptInitializer startup_script_initializer = g_startup_script_initializer;
    if (startup_script_initializer != nullptr) {
        const NookStatus startup_script_status = startup_script_initializer();
        if (startup_script_status != NOOK_STATUS_OK) {
            NOOK_GADGET_LOGE("gadget startup script initializer failed status=%d",
                             startup_script_status);
            RollBackPostBridgeRuntimeState();
            g_runtime_terminal_failure = startup_script_status;
            return startup_script_status;
        }
    }

    if (ShouldWaitOnLoad(config)) {
        lock.unlock();
        OnLoadWaiter on_load_waiter = g_on_load_waiter;
        const NookStatus wait_status =
            on_load_waiter != nullptr ? on_load_waiter(config) : NOOK_STATUS_INTERNAL_ERROR;
        lock.lock();
        if (wait_status != NOOK_STATUS_OK) {
            NOOK_GADGET_LOGE("gadget on_load wait failed status=%d", wait_status);
            RollBackPostBridgeRuntimeState();
            g_runtime_terminal_failure = wait_status;
            return wait_status;
        }
        PumpJavaReadyCallbacksForGadget("gadget-wait-release");
    }

    g_runtime_initialized = true;
    return NOOK_STATUS_OK;
}

NookStatus InitializeConfiguredControlChannelForTesting(const GadgetConfig& config,
                                                        ControlInitializer control_initializer) {
    return InitializeConfiguredControlChannel(config, control_initializer);
}

NookStatus LoadConfiguredStartupScriptForTesting(const GadgetConfig& config,
                                                 AssetFileReader asset_reader,
                                                 StartupScriptLoader script_loader,
                                                 RuntimeDebugLogger debug_logger) {
    return LoadConfiguredStartupScript(config, asset_reader, script_loader, debug_logger, false);
}

NookStatus LoadConfiguredStartupScriptManuallyForTesting(const GadgetConfig& config,
                                                         AssetFileReader asset_reader,
                                                         StartupScriptLoader script_loader,
                                                         RuntimeDebugLogger debug_logger) {
    return LoadConfiguredStartupScript(config, asset_reader, script_loader, debug_logger, true);
}

bool TryHandleConfiguredStartupRpc(const nook::comm::RpcRequest& request,
                                   nook::comm::RpcResponse* response) {
    if (response == nullptr || request.method != kLoadConfiguredStartupRpcMethod) {
        return false;
    }
    *response = HandleLoadConfiguredStartupRpc(request);
    return true;
}

bool IsRuntimeInitialized() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    return g_runtime_initialized;
}

bool ShouldDeferJavaReadyChecksForOnLoadWait() {
    std::lock_guard<std::mutex> lock(OnLoadWaitMutex());
    return g_on_load_wait_enabled && !g_on_load_resume_requested;
}

void ResetRuntimeForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_runtime_initialized = false;
    g_runtime_terminal_failure = NOOK_STATUS_OK;
    framework::UnregisterInternalRpcRequestHandler(kLoadConfiguredStartupRpcMethod);
    framework::RefreshAgentCallbacksForInternalRpc();
    framework::ResetExternalResumeHandler();
    {
        std::lock_guard<std::mutex> wait_lock(OnLoadWaitMutex());
        g_on_load_wait_enabled = false;
        g_on_load_wait_pending = false;
        g_on_load_resume_requested = false;
    }
    OnLoadWaitCv().notify_all();
    StartupScriptRegistry().Clear();
}

void SetAssetFileReaderForTesting(AssetFileReader asset_reader) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_asset_file_reader = asset_reader;
}

void ResetAssetFileReaderForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_asset_file_reader = &ReadGadgetAssetFile;
}

void SetStartupScriptLoaderForTesting(StartupScriptLoader script_loader) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_startup_script_loader = script_loader;
}

void ResetStartupScriptLoaderForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_startup_script_loader = &DefaultStartupScriptLoader;
}

void SetConnectInitializerForTesting(ConnectInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_connect_initializer = initializer;
}

void ResetConnectInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_connect_initializer = nullptr;
}

void EnsureDefaultInitializers(ControlInitializer control_initializer,
                               BridgeInitializer bridge_initializer,
                               RuntimeReadyNotifier runtime_ready_notifier) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    if (g_control_initializer == nullptr) {
        g_control_initializer = control_initializer;
    }
    if (g_connect_initializer == nullptr) {
        g_connect_initializer = &DefaultConnectInitializer;
    }
    if (g_bridge_initializer == nullptr) {
        g_bridge_initializer = bridge_initializer;
    }
    if (g_listen_initializer == nullptr) {
        g_listen_initializer = &DefaultListenInitializer;
    }
    if (g_on_load_waiter == nullptr) {
        g_on_load_waiter = &DefaultOnLoadWaiter;
    }
    if (g_runtime_warmup_initializer == nullptr) {
        g_runtime_warmup_initializer = &DefaultRuntimeWarmupInitializer;
    }
    if (g_runtime_ready_notifier == nullptr) {
        g_runtime_ready_notifier = runtime_ready_notifier;
    }
    if (g_startup_script_initializer == nullptr) {
        g_startup_script_initializer = &DefaultStartupScriptInitializer;
    }
    if (g_startup_script_loader == nullptr) {
        g_startup_script_loader = &DefaultStartupScriptLoader;
    }
}

void SetControlInitializerForTesting(ControlInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_control_initializer = initializer;
}

void ResetControlInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_control_initializer = nullptr;
}

void SetBridgeInitializerForTesting(BridgeInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_bridge_initializer = initializer;
}

void ResetBridgeInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_bridge_initializer = nullptr;
}

void SetListenInitializerForTesting(ListenInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_listen_initializer = initializer;
}

void ResetListenInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_listen_initializer = nullptr;
}

void SetOnLoadWaiterForTesting(OnLoadWaiter waiter) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_on_load_waiter = waiter;
}

void ResetOnLoadWaiterForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_on_load_waiter = nullptr;
}

void SetRuntimeWarmupInitializerForTesting(RuntimeWarmupInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_runtime_warmup_initializer = initializer;
}

void ResetRuntimeWarmupInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_runtime_warmup_initializer = nullptr;
}

void SetRuntimeReadyNotifierForTesting(RuntimeReadyNotifier notifier) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_runtime_ready_notifier = notifier;
}

void ResetRuntimeReadyNotifierForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_runtime_ready_notifier = nullptr;
}

void SetStartupScriptInitializerForTesting(StartupScriptInitializer initializer) {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_startup_script_initializer = initializer;
}

void ResetStartupScriptInitializerForTesting() {
    std::lock_guard<std::mutex> lock(RuntimeMutex());
    g_startup_script_initializer = nullptr;
}

}  // namespace gadget
}  // namespace nook
