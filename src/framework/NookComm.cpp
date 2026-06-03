#include "nook/NookAgent.h"
#include "nook/NookComm.h"
#include "nook/NookJavaHook.h"

#if !defined(NOOK_ZYGOTE_HELPER_ONLY)
#include "../agent_runtime/js_runtime.h"
#include "../agent_runtime/nook_script_runtime_bridge.h"
#endif
#include "../communication/agent/agent_connection.h"
#include "../communication/transport/unix_transport.h"
#include "../java_hook/JVM.h"
#include "../java_hook/deferred/java_hook_class_observer.h"
#include "../java_hook/deferred/java_hook_loader_resolver.h"
#include "../java_hook/deferred/pending_java_hook_registry.h"
#include "NookJavaHookInternal.h"
#include "NookCommInternal.h"
#include "framework/nook_agent_runtime.h"
#include "framework/nook_zygote_control.h"
#include "nook_agent_init_policy.h"
#include "nook/Nook.h"

#include <condition_variable>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NOOK_AGENT_EXPORT __attribute__((visibility("default"), used))
#else
#define NOOK_AGENT_EXPORT
#endif

namespace {

std::mutex g_comm_mutex;
std::mutex g_spawn_gate_mutex;
std::condition_variable g_spawn_gate_cv;
std::unique_ptr<nook::comm::AgentConnection> g_agent_connection;
NookCommMessageCallback g_message_callback = nullptr;
NookCommScriptCreateCallback g_script_create_callback = nullptr;
NookCommScriptLoadCallback g_script_load_callback = nullptr;
NookCommScriptUnloadCallback g_script_unload_callback = nullptr;
std::unordered_map<std::string, NookCommRpcHandler> g_rpc_handlers;
bool g_spawn_gate_armed = false;
bool g_spawn_gate_released = true;
bool g_strict_child_native_gate_armed = false;
bool g_strict_child_native_gate_released = true;
bool g_strict_spawn_resume_requested = false;
uint32_t g_spawn_gate_pid = 0;
int g_spawn_gate_new_application_hook_id = -1;
int g_spawn_gate_call_application_on_create_hook_id = -1;
int g_spawn_gate_call_activity_on_create_hook_id = -1;
int g_highest_agent_ready_stage_sent = -1;
std::atomic<bool> g_deferred_agent_activation_pending{false};
std::atomic<bool> g_deferred_agent_activation_thread_started{false};
std::atomic<bool> g_force_spawn_gate_on_activation{false};
std::atomic<bool> g_promoted_strict_runtime_lifecycle_sync_thread_started{false};
std::mutex g_spawn_token_mutex;
std::string g_spawn_token_override;
std::mutex g_process_name_override_mutex;
std::string g_process_name_override;
bool g_agent_connection_suspended_for_fork = false;

#if defined(__ANDROID__)
constexpr const char* kNookCommTag = "NookCommApi";

#define NOOK_COMM_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kNookCommTag, __VA_ARGS__))
#define NOOK_COMM_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, kNookCommTag, __VA_ARGS__))

std::string ReadProcessName() {
    {
        std::lock_guard<std::mutex> lock(g_process_name_override_mutex);
        if (!g_process_name_override.empty()) {
            return g_process_name_override;
        }
    }

    FILE* fp = fopen("/proc/self/cmdline", "r");
    if (fp == nullptr) {
        return {};
    }

    char cmdline[256] = {0};
    fgets(cmdline, sizeof(cmdline), fp);
    fclose(fp);
    return cmdline;
}

bool IsExperimentalZygoteControlEnabled() {
    const char* value = std::getenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool ShouldSkipAutoInitializeForCurrentProcess() {
    const char* value = std::getenv("NOOK_SKIP_AUTO_INIT");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool ShouldForceZygoteControlReinit() {
    const char* value = std::getenv("NOOK_ZYGOTE_FORCE_REINIT");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

void ClearSpawnGateBootstrapHooks(int& new_application_hook_id,
                                  int& call_application_on_create_hook_id,
                                  int& call_activity_on_create_hook_id) {
    const int new_application = new_application_hook_id;
    const int call_application_on_create = call_application_on_create_hook_id;
    const int call_activity_on_create = call_activity_on_create_hook_id;
    new_application_hook_id = -1;
    call_application_on_create_hook_id = -1;
    call_activity_on_create_hook_id = -1;
    JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(false);

    if (new_application < 0 &&
        call_application_on_create < 0 &&
        call_activity_on_create < 0) {
        return;
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    NOOK_COMM_LOGI("spawn gate bootstrap cleanup skipped helper-only build newApplication=%d callApplicationOnCreate=%d callActivityOnCreate=%d",
                   new_application,
                   call_application_on_create,
                   call_activity_on_create);
    return;
#endif

    std::thread([new_application, call_application_on_create, call_activity_on_create]() {
        // Avoid unhooking while the bootstrap callback is still executing on the
        // same hook's mutex-protected path.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        NOOK_COMM_LOGI("spawn gate async hook cleanup begin newApplication=%d callApplicationOnCreate=%d callActivityOnCreate=%d",
                       new_application,
                       call_application_on_create,
                       call_activity_on_create);
        if (new_application >= 0) {
            (void)NookJavaHookUnhook(new_application);
        }
        if (call_application_on_create >= 0) {
            (void)NookJavaHookUnhook(call_application_on_create);
        }
        if (call_activity_on_create >= 0) {
            (void)NookJavaHookUnhook(call_activity_on_create);
        }
        NOOK_COMM_LOGI("spawn gate async hook cleanup end");
    }).detach();
}

bool ShouldUseHelperOnlyLocalZygoteControl() {
#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    const char* value = std::getenv("NOOK_STRICT_ZYGOTE_CONTROL");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

std::string ResolveStrictZygoteControlTargetProcessName(const std::string& process_name);

bool IsPromotedStrictZygoteControlSpawnChild(const std::string& process_name) {
    const std::string logical_process_name =
        ResolveStrictZygoteControlTargetProcessName(process_name);
    if (logical_process_name.empty() ||
        !nook::framework::ShouldAutoInitializeNookAgent(logical_process_name)) {
        return false;
    }

    const char* strict_request = std::getenv("NOOK_STRICT_ZYGOTE_REQUEST");
    if (strict_request == nullptr || std::strcmp(strict_request, "1") != 0) {
        return false;
    }

    const char* spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    return spawn_token != nullptr && spawn_token[0] != '\0';
}

void MarkCurrentZygoteAgentReinitCapable() {
    setenv("NOOK_ZYGOTE_REINIT_CAPABLE", "1", 1);
}

bool LooksLikeEarlySpawnProcessNameLocal(const std::string& name) {
    return name == "zygote" || name == "zygote64" ||
           name == "usap32" || name == "usap64" ||
           name == "<pre-initialized>" || name == "pre-initialized";
}

std::string ResolveStrictZygoteControlTargetProcessName(const std::string& process_name) {
    if (!LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        return process_name;
    }

    const char* strict_request = std::getenv("NOOK_STRICT_ZYGOTE_REQUEST");
    if (strict_request == nullptr || std::strcmp(strict_request, "1") != 0) {
        return process_name;
    }

    const char* spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (spawn_token == nullptr || spawn_token[0] == '\0') {
        return process_name;
    }

    const char* target_package = std::getenv("NOOK_TARGET_PACKAGE");
    if (target_package == nullptr || target_package[0] == '\0') {
        return process_name;
    }

    if (!nook::framework::ShouldAutoInitializeNookAgent(target_package)) {
        return process_name;
    }

    return target_package;
}

bool ShouldSkipBootstrapHooksForHelperOnlyChild() {
#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    const std::string process_name = ReadProcessName();
    return LooksLikeEarlySpawnProcessNameLocal(process_name) &&
           !IsPromotedStrictZygoteControlSpawnChild(process_name);
#else
    return false;
#endif
}

bool ShouldKeepProcessNameOverrideForCurrentInit(const std::string& process_name) {
    if (process_name.empty()) {
        return false;
    }

    if (LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        return false;
    }

    return nook::framework::ShouldAutoInitializeNookAgent(process_name);
}

bool ShouldPrimeActivatedSpawnChildBootstrap(const std::string& process_name) {
    if (!nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
        return false;
    }

    return IsPromotedStrictZygoteControlSpawnChild(process_name);
}

bool HasStrictZygoteSpawnEnvironment() {
    const char* strict_request = std::getenv("NOOK_STRICT_ZYGOTE_REQUEST");
    if (strict_request == nullptr || std::strcmp(strict_request, "1") != 0) {
        return false;
    }

    const char* spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    return spawn_token != nullptr && spawn_token[0] != '\0';
}

bool ResolveAgentProcessNameForInit(std::string* process_name, bool* spawn_gate_armed) {
    if (process_name == nullptr || spawn_gate_armed == nullptr) {
        return false;
    }

    *process_name = ReadProcessName();
    *spawn_gate_armed = false;
    if (process_name->empty()) {
        return false;
    }

    const std::string observed_process_name = *process_name;
    const std::string promoted_process_name =
        ResolveStrictZygoteControlTargetProcessName(observed_process_name);
    if (promoted_process_name != observed_process_name) {
        *process_name = promoted_process_name;
        std::lock_guard<std::mutex> process_lock(g_process_name_override_mutex);
        g_process_name_override = *process_name;
        NOOK_COMM_LOGI("promote zygote-control child process identity observed=%s target=%s",
                       observed_process_name.c_str(),
                       process_name->c_str());
    }

    if (nook::framework::ShouldAutoInitializeNookAgent(*process_name)) {
        *spawn_gate_armed = g_force_spawn_gate_on_activation.exchange(false, std::memory_order_acq_rel);
        return true;
    }

    const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (env_spawn_token != nullptr && env_spawn_token[0] != '\0' &&
        LooksLikeEarlySpawnProcessNameLocal(*process_name)) {
        *spawn_gate_armed = true;
        NOOK_COMM_LOGI("activate inherited early-spawn agent process=%s token=%s",
                       process_name->c_str(),
                       env_spawn_token);
        return true;
    }

    return false;
}

bool ShouldArmSpawnGateForCurrentProcess() {
    const std::string process_name = ReadProcessName();
    if (LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        NOOK_COMM_LOGI("skip spawn gate probe for early process=%s",
                       process_name.c_str());
        return false;
    }

    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    if (env == nullptr) {
        const char* spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
        if (spawn_token == nullptr || spawn_token[0] == '\0') {
            return false;
        }
        NOOK_COMM_LOGI("conservative spawn gate arming for spawned child without JNIEnv process=%s",
                       process_name.c_str());
        return true;
    }

    const char* spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (spawn_token != nullptr && spawn_token[0] != '\0' &&
        !JavaHookLoaderResolver::IsCurrentApplicationReady(env)) {
        NOOK_COMM_LOGI("spawn gate armed for spawned child pending application readiness process=%s",
                       process_name.c_str());
        return true;
    }

    return !JavaHookLoaderResolver::IsCurrentApplicationReady(env);
}

std::string GetSpawnTokenForCurrentProcess() {
    {
        std::lock_guard<std::mutex> lock(g_spawn_token_mutex);
        if (!g_spawn_token_override.empty()) {
            return g_spawn_token_override;
        }
    }

    const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (env_spawn_token != nullptr && env_spawn_token[0] != '\0') {
        return env_spawn_token;
    }
    return {};
}

void EnsureDeferredAgentActivationThreadStarted();
NookStatus InstallSpawnGateBootstrapHookIfNeededLocked();
NookStatus NookCommInitializeImpl(bool force_early_process_connect);
NookStatus PrimeActivatedSpawnChildBootstrap();
NookStatus EnsureRuntimeBridgeAndReady();
void WaitForStrictChildNativeGateRelease();
void PumpRuntimePendingTasksForSpawnGate(const char* stage);
void EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted();

}  // namespace

namespace nook {
namespace framework {

namespace {

void ResetInheritedSynchronizationForChildProcess() {
    new (&g_comm_mutex) std::mutex();
    new (&g_spawn_gate_mutex) std::mutex();
    new (&g_spawn_gate_cv) std::condition_variable();
    new (&g_spawn_token_mutex) std::mutex();
    new (&g_process_name_override_mutex) std::mutex();
    ResetInheritedInternalSynchronizationForChild();
}

}  // namespace

void ResetInheritedConnectionStateForChild(const std::string& process_name,
                                           const std::string& spawn_token,
                                           bool arm_spawn_gate) {
    ResetInheritedSynchronizationForChildProcess();

    if (!spawn_token.empty()) {
        setenv("NOOK_SPAWN_TOKEN", spawn_token.c_str(), 1);
    }

    if (!process_name.empty()) {
        NOOK_COMM_LOGI("reset inherited zygote connection state for child process=%s token=%s arm=%d",
                       process_name.c_str(),
                       spawn_token.c_str(),
                       arm_spawn_gate ? 1 : 0);
    }

    if (g_agent_connection != nullptr) {
        g_agent_connection.reset();
    }
    g_highest_agent_ready_stage_sent = -1;
    g_agent_connection_suspended_for_fork = false;
    g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
    g_deferred_agent_activation_thread_started.store(false, std::memory_order_relaxed);
    g_force_spawn_gate_on_activation.store(arm_spawn_gate, std::memory_order_release);
    g_promoted_strict_runtime_lifecycle_sync_thread_started.store(false, std::memory_order_relaxed);
    g_message_callback = nullptr;
    g_script_create_callback = nullptr;
    g_script_load_callback = nullptr;
    g_script_unload_callback = nullptr;
    {
        std::unordered_map<std::string, NookCommRpcHandler> empty;
        g_rpc_handlers.swap(empty);
    }
    ResetInheritedInternalRpcStateForChild();

    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        const bool arm_strict_child_native_gate =
            arm_spawn_gate && HasStrictZygoteSpawnEnvironment();
        g_spawn_gate_armed = arm_spawn_gate;
        g_spawn_gate_released = !arm_spawn_gate;
        g_strict_child_native_gate_armed = arm_strict_child_native_gate;
        g_strict_child_native_gate_released = !g_strict_child_native_gate_armed;
        g_strict_spawn_resume_requested = false;
        g_spawn_gate_pid = static_cast<uint32_t>(getpid());
        g_spawn_gate_new_application_hook_id = -1;
        g_spawn_gate_call_application_on_create_hook_id = -1;
        NOOK_COMM_LOGI("strict child native gate reset process=%s armed=%d released=%d spawn_gate=%d",
                       process_name.c_str(),
                       g_strict_child_native_gate_armed ? 1 : 0,
                       g_strict_child_native_gate_released ? 1 : 0,
                       arm_spawn_gate ? 1 : 0);
    }

    g_spawn_token_override = spawn_token;
    g_process_name_override = process_name;

    PendingJavaHookRegistry::Instance().ResetInheritedStateForChild();
    JavaHookClassObserver::ResetInheritedStateForChild();
    JavaHookLoaderResolver::ResetInheritedApplicationLoaderState();
    if (nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
        if (!ShouldPrimeActivatedSpawnChildBootstrap(process_name)) {
            NOOK_COMM_LOGI("skip synchronous child bootstrap prime for child-owned spawn process=%s",
                           process_name.c_str());
            return;
        }
        g_deferred_agent_activation_pending.store(true, std::memory_order_relaxed);
        const NookStatus prime_status = PrimeActivatedSpawnChildBootstrap();
        if (prime_status == NOOK_STATUS_OK) {
            g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
            NOOK_COMM_LOGI("primed child agent bootstrap synchronously process=%s",
                           process_name.c_str());
        } else {
            EnsureDeferredAgentActivationThreadStarted();
            NOOK_COMM_LOGI("armed deferred child agent activation process=%s prime_status=%d",
                           process_name.c_str(),
                           prime_status);
        }
    }
}

void ResetInheritedForkChildConnectionState() {
    ResetInheritedSynchronizationForChildProcess();

    if (g_agent_connection != nullptr) {
        g_agent_connection.reset();
    }
    g_highest_agent_ready_stage_sent = -1;
    g_agent_connection_suspended_for_fork = false;
    g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
    g_deferred_agent_activation_thread_started.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        g_spawn_gate_armed = false;
        g_spawn_gate_released = true;
        g_strict_child_native_gate_armed = false;
        g_strict_child_native_gate_released = true;
        g_strict_spawn_resume_requested = false;
        g_spawn_gate_pid = static_cast<uint32_t>(getpid());
        g_spawn_gate_new_application_hook_id = -1;
        g_spawn_gate_call_application_on_create_hook_id = -1;
    }

    PendingJavaHookRegistry::Instance().ResetInheritedStateForChild();
    JavaHookClassObserver::ResetInheritedStateForChild();
    JavaHookLoaderResolver::ResetInheritedApplicationLoaderState();
    NOOK_COMM_LOGI("reset inherited fork child connection state process=%s pid=%u",
                   ReadProcessName().c_str(),
                   static_cast<uint32_t>(getpid()));
}

}  // namespace framework
}  // namespace nook

namespace {

void DeferredAgentActivationThreadMain() {
    for (int attempt = 0; attempt < 400; ++attempt) {
        {
            std::lock_guard<std::mutex> lock(g_comm_mutex);
            if (g_agent_connection != nullptr) {
                g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
                g_deferred_agent_activation_thread_started.store(false, std::memory_order_relaxed);
                return;
            }
        }

        std::string process_name = ReadProcessName();
        if (nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
            NOOK_COMM_LOGI("deferred agent activation retry process=%s attempt=%d",
                           process_name.c_str(),
                           attempt + 1);
            const NookStatus status = NookAgentInitialize();
            if (status == NOOK_STATUS_OK) {
                g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
                g_deferred_agent_activation_thread_started.store(false, std::memory_order_relaxed);
                return;
            }
        }

        usleep(25000);
    }

    NOOK_COMM_LOGE("deferred agent activation timed out last_process=%s",
                   ReadProcessName().c_str());
    g_deferred_agent_activation_thread_started.store(false, std::memory_order_relaxed);
}

void EnsureDeferredAgentActivationThreadStarted() {
    bool expected = false;
    if (!g_deferred_agent_activation_thread_started.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
        return;
    }

    std::thread(DeferredAgentActivationThreadMain).detach();
}

NookStatus PrimeActivatedSpawnChildBootstrap() {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string process_name = ReadProcessName();
    if (!nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
        NOOK_COMM_LOGI("skip synchronous child bootstrap prime process=%s",
                       process_name.c_str());
        return NOOK_STATUS_OK;
    }

    if (IsPromotedStrictZygoteControlSpawnChild(process_name)) {
        NOOK_COMM_LOGI("synchronous child bootstrap prime zygote-control promoted-child fast path process=%s",
                       process_name.c_str());

        const NookStatus java_status = NookJavaHookInitialize();
        if (java_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime zygote-control promoted-child java hook init failed status=%d process=%s",
                           java_status,
                           process_name.c_str());
            return java_status;
        }

        const NookStatus comm_status = NookCommInitializeImpl(false);
        if (comm_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime zygote-control promoted-child comm init failed status=%d process=%s",
                           comm_status,
                           process_name.c_str());
            return comm_status;
        }

        {
            std::lock_guard<std::mutex> lock(g_comm_mutex);
            if (g_agent_connection == nullptr) {
                NOOK_COMM_LOGE("synchronous child bootstrap prime zygote-control promoted-child missing connection process=%s",
                               process_name.c_str());
                return NOOK_STATUS_INTERNAL_ERROR;
            }
        }

        {
            std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
            const NookStatus bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();
            if (bootstrap_status != NOOK_STATUS_OK) {
                NOOK_COMM_LOGE("synchronous child bootstrap prime zygote-control promoted-child hook install failed status=%d process=%s",
                               bootstrap_status,
                               process_name.c_str());
                return bootstrap_status;
            }
        }

        NOOK_COMM_LOGI("synchronous child bootstrap prime zygote-control promoted-child bootstrap hooks installed process=%s",
                       process_name.c_str());

        const NookStatus control_ready_status =
            nook::framework::NotifyZygoteControlReadyToServer();
        if (control_ready_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime zygote-control promoted-child control-ready failed status=%d process=%s",
                           control_ready_status,
                           process_name.c_str());
            return control_ready_status;
        }

        NOOK_COMM_LOGI("synchronous child bootstrap prime zygote-control promoted-child fast path ok process=%s",
                       process_name.c_str());
        NOOK_COMM_LOGI("strict zygote-control promoted-child fast path must not block native bootstrap before ActivityThread attach completes");
        return NOOK_STATUS_OK;
    }

    const NookStatus java_status = NookJavaHookInitialize();
    if (java_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("synchronous child bootstrap prime java hook init failed status=%d process=%s",
                       java_status,
                       process_name.c_str());
        return java_status;
    }

    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        const NookStatus early_bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();
        if (early_bootstrap_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime early hook install failed status=%d process=%s",
                           early_bootstrap_status,
                           process_name.c_str());
            return early_bootstrap_status;
        }
    }

    const NookStatus comm_status = NookCommInitializeImpl(false);
    if (comm_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("synchronous child bootstrap prime comm init failed status=%d process=%s",
                       comm_status,
                       process_name.c_str());
        return comm_status;
    }

    {
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        if (g_agent_connection == nullptr) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime missing connection process=%s",
                           process_name.c_str());
            return NOOK_STATUS_INTERNAL_ERROR;
        }
    }

    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        const NookStatus bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();
        if (bootstrap_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("synchronous child bootstrap prime hook install failed status=%d process=%s",
                           bootstrap_status,
                           process_name.c_str());
            return bootstrap_status;
        }
    }

#if !defined(NOOK_ZYGOTE_HELPER_ONLY)
    const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
    if (ready_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("synchronous child bootstrap prime runtime ready failed status=%d process=%s",
                       ready_status,
                       process_name.c_str());
        return ready_status;
    }
#else
    const NookStatus control_ready_status = nook::framework::NotifyZygoteControlReadyToServer();
    if (control_ready_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("synchronous child bootstrap prime control-ready failed status=%d process=%s",
                       control_ready_status,
                       process_name.c_str());
        return control_ready_status;
    }
#endif

    NOOK_COMM_LOGI("synchronous child bootstrap prime ok process=%s",
                   process_name.c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

std::string DetectCurrentAgentLibraryPath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&NookCommInitialize), &info) == 0 ||
        info.dli_fname == nullptr || info.dli_fname[0] == '\0') {
        FILE* maps = std::fopen("/proc/self/maps", "r");
        if (maps == nullptr) {
            return std::string();
        }

        char line[1024] = {};
        while (std::fgets(line, sizeof(line), maps) != nullptr) {
            if (std::strstr(line, "libnook-agent") == nullptr ||
                std::strstr(line, ".so") == nullptr) {
                continue;
            }

            char* path = std::strchr(line, '/');
            if (path == nullptr) {
                continue;
            }

            std::string resolved(path);
            while (!resolved.empty() &&
                   (resolved.back() == '\n' || resolved.back() == '\r' || resolved.back() == ' ')) {
                resolved.pop_back();
            }
            std::fclose(maps);
            return resolved;
        }

        std::fclose(maps);
        return std::string();
    }
    return std::string(info.dli_fname);
}

void ClearSpawnTokenAfterRuntimeReadyLocked(const std::string& sent_spawn_token) {
    if (sent_spawn_token.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> spawn_token_lock(g_spawn_token_mutex);
        if (g_spawn_token_override == sent_spawn_token) {
            g_spawn_token_override.clear();
        }
    }

    const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (env_spawn_token != nullptr && sent_spawn_token == env_spawn_token) {
        unsetenv("NOOK_SPAWN_TOKEN");
    }

    NOOK_COMM_LOGI("cleared spawn token after runtime-ready token=%s process=%s",
                   sent_spawn_token.c_str(),
                   ReadProcessName().c_str());
}

void EnsureRuntimeDirectoryEnvironmentForAgent() {
    const std::string agent_path = DetectCurrentAgentLibraryPath();
    const char* existing_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    if (existing_runtime_dir != nullptr && existing_runtime_dir[0] != '\0') {
        NOOK_COMM_LOGI("preserve runtime dir from env=%s agent_path=%s",
                       existing_runtime_dir,
                       agent_path.empty() ? "(empty)" : agent_path.c_str());
        return;
    }

    const std::string runtime_dir = nook::framework::ResolveRuntimeDirectoryFromAgentPath(agent_path);
    if (runtime_dir.empty()) {
        const bool is_memfd_agent =
            agent_path.rfind("/memfd:", 0) == 0 || agent_path.rfind("memfd:", 0) == 0;
        if (is_memfd_agent) {
            NOOK_COMM_LOGI("skip runtime dir resolution for memfd agent path=%s",
                           agent_path.c_str());
        } else {
            NOOK_COMM_LOGE("resolve runtime dir from agent path failed path=%s",
                           agent_path.c_str());
        }
        return;
    }

    setenv("NOOK_RUNTIME_DIR", runtime_dir.c_str(), 1);
    NOOK_COMM_LOGI("resolved runtime dir from agent path dir=%s path=%s previous=%s",
                   runtime_dir.c_str(),
                   agent_path.c_str(),
                   (existing_runtime_dir != nullptr && existing_runtime_dir[0] != '\0')
                       ? existing_runtime_dir
                       : "(empty)");
}

const char* GetArchName() {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__arm__)
    return "arm";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

bool SendAgentReadyLocked(const std::string& process_name,
                          uint32_t pid,
                          nook::comm::AgentReadyStage stage) {
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGE("SendAgentReadyLocked: agent connection unavailable pid=%u process=%s",
                       pid,
                       process_name.c_str());
        return false;
    }
    const int stage_value =
        stage == nook::comm::AgentReadyStage::kControl ? 0 : 1;
    if (g_highest_agent_ready_stage_sent >= stage_value) {
        return true;
    }

    nook::comm::AgentReady ready;
    ready.pid = pid;
    ready.process_name = process_name;
    ready.spawn_token = GetSpawnTokenForCurrentProcess();
    ready.arch = GetArchName();
    ready.version = NookGetVersion();
    ready.stage = stage;

    if (!g_agent_connection->SendAgentReady(ready)) {
        NOOK_COMM_LOGE("send AGENT_READY failed pid=%u name=%s",
                       ready.pid,
                       ready.process_name.c_str());
        return false;
    }

    g_highest_agent_ready_stage_sent = stage_value;
    NOOK_COMM_LOGI("AGENT_READY sent pid=%u name=%s token=%s arch=%s version=%s stage=%u",
                   ready.pid,
                   ready.process_name.c_str(),
                   ready.spawn_token.c_str(),
                   ready.arch.c_str(),
                   ready.version.c_str(),
                   static_cast<unsigned>(ready.stage));
    if (stage == nook::comm::AgentReadyStage::kRuntime) {
        ClearSpawnTokenAfterRuntimeReadyLocked(ready.spawn_token);
    }
    return true;
}

NookStatus EnsureRuntimeBridgeAndReady() {
    {
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        if (g_agent_connection == nullptr) {
            NOOK_COMM_LOGE("EnsureRuntimeBridgeAndReady: agent connection unavailable");
            return NOOK_STATUS_INTERNAL_ERROR;
        }
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    NOOK_COMM_LOGI("runtime bridge unavailable in zygote-helper-only build process=%s",
                   ReadProcessName().c_str());
    return NOOK_STATUS_NOT_IMPLEMENTED;
#else
    NOOK_COMM_LOGI("runtime bridge ensure begin process=%s", ReadProcessName().c_str());
    const NookStatus bridge_status = nook::agent_runtime::NookScriptRuntimeBridgeInitialize();
    if (bridge_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("runtime bridge ensure failed status=%d process=%s",
                       bridge_status,
                       ReadProcessName().c_str());
        return bridge_status;
    }
    NOOK_COMM_LOGI("runtime bridge ensure ok process=%s", ReadProcessName().c_str());

    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGE("EnsureRuntimeBridgeAndReady: agent connection disappeared process=%s",
                       ReadProcessName().c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    NOOK_COMM_LOGI("runtime bridge ensure start recv loop process=%s",
                   ReadProcessName().c_str());
    g_agent_connection->StartRecvLoop();

    const std::string process_name = ReadProcessName();
    const uint32_t pid = static_cast<uint32_t>(getpid());
    if (!SendAgentReadyLocked(process_name, pid, nook::comm::AgentReadyStage::kRuntime)) {
        NOOK_COMM_LOGE("EnsureRuntimeBridgeAndReady: send AGENT_READY failed process=%s",
                       process_name.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return NOOK_STATUS_OK;
#endif
}

void WaitForSpawnGateReleaseLocked(std::unique_lock<std::mutex>& gate_lock) {
    if (!g_spawn_gate_armed || g_spawn_gate_released) {
        return;
    }

    const uint32_t pid = g_spawn_gate_pid;
    NOOK_COMM_LOGI("blocking app bootstrap on spawn gate pid=%u", pid);
    g_spawn_gate_cv.wait(gate_lock, []() {
        return !g_spawn_gate_armed || g_spawn_gate_released;
    });
    NOOK_COMM_LOGI("released app bootstrap on spawn gate pid=%u", pid);
}

void WaitForStrictSpawnResumeRequestLocked(std::unique_lock<std::mutex>& gate_lock) {
    if (!g_spawn_gate_armed || g_spawn_gate_released ||
        !g_strict_child_native_gate_armed || g_strict_child_native_gate_released) {
        return;
    }

    const uint32_t pid = g_spawn_gate_pid;
    NOOK_COMM_LOGI("blocking strict activity bootstrap on resume request pid=%u", pid);
    g_spawn_gate_cv.wait(gate_lock, []() {
        return !g_spawn_gate_armed ||
               g_spawn_gate_released ||
               g_strict_spawn_resume_requested;
    });
    NOOK_COMM_LOGI("released strict activity bootstrap wait pid=%u resume_requested=%d released=%d",
                   pid,
                   g_strict_spawn_resume_requested ? 1 : 0,
                   g_spawn_gate_released ? 1 : 0);
}

void WaitForStrictChildNativeGateRelease() {
    std::unique_lock<std::mutex> gate_lock(g_spawn_gate_mutex);
    if (!g_strict_child_native_gate_armed || g_strict_child_native_gate_released) {
        return;
    }

    const uint32_t pid = g_spawn_gate_pid;
    NOOK_COMM_LOGI("blocking strict child native bootstrap on spawn gate pid=%u", pid);
    g_spawn_gate_cv.wait(gate_lock, []() {
        return !g_strict_child_native_gate_armed || g_strict_child_native_gate_released;
    });
    NOOK_COMM_LOGI("released strict child native bootstrap on spawn gate pid=%u", pid);
}

void UpdateApplicationClassLoaderFromApplication(JNIEnv* env, jobject application) {
    if (env == nullptr || application == nullptr) {
        return;
    }
    jclass application_class = env->GetObjectClass(application);
    if (application_class == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return;
    }

    jmethodID get_class_loader_method =
        env->GetMethodID(application_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(application_class);
    if (get_class_loader_method == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return;
    }

    jobject class_loader = env->CallObjectMethod(application, get_class_loader_method);
    if (class_loader == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return;
    }

    JavaHookLoaderResolver::UpdateApplicationClassLoader(env, class_loader);
    env->DeleteLocalRef(class_loader);
}

void SyncApplicationLifecycleStateFromCurrentApplication(const char* reason) {
    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    if (env == nullptr) {
        return;
    }

    jobject application = JavaHookLoaderResolver::GetCurrentApplication(env);
    if (application == nullptr) {
        NOOK_COMM_LOGI("application lifecycle sync skipped reason=%s process=%s",
                       reason != nullptr ? reason : "(unknown)",
                       ReadProcessName().c_str());
        return;
    }

    UpdateApplicationClassLoaderFromApplication(env, application);
    JavaHookLoaderResolver::MarkApplicationLifecycleReady(env, application);
    env->DeleteLocalRef(application);

    NOOK_COMM_LOGI("application lifecycle sync ok reason=%s process=%s",
                   reason != nullptr ? reason : "(unknown)",
                   ReadProcessName().c_str());
}

bool TrySyncApplicationLifecycleStateFromCurrentApplication(const char* reason) {
    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    if (env == nullptr) {
        NOOK_COMM_LOGI("application lifecycle sync skipped reason=%s process=%s jni=0",
                       reason != nullptr ? reason : "(unknown)",
                       ReadProcessName().c_str());
        return false;
    }

    jobject application = JavaHookLoaderResolver::GetCurrentApplication(env);
    if (application == nullptr) {
        NOOK_COMM_LOGI("application lifecycle sync skipped reason=%s process=%s",
                       reason != nullptr ? reason : "(unknown)",
                       ReadProcessName().c_str());
        return false;
    }

    UpdateApplicationClassLoaderFromApplication(env, application);
    JavaHookLoaderResolver::MarkApplicationLifecycleReady(env, application);
    env->DeleteLocalRef(application);

    NOOK_COMM_LOGI("application lifecycle sync ok reason=%s process=%s",
                   reason != nullptr ? reason : "(unknown)",
                   ReadProcessName().c_str());
    return true;
}

void EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted() {
#if defined(__ANDROID__) && !defined(_WIN32) && !defined(NOOK_ZYGOTE_HELPER_ONLY)
    bool expected = false;
    if (!g_promoted_strict_runtime_lifecycle_sync_thread_started.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }

    const std::string process_name = ReadProcessName();
    NOOK_COMM_LOGI("schedule promoted strict runtime lifecycle sync retry process=%s",
                   process_name.c_str());
    std::thread([process_name]() {
        constexpr int kMaxAttempts = 40;
        constexpr auto kRetryDelay = std::chrono::milliseconds(100);
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            std::this_thread::sleep_for(kRetryDelay);
            if (TrySyncApplicationLifecycleStateFromCurrentApplication(
                    "promoted-strict-spawn-child-runtime-retry")) {
                PumpRuntimePendingTasksForSpawnGate(
                    "promoted-strict-spawn-child-runtime-retry");
                NOOK_COMM_LOGI("promoted strict runtime lifecycle sync retry ok process=%s attempt=%d",
                               process_name.c_str(),
                               attempt);
                g_promoted_strict_runtime_lifecycle_sync_thread_started.store(
                    false, std::memory_order_release);
                return;
            }
        }

        NOOK_COMM_LOGE("promoted strict runtime lifecycle sync retry timed out process=%s",
                       process_name.c_str());
        g_promoted_strict_runtime_lifecycle_sync_thread_started.store(
            false, std::memory_order_release);
    }).detach();
#endif
}

void PumpRuntimePendingTasksForSpawnGate(const char* stage) {
#if !defined(NOOK_ZYGOTE_HELPER_ONLY)
    if (!nook::agent_runtime::JsRuntime::IsInitialized()) {
        return;
    }
    std::string error_message;
    if (!nook::agent_runtime::JsRuntime::DispatchJavaReadyCallbacks(&error_message)) {
        NOOK_COMM_LOGE("spawn gate java-ready dispatch failed stage=%s process=%s error=%s",
                       stage != nullptr ? stage : "(unknown)",
                       ReadProcessName().c_str(),
                       error_message.c_str());
        return;
    }
    if (!nook::agent_runtime::JsRuntime::PumpPendingTasks(&error_message)) {
        NOOK_COMM_LOGE("spawn gate pending task pump failed stage=%s process=%s error=%s",
                       stage != nullptr ? stage : "(unknown)",
                       ReadProcessName().c_str(),
                       error_message.c_str());
    } else {
        NOOK_COMM_LOGI("spawn gate java-ready dispatch/pump ok stage=%s process=%s",
                       stage != nullptr ? stage : "(unknown)",
                       ReadProcessName().c_str());
    }
#else
    (void)stage;
#endif
}

bool ShouldDeferStrictSpawnGateWaitToActivityOnCreateLocked() {
    return g_spawn_gate_armed &&
           !g_spawn_gate_released &&
           g_strict_child_native_gate_armed &&
           !g_strict_child_native_gate_released;
}

std::string ReadJavaObjectClassName(JNIEnv* env, jobject object) {
    if (env == nullptr || object == nullptr) {
        return {};
    }

    jclass object_class = env->GetObjectClass(object);
    if (object_class == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return {};
    }

    jclass class_class = env->FindClass("java/lang/Class");
    if (class_class == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(object_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return {};
    }

    jmethodID get_name_method = env->GetMethodID(class_class, "getName", "()Ljava/lang/String;");
    if (get_name_method == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(object_class);
        env->DeleteLocalRef(class_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return {};
    }

    jstring name_string =
        reinterpret_cast<jstring>(env->CallObjectMethod(object_class, get_name_method));
    env->DeleteLocalRef(object_class);
    env->DeleteLocalRef(class_class);
    if (name_string == nullptr || env->ExceptionCheck()) {
        if (name_string != nullptr) {
            env->DeleteLocalRef(name_string);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return {};
    }

    const char* utf_chars = env->GetStringUTFChars(name_string, nullptr);
    if (utf_chars == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(name_string);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return {};
    }

    std::string class_name(utf_chars);
    env->ReleaseStringUTFChars(name_string, utf_chars);
    env->DeleteLocalRef(name_string);
    return class_name;
}

void ProcessPendingJavaHooksForClass(const char* class_name) {
    JavaHookClassObserver::ScopedSuppression suppression;
    nook::java_hook_internal::ProcessPendingRequests(class_name);
}

void ReleaseStrictSpawnGateAtActivityOnCreate(const char* stage,
                                              const char* activity_class_name) {
#if defined(__ANDROID__) && !defined(_WIN32)
    int new_application_hook_id = -1;
    int call_application_on_create_hook_id = -1;
    int call_activity_on_create_hook_id = -1;
    bool should_release = false;
    uint32_t pid = 0;
    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        if (g_strict_spawn_resume_requested &&
            g_spawn_gate_armed &&
            !g_spawn_gate_released &&
            g_strict_child_native_gate_armed &&
            !g_strict_child_native_gate_released) {
            should_release = true;
            pid = g_spawn_gate_pid;
            g_strict_spawn_resume_requested = false;
            g_spawn_gate_released = true;
            g_strict_child_native_gate_released = true;
            new_application_hook_id = g_spawn_gate_new_application_hook_id;
            call_application_on_create_hook_id = g_spawn_gate_call_application_on_create_hook_id;
            call_activity_on_create_hook_id = g_spawn_gate_call_activity_on_create_hook_id;
            g_spawn_gate_new_application_hook_id = -1;
            g_spawn_gate_call_application_on_create_hook_id = -1;
            g_spawn_gate_call_activity_on_create_hook_id = -1;
        }
    }

    if (!should_release) {
        return;
    }

    NOOK_COMM_LOGI("strict spawn gate activity-stage release stage=%s pid=%u activity=%s newApplication=%d callApplicationOnCreate=%d callActivityOnCreate=%d",
                   stage != nullptr ? stage : "(unknown)",
                   pid,
                   activity_class_name != nullptr ? activity_class_name : "",
                   new_application_hook_id,
                   call_application_on_create_hook_id,
                   call_activity_on_create_hook_id);
    g_spawn_gate_cv.notify_all();
    ClearSpawnGateBootstrapHooks(new_application_hook_id,
                                 call_application_on_create_hook_id,
                                 call_activity_on_create_hook_id);
#else
    (void)stage;
    (void)activity_class_name;
#endif
}

int SpawnGateNewApplicationHookCallback(JNIEnv* env,
                                        jobject thiz,
                                        NookJavaHookValue* args,
                                        size_t arg_count,
                                        NookJavaHookValue* result) {
    (void)thiz;
    if (result != nullptr) {
        result->l = nullptr;
    }

    if (env != nullptr && args != nullptr && arg_count >= 1u && args[0].l != nullptr) {
        JavaHookLoaderResolver::UpdateApplicationClassLoader(
            env, reinterpret_cast<jobject>(args[0].l));
        PumpRuntimePendingTasksForSpawnGate("newApplication");
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    NOOK_COMM_LOGI("SpawnGateNewApplicationHookCallback helper-only wait process=%s",
                   ReadProcessName().c_str());
#else
    const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
    if (ready_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("SpawnGateNewApplicationHookCallback: runtime bridge/ready failed status=%d",
                       ready_status);
        return 1;
    }
#endif

    std::unique_lock<std::mutex> gate_lock(g_spawn_gate_mutex);
    if (ShouldDeferStrictSpawnGateWaitToActivityOnCreateLocked()) {
        NOOK_COMM_LOGI("skip newApplication gate wait for strict activity-stage release pid=%u",
                       g_spawn_gate_pid);
        return 1;
    }
    WaitForSpawnGateReleaseLocked(gate_lock);
    return 1;
}

int SpawnGateCallApplicationOnCreateHookCallback(JNIEnv* env,
                                                 jobject thiz,
                                                 NookJavaHookValue* args,
                                                 size_t arg_count,
                                                 NookJavaHookValue* result) {
    (void)thiz;
    (void)result;

    if (env != nullptr && args != nullptr && arg_count >= 1u && args[0].l != nullptr) {
        jobject application = reinterpret_cast<jobject>(args[0].l);
        UpdateApplicationClassLoaderFromApplication(env, application);
        JavaHookLoaderResolver::MarkApplicationLifecycleReady(env, application);
        PumpRuntimePendingTasksForSpawnGate("callApplicationOnCreate");
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    NOOK_COMM_LOGI("SpawnGateCallApplicationOnCreateHookCallback helper-only wait process=%s",
                   ReadProcessName().c_str());
#else
    const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
    if (ready_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("SpawnGateCallApplicationOnCreateHookCallback: runtime bridge/ready failed status=%d",
                       ready_status);
        return 1;
    }
#endif

    std::unique_lock<std::mutex> gate_lock(g_spawn_gate_mutex);
    if (ShouldDeferStrictSpawnGateWaitToActivityOnCreateLocked()) {
        NOOK_COMM_LOGI("skip callApplicationOnCreate gate wait for strict activity-stage release pid=%u",
                       g_spawn_gate_pid);
        return 1;
    }
    WaitForSpawnGateReleaseLocked(gate_lock);
    return 1;
}

int SpawnGateCallActivityOnCreateHookCallback(JNIEnv* env,
                                              jobject thiz,
                                              NookJavaHookValue* args,
                                              size_t arg_count,
                                              NookJavaHookValue* result) {
    (void)thiz;
    (void)result;

    std::string activity_class_name;
    if (env != nullptr && args != nullptr && arg_count >= 1u && args[0].l != nullptr) {
        activity_class_name =
            ReadJavaObjectClassName(env, reinterpret_cast<jobject>(args[0].l));
        NOOK_COMM_LOGI("strict activity-stage gate observed activity=%s process=%s",
                       activity_class_name.c_str(),
                       ReadProcessName().c_str());
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    NOOK_COMM_LOGI("SpawnGateCallActivityOnCreateHookCallback helper-only wait process=%s",
                   ReadProcessName().c_str());
#else
    const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
    if (ready_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("SpawnGateCallActivityOnCreateHookCallback: runtime bridge/ready failed status=%d",
                       ready_status);
        return 1;
    }
#endif

    {
        std::unique_lock<std::mutex> gate_lock(g_spawn_gate_mutex);
        if (!ShouldDeferStrictSpawnGateWaitToActivityOnCreateLocked()) {
            return 1;
        }
        WaitForStrictSpawnResumeRequestLocked(gate_lock);
        if (!g_strict_spawn_resume_requested) {
            return 1;
        }
    }

    if (!activity_class_name.empty()) {
        const bool had_pending_for_activity =
            PendingJavaHookRegistry::Instance().HasPendingForClass(activity_class_name.c_str());
        if (had_pending_for_activity) {
            NOOK_COMM_LOGI("strict activity-stage install pending hooks activity=%s",
                           activity_class_name.c_str());
            ProcessPendingJavaHooksForClass(activity_class_name.c_str());
        } else {
            NOOK_COMM_LOGI("strict activity-stage no pending hooks activity=%s",
                           activity_class_name.c_str());
        }
    }

    PumpRuntimePendingTasksForSpawnGate("callActivityOnCreate");
    ReleaseStrictSpawnGateAtActivityOnCreate("callActivityOnCreate",
                                             activity_class_name.c_str());
    return 1;
}

NookStatus InstallSpawnGateBootstrapHookIfNeededLocked() {
    if (ShouldSkipBootstrapHooksForHelperOnlyChild()) {
        NOOK_COMM_LOGI("skip spawn gate bootstrap hooks for helper-only child process=%s",
                       ReadProcessName().c_str());
        return NOOK_STATUS_OK;
    }

    if (!g_spawn_gate_armed || g_spawn_gate_released) {
        return NOOK_STATUS_OK;
    }
    if (g_spawn_gate_new_application_hook_id >= 0 ||
        g_spawn_gate_call_application_on_create_hook_id >= 0) {
        JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(true);
        return NOOK_STATUS_OK;
    }

    const NookStatus java_status = NookJavaHookInitialize();
    if (java_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("spawn gate bootstrap hook init failed status=%d", java_status);
        return java_status;
    }

    constexpr const char* kBootstrapClass = "android.app.Instrumentation";
    constexpr const char* kNewApplicationMethod = "newApplication";
    constexpr const char* kNewApplicationSignature =
        "(Ljava/lang/ClassLoader;Ljava/lang/String;Landroid/content/Context;)Landroid/app/Application;";
    constexpr const char* kCallApplicationOnCreateMethod = "callApplicationOnCreate";
    constexpr const char* kCallApplicationOnCreateSignature = "(Landroid/app/Application;)V";
    constexpr const char* kCallActivityOnCreateMethod = "callActivityOnCreate";
    constexpr const char* kCallActivityOnCreateSignature =
        "(Landroid/app/Activity;Landroid/os/Bundle;)V";

    int new_application_hook_id = NookJavaHookHook(kBootstrapClass,
                                                   kNewApplicationMethod,
                                                   kNewApplicationSignature,
                                                   0,
                                                   &SpawnGateNewApplicationHookCallback);
    if (new_application_hook_id < 0) {
        new_application_hook_id = NookJavaHookHookDeferred(kBootstrapClass,
                                                           kNewApplicationMethod,
                                                           kNewApplicationSignature,
                                                           0,
                                                           &SpawnGateNewApplicationHookCallback);
    }
    if (new_application_hook_id < 0) {
        NOOK_COMM_LOGE("spawn gate bootstrap hook install failed class=%s method=%s signature=%s hook=%d",
                       kBootstrapClass,
                       kNewApplicationMethod,
                       kNewApplicationSignature,
                       new_application_hook_id);
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    g_spawn_gate_new_application_hook_id = new_application_hook_id;

    int call_application_on_create_hook_id = NookJavaHookHook(kBootstrapClass,
                                                              kCallApplicationOnCreateMethod,
                                                              kCallApplicationOnCreateSignature,
                                                              0,
                                                              &SpawnGateCallApplicationOnCreateHookCallback);
    if (call_application_on_create_hook_id < 0) {
        call_application_on_create_hook_id = NookJavaHookHookDeferred(kBootstrapClass,
                                                                      kCallApplicationOnCreateMethod,
                                                                      kCallApplicationOnCreateSignature,
                                                                      0,
                                                                      &SpawnGateCallApplicationOnCreateHookCallback);
    }
    if (call_application_on_create_hook_id < 0) {
        NOOK_COMM_LOGE("spawn gate bootstrap fallback hook install failed class=%s method=%s signature=%s hook=%d",
                       kBootstrapClass,
                       kCallApplicationOnCreateMethod,
                       kCallApplicationOnCreateSignature,
                       call_application_on_create_hook_id);
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    g_spawn_gate_call_application_on_create_hook_id = call_application_on_create_hook_id;
    int call_activity_on_create_hook_id = -1;
    if (g_strict_child_native_gate_armed && !g_strict_child_native_gate_released) {
        call_activity_on_create_hook_id = NookJavaHookHook(kBootstrapClass,
                                                           kCallActivityOnCreateMethod,
                                                           kCallActivityOnCreateSignature,
                                                           0,
                                                           &SpawnGateCallActivityOnCreateHookCallback);
        if (call_activity_on_create_hook_id < 0) {
            call_activity_on_create_hook_id = NookJavaHookHookDeferred(
                kBootstrapClass,
                kCallActivityOnCreateMethod,
                kCallActivityOnCreateSignature,
                0,
                &SpawnGateCallActivityOnCreateHookCallback);
        }
        if (call_activity_on_create_hook_id < 0) {
            NOOK_COMM_LOGE("spawn gate strict activity hook install failed class=%s method=%s signature=%s hook=%d",
                           kBootstrapClass,
                           kCallActivityOnCreateMethod,
                           kCallActivityOnCreateSignature,
                           call_activity_on_create_hook_id);
            return NOOK_STATUS_INTERNAL_ERROR;
        }
    }

    g_spawn_gate_call_activity_on_create_hook_id = call_activity_on_create_hook_id;
    JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(true);
    NOOK_COMM_LOGI("spawn gate bootstrap hooks installed newApplication=%d callApplicationOnCreate=%d callActivityOnCreate=%d class=%s",
                   new_application_hook_id,
                   call_application_on_create_hook_id,
                   call_activity_on_create_hook_id,
                   kBootstrapClass);
    return NOOK_STATUS_OK;
}

nook::comm::RpcResponse DispatchPublicOrInternalRpc(const nook::comm::RpcRequest& request) {
    if (nook::framework::HasInternalRpcRequestHandlers()) {
        const nook::comm::RpcResponse internal_response =
            nook::framework::DispatchInternalRpcRequest(request);
        if (internal_response.success ||
            internal_response.error.code != static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT) ||
            internal_response.error.message != "internal rpc handler not found") {
            return internal_response;
        }
    }

    nook::comm::RpcResponse response;
    response.script_id = request.script_id;

    auto it = g_rpc_handlers.find(request.method);
    if (it == g_rpc_handlers.end() || it->second == nullptr) {
        response.success = false;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "rpc handler not found";
        return response;
    }

    char* result_json = nullptr;
    it->second(request.method.c_str(), request.args_json.c_str(), &result_json);
    response.success = true;
    response.result_json = result_json != nullptr ? result_json : "null";
    return response;
}

void ApplyAgentCallbacksLocked() {
    if (g_agent_connection == nullptr) {
        return;
    }

    NookCommMessageCallback callback = g_message_callback;
    if (callback == nullptr) {
        g_agent_connection->SetMessageCallback({});
    } else {
        g_agent_connection->SetMessageCallback([callback](const nook::comm::ScriptPost& post) {
            callback(post.script_id,
                     "script-post",
                     post.message.c_str(),
                     post.data.empty() ? nullptr : post.data.data(),
                     post.data.size());
        });
    }

    NookCommScriptCreateCallback create_callback = g_script_create_callback;
    if (create_callback == nullptr) {
        g_agent_connection->SetScriptCreateHandler({});
    } else {
        g_agent_connection->SetScriptCreateHandler(
                [create_callback](const nook::comm::ScriptCreate& create) {
                    nook::comm::ScriptCreateResponse response;
                    uint32_t script_id = 0;
                    const NookStatus status = create_callback(create.name.c_str(),
                                                              create.source.c_str(),
                                                              &script_id);
                    response.script_id = script_id;
                    response.success = (status == NOOK_STATUS_OK);
                    if (!response.success) {
                        response.error.code = static_cast<int32_t>(status);
                        response.error.message =
                                nook::framework::MakeScriptCallbackErrorMessage(
                                        "script create callback failed");
                    }
                    return response;
                });
    }

    NookCommScriptLoadCallback load_callback = g_script_load_callback;
    if (load_callback == nullptr) {
        g_agent_connection->SetScriptLoadHandler({});
    } else {
        g_agent_connection->SetScriptLoadHandler([load_callback](const nook::comm::ScriptLoad& load) {
            nook::comm::ScriptResponse response;
            response.script_id = load.script_id;
            const NookStatus status = load_callback(load.script_id);
            response.success = (status == NOOK_STATUS_OK);
            if (!response.success) {
                response.error.code = static_cast<int32_t>(status);
                response.error.message =
                        nook::framework::MakeScriptCallbackErrorMessage(
                                "script load callback failed");
            }
            return response;
        });
    }

    NookCommScriptUnloadCallback unload_callback = g_script_unload_callback;
    if (unload_callback == nullptr) {
        g_agent_connection->SetScriptUnloadHandler({});
    } else {
        g_agent_connection->SetScriptUnloadHandler(
                [unload_callback](const nook::comm::ScriptUnload& unload) {
                    nook::comm::ScriptResponse response;
                    response.script_id = unload.script_id;
                    const NookStatus status = unload_callback(unload.script_id);
                    response.success = (status == NOOK_STATUS_OK);
                    if (!response.success) {
                        response.error.code = static_cast<int32_t>(status);
                        response.error.message =
                                nook::framework::MakeScriptCallbackErrorMessage(
                                        "script unload callback failed");
                    }
                    return response;
                 });
    }

    if (nook::framework::HasInternalRpcRequestHandlers()) {
        g_agent_connection->SetSpawnInstallHandler([](const nook::comm::SpawnInstallRequest& request) {
            return nook::framework::HandleSpawnInstallRequest(request);
        });
        g_agent_connection->SetSpawnUninstallHandler([](const nook::comm::SpawnUninstallRequest& request) {
            return nook::framework::HandleSpawnUninstallRequest(request, true);
        });
    } else {
        g_agent_connection->SetSpawnInstallHandler({});
        g_agent_connection->SetSpawnUninstallHandler({});
    }

    if (nook::framework::HasInternalRpcRequestHandlers() || !g_rpc_handlers.empty()) {
        g_agent_connection->SetRpcHandler([](const nook::comm::RpcRequest& request) {
            return DispatchPublicOrInternalRpc(request);
        });
    } else {
        g_agent_connection->SetRpcHandler({});
    }
}

void ApplySpawnGateResumeHandlerLocked(uint32_t pid) {
    if (g_agent_connection == nullptr) {
        return;
    }

    g_agent_connection->SetResumeHandler([pid](const nook::comm::ResumeRequest& request) {
        NOOK_COMM_LOGI("spawn gate resume handler begin request_pid=%u local_pid=%u armed=%d released=%d",
                       request.pid,
                       pid,
                       g_spawn_gate_armed ? 1 : 0,
                       g_spawn_gate_released ? 1 : 0);
        nook::comm::ResumeResponse response;
        response.pid = request.pid;
        if (request.pid != 0 && request.pid != pid) {
            response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
            response.error.message = "resume pid mismatch";
            NOOK_COMM_LOGE("spawn gate resume handler pid mismatch request_pid=%u local_pid=%u",
                           request.pid,
                           pid);
            return response;
        }

        int new_application_hook_id = -1;
        int call_application_on_create_hook_id = -1;
        int call_activity_on_create_hook_id = -1;
        bool delayed_strict_release = false;
        {
            std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
            if (g_spawn_gate_pid == pid) {
                if (g_strict_child_native_gate_armed && !g_strict_child_native_gate_released) {
                    g_strict_spawn_resume_requested = true;
                    delayed_strict_release = true;
                } else {
                    g_spawn_gate_released = true;
                    g_strict_child_native_gate_released = true;
                    new_application_hook_id = g_spawn_gate_new_application_hook_id;
                    call_application_on_create_hook_id =
                        g_spawn_gate_call_application_on_create_hook_id;
                    call_activity_on_create_hook_id =
                        g_spawn_gate_call_activity_on_create_hook_id;
                    g_spawn_gate_new_application_hook_id = -1;
                    g_spawn_gate_call_application_on_create_hook_id = -1;
                    g_spawn_gate_call_activity_on_create_hook_id = -1;
                }
            }
        }
        if (delayed_strict_release) {
            NOOK_COMM_LOGI("spawn gate resume handler defer strict release request_pid=%u", request.pid);
            g_spawn_gate_cv.notify_all();
            return response;
        }
        NOOK_COMM_LOGI("spawn gate resume handler clear hooks request_pid=%u newApplication=%d callApplicationOnCreate=%d",
                       request.pid,
                       new_application_hook_id,
                       call_application_on_create_hook_id);
        NOOK_COMM_LOGI("spawn gate resume handler notify request_pid=%u", request.pid);
        g_spawn_gate_cv.notify_all();
        ClearSpawnGateBootstrapHooks(new_application_hook_id,
                                     call_application_on_create_hook_id,
                                     call_activity_on_create_hook_id);
        NOOK_COMM_LOGI("spawn gate resume handler end request_pid=%u", request.pid);
        return response;
    });
}

NookStatus NookCommInitializeImpl(bool force_early_process_connect) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection != nullptr) {
        if (g_agent_connection->IsConnected()) {
            NOOK_COMM_LOGI("NookComm already initialized");
            return NOOK_STATUS_OK;
        }

        NOOK_COMM_LOGI("NookComm resetting stale connection process=%s",
                       ReadProcessName().c_str());
        int new_application_hook_id = -1;
        int call_application_on_create_hook_id = -1;
        int call_activity_on_create_hook_id = -1;
        {
            std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
            new_application_hook_id = g_spawn_gate_new_application_hook_id;
            call_application_on_create_hook_id = g_spawn_gate_call_application_on_create_hook_id;
            call_activity_on_create_hook_id = g_spawn_gate_call_activity_on_create_hook_id;
            g_spawn_gate_new_application_hook_id = -1;
            g_spawn_gate_call_application_on_create_hook_id = -1;
            g_spawn_gate_call_activity_on_create_hook_id = -1;
        }
        ClearSpawnGateBootstrapHooks(new_application_hook_id,
                                     call_application_on_create_hook_id,
                                     call_activity_on_create_hook_id);
        g_agent_connection.reset();
        g_highest_agent_ready_stage_sent = -1;
        g_agent_connection_suspended_for_fork = false;
    }

    EnsureRuntimeDirectoryEnvironmentForAgent();

    std::string process_name = ReadProcessName();
    bool spawn_gate_armed = false;
    if (!force_early_process_connect) {
        if (nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
            spawn_gate_armed = g_deferred_agent_activation_pending.load(std::memory_order_relaxed);
        } else {
            if (!ResolveAgentProcessNameForInit(&process_name, &spawn_gate_armed)) {
                g_deferred_agent_activation_pending.store(true, std::memory_order_relaxed);
                NOOK_COMM_LOGI("skip NookCommInitialize for early process=%s",
                               ReadProcessName().c_str());
                EnsureDeferredAgentActivationThreadStarted();
                return NOOK_STATUS_OK;
            }
        }
    } else {
        if (process_name.empty()) {
            process_name = ReadProcessName();
        }
        if (process_name.empty()) {
            NOOK_COMM_LOGE("force early process connect failed: process name unavailable");
            return NOOK_STATUS_INTERNAL_ERROR;
        }
        NOOK_COMM_LOGI("force early process connect process=%s", process_name.c_str());
    }

    auto transport = std::make_unique<nook::comm::UnixTransport>(nook::comm::GetDefaultSocketPath());
    auto connection = std::make_unique<nook::comm::AgentConnection>(std::move(transport));
    if (!connection->Connect()) {
        NOOK_COMM_LOGE("connect unix socket failed path=%s",
                       nook::comm::GetDefaultSocketPath().c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    const uint32_t pid = static_cast<uint32_t>(getpid());

    if (!force_early_process_connect && !spawn_gate_armed) {
        if (IsPromotedStrictZygoteControlSpawnChild(process_name)) {
            spawn_gate_armed = false;
            NOOK_COMM_LOGI("skip spawn gate re-arm for promoted zygote-control child process=%s",
                           process_name.c_str());
        } else {
            spawn_gate_armed = ShouldArmSpawnGateForCurrentProcess();
        }
    } else if (force_early_process_connect) {
        NOOK_COMM_LOGI("skip spawn gate arming for force-early process=%s",
                       process_name.c_str());
    }
    {
        std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
        const bool inherited_strict_child_native_gate_armed =
            g_strict_child_native_gate_armed && !g_strict_child_native_gate_released;
        g_spawn_gate_armed = spawn_gate_armed;
        g_spawn_gate_released = !spawn_gate_armed;
        g_strict_child_native_gate_armed = inherited_strict_child_native_gate_armed;
        g_strict_child_native_gate_released = !g_strict_child_native_gate_armed;
        g_strict_spawn_resume_requested = false;
        g_spawn_gate_pid = pid;
        g_spawn_gate_new_application_hook_id = -1;
        g_spawn_gate_call_application_on_create_hook_id = -1;
        NOOK_COMM_LOGI("strict child native gate init process=%s armed=%d released=%d spawn_gate=%d",
                       process_name.c_str(),
                       g_strict_child_native_gate_armed ? 1 : 0,
                       g_strict_child_native_gate_released ? 1 : 0,
                       spawn_gate_armed ? 1 : 0);
    }

    g_agent_connection = std::move(connection);
    g_highest_agent_ready_stage_sent = -1;
    g_deferred_agent_activation_pending.store(false, std::memory_order_relaxed);
    if (!ShouldKeepProcessNameOverrideForCurrentInit(process_name)) {
        std::lock_guard<std::mutex> process_lock(g_process_name_override_mutex);
        if (process_name == g_process_name_override) {
            g_process_name_override.clear();
        }
    }
    ApplyAgentCallbacksLocked();
    ApplySpawnGateResumeHandlerLocked(pid);
    NOOK_COMM_LOGI("spawn gate state pid=%u armed=%d",
                   pid,
                   spawn_gate_armed ? 1 : 0);
    return NOOK_STATUS_OK;
#else
    (void) force_early_process_connect;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

#endif

}  // namespace

extern "C" {

#if defined(__ANDROID__) && !defined(_WIN32)
__attribute__((constructor(115))) static void NookCommAutoInitialize(void) {
    const std::string process_name = ReadProcessName();
    if (LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        if (IsExperimentalZygoteControlEnabled()) {
            if (ShouldSkipAutoInitializeForCurrentProcess()) {
                NOOK_COMM_LOGI("skip zygote auto initialize process=%s env=NOOK_SKIP_AUTO_INIT",
                               process_name.c_str());
                return;
            }
            (void)NookAgentInitializeForZygoteControl();
        }
        return;
    }

    if (ShouldSkipAutoInitializeForCurrentProcess()) {
        NOOK_COMM_LOGI("skip NookComm auto initialize process=%s env=NOOK_SKIP_AUTO_INIT",
                       process_name.c_str());
        return;
    }

    if (nook::framework::ShouldAutoInitializeNookAgent(process_name)) {
        g_force_spawn_gate_on_activation.store(true, std::memory_order_release);
    }
    (void)NookAgentInitialize();
}
#endif

NOOK_AGENT_EXPORT NookStatus NookAgentInitializeForZygoteControl(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string process_name = ReadProcessName();
    if (!LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
        const char* env_target_package = std::getenv("NOOK_TARGET_PACKAGE");
        NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl invalid process=%s target=%s token_set=%d",
                       process_name.c_str(),
                       (env_target_package != nullptr && env_target_package[0] != '\0')
                           ? env_target_package
                           : "",
                       (env_spawn_token != nullptr && env_spawn_token[0] != '\0') ? 1 : 0);
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__)
    const char* native_hooks_env_before = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
    const char* wrapper_hooks_env_before = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
    NOOK_COMM_LOGI("zygote init env before normalize process=%s native=%s wrapper=%s",
                   process_name.c_str(),
                   native_hooks_env_before != nullptr ? native_hooks_env_before : "",
                   wrapper_hooks_env_before != nullptr ? wrapper_hooks_env_before : "");

    const bool helper_only_local_control = ShouldUseHelperOnlyLocalZygoteControl();
    const char* desired_native_hooks_env = nullptr;
    const char* desired_wrapper_hooks_env = nullptr;
    if (helper_only_local_control) {
        desired_native_hooks_env = "0";
        desired_wrapper_hooks_env = "0";
    } else {
        desired_native_hooks_env =
            (native_hooks_env_before != nullptr && native_hooks_env_before[0] != '\0')
                ? native_hooks_env_before
                : "0";
        desired_wrapper_hooks_env =
            (wrapper_hooks_env_before != nullptr && wrapper_hooks_env_before[0] != '\0')
                ? wrapper_hooks_env_before
                : "0";
    }

    if (setenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS", desired_native_hooks_env, 1) != 0) {
        NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl set native hooks env failed process=%s",
                       process_name.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    if (setenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS", desired_wrapper_hooks_env, 1) != 0) {
        NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl set wrapper hooks env failed process=%s",
                       process_name.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    const char* native_hooks_env_after = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
    const char* wrapper_hooks_env_after = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
    NOOK_COMM_LOGI("zygote init env after normalize process=%s native=%s wrapper=%s",
                   process_name.c_str(),
                   native_hooks_env_after != nullptr ? native_hooks_env_after : "",
                   wrapper_hooks_env_after != nullptr ? wrapper_hooks_env_after : "");
#endif

    if (ShouldForceZygoteControlReinit()) {
        NOOK_COMM_LOGI("NookAgentInitializeForZygoteControl forced reinit process=%s",
                       process_name.c_str());
        unsetenv("NOOK_ZYGOTE_FORCE_REINIT");
        const NookStatus reset_status =
            nook::framework::ResetZygoteControlConnectionStateForReinit();
        if (reset_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl forced reinit reset failed status=%d process=%s",
                           reset_status,
                           process_name.c_str());
            return reset_status;
        }
        if (ShouldUseHelperOnlyLocalZygoteControl()) {
            EnsureRuntimeDirectoryEnvironmentForAgent();
            const NookStatus monitor_status = nook::framework::NookZygoteMonitorReinitialize();
            if (monitor_status != NOOK_STATUS_OK) {
                NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl forced reinit helper-only local monitor failed status=%d process=%s",
                               monitor_status,
                               process_name.c_str());
                return monitor_status;
            }
            NOOK_COMM_LOGI("NookAgentInitializeForZygoteControl forced reinit helper-only local monitor ok process=%s",
                           process_name.c_str());
            MarkCurrentZygoteAgentReinitCapable();
            return NOOK_STATUS_OK;
        }
        const NookStatus control_status =
            nook::framework::EnsureControlChannelReadyForCurrentProcess();
        if (control_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl forced reinit control channel failed status=%d process=%s",
                           control_status,
                           process_name.c_str());
            return control_status;
        }
        const NookStatus monitor_status = nook::framework::NookZygoteMonitorReinitialize();
        if (monitor_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl forced reinit monitor failed status=%d process=%s",
                           monitor_status,
                           process_name.c_str());
            return monitor_status;
        }
        NOOK_COMM_LOGI("NookAgentInitializeForZygoteControl forced reinit ok process=%s",
                       process_name.c_str());
        MarkCurrentZygoteAgentReinitCapable();
        return NOOK_STATUS_OK;
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    if (ShouldUseHelperOnlyLocalZygoteControl()) {
        EnsureRuntimeDirectoryEnvironmentForAgent();
        const NookStatus monitor_status = nook::framework::NookZygoteMonitorInitialize();
        if (monitor_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl helper-only local monitor init failed status=%d process=%s",
                           monitor_status,
                           process_name.c_str());
            return monitor_status;
        }

        NOOK_COMM_LOGI("NookAgentInitializeForZygoteControl helper-only local monitor ok process=%s",
                       process_name.c_str());
        MarkCurrentZygoteAgentReinitCapable();
        return NOOK_STATUS_OK;
    }
#endif

    EnsureRuntimeDirectoryEnvironmentForAgent();
    const NookStatus comm_status = NookCommInitializeImpl(true);
    if (comm_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl comm init failed status=%d process=%s",
                       comm_status,
                       process_name.c_str());
        return comm_status;
    }

    const NookStatus monitor_status = nook::framework::NookZygoteMonitorInitialize();
    if (monitor_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("NookAgentInitializeForZygoteControl monitor init failed status=%d process=%s",
                       monitor_status,
                       process_name.c_str());
        return monitor_status;
    }

    NOOK_COMM_LOGI("NookAgentInitializeForZygoteControl ok process=%s", process_name.c_str());
    MarkCurrentZygoteAgentReinitCapable();
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NOOK_AGENT_EXPORT NookStatus NookAgentInitializeForSpawnChild(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string process_name =
        ResolveStrictZygoteControlTargetProcessName(ReadProcessName());
    const bool promoted_strict_spawn_child =
        IsPromotedStrictZygoteControlSpawnChild(process_name);
    const bool early_spawn_process = LooksLikeEarlySpawnProcessNameLocal(process_name);
    NOOK_COMM_LOGI("NookAgentInitializeForSpawnChild begin process=%s", process_name.c_str());

    const NookStatus comm_status = NookCommInitialize();
    if (comm_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("NookAgentInitializeForSpawnChild comm init failed status=%d process=%s",
                       comm_status,
                       process_name.c_str());
        return comm_status;
    }

    {
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        if (g_agent_connection == nullptr) {
            NOOK_COMM_LOGE("NookAgentInitializeForSpawnChild missing connection process=%s",
                           process_name.c_str());
            return NOOK_STATUS_INTERNAL_ERROR;
        }

        if (promoted_strict_spawn_child) {
            NOOK_COMM_LOGI("NookAgentInitializeForSpawnChild skip bootstrap hook reinstall for promoted zygote-control child process=%s",
                           process_name.c_str());
        } else {
            const NookStatus bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();
            if (bootstrap_status != NOOK_STATUS_OK) {
                NOOK_COMM_LOGE("NookAgentInitializeForSpawnChild bootstrap hook failed status=%d process=%s",
                               bootstrap_status,
                               process_name.c_str());
                return bootstrap_status;
            }
        }
    }

#if defined(NOOK_ZYGOTE_HELPER_ONLY)
    const NookStatus control_status = nook::framework::NotifyZygoteControlReadyToServer();
    if (control_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("NookAgentInitializeForSpawnChild helper-only control-ready failed status=%d process=%s",
                       control_status,
                       process_name.c_str());
        return control_status;
    }
#else
    if (early_spawn_process) {
        NOOK_COMM_LOGI("NookAgentInitializeForSpawnChild defer runtime-ready for early process=%s",
                       process_name.c_str());
    } else {
        if (promoted_strict_spawn_child) {
            const bool lifecycle_ready_now =
                TrySyncApplicationLifecycleStateFromCurrentApplication(
                    "promoted-strict-spawn-child-runtime");
            if (!lifecycle_ready_now) {
                EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted();
            }
        }

        const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
        if (ready_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitializeForSpawnChild runtime-ready failed status=%d process=%s",
                           ready_status,
                           process_name.c_str());
            return ready_status;
        }

        if (promoted_strict_spawn_child) {
            PumpRuntimePendingTasksForSpawnGate("promoted-strict-spawn-child-runtime");
            EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted();
        }
    }
#endif

    NOOK_COMM_LOGI("NookAgentInitializeForSpawnChild ok process=%s", process_name.c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NOOK_AGENT_EXPORT NookStatus NookAgentReinitializeForZygoteControl(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    if (setenv("NOOK_ZYGOTE_FORCE_REINIT", "1", 1) != 0) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return NookAgentInitializeForZygoteControl();
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NOOK_AGENT_EXPORT NookStatus NookAgentUninstallZygoteControlHooks(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    nook::comm::SpawnUninstallRequest request;
    const nook::comm::SpawnUninstallResponse response =
        nook::framework::HandleSpawnUninstallRequest(request, true);
    if (response.success) {
        return NOOK_STATUS_OK;
    }
    if (response.error.code != 0) {
        return static_cast<NookStatus>(response.error.code);
    }
    return NOOK_STATUS_INTERNAL_ERROR;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NOOK_AGENT_EXPORT NookStatus NookAgentInitialize(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string process_name = ReadProcessName();
    const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    const char* env_strict_request = std::getenv("NOOK_STRICT_ZYGOTE_REQUEST");
    const bool promoted_strict_spawn_child =
        LooksLikeEarlySpawnProcessNameLocal(process_name) &&
        IsPromotedStrictZygoteControlSpawnChild(process_name);
    NOOK_COMM_LOGI("NookAgentInitialize begin process=%s spawn_token_set=%d strict_request=%s promoted=%d",
                   process_name.c_str(),
                   (env_spawn_token != nullptr && env_spawn_token[0] != '\0') ? 1 : 0,
                   env_strict_request != nullptr ? env_strict_request : "",
                   promoted_strict_spawn_child ? 1 : 0);
    if (LooksLikeEarlySpawnProcessNameLocal(process_name)) {
        if (env_spawn_token != nullptr && env_spawn_token[0] != '\0' &&
            !promoted_strict_spawn_child) {
            NOOK_COMM_LOGI("NookAgentInitialize defer inherited child connect for early process=%s token=%s",
                           process_name.c_str(),
                           env_spawn_token);
            return NOOK_STATUS_OK;
        }
    }

    const NookStatus comm_status = NookCommInitialize();
    if (comm_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("NookAgentInitialize comm init failed status=%d process=%s",
                       comm_status,
                       ReadProcessName().c_str());
        return comm_status;
    }

    {
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        if (g_agent_connection == nullptr) {
            NOOK_COMM_LOGI("NookAgentInitialize deferred for process=%s",
                           ReadProcessName().c_str());
            return NOOK_STATUS_OK;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        NOOK_COMM_LOGI("NookAgentInitialize bootstrap hook begin process=%s",
                       ReadProcessName().c_str());
        const NookStatus bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();
        if (bootstrap_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitialize bootstrap hook failed status=%d process=%s",
                           bootstrap_status,
                           ReadProcessName().c_str());
            return bootstrap_status;
        }
    }
    NOOK_COMM_LOGI("NookAgentInitialize bootstrap hook ok process=%s",
                   ReadProcessName().c_str());

    if (!LooksLikeEarlySpawnProcessNameLocal(process_name)) {
#if defined(NOOK_ZYGOTE_HELPER_ONLY)
        const NookStatus control_ready_status = nook::framework::NotifyZygoteControlReadyToServer();
        if (control_ready_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitialize helper child control-ready failed status=%d process=%s",
                           control_ready_status,
                           process_name.c_str());
            return control_ready_status;
        }
        NOOK_COMM_LOGI("NookAgentInitialize helper child control-ready ok process=%s",
                       process_name.c_str());
#else
        const NookStatus ready_status = EnsureRuntimeBridgeAndReady();
        if (ready_status != NOOK_STATUS_OK) {
            NOOK_COMM_LOGE("NookAgentInitialize ready path failed status=%d process=%s",
                           ready_status,
                           process_name.c_str());
            return ready_status;
        }
#endif
    } else {
        NOOK_COMM_LOGI("NookAgentInitialize defer runtime bridge/ready for early process=%s",
                       process_name.c_str());
    }

    NOOK_COMM_LOGI("NookAgentInitialize ok process=%s", ReadProcessName().c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommInitialize(void) {
    return NookCommInitializeImpl(false);
}
NookStatus NookCommWaitForResumeIfSpawned(void) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::unique_lock<std::mutex> gate_lock(g_spawn_gate_mutex);
    if (!g_spawn_gate_armed || g_spawn_gate_released) {
        return NOOK_STATUS_OK;
    }

    NOOK_COMM_LOGI("waiting for spawn gate release pid=%u", g_spawn_gate_pid);
    g_spawn_gate_cv.wait(gate_lock, []() {
        return !g_spawn_gate_armed || g_spawn_gate_released;
    });
    NOOK_COMM_LOGI("spawn gate released pid=%u", g_spawn_gate_pid);
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommSendMessage(const char* message_json,
                               const uint8_t* data,
                               size_t data_len) {
    if (message_json == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGE("send message before init");
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    nook::comm::ScriptMessage message;
    message.script_id = 0;
    message.message = message_json;
    if (data != nullptr && data_len > 0) {
        message.data.assign(data, data + data_len);
    }

    return g_agent_connection->SendScriptMessage(message)
               ? NOOK_STATUS_OK
               : NOOK_STATUS_INTERNAL_ERROR;
#else
    (void)data;
    (void)data_len;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommSetMessageCallback(NookCommMessageCallback callback) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    g_message_callback = callback;
    ApplyAgentCallbacksLocked();
    return NOOK_STATUS_OK;
#else
    (void)callback;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommSetScriptCreateCallback(NookCommScriptCreateCallback callback) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    g_script_create_callback = callback;
    ApplyAgentCallbacksLocked();
    return NOOK_STATUS_OK;
#else
    (void)callback;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommSetScriptLoadCallback(NookCommScriptLoadCallback callback) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    g_script_load_callback = callback;
    ApplyAgentCallbacksLocked();
    return NOOK_STATUS_OK;
#else
    (void)callback;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommSetScriptUnloadCallback(NookCommScriptUnloadCallback callback) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    g_script_unload_callback = callback;
    ApplyAgentCallbacksLocked();
    return NOOK_STATUS_OK;
#else
    (void)callback;
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookCommRegisterRpc(const char* method, NookCommRpcHandler handler) {
    if (method == nullptr || handler == nullptr || method[0] == '\0') {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    g_rpc_handlers[method] = handler;
    ApplyAgentCallbacksLocked();
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

}

namespace nook {
namespace framework {

void RefreshAgentCallbacksForInternalRpc() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    NOOK_COMM_LOGI("refresh agent callbacks for internal rpc has_internal=%d has_public=%d connection=%d",
                   HasInternalRpcRequestHandlers() ? 1 : 0,
                   !g_rpc_handlers.empty() ? 1 : 0,
                   g_agent_connection != nullptr ? 1 : 0);
    ApplyAgentCallbacksLocked();
#endif
}

NookStatus EnsureControlChannelReadyForCurrentProcess() {
#if defined(__ANDROID__) && !defined(_WIN32)
    const NookStatus comm_status = NookCommInitializeImpl(true);
    if (comm_status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("ensure control channel comm init failed status=%d process=%s",
                       comm_status,
                       ReadProcessName().c_str());
        return comm_status;
    }

    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGE("ensure control channel failed: agent connection unavailable process=%s",
                       ReadProcessName().c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    g_agent_connection->StartRecvLoop();
    NOOK_COMM_LOGI("ensure control channel ok without AGENT_READY process=%s pid=%u",
                   ReadProcessName().c_str(),
                   static_cast<uint32_t>(getpid()));
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus EnsureFullAgentReadyForCurrentProcess() {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string process_name = ReadProcessName();
    NOOK_COMM_LOGI("ensure full agent ready begin process=%s", process_name.c_str());

    const NookStatus status = NookAgentInitialize();
    if (status != NOOK_STATUS_OK) {
        NOOK_COMM_LOGE("ensure full agent ready failed status=%d process=%s",
                       status,
                       process_name.c_str());
        return status;
    }

    NOOK_COMM_LOGI("ensure full agent ready ok process=%s", ReadProcessName().c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NotifyZygoteControlReadyToServer() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGE("notify zygote control ready failed: agent connection unavailable process=%s",
                       ReadProcessName().c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    ApplyAgentCallbacksLocked();
    ApplySpawnGateResumeHandlerLocked(static_cast<uint32_t>(getpid()));
    g_agent_connection->StartRecvLoop();

    const std::string process_name = ReadProcessName();
    if (!SendAgentReadyLocked(process_name,
                              static_cast<uint32_t>(getpid()),
                              nook::comm::AgentReadyStage::kControl)) {
        NOOK_COMM_LOGE("notify zygote control ready failed: send AGENT_READY failed process=%s",
                       process_name.c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    NOOK_COMM_LOGI("notify zygote control ready ok process=%s", process_name.c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus ResetZygoteControlConnectionStateForReinit() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    NOOK_COMM_LOGI("reset zygote-control connection state for reinit process=%s connected=%d suspended=%d",
                   ReadProcessName().c_str(),
                   (g_agent_connection != nullptr && g_agent_connection->IsConnected()) ? 1 : 0,
                   g_agent_connection_suspended_for_fork ? 1 : 0);
    if (g_agent_connection != nullptr) {
        g_agent_connection.reset();
    }
    g_highest_agent_ready_stage_sent = -1;
    g_agent_connection_suspended_for_fork = false;
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

void SetInternalRpcRequestHandler(RpcRequestHandler handler) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (handler) {
        RegisterInternalRpcRequestHandler("*", std::move(handler));
    } else {
        UnregisterInternalRpcRequestHandler("*");
    }
    ApplyAgentCallbacksLocked();
#else
    (void)handler;
#endif
}

void RequestControlChannelDisconnectAfterCurrentReply(const char* reason) {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (g_agent_connection == nullptr) {
        NOOK_COMM_LOGI("request control-channel disconnect skipped process=%s reason=%s connection=0",
                       ReadProcessName().c_str(),
                       (reason != nullptr && reason[0] != '\0') ? reason : "unspecified");
        return;
    }

    NOOK_COMM_LOGI("request control-channel disconnect process=%s reason=%s",
                   ReadProcessName().c_str(),
                   (reason != nullptr && reason[0] != '\0') ? reason : "unspecified");
    g_agent_connection->RequestDisconnectAfterCurrentReply();
#else
    (void)reason;
#endif
}

bool IsCurrentProcessSpawnGateHeld() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
    return (g_spawn_gate_armed && !g_spawn_gate_released) ||
           (g_strict_child_native_gate_armed && !g_strict_child_native_gate_released);
#else
    return false;
#endif
}

bool IsCurrentProcessStrictLifecycleSpawnGateHeld() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> gate_lock(g_spawn_gate_mutex);
    return g_spawn_gate_armed &&
           !g_spawn_gate_released &&
           g_strict_child_native_gate_armed &&
           !g_strict_child_native_gate_released;
#else
    return false;
#endif
}

void ScheduleControlChannelHardCloseAfterDelay(const char* reason, uint32_t delay_ms) {
#if defined(__ANDROID__) && !defined(_WIN32)
    const std::string reason_text =
        (reason != nullptr && reason[0] != '\0') ? reason : "unspecified";
    NOOK_COMM_LOGI("schedule control-channel hard close process=%s reason=%s delay_ms=%u",
                   ReadProcessName().c_str(),
                   reason_text.c_str(),
                   static_cast<unsigned>(delay_ms));
    std::thread([reason_text, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        std::lock_guard<std::mutex> lock(g_comm_mutex);
        if (g_agent_connection == nullptr) {
            NOOK_COMM_LOGI("control-channel hard close skipped process=%s reason=%s connection=0",
                           ReadProcessName().c_str(),
                           reason_text.c_str());
            return;
        }
        NOOK_COMM_LOGI("control-channel hard close execute process=%s reason=%s",
                       ReadProcessName().c_str(),
                       reason_text.c_str());
        g_agent_connection.reset();
        g_highest_agent_ready_stage_sent = -1;
        g_agent_connection_suspended_for_fork = false;
    }).detach();
#else
    (void)reason;
    (void)delay_ms;
#endif
}

bool SuspendAgentConnectionForFork() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);

    // The zygote process must not retain app-owned JNI loader state across
    // fork/specialize boundaries; inherited refs can become stale in later forks.
    JavaHookLoaderResolver::ResetInheritedApplicationLoaderState();

    if (g_agent_connection == nullptr) {
        return true;
    }

    NOOK_COMM_LOGI("suspend agent connection for fork process=%s",
                   ReadProcessName().c_str());
    g_agent_connection.reset();
    g_highest_agent_ready_stage_sent = -1;
    g_agent_connection_suspended_for_fork = true;
    return true;
#else
    return false;
#endif
}

bool ResumeAgentConnectionAfterFork() {
#if defined(__ANDROID__) && !defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_comm_mutex);
    if (!g_agent_connection_suspended_for_fork) {
        return true;
    }
    if (g_agent_connection != nullptr) {
        g_agent_connection_suspended_for_fork = false;
        return true;
    }

    auto transport = std::make_unique<nook::comm::UnixTransport>(nook::comm::GetDefaultSocketPath());
    auto connection = std::make_unique<nook::comm::AgentConnection>(std::move(transport));
    if (!connection->Connect()) {
        NOOK_COMM_LOGE("resume agent connection after fork failed path=%s",
                       nook::comm::GetDefaultSocketPath().c_str());
        return false;
    }

    const uint32_t pid = static_cast<uint32_t>(getpid());
    g_agent_connection = std::move(connection);
    ApplyAgentCallbacksLocked();
    ApplySpawnGateResumeHandlerLocked(pid);
    g_agent_connection_suspended_for_fork = false;
    NOOK_COMM_LOGI("resume agent connection after fork ok process=%s",
                   ReadProcessName().c_str());
    return true;
#else
    return false;
#endif
}

}  // namespace framework
}  // namespace nook

#undef NOOK_AGENT_EXPORT

