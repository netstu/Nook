#include "framework/nook_zygote_control.h"

#include "NookCommInternal.h"
#include "framework/nook_agent_init_policy.h"
#include "framework/NookZygoteSpawn.h"
#include "nook/NookAgent.h"
#include "nook/NookInlineHook.h"
#include "nook/NookJavaHook.h"
#include "NookJavaHookInternal.h"

#include <atomic>
#include <array>
#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#endif

namespace nook {
namespace framework {
namespace {

#if defined(__ANDROID__)
constexpr const char* kZygoteControlTag = "NookZygote";
#define NOOK_ZYGOTE_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kZygoteControlTag, __VA_ARGS__))
#define NOOK_ZYGOTE_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, kZygoteControlTag, __VA_ARGS__))
#else
#define NOOK_ZYGOTE_LOGI(...) ((void)0)
#define NOOK_ZYGOTE_LOGE(...) ((void)0)
#endif

constexpr const char* kZygoteClass = "com.android.internal.os.Zygote";
constexpr const char* kForkAndSpecializeMethod = "forkAndSpecialize";
constexpr const char* kForkAndSpecializeSignature = "*";
constexpr const char* kNativeForkAndSpecializeMethod = "nativeForkAndSpecialize";
constexpr const char* kNativeForkAndSpecializeSignature = "*";
constexpr const char* kSpecializeAppProcessMethod = "specializeAppProcess";
constexpr const char* kSpecializeAppProcessSignature = "*";
constexpr const char* kNativeSpecializeAppProcessMethod = "nativeSpecializeAppProcess";
constexpr const char* kNativeSpecializeAppProcessSignature = "*";
constexpr const char* kLibcModule = "libc.so";
constexpr const char* kAndroidRuntimeModule = "libandroid_runtime.so";
constexpr const char* kSelinuxModule = "libselinux.so";
constexpr const char* kProcessSetArgSymbol =
    "_Z27android_os_Process_setArgV0P7_JNIEnvP8_jobjectP8_jstring";

std::mutex g_zygote_control_mutex;
ZygoteSpawnController g_spawn_controller;
std::atomic<bool> g_zygote_control_ready{false};
std::atomic<bool> g_spawn_fast_armed{false};
std::atomic<int> g_java_fork_hook_id{-1};
std::atomic<int> g_native_fork_hook_id{-1};
std::atomic<int> g_java_specialize_hook_id{-1};
std::atomic<int> g_specialize_hook_id{-1};
std::atomic<bool> g_hooks_installed{false};
std::atomic<bool> g_child_specialize_hooks_installed{false};
using ForkFn = pid_t (*)();
using VForkFn = pid_t (*)();
using ProcessSetArgFn = void (*)(JNIEnv*, jobject, jstring);
using SelinuxSetContextFn = int (*)(uid_t, bool, const char*, const char*);
ForkFn g_orig_fork = nullptr;
VForkFn g_orig_vfork = nullptr;
ProcessSetArgFn g_orig_process_set_arg = nullptr;
SelinuxSetContextFn g_orig_selinux_setcontext = nullptr;
void* g_fork_hook_handle = nullptr;
void* g_vfork_hook_handle = nullptr;
void* g_process_set_arg_hook_handle = nullptr;
void* g_selinux_setcontext_hook_handle = nullptr;
std::array<char, 256> g_spawn_fast_target_package{};
std::array<char, 160> g_spawn_fast_token{};

bool IsZygoteJavaWrapperHooksEnabled() {
#if defined(__ANDROID__)
    const char* value = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

bool IsZygoteJavaNativeHooksEnabled() {
#if defined(__ANDROID__)
    const char* value = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

bool IsZygoteMonitorReadyFromEnvironment() {
#if defined(__ANDROID__)
    const char* value = std::getenv("NOOK_ZYGOTE_MONITOR_READY");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

bool ShouldUseHelperOnlyLocalZygoteControl() {
#if defined(__ANDROID__) && defined(NOOK_ZYGOTE_HELPER_ONLY)
    const char* value = std::getenv("NOOK_STRICT_ZYGOTE_CONTROL");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

bool ShouldInstallParentNativeSpecializeHooks() {
#if defined(__ANDROID__)
    if (ShouldUseHelperOnlyLocalZygoteControl()) {
        return true;
    }
#endif
    return false;
}

std::string ReadProcessNameLocal() {
#if defined(__ANDROID__)
    FILE* fp = fopen("/proc/self/cmdline", "r");
    if (fp == nullptr) {
        return {};
    }

    char cmdline[256] = {0};
    fgets(cmdline, sizeof(cmdline), fp);
    fclose(fp);
    return cmdline;
#else
    return {};
#endif
}

bool ReadJStringUtf8(JNIEnv* env, jstring value, std::string* out) {
    if (env == nullptr || out == nullptr) {
        return false;
    }
    if (value == nullptr) {
        out->clear();
        return true;
    }

    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (utf8 == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }
    *out = utf8;
    env->ReleaseStringUTFChars(value, utf8);
    return true;
}

bool ShouldActivateChildForNiceName(const std::string& nice_name,
                                    std::string* spawn_token);
bool TryConsumeOrMatchSpawnForNiceName(const std::string& nice_name,
                                       std::string* spawn_token,
                                       std::string* error_message);
void ActivateAgentInForkedChild(const std::string& process_name,
                                const std::string& spawn_token);

void ClearFastSpawnConfig() {
    g_spawn_fast_armed.store(false, std::memory_order_release);
    g_spawn_fast_target_package.fill('\0');
    g_spawn_fast_token.fill('\0');
}

void UpdateFastSpawnConfig(const std::string& target_package,
                           const std::string& spawn_token) {
    g_spawn_fast_target_package.fill('\0');
    g_spawn_fast_token.fill('\0');
    std::strncpy(g_spawn_fast_target_package.data(),
                 target_package.c_str(),
                 g_spawn_fast_target_package.size() - 1);
    std::strncpy(g_spawn_fast_token.data(),
                 spawn_token.c_str(),
                 g_spawn_fast_token.size() - 1);
    g_spawn_fast_armed.store(true, std::memory_order_release);
}

bool TryMatchFastSpawnConfig(const std::string& nice_name,
                             std::string* spawn_token,
                             std::string* error_message) {
    if (!g_spawn_fast_armed.load(std::memory_order_acquire)) {
        if (error_message != nullptr) {
            *error_message = "fast spawn config not armed";
        }
        return false;
    }

    const char* target_package = g_spawn_fast_target_package.data();
    if (target_package[0] == '\0') {
        if (error_message != nullptr) {
            *error_message = "fast target package missing";
        }
        return false;
    }
    if (nice_name != target_package) {
        if (error_message != nullptr) {
            *error_message = "fast nice mismatch";
        }
        return false;
    }

    const char* token = g_spawn_fast_token.data();
    if (token[0] == '\0') {
        if (error_message != nullptr) {
            *error_message = "fast spawn token missing";
        }
        return false;
    }

    if (spawn_token != nullptr) {
        *spawn_token = token;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

std::string ExtractNiceNameFromForkArgs(JNIEnv* env,
                                        NookJavaHookValue* args,
                                        size_t arg_count) {
    if (env == nullptr || args == nullptr || arg_count < 8u) {
        return {};
    }

    std::string nice_name;
    if (!ReadJStringUtf8(env, reinterpret_cast<jstring>(args[7].l), &nice_name)) {
        return {};
    }
    return nice_name;
}

std::string ExtractNiceNameFromSpecializeArgs(JNIEnv* env,
                                              NookJavaHookValue* args,
                                              size_t arg_count) {
    if (env == nullptr || args == nullptr || arg_count < 8u) {
        return {};
    }

    std::string nice_name;
    if (!ReadJStringUtf8(env, reinterpret_cast<jstring>(args[7].l), &nice_name)) {
        return {};
    }
    return nice_name;
}

bool TryActivateChildFromNiceName(const char* source,
                                  const std::string& nice_name) {
    if (nice_name.empty()) {
        NOOK_ZYGOTE_LOGI("%s fallback skip empty", source != nullptr ? source : "spawn");
        return false;
    }

    std::string spawn_token;
    std::string match_error;
    if (GetArmedSpawnTokenForNiceName(&g_spawn_controller, nice_name, &spawn_token, &match_error)) {
        NOOK_ZYGOTE_LOGI("%s fallback controller matched nice=%s",
                         source != nullptr ? source : "spawn",
                         nice_name.c_str());
        std::string consume_error;
        if (!g_spawn_controller.Consume(spawn_token, &consume_error)) {
            NOOK_ZYGOTE_LOGI("%s fallback consume failed nice=%s reason=%s",
                             source != nullptr ? source : "spawn",
                             nice_name.c_str(),
                             consume_error.empty() ? "consume-failed" : consume_error.c_str());
            return false;
        }
        ActivateAgentInForkedChild(nice_name, spawn_token);
        return true;
    }

    if (ShouldActivateChildForNiceName(nice_name, &spawn_token)) {
        NOOK_ZYGOTE_LOGI("%s fallback env matched nice=%s",
                         source != nullptr ? source : "spawn",
                         nice_name.c_str());
        ActivateAgentInForkedChild(nice_name, spawn_token);
        return true;
    }

    NOOK_ZYGOTE_LOGI("%s fallback ignored nice=%s reason=%s",
                     source != nullptr ? source : "spawn",
                     nice_name.c_str(),
                     match_error.empty() ? "no-match" : match_error.c_str());
    return false;
}

bool ShouldActivateChildForNiceName(const std::string& nice_name,
                                    std::string* spawn_token) {
    if (nice_name.empty()) {
        NOOK_ZYGOTE_LOGI("spawn match skip empty");
        return false;
    }

    std::string match_error;
    if (GetArmedSpawnTokenForNiceName(&g_spawn_controller, nice_name, spawn_token, &match_error)) {
        NOOK_ZYGOTE_LOGI("spawn match ok controller nice=%s", nice_name.c_str());
        return true;
    }

    std::string compatibility_error;
    if (!IsCompatibilitySpawnFallbackAllowed(&g_spawn_controller, &compatibility_error)) {
        NOOK_ZYGOTE_LOGI("spawn match skip compatibility nice=%s reason=%s",
                         nice_name.c_str(),
                         compatibility_error.empty() ? "compatibility-disabled"
                                                     : compatibility_error.c_str());
        return false;
    }

#if defined(__ANDROID__)
    const char* target_package = std::getenv("NOOK_TARGET_PACKAGE");
    const char* token = std::getenv("NOOK_SPAWN_TOKEN");
    if (target_package != nullptr && target_package[0] != '\0' &&
        token != nullptr && token[0] != '\0') {
        std::string env_error;
        if (!MatchSpawnTargetAndToken(target_package, nice_name, token, &env_error)) {
            NOOK_ZYGOTE_LOGI("spawn match skip env-mismatch nice=%s target=%s",
                             nice_name.c_str(),
                             target_package);
            return false;
        }
        if (spawn_token != nullptr) {
            *spawn_token = token;
        }
        NOOK_ZYGOTE_LOGI("spawn match ok env nice=%s", nice_name.c_str());
        return true;
    }

    if (g_spawn_fast_armed.load(std::memory_order_acquire)) {
        const char* fast_target_package = g_spawn_fast_target_package.data();
        const char* fast_token = g_spawn_fast_token.data();
        std::string fast_error;
        if (MatchSpawnTargetAndToken(fast_target_package,
                                     nice_name,
                                     fast_token,
                                     &fast_error)) {
            if (spawn_token != nullptr) {
                *spawn_token = fast_token;
            }
            NOOK_ZYGOTE_LOGI("spawn match ok fast nice=%s", nice_name.c_str());
            return true;
        }
    }

    NOOK_ZYGOTE_LOGI("spawn match skip no-target nice=%s", nice_name.c_str());
    return false;
#else
    (void)spawn_token;
    return false;
#endif
}

bool TryConsumeOrMatchSpawnForNiceName(const std::string& nice_name,
                                       std::string* spawn_token,
                                       std::string* error_message) {
    if (nice_name.empty()) {
        if (error_message != nullptr) {
            *error_message = "nice name is empty";
        }
        return false;
    }

    if (TryConsumeForNiceName(&g_spawn_controller, nice_name, spawn_token, error_message)) {
        return true;
    }

    return ShouldActivateChildForNiceName(nice_name, spawn_token);
}

bool AreJavaZygoteSpecializeHooksInstalled();
void NativeProcessSetArgHook(JNIEnv* env, jobject obj, jstring arg);
int NativeSelinuxSetContextHook(uid_t uid,
                                bool is_system_server,
                                const char* seinfo,
                                const char* name);

void UninstallInheritedHelperHooksForActivatedChild() {
    if (g_selinux_setcontext_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_selinux_setcontext_hook_handle);
        g_selinux_setcontext_hook_handle = nullptr;
        g_orig_selinux_setcontext = nullptr;
    }

    if (g_process_set_arg_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_process_set_arg_hook_handle);
        g_process_set_arg_hook_handle = nullptr;
        g_orig_process_set_arg = nullptr;
    }

    if (g_vfork_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_vfork_hook_handle);
        g_vfork_hook_handle = nullptr;
        g_orig_vfork = nullptr;
    }

    if (g_fork_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_fork_hook_handle);
        g_fork_hook_handle = nullptr;
        g_orig_fork = nullptr;
    }

    g_child_specialize_hooks_installed.store(false, std::memory_order_release);
    g_hooks_installed.store(false, std::memory_order_release);
    g_zygote_control_ready.store(false, std::memory_order_release);

    NOOK_ZYGOTE_LOGI("activated child unhooked inherited helper hooks current=%s",
                     ReadProcessNameLocal().c_str());
}

void ActivateAgentInForkedChild(const std::string& process_name,
                                const std::string& spawn_token) {
#if defined(__ANDROID__)
    // PrepareInheritedChildAgentActivation() delegates to
    // ResetInheritedConnectionStateForChild(process_name, spawn_token, true)
    // so the forked app process rebuilds inherited comm state before runtime-ready.
    PrepareInheritedChildAgentActivation(process_name, spawn_token, true);
    UninstallInheritedHelperHooksForActivatedChild();
    NOOK_ZYGOTE_LOGI("child activation prepared package=%s token=%s",
                     process_name.c_str(),
                     spawn_token.c_str());
#else
    (void) process_name;
    (void) spawn_token;
#endif
}

const char* GetArmedTargetPackageForLog() {
    static thread_local std::string snapshot_target_package;
    std::string snapshot_spawn_token;
    std::string snapshot_error;
    if (GetActiveSpawnSnapshot(&g_spawn_controller,
                               &snapshot_target_package,
                               &snapshot_spawn_token,
                               &snapshot_error)) {
        return snapshot_target_package.c_str();
    }

#if defined(__ANDROID__)
    const char* env_target_package = std::getenv("NOOK_TARGET_PACKAGE");
    if (env_target_package != nullptr && env_target_package[0] != '\0') {
        return env_target_package;
    }
#endif
    if (g_spawn_fast_target_package[0] != '\0') {
        return g_spawn_fast_target_package.data();
    }
    return "";
}

int GetSpawnTokenSetForLog() {
    std::string snapshot_target_package;
    std::string snapshot_spawn_token;
    std::string snapshot_error;
    if (GetActiveSpawnSnapshot(&g_spawn_controller,
                               &snapshot_target_package,
                               &snapshot_spawn_token,
                               &snapshot_error)) {
        return snapshot_spawn_token.empty() ? 0 : 1;
    }

#if defined(__ANDROID__)
    const char* env_spawn_token = std::getenv("NOOK_SPAWN_TOKEN");
    if (env_spawn_token != nullptr && env_spawn_token[0] != '\0') {
        return 1;
    }
#endif
    return g_spawn_fast_token[0] != '\0' ? 1 : 0;
}

bool InstallChildNativeSpecializeHooks() {
    if (g_child_specialize_hooks_installed.load(std::memory_order_acquire) &&
        g_process_set_arg_hook_handle != nullptr &&
        g_selinux_setcontext_hook_handle != nullptr) {
        NOOK_ZYGOTE_LOGI("child native specialize hooks installed fork=%d vfork=%d setArgV0=%d selinux=%d current=%s",
                         g_fork_hook_handle != nullptr ? 1 : 0,
                         g_vfork_hook_handle != nullptr ? 1 : 0,
                         g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                         g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                         ReadProcessNameLocal().c_str());
        return true;
    }

    const NookStatus process_set_arg_status =
        g_process_set_arg_hook_handle != nullptr
            ? NOOK_STATUS_OK
            : NookInlineHookSymbol(
                  kAndroidRuntimeModule,
                  kProcessSetArgSymbol,
                  reinterpret_cast<void*>(&NativeProcessSetArgHook),
                  reinterpret_cast<void**>(&g_orig_process_set_arg),
                  &g_process_set_arg_hook_handle);
    const NookStatus selinux_status =
        g_selinux_setcontext_hook_handle != nullptr
            ? NOOK_STATUS_OK
            : NookInlineHookSymbol(
                  kSelinuxModule,
                  "selinux_android_setcontext",
                  reinterpret_cast<void*>(&NativeSelinuxSetContextHook),
                  reinterpret_cast<void**>(&g_orig_selinux_setcontext),
                  &g_selinux_setcontext_hook_handle);

    const bool installed_any =
        process_set_arg_status == NOOK_STATUS_OK || selinux_status == NOOK_STATUS_OK;
    g_child_specialize_hooks_installed.store(installed_any, std::memory_order_release);
    NOOK_ZYGOTE_LOGI("child native specialize hooks installed fork=%d vfork=%d setArgV0=%d selinux=%d current=%s",
                     g_fork_hook_handle != nullptr ? 1 : 0,
                     g_vfork_hook_handle != nullptr ? 1 : 0,
                     g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                     g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                     ReadProcessNameLocal().c_str());
    return installed_any;
}

pid_t NativeForkHook() {
#if defined(__ANDROID__)
    if (g_orig_fork == nullptr) {
        NOOK_ZYGOTE_LOGE("fork hook original missing");
        return -1;
    }

    NOOK_ZYGOTE_LOGI("fork hook enter current=%s target=%s token_set=%d",
                     ReadProcessNameLocal().c_str(),
                     GetArmedTargetPackageForLog(),
                     GetSpawnTokenSetForLog());
    const bool bypass_suspend_resume = AreJavaZygoteSpecializeHooksInstalled();
    if (bypass_suspend_resume) {
        NOOK_ZYGOTE_LOGI("fork hook bypass suspend/resume current=%s javaHooks=1",
                         ReadProcessNameLocal().c_str());
    }
    const bool suspended = bypass_suspend_resume
                               ? false
                               : nook::framework::SuspendAgentConnectionForFork();
    NOOK_ZYGOTE_LOGI("fork hook call-original current=%s suspended=%d",
                     ReadProcessNameLocal().c_str(),
                     suspended ? 1 : 0);
    const pid_t pid = g_orig_fork();
    if (pid == 0) {
        NOOK_ZYGOTE_LOGI("fork hook child branch current=%s",
                         ReadProcessNameLocal().c_str());
        nook::framework::ResetInheritedForkChildConnectionState();
        InstallChildNativeSpecializeHooks();
    } else if (suspended) {
        (void)nook::framework::ResumeAgentConnectionAfterFork();
    }
    NOOK_ZYGOTE_LOGI("fork hook pid=%d current=%s suspended=%d",
                     static_cast<int>(pid),
                     ReadProcessNameLocal().c_str(),
                     suspended ? 1 : 0);
    return pid;
#else
    return -1;
#endif
}

pid_t NativeVForkHook() {
#if defined(__ANDROID__)
    if (g_orig_vfork == nullptr) {
        NOOK_ZYGOTE_LOGE("vfork hook original missing");
        return -1;
    }

    NOOK_ZYGOTE_LOGI("vfork hook enter current=%s target=%s token_set=%d",
                     ReadProcessNameLocal().c_str(),
                     GetArmedTargetPackageForLog(),
                     GetSpawnTokenSetForLog());
    const bool bypass_suspend_resume = AreJavaZygoteSpecializeHooksInstalled();
    if (bypass_suspend_resume) {
        NOOK_ZYGOTE_LOGI("vfork hook bypass suspend/resume current=%s javaHooks=1",
                         ReadProcessNameLocal().c_str());
    }
    const bool suspended = bypass_suspend_resume
                               ? false
                               : nook::framework::SuspendAgentConnectionForFork();
    NOOK_ZYGOTE_LOGI("vfork hook call-original current=%s suspended=%d",
                     ReadProcessNameLocal().c_str(),
                     suspended ? 1 : 0);
    const pid_t pid = g_orig_vfork();
    if (pid == 0) {
        NOOK_ZYGOTE_LOGI("vfork hook child branch current=%s",
                         ReadProcessNameLocal().c_str());
        nook::framework::ResetInheritedForkChildConnectionState();
        InstallChildNativeSpecializeHooks();
    } else if (suspended) {
        (void)nook::framework::ResumeAgentConnectionAfterFork();
    }
    NOOK_ZYGOTE_LOGI("vfork hook pid=%d current=%s suspended=%d",
                     static_cast<int>(pid),
                     ReadProcessNameLocal().c_str(),
                     suspended ? 1 : 0);
    return pid;
#else
    return -1;
#endif
}

void NativeProcessSetArgHook(JNIEnv* env, jobject obj, jstring arg) {
    std::string nice_name;
    (void)ReadJStringUtf8(env, arg, &nice_name);
    if (g_orig_process_set_arg != nullptr) {
        g_orig_process_set_arg(env, obj, arg);
    }

    NOOK_ZYGOTE_LOGI("setArgV0 observe current=%s",
                     ReadProcessNameLocal().c_str());
    (void)obj;
    std::string spawn_token;
    std::string match_error;
    if (!TryConsumeOrMatchSpawnForNiceName(nice_name, &spawn_token, &match_error)) {
        NOOK_ZYGOTE_LOGI("setArgV0 child ignored nice=%s",
                         nice_name.c_str());
        return;
    }
    NOOK_ZYGOTE_LOGI("setArgV0 child matched nice=%s",
                     nice_name.c_str());
    ActivateAgentInForkedChild(nice_name, spawn_token);
    (void)obj;
}

int NativeSelinuxSetContextHook(uid_t uid,
                                bool is_system_server,
                                const char* seinfo,
                                const char* name) {
    NOOK_ZYGOTE_LOGI("selinux_android_setcontext enter uid=%u system=%d seinfo=%s name=%s current=%s",
                     static_cast<unsigned>(uid),
                     is_system_server ? 1 : 0,
                     seinfo != nullptr ? seinfo : "",
                     name != nullptr ? name : "",
                     ReadProcessNameLocal().c_str());
    const std::string nice_name = name != nullptr ? name : "";
    std::string spawn_token;
    std::string match_error;
    bool preactivated_child = false;
    if (ShouldInstallParentNativeSpecializeHooks() &&
        TryConsumeOrMatchSpawnForNiceName(nice_name, &spawn_token, &match_error)) {
        NOOK_ZYGOTE_LOGI("selinux_android_setcontext child pre-matched nice=%s activation=observe-only",
                         nice_name.c_str());
        preactivated_child = true;
    }

    int result = -1;
    if (g_orig_selinux_setcontext != nullptr) {
        result = g_orig_selinux_setcontext(uid, is_system_server, seinfo, name);
    }

    NOOK_ZYGOTE_LOGI("selinux_android_setcontext observe identifier=%s result=%d current=%s",
                     name != nullptr ? name : "",
                     result,
                     ReadProcessNameLocal().c_str());

    if (preactivated_child) {
        NOOK_ZYGOTE_LOGI("selinux_android_setcontext child matched nice=%s activation=deferred-post",
                         nice_name.c_str());
        ActivateAgentInForkedChild(nice_name, spawn_token);
        return result;
    }

    NOOK_ZYGOTE_LOGI("selinux_android_setcontext match-begin");
    if (!TryConsumeOrMatchSpawnForNiceName(nice_name, &spawn_token, &match_error)) {
        NOOK_ZYGOTE_LOGI("selinux_android_setcontext child ignored nice=%s",
                         nice_name.c_str());
        return result;
    }

    NOOK_ZYGOTE_LOGI("selinux_android_setcontext child matched nice=%s activation=prepare",
                     nice_name.c_str());
    ActivateAgentInForkedChild(nice_name, spawn_token);
    return result;
}

bool InstallNativeZygoteForkMonitor() {
    const NookStatus inline_status = NookInlineHookInitialize();
    if (inline_status != NOOK_STATUS_OK) {
        NOOK_ZYGOTE_LOGE("inline hook init failed status=%d", inline_status);
        return false;
    }

    const NookStatus fork_status = NookInlineHookSymbol(
        kLibcModule,
        "fork",
        reinterpret_cast<void*>(&NativeForkHook),
        reinterpret_cast<void**>(&g_orig_fork),
        &g_fork_hook_handle);
    const NookStatus vfork_status = NookInlineHookSymbol(
        kLibcModule,
        "vfork",
        reinterpret_cast<void*>(&NativeVForkHook),
        reinterpret_cast<void**>(&g_orig_vfork),
        &g_vfork_hook_handle);

    int process_set_arg_status = NOOK_STATUS_OK;
    int selinux_status = NOOK_STATUS_OK;
    if (ShouldInstallParentNativeSpecializeHooks()) {
        process_set_arg_status =
            g_process_set_arg_hook_handle != nullptr
                ? NOOK_STATUS_OK
                : NookInlineHookSymbol(
                      kAndroidRuntimeModule,
                      kProcessSetArgSymbol,
                      reinterpret_cast<void*>(&NativeProcessSetArgHook),
                      reinterpret_cast<void**>(&g_orig_process_set_arg),
                      &g_process_set_arg_hook_handle);
        selinux_status =
            g_selinux_setcontext_hook_handle != nullptr
                ? NOOK_STATUS_OK
                : NookInlineHookSymbol(
                      kSelinuxModule,
                      "selinux_android_setcontext",
                      reinterpret_cast<void*>(&NativeSelinuxSetContextHook),
                      reinterpret_cast<void**>(&g_orig_selinux_setcontext),
                      &g_selinux_setcontext_hook_handle);
    } else {
        process_set_arg_status =
            g_process_set_arg_hook_handle != nullptr ? NOOK_STATUS_OK : -1;
        selinux_status =
            g_selinux_setcontext_hook_handle != nullptr ? NOOK_STATUS_OK : -1;
    }

    NOOK_ZYGOTE_LOGI("native zygote monitor installed fork=%d vfork=%d setArgV0=%d selinux=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d orig fork=%d vfork=%d setArgV0=%d selinux=%d",
                     fork_status,
                     vfork_status,
                     process_set_arg_status,
                     selinux_status,
                     g_fork_hook_handle != nullptr ? 1 : 0,
                     g_vfork_hook_handle != nullptr ? 1 : 0,
                     g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                     g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                     g_orig_fork != nullptr ? 1 : 0,
                     g_orig_vfork != nullptr ? 1 : 0,
                     g_orig_process_set_arg != nullptr ? 1 : 0,
                     g_orig_selinux_setcontext != nullptr ? 1 : 0);

    return fork_status == NOOK_STATUS_OK ||
           vfork_status == NOOK_STATUS_OK ||
           process_set_arg_status == NOOK_STATUS_OK ||
           selinux_status == NOOK_STATUS_OK;
}

int NativeForkAndSpecializeHookCallback(JNIEnv* env,
                                        jobject thiz,
                                        NookJavaHookValue* args,
                                        size_t arg_count,
                                        NookJavaHookValue* result);
int ForkAndSpecializeHookCallback(JNIEnv* env,
                                  jobject thiz,
                                  NookJavaHookValue* args,
                                  size_t arg_count,
                                  NookJavaHookValue* result);
int NativeSpecializeAppProcessHookCallback(JNIEnv* env,
                                           jobject thiz,
                                           NookJavaHookValue* args,
                                           size_t arg_count,
                                           NookJavaHookValue* result);
int SpecializeAppProcessHookCallback(JNIEnv* env,
                                     jobject thiz,
                                     NookJavaHookValue* args,
                                     size_t arg_count,
                                     NookJavaHookValue* result);

bool InstallJavaZygoteSpecializeHooks() {
    const bool enable_java_native_hooks = IsZygoteJavaNativeHooksEnabled();
    const bool enable_java_wrapper_hooks = IsZygoteJavaWrapperHooksEnabled();
#if defined(__ANDROID__)
    const char* native_env_value = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
    const char* wrapper_env_value = std::getenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
    NOOK_ZYGOTE_LOGI("zygote java hook env native=%s wrapper=%s process=%s",
                     native_env_value != nullptr ? native_env_value : "",
                     wrapper_env_value != nullptr ? wrapper_env_value : "",
                     ReadProcessNameLocal().c_str());
#endif
    if (!enable_java_native_hooks && !enable_java_wrapper_hooks) {
        NOOK_ZYGOTE_LOGI("zygote java specialize hooks disabled by default; skip JavaHook init");
        return false;
    }

    const NookStatus java_status = NookJavaHookInitialize();
    if (java_status != NOOK_STATUS_OK) {
        NOOK_ZYGOTE_LOGE("zygote java hook init failed status=%d", java_status);
        return false;
    }

    bool installed_any = false;

    if (enable_java_native_hooks) {
        int native_fork_hook_id = NookJavaHookHook(
            kZygoteClass,
            kNativeForkAndSpecializeMethod,
            kNativeForkAndSpecializeSignature,
            1,
            &NativeForkAndSpecializeHookCallback);
        if (native_fork_hook_id < 0) {
            native_fork_hook_id = NookJavaHookHookDeferred(
                kZygoteClass,
                kNativeForkAndSpecializeMethod,
                kNativeForkAndSpecializeSignature,
                1,
                &NativeForkAndSpecializeHookCallback);
        }
        if (native_fork_hook_id >= 0) {
            g_native_fork_hook_id.store(native_fork_hook_id, std::memory_order_release);
            installed_any = true;
        } else {
            NOOK_ZYGOTE_LOGE("install nativeForkAndSpecialize hook failed hook=%d",
                             native_fork_hook_id);
        }

        int native_specialize_hook_id = NookJavaHookHook(
            kZygoteClass,
            kNativeSpecializeAppProcessMethod,
            kNativeSpecializeAppProcessSignature,
            1,
            &NativeSpecializeAppProcessHookCallback);
        if (native_specialize_hook_id < 0) {
            native_specialize_hook_id = NookJavaHookHookDeferred(
                kZygoteClass,
                kNativeSpecializeAppProcessMethod,
                kNativeSpecializeAppProcessSignature,
                1,
                &NativeSpecializeAppProcessHookCallback);
        }
        if (native_specialize_hook_id >= 0) {
            g_specialize_hook_id.store(native_specialize_hook_id, std::memory_order_release);
            installed_any = true;
        } else {
            NOOK_ZYGOTE_LOGE("install nativeSpecializeAppProcess hook failed hook=%d",
                             native_specialize_hook_id);
        }
    } else {
        NOOK_ZYGOTE_LOGI("zygote java native hooks disabled; wrapper hooks remain enabled");
    }

    if (enable_java_wrapper_hooks) {
        int java_fork_hook_id = NookJavaHookHook(
            kZygoteClass,
            kForkAndSpecializeMethod,
            kForkAndSpecializeSignature,
            1,
            &ForkAndSpecializeHookCallback);
        if (java_fork_hook_id < 0) {
            java_fork_hook_id = NookJavaHookHookDeferred(
                kZygoteClass,
                kForkAndSpecializeMethod,
                kForkAndSpecializeSignature,
                1,
                &ForkAndSpecializeHookCallback);
        }
        if (java_fork_hook_id >= 0) {
            g_java_fork_hook_id.store(java_fork_hook_id, std::memory_order_release);
            installed_any = true;
        } else {
            NOOK_ZYGOTE_LOGE("install forkAndSpecialize wrapper hook failed hook=%d",
                             java_fork_hook_id);
        }

        int java_specialize_hook_id = NookJavaHookHook(
            kZygoteClass,
            kSpecializeAppProcessMethod,
            kSpecializeAppProcessSignature,
            1,
            &SpecializeAppProcessHookCallback);
        if (java_specialize_hook_id < 0) {
            java_specialize_hook_id = NookJavaHookHookDeferred(
                kZygoteClass,
                kSpecializeAppProcessMethod,
                kSpecializeAppProcessSignature,
                1,
                &SpecializeAppProcessHookCallback);
        }
        if (java_specialize_hook_id >= 0) {
            g_java_specialize_hook_id.store(java_specialize_hook_id, std::memory_order_release);
            installed_any = true;
        } else {
            NOOK_ZYGOTE_LOGE("install specializeAppProcess wrapper hook failed hook=%d",
                             java_specialize_hook_id);
        }
    } else {
        NOOK_ZYGOTE_LOGI("zygote java wrapper hooks disabled by default; native fork/vfork monitor active");
    }

    NOOK_ZYGOTE_LOGI("zygote java specialize hooks install result nativeFork=%d nativeSpecialize=%d javaFork=%d javaSpecialize=%d",
                     g_native_fork_hook_id.load(std::memory_order_acquire),
                     g_specialize_hook_id.load(std::memory_order_acquire),
                     g_java_fork_hook_id.load(std::memory_order_acquire),
                     g_java_specialize_hook_id.load(std::memory_order_acquire));
    return installed_any;
}

bool AreJavaZygoteSpecializeHooksInstalled() {
    return g_native_fork_hook_id.load(std::memory_order_acquire) >= 0 ||
           g_specialize_hook_id.load(std::memory_order_acquire) >= 0 ||
           g_java_fork_hook_id.load(std::memory_order_acquire) >= 0 ||
           g_java_specialize_hook_id.load(std::memory_order_acquire) >= 0;
}

bool EnsureJavaZygoteSpecializeHooksInstalled() {
    if (AreJavaZygoteSpecializeHooksInstalled()) {
        return true;
    }
    return InstallJavaZygoteSpecializeHooks();
}

int NativeForkAndSpecializeHookCallback(JNIEnv* env,
                                        jobject thiz,
                                        NookJavaHookValue* args,
                                        size_t arg_count,
                                        NookJavaHookValue* result) {
    (void) thiz;
    (void) env;
    (void) args;
    NOOK_ZYGOTE_LOGI("nativeForkAndSpecialize enter arg_count=%zu current=%s",
                     arg_count,
                     ReadProcessNameLocal().c_str());
    int installed_hook_id = -1;
    if (!nook::java_hook_internal::ResolveInstalledHookId(g_native_fork_hook_id.load(std::memory_order_acquire),
                                                          &installed_hook_id)) {
        NOOK_ZYGOTE_LOGE("nativeForkAndSpecialize resolve hook id failed");
        return 1;
    }

    const bool suspended = nook::framework::SuspendAgentConnectionForFork();
    NookJavaHookValue original_result = {};
    if (!nook::java_hook_internal::CallOriginalNow(installed_hook_id,
                                                   env,
                                                   thiz,
                                                   args,
                                                   arg_count,
                                                   &original_result)) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        NOOK_ZYGOTE_LOGE("nativeForkAndSpecialize callOriginal failed");
        if (result != nullptr) {
            result->i = -1;
        }
        return 0;
    }

    if (result != nullptr) {
        *result = original_result;
    }

    if (static_cast<int>(original_result.i) != 0) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        return 0;
    }
    const std::string matched_process_name = ExtractNiceNameFromForkArgs(env, args, arg_count);
    (void)TryActivateChildFromNiceName("nativeForkAndSpecialize", matched_process_name);
    NOOK_ZYGOTE_LOGI("nativeForkAndSpecialize child forked current=%s; defer activation to safer hooks",
                     ReadProcessNameLocal().c_str());
    return 0;
}

int ForkAndSpecializeHookCallback(JNIEnv* env,
                                  jobject thiz,
                                  NookJavaHookValue* args,
                                  size_t arg_count,
                                  NookJavaHookValue* result) {
    (void) thiz;
    (void) env;
    (void) args;
    NOOK_ZYGOTE_LOGI("forkAndSpecialize enter arg_count=%zu current=%s",
                     arg_count,
                     ReadProcessNameLocal().c_str());
    int installed_hook_id = -1;
    if (!nook::java_hook_internal::ResolveInstalledHookId(g_java_fork_hook_id.load(std::memory_order_acquire),
                                                          &installed_hook_id)) {
        NOOK_ZYGOTE_LOGE("forkAndSpecialize resolve hook id failed");
        return 1;
    }

    const bool suspended = nook::framework::SuspendAgentConnectionForFork();
    NookJavaHookValue original_result = {};
    if (!nook::java_hook_internal::CallOriginalNow(installed_hook_id,
                                                   env,
                                                   thiz,
                                                   args,
                                                   arg_count,
                                                   &original_result)) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        NOOK_ZYGOTE_LOGE("forkAndSpecialize callOriginal failed");
        if (result != nullptr) {
            result->i = -1;
        }
        return 0;
    }

    if (result != nullptr) {
        *result = original_result;
    }

    if (static_cast<int>(original_result.i) != 0) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        return 0;
    }
    const std::string matched_process_name = ExtractNiceNameFromForkArgs(env, args, arg_count);
    (void)TryActivateChildFromNiceName("forkAndSpecialize", matched_process_name);
    NOOK_ZYGOTE_LOGI("forkAndSpecialize child forked current=%s; defer activation to safer hooks",
                     ReadProcessNameLocal().c_str());
    return 0;
}

int NativeSpecializeAppProcessHookCallback(JNIEnv* env,
                                           jobject thiz,
                                           NookJavaHookValue* args,
                                           size_t arg_count,
                                           NookJavaHookValue* result) {
    (void) result;
    (void) env;
    (void) thiz;
    (void) args;
    NOOK_ZYGOTE_LOGI("nativeSpecializeAppProcess enter arg_count=%zu current=%s",
                     arg_count,
                     ReadProcessNameLocal().c_str());
    int installed_hook_id = -1;
    if (!nook::java_hook_internal::ResolveInstalledHookId(g_specialize_hook_id.load(std::memory_order_acquire),
                                                          &installed_hook_id)) {
        NOOK_ZYGOTE_LOGE("nativeSpecializeAppProcess resolve hook id failed");
        return 1;
    }

    const std::string matched_process_name =
        ExtractNiceNameFromSpecializeArgs(env, args, arg_count);
    if (ShouldInstallParentNativeSpecializeHooks()) {
        (void)TryActivateChildFromNiceName("nativeSpecializeAppProcess-pre",
                                           matched_process_name);
        NOOK_ZYGOTE_LOGI("nativeSpecializeAppProcess pre-original activation probe current=%s",
                         ReadProcessNameLocal().c_str());
    }

    const bool suspended = nook::framework::SuspendAgentConnectionForFork();
    NookJavaHookValue original_result = {};
    if (!nook::java_hook_internal::CallOriginalNow(installed_hook_id,
                                                   env,
                                                   thiz,
                                                   args,
                                                   arg_count,
                                                   &original_result)) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        NOOK_ZYGOTE_LOGE("nativeSpecializeAppProcess callOriginal failed");
        return 0;
    }

    if (suspended) {
        (void)nook::framework::ResumeAgentConnectionAfterFork();
    }
    (void)TryActivateChildFromNiceName("nativeSpecializeAppProcess",
                                       matched_process_name);
    NOOK_ZYGOTE_LOGI("nativeSpecializeAppProcess post-original current=%s; defer activation to safer hooks",
                     ReadProcessNameLocal().c_str());
    return 0;
}

int SpecializeAppProcessHookCallback(JNIEnv* env,
                                     jobject thiz,
                                     NookJavaHookValue* args,
                                     size_t arg_count,
                                     NookJavaHookValue* result) {
    (void) result;
    (void) thiz;
    (void) env;
    (void) args;
    NOOK_ZYGOTE_LOGI("specializeAppProcess enter arg_count=%zu current=%s",
                     arg_count,
                     ReadProcessNameLocal().c_str());
    int installed_hook_id = -1;
    if (!nook::java_hook_internal::ResolveInstalledHookId(g_java_specialize_hook_id.load(std::memory_order_acquire),
                                                          &installed_hook_id)) {
        NOOK_ZYGOTE_LOGE("specializeAppProcess resolve hook id failed");
        return 1;
    }

    const std::string matched_process_name =
        ExtractNiceNameFromSpecializeArgs(env, args, arg_count);
    if (ShouldInstallParentNativeSpecializeHooks()) {
        (void)TryActivateChildFromNiceName("specializeAppProcess-pre",
                                           matched_process_name);
        NOOK_ZYGOTE_LOGI("specializeAppProcess pre-original activation probe current=%s",
                         ReadProcessNameLocal().c_str());
    }

    const bool suspended = nook::framework::SuspendAgentConnectionForFork();
    NookJavaHookValue original_result = {};
    if (!nook::java_hook_internal::CallOriginalNow(installed_hook_id,
                                                   env,
                                                   thiz,
                                                   args,
                                                   arg_count,
                                                   &original_result)) {
        if (suspended) {
            (void)nook::framework::ResumeAgentConnectionAfterFork();
        }
        NOOK_ZYGOTE_LOGE("specializeAppProcess callOriginal failed");
        return 0;
    }

    if (suspended) {
        (void)nook::framework::ResumeAgentConnectionAfterFork();
    }
    (void)TryActivateChildFromNiceName("specializeAppProcess",
                                       matched_process_name);
    NOOK_ZYGOTE_LOGI("specializeAppProcess post-original current=%s; defer activation to safer hooks",
                     ReadProcessNameLocal().c_str());
    return 0;
}

bool EnsureZygoteHooksInstalled() {
    if (g_hooks_installed.load(std::memory_order_acquire)) {
        NOOK_ZYGOTE_LOGI("zygote hooks install short-circuit handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d",
                         g_fork_hook_handle != nullptr ? 1 : 0,
                         g_vfork_hook_handle != nullptr ? 1 : 0,
                         g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                         g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                         g_native_fork_hook_id.load(std::memory_order_acquire),
                         g_specialize_hook_id.load(std::memory_order_acquire),
                         g_java_fork_hook_id.load(std::memory_order_acquire),
                         g_java_specialize_hook_id.load(std::memory_order_acquire));
        return true;
    }

    const bool native_monitor_ok = InstallNativeZygoteForkMonitor();
    const bool java_hooks_ok = InstallJavaZygoteSpecializeHooks();
    g_hooks_installed.store(true, std::memory_order_release);
    NOOK_ZYGOTE_LOGI("zygote hooks installed javaHooks=%d nativeMonitor=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d",
                     java_hooks_ok ? 1 : 0,
                     native_monitor_ok ? 1 : 0,
                     g_fork_hook_handle != nullptr ? 1 : 0,
                     g_vfork_hook_handle != nullptr ? 1 : 0,
                     g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                     g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                     g_native_fork_hook_id.load(std::memory_order_acquire),
                     g_specialize_hook_id.load(std::memory_order_acquire),
                     g_java_fork_hook_id.load(std::memory_order_acquire),
                     g_java_specialize_hook_id.load(std::memory_order_acquire));
    return native_monitor_ok || java_hooks_ok;
}

void UninstallZygoteHooksLocked() {
    const int native_fork_hook_id = g_native_fork_hook_id.exchange(-1, std::memory_order_acq_rel);
    if (native_fork_hook_id >= 0) {
        (void)NookJavaHookUnhook(native_fork_hook_id);
    }

    const int native_specialize_hook_id =
        g_specialize_hook_id.exchange(-1, std::memory_order_acq_rel);
    if (native_specialize_hook_id >= 0) {
        (void)NookJavaHookUnhook(native_specialize_hook_id);
    }

    const int java_fork_hook_id = g_java_fork_hook_id.exchange(-1, std::memory_order_acq_rel);
    if (java_fork_hook_id >= 0) {
        (void)NookJavaHookUnhook(java_fork_hook_id);
    }

    const int java_specialize_hook_id =
        g_java_specialize_hook_id.exchange(-1, std::memory_order_acq_rel);
    if (java_specialize_hook_id >= 0) {
        (void)NookJavaHookUnhook(java_specialize_hook_id);
    }

    if (g_selinux_setcontext_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_selinux_setcontext_hook_handle);
        g_selinux_setcontext_hook_handle = nullptr;
        g_orig_selinux_setcontext = nullptr;
    }

    if (g_vfork_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_vfork_hook_handle);
        g_vfork_hook_handle = nullptr;
        g_orig_vfork = nullptr;
    }

    if (g_fork_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_fork_hook_handle);
        g_fork_hook_handle = nullptr;
        g_orig_fork = nullptr;
    }

    if (g_process_set_arg_hook_handle != nullptr) {
        (void)NookInlineUnhook(g_process_set_arg_hook_handle);
        g_process_set_arg_hook_handle = nullptr;
        g_orig_process_set_arg = nullptr;
    }

    g_child_specialize_hooks_installed.store(false, std::memory_order_release);
    g_hooks_installed.store(false, std::memory_order_release);
    g_zygote_control_ready.store(false, std::memory_order_release);
#if defined(__ANDROID__)
    unsetenv("NOOK_ZYGOTE_MONITOR_READY");
#endif
}

comm::RpcResponse HandleStatusRpc(const comm::RpcRequest& request);
comm::RpcResponse HandleInstallForkHookRpc(const comm::RpcRequest& request);
comm::RpcResponse HandleClearForkHookRpc(const comm::RpcRequest& request);
comm::RpcResponse HandleUninstallForkHookRpc(const comm::RpcRequest& request);

comm::RpcResponse HandleZygoteControlRpc(const comm::RpcRequest& request) {
    if (request.method == "nook.spawn.status") {
        return HandleStatusRpc(request);
    }
    if (request.method == "nook.spawn.installForkHook") {
        return HandleInstallForkHookRpc(request);
    }
    if (request.method == "nook.spawn.clearForkHook") {
        return HandleClearForkHookRpc(request);
    }
    if (request.method == "nook.spawn.uninstallForkHook") {
        return HandleUninstallForkHookRpc(request);
    }

    comm::RpcResponse response;
    response.script_id = request.script_id;
    response.success = false;
    response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
    response.error.message = "internal rpc handler not found";
    return response;
}

comm::RpcResponse HandleStatusRpc(const comm::RpcRequest& request) {
    comm::RpcResponse response;
    response.script_id = request.script_id;
    response.success = true;

    std::string result_json;
    {
        std::lock_guard<std::mutex> lock(g_zygote_control_mutex);
        result_json = BuildZygoteSpawnStatusJson(
            g_zygote_control_ready.load(std::memory_order_acquire),
            g_spawn_controller);
    }
    response.result_json = result_json;
    return response;
}

}  // namespace

comm::SpawnInstallResponse HandleSpawnInstallRequest(const comm::SpawnInstallRequest& request) {
    comm::SpawnInstallResponse response;
    response.success = false;
    std::string error_message;
    if (request.mode != "stable") {
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "unsupported spawn mode";
        return response;
    }

    {
        std::lock_guard<std::mutex> lock(g_zygote_control_mutex);
        NOOK_ZYGOTE_LOGI("install fork hook entry state=%s process=%s",
                         ToString(g_spawn_controller.state()),
                         ReadProcessNameLocal().c_str());
        if (g_spawn_controller.state() == ZygoteSpawnState::kArmed ||
            g_spawn_controller.state() == ZygoteSpawnState::kConsumed) {
            g_spawn_controller.Uninstall();
            ClearFastSpawnConfig();
            NOOK_ZYGOTE_LOGI("install fork hook cleared residual controller state process=%s",
                             ReadProcessNameLocal().c_str());
        }
        if (!g_spawn_controller.Install(request.target_package, request.spawn_token, &error_message)) {
            response.error.code = static_cast<int32_t>(NOOK_STATUS_INTERNAL_ERROR);
            response.error.message = error_message;
            return response;
        }
#if defined(__ANDROID__)
        unsetenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
        unsetenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
        NOOK_ZYGOTE_LOGI("install fork hook stable mode keeps native zygote monitor path process=%s",
                         ReadProcessNameLocal().c_str());
#endif
        ClearFastSpawnConfig();
    }

    response.success = true;
    return response;
}

comm::SpawnUninstallResponse HandleSpawnUninstallRequest(const comm::SpawnUninstallRequest& request,
                                                         bool uninstall_hooks) {
    comm::SpawnUninstallResponse response;
    response.success = true;
    {
        std::lock_guard<std::mutex> lock(g_zygote_control_mutex);
        (void)request;
        g_spawn_controller.Uninstall();
        ClearFastSpawnConfig();
#if defined(__ANDROID__)
        unsetenv("NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
        unsetenv("NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
#endif
        if (uninstall_hooks) {
            UninstallZygoteHooksLocked();
        }
    }
    return response;
}

namespace {

comm::RpcResponse HandleInstallForkHookRpc(const comm::RpcRequest& request) {
    comm::RpcResponse response;
    response.script_id = request.script_id;
    response.success = false;

    comm::SpawnInstallRequest install_request;
    std::string error_message;
    if (!ParseSpawnInstallArgsJson(request.args_json,
                                   &install_request.target_package,
                                   &install_request.spawn_token,
                                   &install_request.mode,
                                   &error_message)) {
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = error_message;
        return response;
    }

    const comm::SpawnInstallResponse install_response = HandleSpawnInstallRequest(install_request);
    response.success = install_response.success;
    response.error = install_response.error;
    response.result_json = install_response.success ? "{\"ok\":true}" : "";
    return response;
}

comm::RpcResponse HandleUninstallForkHookRpc(const comm::RpcRequest& request) {
    comm::RpcResponse response;
    response.script_id = request.script_id;
    const comm::SpawnUninstallRequest uninstall_request{
        request.args_json
    };
    const comm::SpawnUninstallResponse uninstall_response =
        HandleSpawnUninstallRequest(uninstall_request, true);
    response.success = uninstall_response.success;
    response.error = uninstall_response.error;
    response.result_json = uninstall_response.success ? "{\"ok\":true}" : "";
    return response;
}

comm::RpcResponse HandleClearForkHookRpc(const comm::RpcRequest& request) {
    comm::RpcResponse response;
    response.script_id = request.script_id;
    response.success = true;
    {
        std::lock_guard<std::mutex> lock(g_zygote_control_mutex);
        (void)request;
        g_spawn_controller.Uninstall();
        ClearFastSpawnConfig();
    }
    response.result_json = "{\"ok\":true}";
    return response;
}

}  // namespace

NookStatus NookZygoteMonitorInitialize() {
#if defined(__ANDROID__) && !defined(_WIN32)
    const bool helper_only_local_control = ShouldUseHelperOnlyLocalZygoteControl();
    const bool ready_from_instance = g_zygote_control_ready.load(std::memory_order_acquire);
    const bool ready_from_env = IsZygoteMonitorReadyFromEnvironment();
    if (ready_from_instance || ready_from_env) {
        bool native_hooks_ready = g_hooks_installed.load(std::memory_order_acquire);
        if (ready_from_env && !ready_from_instance && !native_hooks_ready) {
            native_hooks_ready = EnsureZygoteHooksInstalled();
            NOOK_ZYGOTE_LOGI("zygote monitor reattach hook backfill envReady=1 instanceReady=0 installed=%d process=%s",
                             native_hooks_ready ? 1 : 0,
                             ReadProcessNameLocal().c_str());
            if (!native_hooks_ready) {
                NOOK_ZYGOTE_LOGE("zygote monitor reattach hook backfill failed process=%s",
                                 ReadProcessNameLocal().c_str());
                return NOOK_STATUS_INTERNAL_ERROR;
            }
        }

        const bool java_hooks_requested =
            IsZygoteJavaNativeHooksEnabled() || IsZygoteJavaWrapperHooksEnabled();
        bool java_hooks_ready = AreJavaZygoteSpecializeHooksInstalled();
        if (ready_from_env && !ready_from_instance && java_hooks_requested && !java_hooks_ready) {
            NOOK_ZYGOTE_LOGI("zygote monitor reattach skip java hook backfill process=%s requested=1 envReady=1",
                             ReadProcessNameLocal().c_str());
        } else if (java_hooks_requested && !java_hooks_ready) {
            java_hooks_ready = EnsureJavaZygoteSpecializeHooksInstalled();
            NOOK_ZYGOTE_LOGI("zygote monitor reinit java hook backfill requested=1 installed=%d process=%s",
                             java_hooks_ready ? 1 : 0,
                             ReadProcessNameLocal().c_str());
        }
        RegisterInternalRpcRequestHandler("*", HandleZygoteControlRpc);
        if (!helper_only_local_control) {
            RefreshAgentCallbacksForInternalRpc();

            const NookStatus notify_status = NotifyZygoteControlReadyToServer();
            if (notify_status != NOOK_STATUS_OK) {
                NOOK_ZYGOTE_LOGE("zygote monitor reattach control-ready notify failed process=%s status=%d envReady=%d instanceReady=%d",
                                 ReadProcessNameLocal().c_str(),
                                 notify_status,
                                 ready_from_env ? 1 : 0,
                                 ready_from_instance ? 1 : 0);
                return notify_status;
            }
        } else {
            NOOK_ZYGOTE_LOGI("zygote monitor reattach helper-only local control keeps zygote offline process=%s",
                             ReadProcessNameLocal().c_str());
        }

        g_zygote_control_ready.store(true, std::memory_order_release);
        NOOK_ZYGOTE_LOGI("zygote monitor init short-circuit process=%s nativeHooks=%d javaHooks=%d requested=%d envReady=%d instanceReady=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d",
                         ReadProcessNameLocal().c_str(),
                         native_hooks_ready ? 1 : 0,
                         java_hooks_ready ? 1 : 0,
                         java_hooks_requested ? 1 : 0,
                         ready_from_env ? 1 : 0,
                         ready_from_instance ? 1 : 0,
                         g_fork_hook_handle != nullptr ? 1 : 0,
                         g_vfork_hook_handle != nullptr ? 1 : 0,
                         g_process_set_arg_hook_handle != nullptr ? 1 : 0,
                         g_selinux_setcontext_hook_handle != nullptr ? 1 : 0,
                         g_native_fork_hook_id.load(std::memory_order_acquire),
                         g_specialize_hook_id.load(std::memory_order_acquire),
                         g_java_fork_hook_id.load(std::memory_order_acquire),
                         g_java_specialize_hook_id.load(std::memory_order_acquire));
        return NOOK_STATUS_OK;
    }

    NOOK_ZYGOTE_LOGI("zygote monitor init begin process=%s",
                     ReadProcessNameLocal().c_str());

    if (!EnsureZygoteHooksInstalled()) {
        NOOK_ZYGOTE_LOGE("zygote monitor hook install failed process=%s",
                         ReadProcessNameLocal().c_str());
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    RegisterInternalRpcRequestHandler("*", HandleZygoteControlRpc);
    if (!helper_only_local_control) {
        RefreshAgentCallbacksForInternalRpc();
    }
    NOOK_ZYGOTE_LOGI("zygote monitor hook install complete process=%s",
                     ReadProcessNameLocal().c_str());

    if (!helper_only_local_control) {
        const NookStatus notify_status = NotifyZygoteControlReadyToServer();
        if (notify_status != NOOK_STATUS_OK) {
            NOOK_ZYGOTE_LOGE("zygote monitor control-ready notify failed process=%s status=%d",
                             ReadProcessNameLocal().c_str(),
                             notify_status);
            return notify_status;
        }
    } else {
        NOOK_ZYGOTE_LOGI("zygote monitor helper-only local control keeps zygote offline process=%s",
                         ReadProcessNameLocal().c_str());
    }

#if defined(__ANDROID__)
    setenv("NOOK_ZYGOTE_MONITOR_READY", "1", 1);
#endif
    g_zygote_control_ready.store(true, std::memory_order_release);
    NOOK_ZYGOTE_LOGI("zygote monitor initialized process=%s", ReadProcessNameLocal().c_str());
    return NOOK_STATUS_OK;
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}

NookStatus NookZygoteMonitorReinitialize() {
#if defined(__ANDROID__) && !defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(g_zygote_control_mutex);
        NOOK_ZYGOTE_LOGI("zygote monitor explicit reinitialize process=%s ready=%d envReady=%d hooks=%d",
                         ReadProcessNameLocal().c_str(),
                         g_zygote_control_ready.load(std::memory_order_acquire) ? 1 : 0,
                         IsZygoteMonitorReadyFromEnvironment() ? 1 : 0,
                         g_hooks_installed.load(std::memory_order_acquire) ? 1 : 0);
        g_zygote_control_ready.store(false, std::memory_order_release);
#if defined(__ANDROID__)
        unsetenv("NOOK_ZYGOTE_MONITOR_READY");
#endif
    }
    return NookZygoteMonitorInitialize();
#else
    return NOOK_STATUS_NOT_IMPLEMENTED;
#endif
}
}  // namespace framework
}  // namespace nook
