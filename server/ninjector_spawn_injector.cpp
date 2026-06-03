#include "ninjector_spawn_injector.h"

#include "../src/communication/protocol/messages.h"
#include "ninjector_compat.h"
#include "server_runtime.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dirent.h>
#include <unistd.h>
#define NOOK_SPAWN_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookServer", __VA_ARGS__))
#define NOOK_SPAWN_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookServer", __VA_ARGS__))
#else
#define NOOK_SPAWN_LOGI(...) ((void)0)
#define NOOK_SPAWN_LOGE(...) ((void)0)
#endif

namespace nook {
namespace server {

namespace {

constexpr const char* kEmbeddedAgentSentinel = "__embedded_agent__";
constexpr const char* kEmbeddedZygoteHelperSentinel = "__embedded_zygote_helper__";
constexpr const char* kEmbeddedNcoreSentinel = "__embedded_ncore__";
constexpr const char* kStrictZygoteControlMarker = "--nook-strict-zygote-control";
constexpr const char* kEmbeddedZygoteHelperRuntimeDirPrefix = "__embedded_zygote_helper_runtime_dir__:";

bool IsEnvEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool PathLooksLikeEmbeddedRuntimeArtifact(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path == kEmbeddedAgentSentinel ||
        path == kEmbeddedZygoteHelperSentinel ||
        path == kEmbeddedNcoreSentinel) {
        return true;
    }
    const std::string::size_type slash = path.find_last_of("/\\");
    const std::string file_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    return file_name == "libncore.so" || file_name == "libnook-agent.so";
}

std::string ResolveEmbeddedAgentMaterializationPath(const std::string& runtime_dir) {
    return BuildEmbeddedAgentPathForRuntimeDirectory(runtime_dir,
                                                    kNookEmbeddedAgentBlob,
                                                    static_cast<size_t>(kNookEmbeddedAgentBlobSize));
}

bool ShouldAllowLegacyNcoreBackendFallback() {
    return true;
}

bool ShouldAllowSymbiBackendFallback() {
    return IsEnvEnabled("NOOK_ALLOW_SYMBI_FALLBACK");
}

bool ShouldPreferSymbiBackendByDefault() {
    if (IsEnvEnabled("NOOK_DISABLE_SYMBI_PREFERENCE")) {
        return false;
    }
    const char* explicit_preference = std::getenv("NOOK_PREFER_SYMBI_BACKEND");
    if (explicit_preference != nullptr) {
        return std::strcmp(explicit_preference, "1") == 0;
    }
    return false;
}

bool IsStrictZygoteControlRequested(const comm::SpawnRequest& request) {
    for (const std::string& arg : request.argv) {
        if (arg == kStrictZygoteControlMarker) {
            return true;
        }
    }
    return false;
}

std::string RuntimeDirectoryFromAgentPath(const std::string& agent_path) {
    if (agent_path.empty()) {
        return {};
    }
    if (agent_path == kEmbeddedAgentSentinel ||
        agent_path == kEmbeddedZygoteHelperSentinel) {
        const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
        if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
            return std::string(runtime_dir);
        }
        return {};
    }
    const std::string::size_type slash = agent_path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    if (slash == 0) {
        return agent_path.substr(0, 1);
    }
    return agent_path.substr(0, slash);
}

std::string ResolveAuthoritativeRuntimeDirectory(const NinjectorSpawnConfig& config) {
    if (!config.runtime_dir.empty()) {
        return config.runtime_dir;
    }
    const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return std::string(runtime_dir);
    }
    return {};
}

bool ShouldAllowLegacyNcoreSidecarFallback() {
    if (kNookEmbeddedNcoreBlobSize == 0) {
        return true;
    }

    const char* explicit_ncore_path = std::getenv("NOOK_NCORE_PATH");
    if (explicit_ncore_path != nullptr && explicit_ncore_path[0] != '\0') {
        return true;
    }

    return IsEnvEnabled("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK");
}

bool ShouldUseEmbeddedSymbiAgent(const std::string& agent_path,
                                 const NinjectorSpawnOps& ops) {
    if (!ops.spawn_symbi_embedded) {
        return false;
    }

    if (kNookEmbeddedAgentBlobSize == 0) {
        return false;
    }

    if (agent_path.empty()) {
        return false;
    }

    if (agent_path != kEmbeddedAgentSentinel &&
        std::ifstream(agent_path, std::ios::binary).good()) {
        return false;
    }

    const char* explicit_agent_path = std::getenv("NOOK_AGENT_PATH");
    if (explicit_agent_path != nullptr && explicit_agent_path[0] != '\0') {
        return false;
    }

    return true;
}

bool HasExplicitExternalAgentPath() {
    const char* explicit_agent_path = std::getenv("NOOK_AGENT_PATH");
    return explicit_agent_path != nullptr && explicit_agent_path[0] != '\0';
}

bool IsEmbeddedAgentSentinelPath(const std::string& agent_path) {
    return agent_path == kEmbeddedAgentSentinel ||
           agent_path == kEmbeddedZygoteHelperSentinel;
}

bool ShouldPreferEmbeddedAttachAgent(const std::string& agent_path,
                                     const NinjectorSpawnOps& ops) {
    if (!ops.inject_embedded_agent_by_pid) {
        return false;
    }

    if (kNookEmbeddedAgentBlobSize == 0) {
        return false;
    }

    if (HasExplicitExternalAgentPath()) {
        return false;
    }

    return !agent_path.empty();
}

bool ShouldUseLegacyZygoteSpawnControlSideChannel(bool has_install_hook) {
    return !has_install_hook;
}

bool Is64BitZygoteProcessName(const std::string& process_name) {
    return process_name == "zygote64" || process_name == "usap64";
}

bool Is32BitZygoteProcessName(const std::string& process_name) {
    return process_name == "zygote" || process_name == "usap32";
}

bool IsRecognizedZygoteProcessName(const std::string& process_name) {
    return Is64BitZygoteProcessName(process_name) || Is32BitZygoteProcessName(process_name);
}

bool IsSameAuthoritativeZygoteFamily(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    if (Is64BitZygoteProcessName(lhs) && Is64BitZygoteProcessName(rhs)) {
        return true;
    }
    if (Is32BitZygoteProcessName(lhs) && Is32BitZygoteProcessName(rhs)) {
        return true;
    }
    return false;
}

bool IsCurrentBuild64BitOnly() {
#if defined(__aarch64__) && !defined(__arm__)
    return true;
#else
    return false;
#endif
}

bool ShouldSkipUnsupportedZygoteTargetArch(int pid,
                                           const std::string& process_name,
                                           std::string* reason) {
    if (reason != nullptr) {
        reason->clear();
    }

    if (!IsCurrentBuild64BitOnly()) {
        return false;
    }

    bool is_remote_64_bit = false;
    if (!ninjector::IsRemoteProcess64Bit(pid, &is_remote_64_bit)) {
        return false;
    }

    if (is_remote_64_bit) {
        return false;
    }

    if (reason != nullptr) {
        *reason = "32-bit zygote unsupported by current arm64-only build";
        if (!process_name.empty()) {
            *reason += " process=";
            *reason += process_name;
        }
    }
    return true;
}

#if defined(__ANDROID__)
std::string ReadProcessCmdlineNameByPid(int pid) {
    if (pid <= 0) {
        return {};
    }

    std::ifstream cmdline("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    std::string name;
    std::getline(cmdline, name, '\0');
    return name;
}

bool ReadProcessRealUidByPid(int pid, int* uid) {
    if (uid == nullptr || pid <= 0) {
        return false;
    }

    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    if (!status.good()) {
        return false;
    }

    std::string line;
    while (std::getline(status, line)) {
        constexpr const char* kUidPrefix = "Uid:";
        if (line.rfind(kUidPrefix, 0) != 0) {
            continue;
        }

        std::istringstream stream(line.substr(std::strlen(kUidPrefix)));
        int real_uid = -1;
        if (!(stream >> real_uid)) {
            return false;
        }
        *uid = real_uid;
        return true;
    }

    return false;
}

bool RefreshZygoteTargetIdentity(int pid,
                                 const std::string& expected_name,
                                 std::string* actual_name,
                                 std::string* validation_error) {
    if (actual_name != nullptr) {
        actual_name->clear();
    }
    if (validation_error != nullptr) {
        validation_error->clear();
    }

    if (pid <= 0) {
        if (validation_error != nullptr) {
            *validation_error = "invalid pid";
        }
        return false;
    }

    const std::string current_name = ReadProcessCmdlineNameByPid(pid);
    if (actual_name != nullptr) {
        *actual_name = current_name;
    }
    if (current_name.empty()) {
        if (validation_error != nullptr) {
            *validation_error = "cmdline unavailable";
        }
        return false;
    }
    if (!IsRecognizedZygoteProcessName(current_name)) {
        if (validation_error != nullptr) {
            *validation_error = "cmdline no longer zygote-family: " + current_name;
        }
        return false;
    }
    if (!expected_name.empty() && current_name != expected_name) {
        if (validation_error != nullptr) {
            *validation_error = "cmdline mismatch current=" + current_name +
                                " expected=" + expected_name;
        }
        return false;
    }

    int real_uid = -1;
    if (!ReadProcessRealUidByPid(pid, &real_uid)) {
        if (validation_error != nullptr) {
            *validation_error = "uid unavailable";
        }
        return false;
    }
    if (real_uid != 0) {
        if (validation_error != nullptr) {
            *validation_error = "uid no longer root: " + std::to_string(real_uid);
        }
        return false;
    }

    return true;
}

std::string ReadTextFileForDebug(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

void DumpProcessFdSnapshotForDebug(int pid,
                                   const std::string& process_name,
                                   const char* stage) {
    if (pid <= 0) {
        return;
    }

    const std::string stage_name =
        (stage != nullptr && stage[0] != '\0') ? stage : "unknown";
    std::ostringstream output;
    output << "pid=" << pid
           << " process=" << process_name
           << " stage=" << stage_name << "\n";

    const std::string fd_dir_path = "/proc/" + std::to_string(pid) + "/fd";
    DIR* dir = opendir(fd_dir_path.c_str());
    if (dir == nullptr) {
        output << "fd_dir_open_failed path=" << fd_dir_path << "\n";
    } else {
        for (dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
            if (entry->d_name[0] == '.' ||
                std::strcmp(entry->d_name, ".") == 0 ||
                std::strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            const std::string fd_name = entry->d_name;
            const std::string fd_path = fd_dir_path + "/" + fd_name;
            char link_target[512] = {};
            const ssize_t link_size = readlink(fd_path.c_str(),
                                               link_target,
                                               sizeof(link_target) - 1);
            if (link_size >= 0) {
                link_target[link_size] = '\0';
            }
            output << "fd " << fd_name << " -> "
                   << ((link_size >= 0) ? link_target : "readlink_failed")
                   << "\n";

            const std::string fdinfo_path =
                "/proc/" + std::to_string(pid) + "/fdinfo/" + fd_name;
            const std::string fdinfo = ReadTextFileForDebug(fdinfo_path);
            if (!fdinfo.empty()) {
                output << "fdinfo[" << fd_name << "]\n" << fdinfo;
                if (fdinfo.back() != '\n') {
                    output << "\n";
                }
            }
        }
        closedir(dir);
    }

    const std::string net_unix_path = "/proc/" + std::to_string(pid) + "/net/unix";
    const std::string net_unix = ReadTextFileForDebug(net_unix_path);
    if (!net_unix.empty()) {
        output << "net_unix\n" << net_unix;
        if (net_unix.back() != '\n') {
            output << "\n";
        }
    } else {
        output << "net_unix_read_failed path=" << net_unix_path << "\n";
    }

    const std::string out_path = "/data/local/tmp/nook-fd-snapshot-" +
                                 std::to_string(pid) +
                                 "-" + stage_name + ".txt";
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (out.good()) {
        out << output.str();
        out.close();
        NOOK_SPAWN_LOGI("zygote fd snapshot captured pid=%d process=%s stage=%s path=%s",
                        pid,
                        process_name.c_str(),
                        stage_name.c_str(),
                        out_path.c_str());
    } else {
        NOOK_SPAWN_LOGE("zygote fd snapshot write failed pid=%d process=%s stage=%s path=%s",
                        pid,
                        process_name.c_str(),
                        stage_name.c_str(),
                        out_path.c_str());
    }
}
#else
bool RefreshZygoteTargetIdentity(int pid,
                                 const std::string& expected_name,
                                 std::string* actual_name,
                                 std::string* validation_error) {
    (void) pid;
    if (actual_name != nullptr) {
        *actual_name = expected_name;
    }
    if (validation_error != nullptr) {
        validation_error->clear();
    }
    return true;
}

void DumpProcessFdSnapshotForDebug(int,
                                   const std::string&,
                                   const char*) {
}
#endif

using ZygoteControlLifecycleState = ZygoteControlFailureState;

ZygoteControlLifecycleState InferZygoteControlLifecycleStateFromError(const std::string& error) {
    if (error.empty()) {
        return ZygoteControlLifecycleState::kUnknown;
    }
    if (error.find("install zygote fork hook failed: rpc timeout") != std::string::npos ||
        error.find("install zygote fork hook failed: zygote control rpc timeout") != std::string::npos ||
        error.find("install zygote fork hook failed: zygote control-ready wait timed out") != std::string::npos ||
        error.find("install zygote fork hook failed: zygote control ready wait timed out") != std::string::npos ||
        error.find("install zygote fork hook failed: ready wait timed out") != std::string::npos) {
        return ZygoteControlLifecycleState::kReadyWait;
    }
    if (error.find("inject zygote agent failed") != std::string::npos) {
        return ZygoteControlLifecycleState::kInjectAgent;
    }
    if (error.find("zygote control-ready agent session not found") != std::string::npos ||
        error.find("zygote agent session not found") != std::string::npos ||
        error.find("zygote control-ready wait timed out") != std::string::npos ||
        error.find("zygote control ready wait timed out") != std::string::npos ||
        error.find("ready wait timed out") != std::string::npos ||
        error.find("zygote control rpc timeout") != std::string::npos) {
        return ZygoteControlLifecycleState::kReadyWait;
    }
    if (error.find("set zygote spawn control failed") != std::string::npos) {
        return ZygoteControlLifecycleState::kArmControl;
    }
    if (error.find("install zygote fork hook failed") != std::string::npos) {
        return ZygoteControlLifecycleState::kInstallHook;
    }
    if (error.find("no zygote-monitor targets armed") != std::string::npos) {
        return ZygoteControlLifecycleState::kTargetsArmed;
    }
    if (error.find("start_target_app failed") != std::string::npos) {
        return ZygoteControlLifecycleState::kLaunchApp;
    }
    if (error.find("clear zygote spawn control failed") != std::string::npos) {
        return ZygoteControlLifecycleState::kFinalizeClear;
    }
    return ZygoteControlLifecycleState::kUnknown;
}

bool IsSoftZygoteControlInstallFailure(const std::string& error) {
    if (error.empty()) {
        return false;
    }
    return error.find("zygote control-ready agent session not found") != std::string::npos ||
           error.find("zygote agent session not found") != std::string::npos ||
           error.find("zygote control-ready wait timed out") != std::string::npos ||
           error.find("zygote control ready wait timed out") != std::string::npos ||
           error.find("ready wait timed out") != std::string::npos ||
           error.find("zygote control rpc timeout") != std::string::npos ||
           error.find("rpc timeout") != std::string::npos ||
           error.find("remote_dlopen_failed:dlopen failed: library \"/proc/self/fd/") != std::string::npos;
}

bool IsSoftZygoteControlInjectFailure(const std::string& error) {
    if (error.empty()) {
        return false;
    }
    return error.find("remote_dlopen_failed:dlopen failed: library \"/proc/self/fd/") != std::string::npos ||
           error.find("attach_process_failed:atomic_inject") != std::string::npos;
}

bool IsMissingZygoteControlSessionError(const std::string& error) {
    if (error.empty()) {
        return false;
    }
    return error.find("zygote control-ready agent session not found") != std::string::npos ||
           error.find("zygote agent session not found") != std::string::npos;
}

bool ShouldAllowFallbackForZygoteControlState(ZygoteControlLifecycleState state,
                                              const std::string& error) {
    switch (state) {
    case ZygoteControlLifecycleState::kInjectAgent:
        return IsSoftZygoteControlInjectFailure(error);
    case ZygoteControlLifecycleState::kReadyWait:
        return true;
    case ZygoteControlLifecycleState::kInstallHook:
        return IsSoftZygoteControlInstallFailure(error);
    case ZygoteControlLifecycleState::kArmControl:
    case ZygoteControlLifecycleState::kTargetsArmed:
    case ZygoteControlLifecycleState::kLaunchApp:
    case ZygoteControlLifecycleState::kFinalizeClear:
    case ZygoteControlLifecycleState::kUnknown:
        return false;
    }

    return false;
}

bool ShouldAllowFallbackAfterZygoteControlFailure(const std::string& error) {
    if (error.empty()) {
        return false;
    }

    return ShouldAllowFallbackForZygoteControlState(
        InferZygoteControlLifecycleStateFromError(error),
        error);
}

const char* ClassifyZygoteControlFailureClass(const std::string& error) {
    return ShouldAllowFallbackAfterZygoteControlFailure(error) ? "soft" : "hard";
}

const char* ZygoteControlFailureStateToString(ZygoteControlFailureState state) {
    switch (state) {
    case ZygoteControlFailureState::kInjectAgent:
        return "inject-agent";
    case ZygoteControlFailureState::kReadyWait:
        return "ready-wait";
    case ZygoteControlFailureState::kArmControl:
        return "arm-control";
    case ZygoteControlFailureState::kInstallHook:
        return "install-hook";
    case ZygoteControlFailureState::kTargetsArmed:
        return "targets-armed";
    case ZygoteControlFailureState::kLaunchApp:
        return "launch-app";
    case ZygoteControlFailureState::kFinalizeClear:
        return "finalize-clear";
    case ZygoteControlFailureState::kUnknown:
    default:
        return "unknown";
    }
}

std::string FormatZygoteControlFinalError(const char* stage,
                                          ZygoteControlFailureState state,
                                          const std::string& detail) {
    std::string message = "zygote-control stage=";
    message += (stage != nullptr && stage[0] != '\0') ? stage : "unknown";
    message += " class=";
    message += ClassifyZygoteControlFailureClass(detail);
    message += " state=";
    message += ZygoteControlFailureStateToString(state);
    message += " detail=";
    message += detail.empty() ? "unknown" : detail;
    return message;
}

}  // namespace

std::string FormatZygoteControlSpawnDecisionLog(const char* event_name,
                                                const std::string& package_name,
                                                bool strict_mode,
                                                const char* fallback_backend,
                                                const std::string& detail) {
    std::string line = "zygote-control stage=spawn-route event=";
    line += (event_name != nullptr && event_name[0] != '\0') ? event_name : "unknown";
    line += " package=";
    line += package_name.empty() ? "unknown" : package_name;
    line += " strict=";
    line += strict_mode ? "1" : "0";
    line += " fallback=";
    line += (fallback_backend != nullptr && fallback_backend[0] != '\0') ? fallback_backend : "none";
    if (!detail.empty()) {
        line += " detail=";
        line += detail;
    }
    return line;
}

std::string FormatZygoteControlFinalizeDecisionLog(const char* event_name,
                                                   const std::string& package_name,
                                                   const char* backend_name,
                                                   const std::string& detail) {
    std::string line = "zygote-control stage=finalize-route event=";
    line += (event_name != nullptr && event_name[0] != '\0') ? event_name : "unknown";
    line += " package=";
    line += package_name.empty() ? "unknown" : package_name;
    line += " backend=";
    line += (backend_name != nullptr && backend_name[0] != '\0') ? backend_name : "unknown";
    if (!detail.empty()) {
        line += " detail=";
        line += detail;
    }
    return line;
}

std::string FormatZygoteControlTerminalOutcomeLog(const char* stage_name,
                                                  const char* event_name,
                                                  const std::string& package_name,
                                                  const char* primary_backend,
                                                  const char* secondary_backend,
                                                  const char* state_name,
                                                  const std::string& detail) {
    std::string line = "zygote-control stage=";
    line += (stage_name != nullptr && stage_name[0] != '\0') ? stage_name : "unknown";
    line += " event=";
    line += (event_name != nullptr && event_name[0] != '\0') ? event_name : "unknown";
    line += " package=";
    line += package_name.empty() ? "unknown" : package_name;
    line += " primary=";
    line += (primary_backend != nullptr && primary_backend[0] != '\0') ? primary_backend : "unknown";
    line += " secondary=";
    line += (secondary_backend != nullptr && secondary_backend[0] != '\0') ? secondary_backend : "none";
    line += " state=";
    line += (state_name != nullptr && state_name[0] != '\0') ? state_name : "unknown";
    if (!detail.empty()) {
        line += " detail=";
        line += detail;
    }
    return line;
}

std::string FormatZygoteControlLifecycleStageLog(const char* stage_name,
                                                 const char* event_name,
                                                 const std::string& package_name,
                                                 const char* state_name,
                                                 const std::string& detail) {
    std::string line = "zygote-control stage=";
    line += (stage_name != nullptr && stage_name[0] != '\0') ? stage_name : "unknown";
    line += " event=";
    line += (event_name != nullptr && event_name[0] != '\0') ? event_name : "unknown";
    line += " package=";
    line += package_name.empty() ? "unknown" : package_name;
    line += " state=";
    line += (state_name != nullptr && state_name[0] != '\0') ? state_name : "unknown";
    if (!detail.empty()) {
        line += " detail=";
        line += detail;
    }
    return line;
}

NinjectorSpawnInjector::NinjectorSpawnInjector(NinjectorSpawnConfig config,
                                               NinjectorSpawnOps ops,
                                               ZygoteControlInstallHookFn install_hook,
                                               ZygoteControlUninstallHookFn uninstall_hook,
                                               HasPreexistingZygoteSessionFn has_preexisting_session,
                                               EnumerateZygoteProcessesFn enumerate_zygote_processes)
    : config_(std::move(config)),
      ops_(std::move(ops)),
      install_zygote_hook_(std::move(install_hook)),
      uninstall_zygote_hook_(std::move(uninstall_hook)),
      has_preexisting_zygote_session_(std::move(has_preexisting_session)),
      enumerate_zygote_processes_(std::move(enumerate_zygote_processes)) {}

void NinjectorSpawnInjector::RecordZygoteControlFailureState(ZygoteControlFailureState state) const {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    last_zygote_control_failure_state_ = state;
}

void NinjectorSpawnInjector::RecordZygoteControlLifecycleStage(ZygoteControlFailureState state) const {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    current_zygote_control_lifecycle_stage_ = state;
}

ZygoteControlFailureState NinjectorSpawnInjector::ReadZygoteControlLifecycleStage() const {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    return current_zygote_control_lifecycle_stage_;
}

void NinjectorSpawnInjector::ClearZygoteControlLifecycleStage() const {
    RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kUnknown);
}

ZygoteControlFailureState NinjectorSpawnInjector::ReadZygoteControlFailureState() const {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    return last_zygote_control_failure_state_;
}

void NinjectorSpawnInjector::ClearZygoteControlFailureState() const {
    RecordZygoteControlFailureState(ZygoteControlFailureState::kUnknown);
}

void NinjectorSpawnInjector::CommitPendingSpawn(const PendingSpawnCommit& pending_commit) {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    active_spawn_owner_ = pending_commit;
    StripCompatibilityArtifactResidueForMatchingShellOwnerLocked();
    if (HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction)) {
        // Zygote-control keeps only transaction ownership plus request-token compatibility state.
        active_spawn_owner_.shell_owner_state = SpawnOwnedState{};
        active_spawn_owner_.spawn_state.backend = SpawnBackend::kNone;
        active_spawn_owner_.spawn_state.identifier.clear();
        active_spawn_owner_.spawn_state.ncore_path.clear();
        active_spawn_owner_.spawn_state.agent_path.clear();
        active_spawn_owner_.spawn_state.materialized_ncore = false;
        active_spawn_owner_.spawn_state.materialized_agent = false;
    }
}

void NinjectorSpawnInjector::StripCompatibilityArtifactResidueForMatchingShellOwnerLocked() {
    if (!HasAuthoritativeSpawnOwner(active_spawn_owner_.shell_owner_state)) {
        return;
    }
    if (active_spawn_owner_.spawn_state.spawn_token !=
        active_spawn_owner_.shell_owner_state.spawn_token) {
        return;
    }
    if (!active_spawn_owner_.spawn_state.identifier.empty() &&
        active_spawn_owner_.spawn_state.identifier !=
            active_spawn_owner_.shell_owner_state.identifier) {
        return;
    }
    active_spawn_owner_.spawn_state.backend = SpawnBackend::kNone;
    active_spawn_owner_.spawn_state.identifier.clear();
    active_spawn_owner_.spawn_state.ncore_path.clear();
    active_spawn_owner_.spawn_state.agent_path.clear();
    active_spawn_owner_.spawn_state.materialized_ncore = false;
    active_spawn_owner_.spawn_state.materialized_agent = false;
}

void NinjectorSpawnInjector::ClearAuthoritativeSpawnOwnerSlot(
    const SpawnOwnedState* authoritative_spawn_owner) {
    if (authoritative_spawn_owner == &active_spawn_owner_.shell_owner_state) {
        StripCompatibilityArtifactResidueForMatchingShellOwnerLocked();
        active_spawn_owner_.shell_owner_state = SpawnOwnedState{};
    } else if (authoritative_spawn_owner == &active_spawn_owner_.spawn_state) {
        active_spawn_owner_.spawn_state = SpawnOwnedState{};
    } else {
        active_spawn_owner_.shell_owner_state = SpawnOwnedState{};
        active_spawn_owner_.spawn_state = SpawnOwnedState{};
    }
}

void NinjectorSpawnInjector::RestoreOwnedSpawnStateForRetry(
    const SpawnOwnedState& owned_spawn_state) {
    if (IsLegacyShellOwnedBackend(owned_spawn_state.backend)) {
        active_spawn_owner_.shell_owner_state = owned_spawn_state;
    } else {
        active_spawn_owner_.spawn_state = owned_spawn_state;
    }
}

bool NinjectorSpawnInjector::HasOwnedSpawnStateForRetry(
    const SpawnOwnedState& owned_spawn_state) {
    return HasAuthoritativeSpawnOwner(owned_spawn_state);
}

bool NinjectorSpawnInjector::IsLegacyShellOwnedBackend(SpawnBackend backend) {
    return backend == SpawnBackend::kLegacyNcore;
}

bool NinjectorSpawnInjector::IsChildOwnedBackend(SpawnBackend backend) {
    return backend == SpawnBackend::kSymbi;
}

bool NinjectorSpawnInjector::HasAuthoritativeSpawnOwner(
    const SpawnOwnedState& spawn_state) {
    return spawn_state.backend != SpawnBackend::kNone && !spawn_state.identifier.empty();
}

const NinjectorSpawnInjector::SpawnOwnedState*
NinjectorSpawnInjector::ResolveAuthoritativeSpawnOwner(
    const ActiveSpawnOwner& active_spawn_owner) {
    // Authoritative ownership is now split:
    // - legacy shell-owned routes live in `shell_owner_state`
    // - child-owned routes like symbi live in `spawn_state`
    // - zygote-control lives in `zygote_control_transaction`
    if (HasAuthoritativeSpawnOwner(active_spawn_owner.shell_owner_state)) {
        return &active_spawn_owner.shell_owner_state;
    }
    if (IsChildOwnedBackend(active_spawn_owner.spawn_state.backend) &&
        HasAuthoritativeSpawnOwner(active_spawn_owner.spawn_state)) {
        return &active_spawn_owner.spawn_state;
    }
    return nullptr;
}

NinjectorSpawnConfig NinjectorSpawnInjector::DefaultConfig() {
    const char* env_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    const char* env_enable_zygote_control =
        std::getenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL");
    const std::string runtime_dir =
        (env_runtime_dir != nullptr && env_runtime_dir[0] != '\0')
            ? std::string(env_runtime_dir)
            : std::string();
    const bool enable_zygote_control =
        env_enable_zygote_control != nullptr &&
        std::strcmp(env_enable_zygote_control, "1") == 0;
    return NinjectorSpawnConfig{
        .ncore_path = ResolveNcorePathFromRuntimeDirectory(runtime_dir),
        .runtime_dir = runtime_dir,
        .spawn_source_process = ninjector::GetDefaultSpawnSourceProcess(),
        .enable_zygote_control = enable_zygote_control,
    };
}

NinjectorSpawnOps NinjectorSpawnInjector::MakeDefaultOps() {
    const char* env_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    const std::string runtime_dir =
        (env_runtime_dir != nullptr && env_runtime_dir[0] != '\0')
            ? std::string(env_runtime_dir)
            : std::string();
    return NinjectorSpawnOps{
        .get_pid = ninjector::GetPid,
        .is_zygote_monitor_ready = ninjector::IsZygoteMonitorReady,
        .spawn_symbi = ninjector::SpawnViaSymbi,
        .spawn_symbi_embedded = ninjector::SpawnViaSymbiEmbedded,
        .prepare_spawn = ninjector::PrepareSpawnInZygote,
        .clear_spawn = ninjector::ClearSpawnInZygote,
        .inject_embedded_agent_by_pid = [](int pid, const char* runtime_dir_for_injection, const char* ready_token) {
            return ninjector::InjectEmbeddedAgentByPid(pid,
                                                       runtime_dir_for_injection,
                                                       ready_token);
        },
        .inject_so_by_pid = [](int pid, const char* so_path, const char* ready_token) {
            return ninjector::InjectSoByPid(pid, so_path, ready_token);
        },
        .inject_zygote_so_by_pid = [](int pid, const char* runtime_dir_or_so_path) {
            return ninjector::InjectZygoteAgentByPid(pid, runtime_dir_or_so_path);
        },
        .uninstall_embedded_zygote_control_hooks = ninjector::UninstallEmbeddedZygoteControlHooksByPid,
        .get_inject_error = ninjector::GetLastInjectError,
        .start_target_app = ninjector::StartTargetApp,
        .set_zygote_spawn_control = [](int zygote_pid,
                                       const char* package_name,
                                       const char* spawn_token,
                                       bool strict_request) {
            return ninjector::SetZygoteSpawnControl(
                zygote_pid, package_name, spawn_token, strict_request);
        },
        .clear_zygote_spawn_control = [](int zygote_pid,
                                         const char* spawn_token,
                                         bool strict_request) {
            return ninjector::ClearZygoteSpawnControl(zygote_pid,
                                                      spawn_token,
                                                      strict_request);
        },
    };
}

void NinjectorSpawnInjector::SetError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

bool NinjectorSpawnInjector::HasZygoteControlTransactionRecord(
    const ZygoteControlOwnedTransaction& transaction) {
    return !transaction.identifier.empty() || !transaction.spawn_token.empty() ||
           transaction.failure_state != ZygoteControlFailureState::kUnknown ||
           transaction.lifecycle_state != ZygoteControlFailureState::kUnknown ||
           !transaction.targets.empty();
}

const char* NinjectorSpawnInjector::SpawnTerminalBackendToString(SpawnTerminalBackend backend) {
    switch (backend) {
    case SpawnTerminalBackend::kNone:
        return "none";
    case SpawnTerminalBackend::kUnknown:
        return "unknown";
    case SpawnTerminalBackend::kZygoteControl:
        return "zygote-control";
    case SpawnTerminalBackend::kSymbi:
        return "symbi";
    case SpawnTerminalBackend::kLegacy:
        return "legacy";
    case SpawnTerminalBackend::kExplicitSymbi:
        return "symbi-explicit";
    }
    return "unknown";
}

bool NinjectorSpawnInjector::PrepareRuntimeArtifact(const std::string& requested_path,
                                                    const unsigned char* embedded_blob,
                                                    size_t embedded_blob_size,
                                                    const char* empty_path_error,
                                                    const char* missing_path_error,
                                                    const char* materialize_error_prefix,
                                                    bool treat_embedded_sentinel_as_ready,
                                                    PreparedRuntimeArtifact* artifact,
                                                    std::string* error_message) {
    if (artifact == nullptr) {
        SetError(error_message, "prepared runtime artifact output is null");
        return false;
    }

    artifact->resolved_path = requested_path;
    artifact->materialized_embedded = false;

    if (artifact->resolved_path.empty()) {
        SetError(error_message, empty_path_error != nullptr ? empty_path_error : "runtime artifact path is empty");
        return false;
    }

    if (treat_embedded_sentinel_as_ready && IsEmbeddedAgentSentinelPath(artifact->resolved_path)) {
        if (embedded_blob_size == 0) {
            SetError(error_message, "embedded agent blob is empty");
            return false;
        }
        SetError(error_message, "");
        return true;
    }

    if (std::ifstream(artifact->resolved_path, std::ios::binary).good()) {
        SetError(error_message, "");
        return true;
    }

    if (embedded_blob_size == 0) {
        SetError(error_message, missing_path_error != nullptr ? missing_path_error : "runtime artifact path does not exist");
        return false;
    }

    EmbeddedFileMaterializationResult result = EmbeddedFileMaterializationResult::kError;
    std::string materialization_error;
    if (!EnsureEmbeddedFileAtPath(artifact->resolved_path.c_str(),
                                  embedded_blob,
                                  embedded_blob_size,
                                  &result,
                                  &materialization_error)) {
        if (!materialization_error.empty()) {
            SetError(error_message, materialization_error);
        } else if (materialize_error_prefix != nullptr && materialize_error_prefix[0] != '\0') {
            SetError(error_message, std::string(materialize_error_prefix) + " failed");
        } else {
            SetError(error_message, "embedded runtime artifact materialization failed");
        }
        return false;
    }

    artifact->materialized_embedded = true;
    SetError(error_message, "");
    return true;
}

void NinjectorSpawnInjector::MaybeCleanupRuntimeArtifact(const PreparedRuntimeArtifact& artifact,
                                                         const char* explicit_env_name) {
    if (!artifact.materialized_embedded || !PathLooksLikeEmbeddedRuntimeArtifact(artifact.resolved_path)) {
        return;
    }

    if (explicit_env_name != nullptr && explicit_env_name[0] != '\0') {
        const char* explicit_path = std::getenv(explicit_env_name);
        if (explicit_path != nullptr && explicit_path[0] != '\0') {
            return;
        }
    }

    std::string cleanup_error;
    (void)RemoveFileIfExists(artifact.resolved_path, &cleanup_error);
}

bool NinjectorSpawnInjector::EnsureLegacyNcoreReady(std::string* resolved_ncore_path,
                                                    bool* materialized_embedded_ncore,
                                                    std::string* error_message) {
    if (resolved_ncore_path == nullptr) {
        SetError(error_message, "resolved ncore output is null");
        return false;
    }
    PreparedRuntimeArtifact artifact;
    if (!PrepareRuntimeArtifact(config_.ncore_path,
                                kNookEmbeddedNcoreBlob,
                                static_cast<size_t>(kNookEmbeddedNcoreBlobSize),
                                "ncore path is empty",
                                "ncore path does not exist",
                                "embedded ncore materialization",
                                false,
                                &artifact,
                                error_message)) {
        return false;
    }

    *resolved_ncore_path = artifact.resolved_path;
    if (materialized_embedded_ncore != nullptr) {
        *materialized_embedded_ncore = artifact.materialized_embedded;
    }
    return true;
}

void NinjectorSpawnInjector::MaybeCleanupLegacyNcoreArtifact(const std::string& resolved_ncore_path,
                                                             bool materialized_embedded_ncore) {
    MaybeCleanupRuntimeArtifact(PreparedRuntimeArtifact{
                                    .resolved_path = resolved_ncore_path,
                                    .materialized_embedded = materialized_embedded_ncore,
                                },
                                "NOOK_NCORE_PATH");
}

bool NinjectorSpawnInjector::EnsureLegacyAgentReady(const std::string& agent_path,
                                                    std::string* resolved_agent_path,
                                                    bool* materialized_embedded_agent,
                                                    std::string* error_message) {
    if (resolved_agent_path == nullptr) {
        SetError(error_message, "resolved agent output is null");
        return false;
    }
    PreparedRuntimeArtifact artifact;
    if (!PrepareRuntimeArtifact(agent_path,
                                kNookEmbeddedAgentBlob,
                                static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                "agent path is empty",
                                "agent path does not exist",
                                "embedded agent materialization",
                                true,
                                &artifact,
                                error_message)) {
        return false;
    }

    *resolved_agent_path = artifact.resolved_path;
    if (materialized_embedded_agent != nullptr) {
        *materialized_embedded_agent = artifact.materialized_embedded;
    }
    return true;
}

void NinjectorSpawnInjector::MaybeCleanupLegacyAgentArtifact(const std::string& resolved_agent_path,
                                                             bool materialized_embedded_agent) {
    MaybeCleanupRuntimeArtifact(PreparedRuntimeArtifact{
                                    .resolved_path = resolved_agent_path,
                                    .materialized_embedded = materialized_embedded_agent,
                                },
                                "NOOK_AGENT_PATH");
}

std::string NinjectorSpawnInjector::ExtractSpawnToken(const comm::SpawnRequest& request) {
    for (const std::string& arg : request.argv) {
        constexpr const char* kPrefix = "--nook-spawn-token=";
        if (arg.rfind(kPrefix, 0) == 0) {
            return arg.substr(std::strlen(kPrefix));
        }
    }
    return {};
}

bool NinjectorSpawnInjector::IsExplicitSymbiSpawnRequested(const comm::SpawnRequest& request) {
    for (const std::string& arg : request.argv) {
        if (arg == "--nook-spawn-backend=symbi") {
            return true;
        }
    }
    return false;
}

bool ReinjectZygoteControlAgentForFinalize(const NinjectorSpawnOps& ops,
                                           const NinjectorSpawnConfig& config,
                                           int pid,
                                           const std::string& process_name) {
    if (pid <= 0 || !ops.inject_zygote_so_by_pid) {
        return false;
    }

    const std::string runtime_dir = ResolveAuthoritativeRuntimeDirectory(config);
    const bool injected =
        ops.inject_zygote_so_by_pid(pid,
                                    runtime_dir.empty() ? nullptr : runtime_dir.c_str());
    if (!injected) {
        return false;
    }

    return ops.is_zygote_monitor_ready ? ops.is_zygote_monitor_ready(pid)
                                       : ninjector::IsZygoteMonitorReady(pid);
}

NinjectorSpawnInjector::ZygoteControlAttemptResult
NinjectorSpawnInjector::TrySpawnViaZygoteControl(const comm::SpawnRequest& request,
                                                 const std::string& agent_path) {
    ZygoteControlAttemptResult result;
    ClearZygoteControlLifecycleStage();
    ClearZygoteControlFailureState();
    const std::string spawn_token = ExtractSpawnToken(request);
    result.owned_transaction = ZygoteControlOwnedTransaction{};
    result.owned_transaction.identifier = request.identifier;
    result.owned_transaction.spawn_token = spawn_token;
    auto record_transaction_failure_state = [&](ZygoteControlFailureState state) {
        result.owned_transaction.failure_state = state;
    };
    auto record_transaction_lifecycle_stage = [&](ZygoteControlFailureState state) {
        result.owned_transaction.lifecycle_state = state;
    };

    NOOK_SPAWN_LOGI("zygote monitor path begin pkg=%s source=%s agent=%s",
                    request.identifier.c_str(),
                    config_.spawn_source_process.c_str(),
                    agent_path.c_str());
    if (!ops_.get_pid || !ops_.start_target_app ||
        (!ops_.inject_zygote_so_by_pid && !ops_.inject_so_by_pid)) {
        record_transaction_failure_state(ZygoteControlFailureState::kUnknown);
        NOOK_SPAWN_LOGE("zygote monitor unavailable: ops incomplete get_pid=%d inject=%d zygote_inject=%d start=%d",
                        ops_.get_pid ? 1 : 0,
                        ops_.inject_so_by_pid ? 1 : 0,
                        ops_.inject_zygote_so_by_pid ? 1 : 0,
                        ops_.start_target_app ? 1 : 0);
        FailZygoteControlSpawn(&result.owned_transaction,
                               nullptr,
                               "zygote monitor ops incomplete",
                               &result.error_message);
        return result;
    }

    std::vector<std::pair<int, std::string>> zygote_targets;
    if (enumerate_zygote_processes_) {
        for (const ProcessInfo& process : enumerate_zygote_processes_()) {
            if (process.pid <= 0) {
                continue;
            }
            if (process.name == "zygote" || process.name == "zygote64" ||
                process.name == "usap32" || process.name == "usap64") {
                zygote_targets.emplace_back(process.pid, process.name);
            }
        }
    }
    if (zygote_targets.empty()) {
        const int zygote_pid = ops_.get_pid(config_.spawn_source_process.c_str());
        if (zygote_pid > 0) {
            zygote_targets.emplace_back(zygote_pid, config_.spawn_source_process);
        }
    } else {
        const int zygote_pid = ops_.get_pid(config_.spawn_source_process.c_str());
        if (zygote_pid > 0) {
            bool seen_source_pid = false;
            for (const auto& target : zygote_targets) {
                if (target.first == zygote_pid && target.second == config_.spawn_source_process) {
                    seen_source_pid = true;
                    break;
                }
            }
            if (!seen_source_pid) {
                zygote_targets.emplace_back(zygote_pid, config_.spawn_source_process);
            }
        }
    }

    if (zygote_targets.empty()) {
        record_transaction_failure_state(ZygoteControlFailureState::kUnknown);
        NOOK_SPAWN_LOGE("zygote monitor failed: source process not found name=%s",
                        config_.spawn_source_process.c_str());
        FailZygoteControlSpawn(&result.owned_transaction,
                               nullptr,
                               "spawn source process not found",
                               &result.error_message);
        return result;
    }

    std::vector<std::pair<int, std::string>> ordered_targets;
    ordered_targets.reserve(zygote_targets.size());
    for (const auto& target : zygote_targets) {
        if (target.second == config_.spawn_source_process) {
            ordered_targets.push_back(target);
        }
    }
    for (const auto& target : zygote_targets) {
        if (target.second == config_.spawn_source_process) {
            continue;
        }
        ordered_targets.push_back(target);
    }

    const bool strict_zygote_control_requested =
        config_.enable_zygote_control &&
        IsStrictZygoteControlRequested(request);
    const bool use_embedded_zygote_agent =
        IsEmbeddedAgentSentinelPath(agent_path) && ops_.inject_zygote_so_by_pid;
    const bool use_embedded_zygote_helper =
        use_embedded_zygote_agent &&
        kNookEmbeddedZygoteHelperBlobSize > 0;
    const bool strict_helper_local_control = use_embedded_zygote_helper;
    const bool use_legacy_spawn_control_side_channel =
        strict_helper_local_control ||
        ShouldUseLegacyZygoteSpawnControlSideChannel(static_cast<bool>(install_zygote_hook_));
    auto uninstall_helper_only_local_target = [&](const std::pair<int, std::string>& target,
                                                  std::string* uninstall_error) -> bool {
        if (uninstall_error != nullptr) {
            uninstall_error->clear();
        }

        const auto uninstall_local_target =
            ops_.uninstall_embedded_zygote_control_hooks
                ? ops_.uninstall_embedded_zygote_control_hooks
                : std::function<bool(int)>(ninjector::UninstallEmbeddedZygoteControlHooksByPid);
        if (uninstall_local_target && uninstall_local_target(target.first)) {
            NOOK_SPAWN_LOGI("zygote helper local uninstall ok pid=%d process=%s",
                            target.first,
                            target.second.c_str());
            return true;
        }

        if (uninstall_error != nullptr) {
            *uninstall_error = ninjector::GetLastInjectError();
            if (uninstall_error->empty()) {
                *uninstall_error = "embedded helper uninstall failed";
            }
        }
        return false;
    };
    auto inject_zygote = [&](int pid, const char* inject_arg) -> bool {
        if (ops_.inject_zygote_so_by_pid) {
            return ops_.inject_zygote_so_by_pid(pid, inject_arg);
        }
        if (ops_.inject_so_by_pid) {
            return ops_.inject_so_by_pid(pid, inject_arg, nullptr);
        }
        return false;
    };
    auto rollback_armed_targets = [&](const std::vector<std::pair<int, std::string>>& targets,
                                      std::string* rollback_error) -> bool {
        bool ok = true;
        std::string first_error;
        for (const auto& armed_target : targets) {
            if (strict_helper_local_control) {
                std::string uninstall_error;
                if (!uninstall_helper_only_local_target(armed_target, &uninstall_error)) {
                    if (first_error.empty()) {
                        first_error = uninstall_error.empty()
                                          ? "uninstall zygote helper hooks failed"
                                          : "uninstall zygote helper hooks failed: " + uninstall_error;
                    }
                    ok = false;
                }
            } else if (uninstall_zygote_hook_) {
                std::string uninstall_error;
                if (!uninstall_zygote_hook_(armed_target.first, armed_target.second, &uninstall_error)) {
                    if (first_error.empty()) {
                        first_error = uninstall_error.empty()
                                          ? "uninstall zygote hook failed"
                                          : "uninstall zygote hook failed: " + uninstall_error;
                    }
                    ok = false;
                }
            }
            if (use_legacy_spawn_control_side_channel) {
                const bool cleared = ops_.clear_zygote_spawn_control
                                         ? ops_.clear_zygote_spawn_control(armed_target.first,
                                                                           spawn_token.c_str(),
                                                                           strict_zygote_control_requested)
                                         : ninjector::ClearZygoteSpawnControl(
                                               armed_target.first,
                                               spawn_token.c_str(),
                                               strict_zygote_control_requested);
                if (!cleared) {
                    std::string clear_error = "clear zygote spawn control failed";
                    if (ops_.get_inject_error) {
                        const std::string detail = ops_.get_inject_error();
                        if (!detail.empty()) {
                            clear_error += ": ";
                            clear_error += detail;
                        }
                    }
                    if (first_error.empty()) {
                        first_error = clear_error;
                    }
                    ok = false;
                }
            }
        }
        if (rollback_error != nullptr) {
            *rollback_error = first_error;
        }
        return ok;
    };
    std::vector<std::pair<int, std::string>> armed_targets;
    for (const auto& target : ordered_targets) {
        const int zygote_pid = target.first;
        const std::string& process_name = target.second;
        const bool is_required_target =
            IsSameAuthoritativeZygoteFamily(process_name, config_.spawn_source_process);
        std::string refreshed_name;
        std::string identity_error;
        if (!RefreshZygoteTargetIdentity(zygote_pid,
                                         process_name,
                                         &refreshed_name,
                                         &identity_error)) {
            NOOK_SPAWN_LOGI("zygote monitor skip invalid target pid=%d expected=%s actual=%s reason=%s",
                            zygote_pid,
                            process_name.c_str(),
                            refreshed_name.empty() ? "(unknown)" : refreshed_name.c_str(),
                            identity_error.empty() ? "(none)" : identity_error.c_str());
            continue;
        }
        std::string unsupported_arch_reason;
        if (ShouldSkipUnsupportedZygoteTargetArch(zygote_pid,
                                                  process_name,
                                                  &unsupported_arch_reason)) {
            NOOK_SPAWN_LOGI("zygote monitor skip unsupported target pid=%d process=%s reason=%s",
                            zygote_pid,
                            process_name.c_str(),
                            unsupported_arch_reason.c_str());
            continue;
        }
        const bool has_authoritative_session =
            has_preexisting_zygote_session_
                ? has_preexisting_zygote_session_(zygote_pid, process_name)
                : false;
        bool monitor_ready = false;
        if (has_authoritative_session) {
            monitor_ready = ops_.is_zygote_monitor_ready
                                ? ops_.is_zygote_monitor_ready(zygote_pid)
                                : ninjector::IsZygoteMonitorReady(zygote_pid);
            if (!monitor_ready) {
                NOOK_SPAWN_LOGI("zygote monitor owned-session-not-ready pid=%d process=%s; forcing reinject",
                                zygote_pid,
                                process_name.c_str());
            }
        } else {
            NOOK_SPAWN_LOGI("zygote monitor skip readiness probe pid=%d process=%s; no authoritative session",
                            zygote_pid,
                            process_name.c_str());
        }
        if (!monitor_ready) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                 "enter",
                                                                 request.identifier,
                                                                 "inject-agent",
                                                                 process_name)
                                .c_str());
            record_transaction_lifecycle_stage(ZygoteControlFailureState::kInjectAgent);
            std::string inject_runtime_dir;
            const char* inject_arg = agent_path.c_str();
            if (use_embedded_zygote_helper) {
                inject_runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
                if (inject_runtime_dir.empty()) {
                    inject_runtime_dir = RuntimeDirectoryFromAgentPath(agent_path);
                }
                if (inject_runtime_dir.empty()) {
                    inject_arg = kEmbeddedZygoteHelperSentinel;
                } else {
                    inject_runtime_dir =
                        std::string(kEmbeddedZygoteHelperRuntimeDirPrefix) + inject_runtime_dir;
                    inject_arg = inject_runtime_dir.c_str();
                }
            } else if (use_embedded_zygote_agent) {
                inject_runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
                if (inject_runtime_dir.empty()) {
                    inject_runtime_dir = RuntimeDirectoryFromAgentPath(agent_path);
                }
                inject_arg = inject_runtime_dir.empty() ? "" : inject_runtime_dir.c_str();
            }
            NOOK_SPAWN_LOGI("zygote monitor inject pid=%d process=%s agent=%s",
                            zygote_pid,
                            process_name.c_str(),
                            agent_path.c_str());
            if (!inject_zygote(zygote_pid, inject_arg)) {
                record_transaction_failure_state(ZygoteControlFailureState::kInjectAgent);
                std::string local_error = "inject zygote agent failed";
                if (ops_.get_inject_error) {
                    const std::string detail = ops_.get_inject_error();
                    if (!detail.empty()) {
                        local_error += ": ";
                        local_error += detail;
                    }
                }
                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                     "fail",
                                                                     request.identifier,
                                                                     "inject-agent",
                                                                     local_error)
                                    .c_str());
                NOOK_SPAWN_LOGE("zygote monitor inject failed pid=%d process=%s error=%s",
                                zygote_pid,
                                process_name.c_str(),
                                local_error.c_str());
                if (IsSoftZygoteControlInjectFailure(local_error)) {
                    NOOK_SPAWN_LOGI("zygote monitor inject degraded to fallback backend pid=%d process=%s",
                                    zygote_pid,
                                    process_name.c_str());
                    if (is_required_target) {
                        FailZygoteControlSpawn(&result.owned_transaction,
                                               nullptr,
                                               local_error,
                                               &result.error_message);
                        return result;
                    }
                    continue;
                }
                if (is_required_target) {
                    FailZygoteControlSpawn(&result.owned_transaction,
                                           nullptr,
                                           local_error,
                                           &result.error_message);
                    return result;
                }
                NOOK_SPAWN_LOGI("zygote monitor optional target inject skipped pid=%d process=%s",
                                zygote_pid,
                                process_name.c_str());
                continue;
            }
        }

        if (use_legacy_spawn_control_side_channel) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                 "enter",
                                                                 request.identifier,
                                                                 "arm-control",
                                                                 process_name)
                                .c_str());
            record_transaction_lifecycle_stage(ZygoteControlFailureState::kArmControl);
            const bool armed = ops_.set_zygote_spawn_control
                                   ? ops_.set_zygote_spawn_control(zygote_pid,
                                                                   request.identifier.c_str(),
                                                                   spawn_token.c_str(),
                                                                   strict_zygote_control_requested)
                                   : ninjector::SetZygoteSpawnControl(
                                         zygote_pid,
                                         request.identifier.c_str(),
                                         spawn_token.c_str(),
                                         strict_zygote_control_requested);
            if (!armed) {
                record_transaction_failure_state(ZygoteControlFailureState::kArmControl);
                std::string local_error = "set zygote spawn control failed";
                if (ops_.get_inject_error) {
                    const std::string detail = ops_.get_inject_error();
                    if (!detail.empty()) {
                        local_error += ": ";
                        local_error += detail;
                    }
                }
                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                     "fail",
                                                                     request.identifier,
                                                                     "arm-control",
                                                                     local_error)
                                    .c_str());
                NOOK_SPAWN_LOGE("zygote monitor arm failed pid=%d process=%s error=%s",
                                zygote_pid,
                                process_name.c_str(),
                                local_error.c_str());
                if (strict_helper_local_control) {
                    std::string uninstall_error;
                    if (!uninstall_helper_only_local_target(target, &uninstall_error) &&
                        !uninstall_error.empty()) {
                        local_error += "; rollback failed: ";
                        local_error += uninstall_error;
                    }
                }
                if (is_required_target) {
                    FailZygoteControlSpawn(&result.owned_transaction,
                                           nullptr,
                                           local_error,
                                           &result.error_message);
                    return result;
                }
                continue;
            }
        }

        if (install_zygote_hook_ && !strict_helper_local_control) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                 "enter",
                                                                 request.identifier,
                                                                 "install-hook",
                                                                 process_name)
                                .c_str());
            record_transaction_lifecycle_stage(ZygoteControlFailureState::kInstallHook);
            std::string install_error;
            if (!install_zygote_hook_(zygote_pid,
                                      process_name,
                                      agent_path,
                                      request.identifier,
                                      spawn_token,
                                      &install_error)) {
                record_transaction_failure_state(
                    InferZygoteControlLifecycleStateFromError(install_error) == ZygoteControlFailureState::kReadyWait
                        ? ZygoteControlFailureState::kReadyWait
                        : ZygoteControlFailureState::kInstallHook);
                std::string local_error = "install zygote fork hook failed";
                if (!install_error.empty()) {
                    local_error += ": ";
                    local_error += install_error;
                }
                if (use_legacy_spawn_control_side_channel) {
                    const bool cleared = ops_.clear_zygote_spawn_control
                                             ? ops_.clear_zygote_spawn_control(zygote_pid,
                                                                               spawn_token.c_str(),
                                                                               strict_zygote_control_requested)
                                             : ninjector::ClearZygoteSpawnControl(
                                                   zygote_pid,
                                                   spawn_token.c_str(),
                                                   strict_zygote_control_requested);
                    if (!cleared) {
                        std::string clear_error = "clear zygote spawn control failed";
                        if (ops_.get_inject_error) {
                            const std::string detail = ops_.get_inject_error();
                            if (!detail.empty()) {
                                clear_error += ": ";
                                clear_error += detail;
                            }
                        }
                        local_error += "; rollback failed: ";
                        local_error += clear_error;
                    }
                }
                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                     "fail",
                                                                     request.identifier,
                                                                     InferZygoteControlLifecycleStateFromError(install_error) ==
                                                                             ZygoteControlFailureState::kReadyWait
                                                                         ? "ready-wait"
                                                                         : "install-hook",
                                                                     local_error)
                                    .c_str());
                NOOK_SPAWN_LOGE("zygote monitor install failed pid=%d process=%s error=%s",
                                zygote_pid,
                                process_name.c_str(),
                                local_error.c_str());
                if (IsSoftZygoteControlInstallFailure(install_error)) {
                    NOOK_SPAWN_LOGI("zygote monitor install degraded to fallback backend pid=%d process=%s",
                                    zygote_pid,
                                    process_name.c_str());
                    if (is_required_target) {
                        FailZygoteControlSpawn(&result.owned_transaction,
                                               nullptr,
                                               local_error,
                                               &result.error_message);
                        return result;
                    }
                    continue;
                }
                if (is_required_target) {
                    FailZygoteControlSpawn(&result.owned_transaction,
                                           nullptr,
                                           local_error,
                                           &result.error_message);
                    return result;
                }
                continue;
            }
        }

        armed_targets.push_back(target);
    }

    if (armed_targets.empty()) {
        record_transaction_failure_state(ZygoteControlFailureState::kTargetsArmed);
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                             "fail",
                                                             request.identifier,
                                                             "targets-armed",
                                                             "no zygote-monitor targets armed")
                            .c_str());
        FailZygoteControlSpawn(&result.owned_transaction,
                               nullptr,
                               "no zygote-monitor targets armed",
                               &result.error_message);
        return result;
    }

    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                         "enter",
                                                         request.identifier,
                                                         "targets-armed",
                                                         config_.spawn_source_process)
                        .c_str());
    record_transaction_lifecycle_stage(ZygoteControlFailureState::kTargetsArmed);
    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                         "enter",
                                                         request.identifier,
                                                         "launch-app",
                                                         config_.spawn_source_process)
                        .c_str());
    record_transaction_lifecycle_stage(ZygoteControlFailureState::kLaunchApp);
    for (const auto& armed_target : armed_targets) {
        DumpProcessFdSnapshotForDebug(armed_target.first,
                                      armed_target.second,
                                      "pre-launch");
    }
    if (!ops_.start_target_app(request.identifier.c_str())) {
        record_transaction_failure_state(ZygoteControlFailureState::kLaunchApp);
        std::string rollback_error;
        (void)rollback_armed_targets(armed_targets, &rollback_error);
        if (!rollback_error.empty()) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                                 "fail",
                                                                 request.identifier,
                                                                 "launch-app",
                                                                 std::string("start_target_app failed; rollback failed: ") + rollback_error)
                                .c_str());
            FailZygoteControlSpawn(&result.owned_transaction,
                                   &armed_targets,
                                   std::string("start_target_app failed; rollback failed: ") +
                                       rollback_error,
                                   &result.error_message);
            return result;
        }
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                             "fail",
                                                             request.identifier,
                                                             "launch-app",
                                                             "start_target_app failed")
                            .c_str());
        FailZygoteControlSpawn(&result.owned_transaction,
                               &armed_targets,
                               "start_target_app failed",
                               &result.error_message);
        return result;
    }

    result.success = true;
    result.pid = 1;
    result.owned_transaction.identifier = request.identifier;
    result.owned_transaction.spawn_token = spawn_token;
    result.owned_transaction.helper_only_local_control = strict_helper_local_control;
    SnapshotCurrentZygoteControlTransactionState(&result.owned_transaction, &armed_targets);
    NOOK_SPAWN_LOGI("zygote monitor armed pkg=%s source=%s targets=%zu",
                    request.identifier.c_str(),
                    config_.spawn_source_process.c_str(),
                    armed_targets.size());
    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                         "leave",
                                                         request.identifier,
                                                         "success",
                                                         "")
                        .c_str());
    ClearZygoteControlLifecycleStage();
    ClearZygoteControlFailureState();
    result.error_message.clear();
    return result;
}

bool NinjectorSpawnInjector::SpawnViaSymbi(const comm::SpawnRequest& request,
                                           const std::string& agent_path,
                                           int* pid,
                                           SpawnOwnedState* owned_state,
                                           std::string* error_message) {
    const std::string spawn_token = ExtractSpawnToken(request);
    if (config_.spawn_source_process.empty()) {
        SetError(error_message, "spawn source process is empty");
        return false;
    }
    if (!ops_.get_pid || !ops_.spawn_symbi) {
        SetError(error_message, "symbi spawn ops are incomplete");
        return false;
    }

    const bool use_embedded_symbi_agent = ShouldUseEmbeddedSymbiAgent(agent_path, ops_);

    std::string resolved_agent_path;
    bool materialized_embedded_agent = false;
    std::string runtime_dir;
    if (use_embedded_symbi_agent) {
        runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
        if (runtime_dir.empty()) {
            runtime_dir = RuntimeDirectoryFromAgentPath(agent_path);
        }
        if (runtime_dir.empty()) {
            SetError(error_message, "agent runtime directory is empty");
            return false;
        }
        resolved_agent_path = kEmbeddedAgentSentinel;
    } else {
        std::string resolved_agent_error;
        if (!EnsureLegacyAgentReady(agent_path,
                                    &resolved_agent_path,
                                    &materialized_embedded_agent,
                                    &resolved_agent_error)) {
            SetError(error_message, resolved_agent_error.empty() ? "agent path is empty" : resolved_agent_error);
            return false;
        }

        runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
        if (runtime_dir.empty()) {
            runtime_dir = RuntimeDirectoryFromAgentPath(resolved_agent_path);
        }
        if (runtime_dir.empty()) {
            MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
            SetError(error_message, "agent runtime directory is empty");
            return false;
        }
    }

    const int zygote_pid = ops_.get_pid(config_.spawn_source_process.c_str());
    if (zygote_pid <= 0) {
        MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
        SetError(error_message, "spawn source process not found");
        return false;
    }

    const bool spawn_ok = use_embedded_symbi_agent
        ? ops_.spawn_symbi_embedded(zygote_pid,
                                    request.identifier.c_str(),
                                    runtime_dir.c_str(),
                                    spawn_token.c_str(),
                                    pid)
        : ops_.spawn_symbi(zygote_pid,
                           request.identifier.c_str(),
                           resolved_agent_path.c_str(),
                           runtime_dir.c_str(),
                           spawn_token.c_str(),
                           pid);
    if (!spawn_ok) {
        std::string error = "spawn_symbi failed";
        if (ops_.get_inject_error) {
            const std::string detail = ops_.get_inject_error();
            if (!detail.empty()) {
                error += ": ";
                error += detail;
            }
        }
        MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
        SetError(error_message, error);
        return false;
    }

    if (owned_state != nullptr) {
        owned_state->spawn_token = spawn_token;
        owned_state->agent_path = use_embedded_symbi_agent ? agent_path : resolved_agent_path;
        owned_state->materialized_agent = materialized_embedded_agent;
    }
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::SpawnViaLegacyNcore(const comm::SpawnRequest& request,
                                                 const std::string& agent_path,
                                                 int* pid,
                                                 SpawnOwnedState* owned_state,
                                                 std::string* error_message) {
    const std::string spawn_token = ExtractSpawnToken(request);
    if (config_.spawn_source_process.empty()) {
        SetError(error_message, "spawn source process is empty");
        return false;
    }
    if (!ops_.get_pid || !ops_.prepare_spawn || !ops_.clear_spawn ||
        !ops_.start_target_app) {
        SetError(error_message, "ninjector spawn ops are incomplete");
        return false;
    }

    std::string resolved_agent_path;
    bool materialized_embedded_agent = false;
    std::string resolved_agent_error;
    if (!EnsureLegacyAgentReady(agent_path,
                                &resolved_agent_path,
                                &materialized_embedded_agent,
                                &resolved_agent_error)) {
        SetError(error_message, resolved_agent_error.empty() ? "agent path is empty" : resolved_agent_error);
        return false;
    }

    const int zygote_pid = ops_.get_pid(config_.spawn_source_process.c_str());
    if (zygote_pid <= 0) {
        SetError(error_message, "spawn source process not found");
        return false;
    }

    std::string runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
    if (runtime_dir.empty()) {
        runtime_dir = RuntimeDirectoryFromAgentPath(resolved_agent_path);
    }
    if (runtime_dir.empty()) {
        SetError(error_message, "agent runtime directory is empty");
        return false;
    }

    std::string resolved_ncore_path = kEmbeddedNcoreSentinel;
    bool materialized_embedded_ncore = false;
    std::string embedded_prepare_error;
    bool prepared = ninjector::PrepareSpawnInZygoteEmbedded(zygote_pid,
                                                            request.identifier.c_str(),
                                                            resolved_agent_path.c_str(),
                                                            runtime_dir.c_str(),
                                                            spawn_token.c_str());
    if (!prepared) {
        if (ops_.get_inject_error) {
            embedded_prepare_error = ops_.get_inject_error();
        }

        if (!ShouldAllowLegacyNcoreSidecarFallback()) {
            std::string error = "prepare_spawn_in_zygote_embedded failed";
            if (!embedded_prepare_error.empty()) {
                error += ": ";
                error += embedded_prepare_error;
            }
            NOOK_SPAWN_LOGE("legacy spawn embedded path failed without sidecar fallback pkg=%s error=%s",
                            request.identifier.c_str(),
                            error.c_str());
            SetError(error_message, error);
            return false;
        }

        std::string resolved_ncore_error;
        if (!EnsureLegacyNcoreReady(&resolved_ncore_path,
                                    &materialized_embedded_ncore,
                                    &resolved_ncore_error)) {
            SetError(error_message,
                     resolved_ncore_error.empty() ? "ncore path is empty" : resolved_ncore_error);
            return false;
        }

        if (!ops_.prepare_spawn(zygote_pid,
                                resolved_ncore_path.c_str(),
                                request.identifier.c_str(),
                                resolved_agent_path.c_str(),
                                runtime_dir.c_str(),
                                spawn_token.c_str())) {
            std::string error = "prepare_spawn_in_zygote failed";
            if (!embedded_prepare_error.empty()) {
                error += " (embedded: ";
                error += embedded_prepare_error;
                error += ")";
            }
            if (ops_.get_inject_error) {
                const std::string detail = ops_.get_inject_error();
                if (!detail.empty()) {
                    error += ": ";
                    error += detail;
                }
            }
            SetError(error_message, error);
            return false;
        }
    }

    if (!ops_.start_target_app(request.identifier.c_str())) {
        std::string error = "start_target_app failed";
        if (ops_.clear_spawn != nullptr &&
            !ops_.clear_spawn(zygote_pid,
                              resolved_ncore_path.c_str(),
                              runtime_dir.c_str(),
                              spawn_token.c_str())) {
            error += "; rollback failed: clear_spawn_in_zygote failed";
        }
        SetError(error_message, error);
        return false;
    }

    if (pid != nullptr) {
        *pid = 1;
    }
    NOOK_SPAWN_LOGI("legacy spawn armed pkg=%s source=%s",
                    request.identifier.c_str(),
                    config_.spawn_source_process.c_str());
    if (owned_state != nullptr) {
        owned_state->spawn_token = spawn_token;
        owned_state->ncore_path = resolved_ncore_path;
        owned_state->agent_path = resolved_agent_path;
        owned_state->materialized_ncore = materialized_embedded_ncore;
        owned_state->materialized_agent = materialized_embedded_agent;
    }
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::FinalizeZygoteControlSpawn(const comm::SpawnRequest& request,
                                                        ZygoteControlOwnedTransaction* owned_transaction,
                                                        std::string* error_message) {
    auto set_finalize_lifecycle_state = [&](ZygoteControlFailureState state) {
        if (owned_transaction != nullptr) {
            owned_transaction->lifecycle_state = state;
        }
        RecordZygoteControlLifecycleStage(state);
    };
    auto set_finalize_failure_state = [&](ZygoteControlFailureState state) {
        if (owned_transaction != nullptr) {
            owned_transaction->failure_state = state;
        }
        RecordZygoteControlFailureState(state);
    };
    auto clear_finalize_state = [&]() {
        if (owned_transaction != nullptr) {
            owned_transaction->failure_state = ZygoteControlFailureState::kUnknown;
            owned_transaction->lifecycle_state = ZygoteControlFailureState::kUnknown;
        }
        ClearZygoteControlLifecycleStage();
        ClearZygoteControlFailureState();
    };

    clear_finalize_state();
    std::vector<std::pair<int, std::string>> targets;
    if (owned_transaction != nullptr) {
        targets = owned_transaction->targets;
    }
    if (targets.empty()) {
        const int zygote_pid = ops_.get_pid ? ops_.get_pid(config_.spawn_source_process.c_str()) : -1;
        if (zygote_pid > 0) {
            targets.emplace_back(zygote_pid, config_.spawn_source_process);
        }
    }
    if (targets.empty()) {
        SetError(error_message, "spawn source process not found");
        return false;
    }

    const std::string spawn_token =
        (owned_transaction != nullptr && !owned_transaction->spawn_token.empty())
            ? owned_transaction->spawn_token
            : ExtractSpawnToken(request);
    const bool helper_only_local_control =
        owned_transaction != nullptr && owned_transaction->helper_only_local_control;
    const bool use_legacy_spawn_control_side_channel =
        helper_only_local_control ||
        ShouldUseLegacyZygoteSpawnControlSideChannel(static_cast<bool>(install_zygote_hook_));
    auto uninstall_helper_only_local_target = [&](const std::pair<int, std::string>& target,
                                                  std::string* uninstall_error) -> bool {
        if (uninstall_error != nullptr) {
            uninstall_error->clear();
        }

        const auto uninstall_local_target =
            ops_.uninstall_embedded_zygote_control_hooks
                ? ops_.uninstall_embedded_zygote_control_hooks
                : std::function<bool(int)>(ninjector::UninstallEmbeddedZygoteControlHooksByPid);
        if (uninstall_local_target && uninstall_local_target(target.first)) {
            return true;
        }

        if (uninstall_error != nullptr) {
            *uninstall_error = ninjector::GetLastInjectError();
            if (uninstall_error->empty()) {
                *uninstall_error = "embedded helper uninstall failed";
            }
        }
        return false;
    };

    if (helper_only_local_control) {
        for (const auto& target : targets) {
            std::string uninstall_error;
            if (!uninstall_helper_only_local_target(target, &uninstall_error)) {
                set_finalize_failure_state(ZygoteControlFailureState::kFinalizeClear);
                set_finalize_lifecycle_state(ZygoteControlFailureState::kFinalizeClear);
                const std::string local_error =
                    uninstall_error.empty()
                        ? "uninstall zygote helper hooks failed"
                        : "uninstall zygote helper hooks failed: " + uninstall_error;
                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlLifecycleStageLog("finalize-lifecycle",
                                                                     "fail",
                                                                     request.identifier,
                                                                     "finalize-uninstall",
                                                                     local_error)
                                    .c_str());
                SetError(error_message, local_error);
                return false;
            }
        }
    }

    for (const auto& target : targets) {
        if (uninstall_zygote_hook_ && !helper_only_local_control) {
            std::string uninstall_error;
            if (!uninstall_zygote_hook_(target.first, target.second, &uninstall_error)) {
                if (IsMissingZygoteControlSessionError(uninstall_error)) {
                    NOOK_SPAWN_LOGI("zygote monitor uninstall skipped missing-session pid=%d process=%s error=%s",
                                    target.first,
                                    target.second.c_str(),
                                    uninstall_error.empty() ? "(empty)" : uninstall_error.c_str());
                } else {
                    NOOK_SPAWN_LOGI("zygote monitor uninstall skipped pid=%d process=%s error=%s",
                                    target.first,
                                    target.second.c_str(),
                                    uninstall_error.empty() ? "(empty)" : uninstall_error.c_str());
                }
            }
        }
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlLifecycleStageLog("finalize-lifecycle",
                                                             "enter",
                                                             request.identifier,
                                                             "finalize-clear",
                                                             target.second)
                            .c_str());
        set_finalize_lifecycle_state(ZygoteControlFailureState::kFinalizeClear);
        if (use_legacy_spawn_control_side_channel) {
            const bool cleared = ops_.clear_zygote_spawn_control
                                     ? ops_.clear_zygote_spawn_control(target.first,
                                                                       spawn_token.c_str(),
                                                                       helper_only_local_control)
                                     : ninjector::ClearZygoteSpawnControl(
                                           target.first,
                                           spawn_token.c_str(),
                                           helper_only_local_control);
            if (!cleared) {
                set_finalize_failure_state(ZygoteControlFailureState::kFinalizeClear);
                set_finalize_lifecycle_state(ZygoteControlFailureState::kFinalizeClear);
                std::string local_error = "clear zygote spawn control failed";
                if (ops_.get_inject_error) {
                    const std::string detail = ops_.get_inject_error();
                    if (!detail.empty()) {
                        local_error += ": ";
                        local_error += detail;
                    }
                }
                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlLifecycleStageLog("finalize-lifecycle",
                                                                     "fail",
                                                                     request.identifier,
                                                                     "finalize-clear",
                                                                     local_error)
                                    .c_str());
                SetError(error_message, local_error);
                return false;
            }
        }
    }

    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlLifecycleStageLog("finalize-lifecycle",
                                                         "leave",
                                                         request.identifier,
                                                         "success",
                                                         "")
                        .c_str());
    clear_finalize_state();
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::FinalizeLegacySpawn(const comm::SpawnRequest& request,
                                                 const std::string& owned_spawn_token,
                                                 const std::string& owned_ncore_path,
                                                 bool owned_materialized_ncore,
                                                 const std::string& owned_agent_path,
                                                 bool owned_materialized_agent,
                                                 std::string* error_message) {
    const std::string spawn_token =
        !owned_spawn_token.empty() ? owned_spawn_token : ExtractSpawnToken(request);
    const std::string resolved_ncore_path =
        !owned_ncore_path.empty() ? owned_ncore_path : config_.ncore_path;
    const bool materialized_embedded_ncore = owned_materialized_ncore;
    const std::string resolved_agent_path = owned_agent_path;
    const bool materialized_embedded_agent = owned_materialized_agent;
    std::string runtime_dir = ResolveAuthoritativeRuntimeDirectory(config_);
    if (runtime_dir.empty()) {
        runtime_dir = RuntimeDirectoryFromAgentPath(resolved_agent_path);
    }
    if (!ops_.get_pid || !ops_.clear_spawn) {
        SetError(error_message, "legacy spawn finalize ops are incomplete");
        return false;
    }

    const int zygote_pid = ops_.get_pid(config_.spawn_source_process.c_str());
    if (zygote_pid <= 0) {
        SetError(error_message, "spawn source process not found");
        return false;
    }

    if (!ops_.clear_spawn(zygote_pid,
                          resolved_ncore_path.c_str(),
                          runtime_dir.empty() ? nullptr : runtime_dir.c_str(),
                          spawn_token.c_str())) {
        SetError(error_message, "clear_spawn_in_zygote failed");
        return false;
    }

    MaybeCleanupLegacyNcoreArtifact(resolved_ncore_path, materialized_embedded_ncore);
    MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);

    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::TakeActiveOwnerForFinalize(
    const std::string& identifier,
    SpawnOwnershipState* finalize_owner,
    SpawnOwnedState* owned_spawn_state,
    ZygoteControlOwnedTransaction* owned_zygote_transaction,
    bool* has_foreign_active_owner,
    bool*) {
    if (finalize_owner != nullptr) {
        *finalize_owner = SpawnOwnershipState::kNone;
    }
    if (owned_spawn_state != nullptr) {
        *owned_spawn_state = SpawnOwnedState{};
    }
    if (owned_zygote_transaction != nullptr) {
        *owned_zygote_transaction = ZygoteControlOwnedTransaction{};
    }
    if (has_foreign_active_owner != nullptr) {
        *has_foreign_active_owner = false;
    }
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    const SpawnOwnedState* authoritative_spawn_owner =
        ResolveAuthoritativeSpawnOwner(active_spawn_owner_);
    const bool matches_spawn_owner =
        authoritative_spawn_owner != nullptr && authoritative_spawn_owner->identifier == identifier;
    const bool matches_zygote_transaction =
        HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction) &&
        active_spawn_owner_.zygote_control_transaction.identifier == identifier;

    if (matches_spawn_owner) {
        if (finalize_owner != nullptr) {
            *finalize_owner = ResolveOwnershipStateFromBackend(authoritative_spawn_owner->backend);
        }
        if (owned_spawn_state != nullptr) {
            *owned_spawn_state = std::move(*authoritative_spawn_owner);
        }
        ClearAuthoritativeSpawnOwnerSlot(authoritative_spawn_owner);
        if (owned_zygote_transaction != nullptr && matches_zygote_transaction) {
            *owned_zygote_transaction =
                std::move(active_spawn_owner_.zygote_control_transaction);
            active_spawn_owner_.zygote_control_transaction =
                ZygoteControlOwnedTransaction{};
        }
    }
    if (matches_zygote_transaction) {
        if (owned_zygote_transaction != nullptr &&
            !HasZygoteControlTransactionRecord(*owned_zygote_transaction)) {
            *owned_zygote_transaction =
                std::move(active_spawn_owner_.zygote_control_transaction);
        }
        active_spawn_owner_.zygote_control_transaction =
            ZygoteControlOwnedTransaction{};
        if (finalize_owner != nullptr) {
            *finalize_owner = SpawnOwnershipState::kZygoteControlOwned;
        }
        if (matches_spawn_owner || authoritative_spawn_owner == nullptr) {
            active_spawn_owner_.spawn_state = SpawnOwnedState{};
        }
        if (owned_spawn_state != nullptr) {
            if (matches_spawn_owner) {
                owned_spawn_state->backend = SpawnBackend::kNone;
                owned_spawn_state->ncore_path.clear();
                owned_spawn_state->agent_path.clear();
                owned_spawn_state->materialized_ncore = false;
                owned_spawn_state->materialized_agent = false;
            } else {
                *owned_spawn_state = SpawnOwnedState{};
            }
        }
    }
    if (has_foreign_active_owner != nullptr) {
        const SpawnOwnedState* remaining_authoritative_spawn_owner =
            ResolveAuthoritativeSpawnOwner(active_spawn_owner_);
        const bool has_foreign_spawn_owner =
            remaining_authoritative_spawn_owner != nullptr &&
            remaining_authoritative_spawn_owner->identifier != identifier;
        const bool has_foreign_zygote_transaction =
            HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction) &&
            active_spawn_owner_.zygote_control_transaction.identifier != identifier;
        *has_foreign_active_owner =
            has_foreign_spawn_owner || has_foreign_zygote_transaction;
    }
    return true;
}

bool NinjectorSpawnInjector::ReleaseActiveOwnerAfterDeferredRouting(
    const std::string& identifier,
    const std::string& spawn_token) {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    const SpawnOwnedState* authoritative_spawn_owner =
        ResolveAuthoritativeSpawnOwner(active_spawn_owner_);
    const bool matches_zygote_transaction =
        HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction) &&
        active_spawn_owner_.zygote_control_transaction.identifier == identifier &&
        active_spawn_owner_.zygote_control_transaction.spawn_token == spawn_token;
    const bool matches_spawn_owner =
        authoritative_spawn_owner != nullptr && authoritative_spawn_owner->identifier == identifier &&
        authoritative_spawn_owner->spawn_token == spawn_token;

    if (matches_zygote_transaction) {
        active_spawn_owner_.zygote_control_transaction = ZygoteControlOwnedTransaction{};
        if (authoritative_spawn_owner == nullptr || authoritative_spawn_owner->identifier == identifier) {
            ClearAuthoritativeSpawnOwnerSlot(authoritative_spawn_owner);
        }
        return true;
    }

    if (matches_spawn_owner) {
        ClearAuthoritativeSpawnOwnerSlot(authoritative_spawn_owner);
        return true;
    }

    return false;
}

NinjectorSpawnInjector::FinalizeSession
NinjectorSpawnInjector::BuildFinalizeSession(const comm::SpawnRequest& request) {
    FinalizeSession session;
    (void)TakeActiveOwnerForFinalize(request.identifier,
                                     &session.finalize_owner,
                                     &session.owned_spawn_state,
                                     &session.owned_zygote_transaction,
                                     &session.has_foreign_active_owner,
                                     nullptr);
    return session;
}

bool NinjectorSpawnInjector::FinalizeOwnedSpawnByOwner(
    const comm::SpawnRequest& request,
    SpawnOwnershipState finalize_owner,
    const SpawnOwnedState& owned_spawn_state,
    ZygoteControlOwnedTransaction* owned_zygote_transaction,
    std::string* error_message) {
    if (finalize_owner == SpawnOwnershipState::kZygoteControlOwned) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlFinalizeDecisionLog("owned-zygote-control",
                                                               request.identifier,
                                                               "zygote-control",
                                                               "direct teardown")
                            .c_str());
        return FinalizeZygoteControlSpawn(request, owned_zygote_transaction, error_message);
    }
    if (finalize_owner == SpawnOwnershipState::kSymbiOwned) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlFinalizeDecisionLog("owned-symbi",
                                                               request.identifier,
                                                               "symbi",
                                                               "cleanup materialized agent only")
                            .c_str());
        MaybeCleanupLegacyAgentArtifact(owned_spawn_state.agent_path,
                                        owned_spawn_state.materialized_agent);
        SetError(error_message, "");
        return true;
    }
    if (finalize_owner == SpawnOwnershipState::kLegacyOwned) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlFinalizeDecisionLog("owned-legacy",
                                                               request.identifier,
                                                               "legacy-ncore",
                                                               "direct clear path")
                            .c_str());
        return FinalizeLegacySpawn(request,
                                   owned_spawn_state.spawn_token,
                                   owned_spawn_state.ncore_path,
                                   owned_spawn_state.materialized_ncore,
                                   owned_spawn_state.agent_path,
                                   owned_spawn_state.materialized_agent,
                                   error_message);
    }

    SetError(error_message, "");
    return false;
}

bool NinjectorSpawnInjector::FinalizeWithoutOwnedBackend(
    const comm::SpawnRequest& request,
    bool has_foreign_active_owner,
    const ZygoteControlOwnedTransaction&,
    std::string* error_message) {
    if (has_foreign_active_owner) {
        SetError(error_message, "");
        return true;
    }

    std::string legacy_error;
    if (FinalizeLegacySpawn(request,
                            std::string(),
                            std::string(),
                            false,
                            std::string(),
                            false,
                            &legacy_error)) {
        SetError(error_message, "");
        return true;
    }
    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlTerminalOutcomeLog("finalize-result",
                                                          legacy_error.empty() ? "success" : "fail",
                                                          request.identifier,
                                                          "legacy",
                                                          "none",
                                                          "unknown",
                                                          legacy_error)
                        .c_str());
    SetError(error_message, legacy_error);
    return false;
}

ZygoteControlFailureState NinjectorSpawnInjector::ResolveCurrentZygoteControlState(
    const std::string& detail) const {
    const ZygoteControlFailureState recorded_failure_state = ReadZygoteControlFailureState();
    if (recorded_failure_state != ZygoteControlFailureState::kUnknown) {
        return recorded_failure_state;
    }

    const ZygoteControlFailureState recorded_lifecycle_stage = ReadZygoteControlLifecycleStage();
    if (recorded_lifecycle_stage != ZygoteControlFailureState::kUnknown) {
        return recorded_lifecycle_stage;
    }

    return InferZygoteControlLifecycleStateFromError(detail);
}

ZygoteControlFailureState NinjectorSpawnInjector::ResolveTransactionZygoteControlState(
    const ZygoteControlOwnedTransaction* transaction,
    const std::string& detail) const {
    if (transaction != nullptr) {
        if (transaction->failure_state != ZygoteControlFailureState::kUnknown) {
            return transaction->failure_state;
        }
        if (transaction->lifecycle_state != ZygoteControlFailureState::kUnknown) {
            return transaction->lifecycle_state;
        }
    }

    const ZygoteControlFailureState inferred_state =
        InferZygoteControlLifecycleStateFromError(detail);
    if (inferred_state != ZygoteControlFailureState::kUnknown) {
        return inferred_state;
    }

    return ResolveCurrentZygoteControlState(detail);
}

ZygoteControlFailureState NinjectorSpawnInjector::ResolveOutcomeZygoteControlState(
    const SpawnOutcome& outcome) const {
    if (outcome.zygote_control_state != ZygoteControlFailureState::kUnknown) {
        return outcome.zygote_control_state;
    }
    if (HasZygoteControlTransactionRecord(outcome.failed_zygote_control_transaction)) {
        return ResolveTransactionZygoteControlState(&outcome.failed_zygote_control_transaction,
                                                    outcome.zygote_control_error);
    }
    return InferZygoteControlLifecycleStateFromError(outcome.zygote_control_error);
}

bool NinjectorSpawnInjector::FinalizeSpawnOutcome(const comm::SpawnRequest& request,
                                                  const SpawnOutcome& outcome,
                                                  bool explicit_symbi_requested,
                                                  bool allow_symbi_backend,
                                                  bool allow_legacy_backend_fallback,
                                                  std::string* error_message) {
    const ZygoteControlFailureState zygote_error_state =
        ResolveOutcomeZygoteControlState(outcome);

    switch (outcome.final_status) {
    case SpawnFinalStatus::kSuccess:
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                              "success",
                                                              request.identifier,
                                                              SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                              SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                              ZygoteControlFailureStateToString(zygote_error_state),
                                                              outcome.legacy_error)
                            .c_str());
        SetError(error_message, outcome.legacy_error);
        return true;

    case SpawnFinalStatus::kAbort:
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                              "fail",
                                                              request.identifier,
                                                              SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                              SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                              ZygoteControlFailureStateToString(zygote_error_state),
                                                              outcome.zygote_control_error)
                            .c_str());
        SetError(error_message, FormatZygoteControlFinalError("spawn",
                                                              zygote_error_state,
                                                              outcome.zygote_control_error));
        return false;

    case SpawnFinalStatus::kFallbackFailed:
        if (!outcome.zygote_control_error.empty()) {
            if (!allow_legacy_backend_fallback) {
                if (!outcome.symbi_error.empty()) {
                    NOOK_SPAWN_LOGI("%s",
                                    FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                          "fail",
                                                                          request.identifier,
                                                                          SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                          SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                          ZygoteControlFailureStateToString(zygote_error_state),
                                                                          outcome.zygote_control_error + "; symbi failed: " + outcome.symbi_error)
                                        .c_str());
                    SetError(error_message,
                             FormatZygoteControlFinalError("spawn",
                                                           zygote_error_state,
                                                           outcome.zygote_control_error) +
                                 "; symbi failed: " + outcome.symbi_error);
                    return false;
                }

                NOOK_SPAWN_LOGI("%s",
                                FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                      "fail",
                                                                      request.identifier,
                                                                      SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                      SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                      ZygoteControlFailureStateToString(zygote_error_state),
                                                                      outcome.zygote_control_error)
                                    .c_str());
                SetError(error_message, FormatZygoteControlFinalError("spawn",
                                                                      zygote_error_state,
                                                                      outcome.zygote_control_error));
                return false;
            }

            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  outcome.zygote_control_error + "; fallback failed: " + outcome.legacy_error)
                                .c_str());
            SetError(error_message,
                     FormatZygoteControlFinalError("spawn",
                                                   zygote_error_state,
                                                   outcome.zygote_control_error) +
                         "; fallback failed: " + outcome.legacy_error);
            return false;
        }

        if (outcome.symbi_error.empty()) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  outcome.legacy_error)
                                .c_str());
            SetError(error_message, outcome.legacy_error);
            return false;
        }

        {
            const std::string symbi_prefix =
                explicit_symbi_requested ? "symbi spawn failed (--symbi): " : "symbi spawn failed: ";
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  outcome.symbi_error + "; fallback failed: " + outcome.legacy_error)
                                .c_str());
            SetError(error_message,
                     symbi_prefix + outcome.symbi_error +
                         "; fallback failed: " + outcome.legacy_error);
        }
        return false;

    case SpawnFinalStatus::kBackendUnavailable:
        if (!outcome.zygote_control_error.empty()) {
            const std::string backend_error =
                !allow_symbi_backend
                    ? outcome.zygote_control_error
                    : outcome.zygote_control_error + "; symbi failed: symbi spawn backend unavailable";
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  backend_error)
                                .c_str());
            SetError(error_message,
                     !allow_symbi_backend
                         ? FormatZygoteControlFinalError("spawn",
                                                         zygote_error_state,
                                                         outcome.zygote_control_error)
                         : FormatZygoteControlFinalError("spawn",
                                                         zygote_error_state,
                                                         outcome.zygote_control_error) +
                               "; symbi failed: symbi spawn backend unavailable");
            return false;
        }

        {
            const char* unavailable_error = explicit_symbi_requested
                                                ? "symbi spawn failed (--symbi): symbi spawn backend unavailable"
                                                : "symbi spawn failed: symbi spawn backend unavailable";
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  "symbi spawn backend unavailable")
                                .c_str());
            SetError(error_message, unavailable_error);
        }
        return false;

    case SpawnFinalStatus::kHardStop:
        if (!outcome.zygote_control_error.empty()) {
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  outcome.zygote_control_error)
                                .c_str());
            SetError(error_message, FormatZygoteControlFinalError("spawn",
                                                                  zygote_error_state,
                                                                  outcome.zygote_control_error));
            return false;
        }

        {
            const std::string symbi_prefix =
                explicit_symbi_requested ? "symbi spawn failed (--symbi): " : "symbi spawn failed: ";
            NOOK_SPAWN_LOGI("%s",
                            FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                                  "fail",
                                                                  request.identifier,
                                                                  SpawnTerminalBackendToString(outcome.terminal_primary_backend),
                                                                  SpawnTerminalBackendToString(outcome.terminal_secondary_backend),
                                                                  ZygoteControlFailureStateToString(zygote_error_state),
                                                                  outcome.symbi_error)
                                .c_str());
            SetError(error_message, symbi_prefix + outcome.symbi_error);
        }
        return false;

    case SpawnFinalStatus::kUnknown:
        break;
    }

    SetError(error_message, "spawn outcome unresolved");
    return false;
}

void NinjectorSpawnInjector::ClassifyTerminalSpawnOutcome(SpawnOutcome* outcome,
                                                          bool explicit_symbi_requested,
                                                          bool allow_symbi_backend,
                                                          bool allow_legacy_backend_fallback) const {
    if (outcome == nullptr) {
        return;
    }

    const SpawnTerminalBackend symbi_terminal_backend =
        explicit_symbi_requested ? SpawnTerminalBackend::kExplicitSymbi
                                 : SpawnTerminalBackend::kSymbi;

    if (!outcome->zygote_control_error.empty()) {
        if (!allow_legacy_backend_fallback) {
            if (!outcome->symbi_error.empty()) {
                ApplyTerminalOutcomeClassification(outcome,
                                                   SpawnFinalStatus::kFallbackFailed,
                                                   SpawnTerminalBackend::kZygoteControl,
                                                   SpawnTerminalBackend::kSymbi);
            } else if (!allow_symbi_backend) {
                ApplyTerminalOutcomeClassification(outcome,
                                                   SpawnFinalStatus::kHardStop,
                                                   SpawnTerminalBackend::kZygoteControl,
                                                   SpawnTerminalBackend::kNone);
            } else if (!ops_.spawn_symbi) {
                ApplyTerminalOutcomeClassification(outcome,
                                                   SpawnFinalStatus::kBackendUnavailable,
                                                   SpawnTerminalBackend::kZygoteControl,
                                                   SpawnTerminalBackend::kSymbi);
            } else {
                ApplyTerminalOutcomeClassification(outcome,
                                                   SpawnFinalStatus::kHardStop,
                                                   SpawnTerminalBackend::kZygoteControl,
                                                   SpawnTerminalBackend::kNone);
            }
            return;
        }

        if (!outcome->legacy_error.empty()) {
            ApplyTerminalOutcomeClassification(outcome,
                                               SpawnFinalStatus::kFallbackFailed,
                                               SpawnTerminalBackend::kZygoteControl,
                                               SpawnTerminalBackend::kLegacy);
        } else {
            ApplyTerminalOutcomeClassification(outcome,
                                               SpawnFinalStatus::kHardStop,
                                               SpawnTerminalBackend::kZygoteControl,
                                               SpawnTerminalBackend::kNone);
        }
        return;
    }

    if (!outcome->symbi_error.empty()) {
        if (!allow_legacy_backend_fallback) {
            ApplyTerminalOutcomeClassification(outcome,
                                               SpawnFinalStatus::kHardStop,
                                               symbi_terminal_backend,
                                               SpawnTerminalBackend::kNone);
        } else if (!outcome->legacy_error.empty()) {
            ApplyTerminalOutcomeClassification(outcome,
                                               SpawnFinalStatus::kFallbackFailed,
                                               symbi_terminal_backend,
                                               SpawnTerminalBackend::kLegacy);
        } else {
            ApplyTerminalOutcomeClassification(outcome,
                                               SpawnFinalStatus::kHardStop,
                                               symbi_terminal_backend,
                                               SpawnTerminalBackend::kNone);
        }
        return;
    }

    if ((!allow_symbi_backend || !ops_.spawn_symbi) &&
        !allow_legacy_backend_fallback) {
        ApplyTerminalOutcomeClassification(outcome,
                                           SpawnFinalStatus::kBackendUnavailable,
                                           symbi_terminal_backend,
                                           SpawnTerminalBackend::kNone);
        return;
    }

    ApplyTerminalOutcomeClassification(
        outcome,
        outcome->legacy_error.empty() ? SpawnFinalStatus::kSuccess
                                      : SpawnFinalStatus::kFallbackFailed,
        SpawnTerminalBackend::kLegacy,
        SpawnTerminalBackend::kNone);
}

void NinjectorSpawnInjector::ApplyTerminalOutcomeClassification(
    SpawnOutcome* outcome,
    SpawnFinalStatus final_status,
    SpawnTerminalBackend terminal_primary_backend,
    SpawnTerminalBackend terminal_secondary_backend) const {
    if (outcome == nullptr) {
        return;
    }

    outcome->final_status = final_status;
    outcome->terminal_primary_backend = terminal_primary_backend;
    outcome->terminal_secondary_backend = terminal_secondary_backend;
}

void NinjectorSpawnInjector::ApplyFailedZygoteControlClassification(
    SpawnOutcome* outcome,
    const ZygoteControlOwnedTransaction& transaction,
    bool allow_fallback) const {
    if (outcome == nullptr) {
        return;
    }

    SnapshotFailedZygoteControlTransaction(outcome, transaction);
    outcome->fallback_policy =
        allow_fallback ? SpawnFallbackPolicy::kAllowed : SpawnFallbackPolicy::kForbidden;
    outcome->final_status =
        allow_fallback ? SpawnFinalStatus::kUnknown : SpawnFinalStatus::kAbort;
}

bool NinjectorSpawnInjector::ShouldAllowZygoteControlFallback(const SpawnOutcome& outcome,
                                                              bool strict_zygote_control) const {
    if (strict_zygote_control) {
        return false;
    }

    if (outcome.zygote_control_state != ZygoteControlFailureState::kUnknown) {
        return ShouldAllowFallbackForZygoteControlState(outcome.zygote_control_state,
                                                        outcome.zygote_control_error);
    }
    if (HasZygoteControlTransactionRecord(outcome.failed_zygote_control_transaction)) {
        const ZygoteControlFailureState transaction_state =
            ResolveTransactionZygoteControlState(&outcome.failed_zygote_control_transaction,
                                                 outcome.zygote_control_error);
        if (transaction_state != ZygoteControlFailureState::kUnknown) {
            return ShouldAllowFallbackForZygoteControlState(transaction_state,
                                                            outcome.zygote_control_error);
        }
    }

    return ShouldAllowFallbackAfterZygoteControlFailure(outcome.zygote_control_error);
}

bool NinjectorSpawnInjector::CommitSuccessfulSpawnOutcome(SpawnOutcome* outcome,
                                                          SpawnOwnedState owned_state,
                                                          int*,
                                                          std::string* error_message,
                                                          SpawnBackend backend,
                                                          const std::string& identifier,
                                                          ZygoteControlOwnedTransaction owned_transaction) {
    if (outcome == nullptr) {
        SetError(error_message, "spawn outcome is null");
        return false;
    }

    outcome->pending_commit = BuildPendingSpawnCommit(backend,
                                                      identifier,
                                                      std::move(owned_state),
                                                      std::move(owned_transaction));
    outcome->final_status = SpawnFinalStatus::kSuccess;
    CommitPendingSpawn(outcome->pending_commit);
    SetError(error_message, "");
    return true;
}

NinjectorSpawnInjector::PendingSpawnCommit
NinjectorSpawnInjector::BuildPendingSpawnCommit(SpawnBackend backend,
                                                const std::string& identifier,
                                                SpawnOwnedState owned_state,
                                                ZygoteControlOwnedTransaction owned_transaction) const {
    PendingSpawnCommit pending_commit;
    if (IsLegacyShellOwnedBackend(backend)) {
        pending_commit.shell_owner_state = owned_state;
        pending_commit.shell_owner_state.backend = backend;
        pending_commit.shell_owner_state.identifier = identifier;
        owned_state.backend = SpawnBackend::kNone;
        owned_state.identifier.clear();
        owned_state.ncore_path.clear();
        owned_state.agent_path.clear();
        owned_state.materialized_ncore = false;
        owned_state.materialized_agent = false;
    } else if (IsChildOwnedBackend(backend)) {
        owned_state.backend = backend;
        owned_state.identifier = identifier;
        owned_state.ncore_path.clear();
        owned_state.materialized_ncore = false;
    } else {
        owned_state.backend = SpawnBackend::kNone;
        owned_state.identifier.clear();
        owned_state.ncore_path.clear();
        owned_state.agent_path.clear();
        owned_state.materialized_ncore = false;
        owned_state.materialized_agent = false;
    }
    pending_commit.spawn_state = std::move(owned_state);

    if (backend == SpawnBackend::kZygoteControl) {
        if (owned_transaction.identifier.empty()) {
            owned_transaction.identifier = identifier;
        }
        pending_commit.zygote_control_transaction = std::move(owned_transaction);
    }

    return pending_commit;
}

bool NinjectorSpawnInjector::ApplySuccessfulRouteCommit(SpawnOutcome* outcome,
                                                        SpawnBackend backend,
                                                        const std::string& identifier,
                                                        SpawnOwnedState owned_state,
                                                        int committed_pid,
                                                        SpawnFallbackPolicy fallback_policy,
                                                        ZygoteControlOwnedTransaction owned_transaction,
                                                        int* pid,
                                                        std::string* error_message) {
    if (outcome == nullptr) {
        SetError(error_message, "spawn outcome is null");
        return false;
    }

    if (fallback_policy != SpawnFallbackPolicy::kUnknown) {
        outcome->fallback_policy = fallback_policy;
    }
    if (pid != nullptr) {
        *pid = committed_pid;
    }

    return CommitSuccessfulSpawnOutcome(outcome,
                                        std::move(owned_state),
                                        pid,
                                        error_message,
                                        backend,
                                        identifier,
                                        std::move(owned_transaction));
}

bool NinjectorSpawnInjector::ApplySuccessfulZygoteControlAttemptResult(
    SpawnOutcome* outcome,
    const ZygoteControlAttemptResult& attempt,
    SpawnOwnedState owned_state,
    int* pid,
    std::string* error_message) {
    if (!attempt.success) {
        SetError(error_message, "zygote-control attempt was not successful");
        return false;
    }
    return ApplySuccessfulRouteCommit(outcome,
                                      SpawnBackend::kZygoteControl,
                                      attempt.owned_transaction.identifier,
                                      std::move(owned_state),
                                      attempt.pid,
                                      SpawnFallbackPolicy::kUnknown,
                                      attempt.owned_transaction,
                                      pid,
                                      error_message);
}

bool NinjectorSpawnInjector::ApplyZygoteControlRouteAttempt(
    const comm::SpawnRequest& request,
    const ZygoteControlAttemptResult& attempt,
    bool strict_zygote_control,
    SpawnOutcome* outcome,
    int* pid,
    std::string* error_message) {
    if (outcome == nullptr) {
        SetError(error_message, "spawn outcome is null");
        return false;
    }

    if (attempt.success) {
        SpawnOwnedState pending_spawn_state;
        pending_spawn_state.spawn_token = ExtractSpawnToken(request);
        return ApplySuccessfulZygoteControlAttemptResult(outcome,
                                                         attempt,
                                                         std::move(pending_spawn_state),
                                                         pid,
                                                         error_message);
    }

    if (!ApplyFailedZygoteControlAttemptResult(outcome, attempt, strict_zygote_control)) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("abort",
                                                            request.identifier,
                                                            strict_zygote_control,
                                                            "none",
                                                            outcome->zygote_control_error)
                            .c_str());
        SetError(error_message, FormatZygoteControlFinalError("spawn",
                                                              outcome->zygote_control_state,
                                                              outcome->zygote_control_error));
        return false;
    }

    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlSpawnDecisionLog("fallback",
                                                        request.identifier,
                                                        strict_zygote_control,
                                                        "legacy-or-symbi",
                                                        outcome->zygote_control_error)
                        .c_str());
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::ApplySymbiRouteResult(const comm::SpawnRequest& request,
                                                   bool explicit_symbi_requested,
                                                   bool spawn_ok,
                                                   const std::string& symbi_error,
                                                   SpawnOwnedState owned_state,
                                                   SpawnOutcome* outcome,
                                                   int* pid,
                                                   std::string* error_message) {
    if (outcome == nullptr) {
        SetError(error_message, "spawn outcome is null");
        return false;
    }

    if (spawn_ok) {
        return ApplySuccessfulRouteCommit(outcome,
                                          SpawnBackend::kSymbi,
                                          request.identifier,
                                          std::move(owned_state),
                                          pid != nullptr ? *pid : 0,
                                          SpawnFallbackPolicy::kUnknown,
                                          ZygoteControlOwnedTransaction{},
                                          pid,
                                          error_message);
    }

    outcome->symbi_error = symbi_error;
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::ApplyLegacyRouteResult(const comm::SpawnRequest& request,
                                                    bool spawn_ok,
                                                    SpawnOwnedState owned_state,
                                                    const std::string& legacy_error,
                                                    SpawnOutcome* outcome,
                                                    int* pid,
                                                    std::string* error_message) {
    if (outcome == nullptr) {
        SetError(error_message, "spawn outcome is null");
        return false;
    }

    if (spawn_ok) {
        const SpawnFallbackPolicy fallback_policy =
            outcome->zygote_control_error.empty() && outcome->symbi_error.empty()
                ? SpawnFallbackPolicy::kNotNeeded
                : SpawnFallbackPolicy::kAllowed;
        return ApplySuccessfulRouteCommit(outcome,
                                          SpawnBackend::kLegacyNcore,
                                          request.identifier,
                                          std::move(owned_state),
                                          pid != nullptr ? *pid : 0,
                                          fallback_policy,
                                          ZygoteControlOwnedTransaction{},
                                          pid,
                                          error_message);
    }

    outcome->legacy_error = legacy_error;
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::ApplyFailedZygoteControlAttemptResult(
    SpawnOutcome* outcome,
    const ZygoteControlAttemptResult& attempt,
    bool strict_zygote_control) const {
    if (outcome == nullptr) {
        return false;
    }

    outcome->zygote_control_error = attempt.error_message;
    return ApplyFailedZygoteControlOutcome(outcome,
                                           attempt.owned_transaction,
                                           strict_zygote_control);
}

bool NinjectorSpawnInjector::ApplyFailedZygoteControlOutcome(
    SpawnOutcome* outcome,
    const ZygoteControlOwnedTransaction& transaction,
    bool strict_zygote_control) const {
    if (outcome == nullptr) {
        return false;
    }

    const bool allow_fallback = ShouldAllowZygoteControlFallback(*outcome, strict_zygote_control);
    ApplyFailedZygoteControlClassification(outcome, transaction, allow_fallback);
    return allow_fallback;
}

void NinjectorSpawnInjector::SnapshotFailedZygoteControlTransaction(
    SpawnOutcome* outcome,
    const ZygoteControlOwnedTransaction& transaction) const {
    if (outcome == nullptr) {
        return;
    }

    outcome->failed_zygote_control_transaction = transaction;
    const ZygoteControlFailureState inferred_state =
        InferZygoteControlLifecycleStateFromError(outcome->zygote_control_error);
    if (outcome->failed_zygote_control_transaction.failure_state ==
            ZygoteControlFailureState::kUnknown &&
        inferred_state != ZygoteControlFailureState::kUnknown) {
        outcome->failed_zygote_control_transaction.failure_state = inferred_state;
    }
    if (outcome->failed_zygote_control_transaction.lifecycle_state ==
            ZygoteControlFailureState::kUnknown &&
        inferred_state != ZygoteControlFailureState::kUnknown) {
        outcome->failed_zygote_control_transaction.lifecycle_state = inferred_state;
    }
    if (outcome->zygote_control_state == ZygoteControlFailureState::kUnknown) {
        outcome->zygote_control_state =
            ResolveTransactionZygoteControlState(&outcome->failed_zygote_control_transaction,
                                                 outcome->zygote_control_error);
    }
}

void NinjectorSpawnInjector::SnapshotCurrentZygoteControlTransactionState(
    ZygoteControlOwnedTransaction* transaction,
    const std::vector<std::pair<int, std::string>>* targets) const {
    if (transaction == nullptr) {
        return;
    }

    if (transaction->failure_state == ZygoteControlFailureState::kUnknown) {
        transaction->failure_state = ReadZygoteControlFailureState();
    }
    if (transaction->lifecycle_state == ZygoteControlFailureState::kUnknown) {
        transaction->lifecycle_state = ReadZygoteControlLifecycleStage();
    }
    if (targets != nullptr) {
        transaction->targets = *targets;
    }
}

bool NinjectorSpawnInjector::FailZygoteControlSpawn(
    ZygoteControlOwnedTransaction* transaction,
    const std::vector<std::pair<int, std::string>>* targets,
    const std::string& message,
    std::string* error_message) const {
    SnapshotCurrentZygoteControlTransactionState(transaction, targets);
    if (transaction != nullptr) {
        const ZygoteControlFailureState inferred_state =
            InferZygoteControlLifecycleStateFromError(message);
        if (transaction->failure_state == ZygoteControlFailureState::kUnknown) {
            transaction->failure_state = inferred_state;
        }
        if (transaction->lifecycle_state == ZygoteControlFailureState::kUnknown) {
            transaction->lifecycle_state = inferred_state;
        }
    }
    ClearZygoteControlLifecycleStage();
    ClearZygoteControlFailureState();
    SetError(error_message, message);
    return false;
}

NinjectorSpawnInjector::SpawnExecutionPolicy
NinjectorSpawnInjector::BuildSpawnExecutionPolicy(const comm::SpawnRequest& request) const {
    SpawnExecutionPolicy policy;
    const bool strict_zygote_requested =
        config_.enable_zygote_control && IsStrictZygoteControlRequested(request);
    const bool explicit_symbi_requested =
        strict_zygote_requested ? false : IsExplicitSymbiSpawnRequested(request);
    const bool prefer_symbi_by_default =
        !strict_zygote_requested && ShouldPreferSymbiBackendByDefault();

    if (strict_zygote_requested) {
        policy.primary_route = SpawnPrimaryRoute::kStrictZygoteControl;
    } else if (explicit_symbi_requested) {
        policy.primary_route = SpawnPrimaryRoute::kExplicitSymbi;
    } else if (prefer_symbi_by_default) {
        policy.primary_route = SpawnPrimaryRoute::kSymbiDefault;
    } else {
        policy.primary_route = SpawnPrimaryRoute::kLegacyDefault;
    }

    policy.explicit_symbi_requested = explicit_symbi_requested;
    policy.should_try_symbi_first =
        policy.primary_route == SpawnPrimaryRoute::kExplicitSymbi ||
        policy.primary_route == SpawnPrimaryRoute::kSymbiDefault;
    policy.strict_zygote_control = strict_zygote_requested;
    policy.allow_symbi_backend =
        !strict_zygote_requested &&
        (policy.should_try_symbi_first || ShouldAllowSymbiBackendFallback());
    policy.allow_legacy_backend_fallback =
        !strict_zygote_requested &&
        !policy.explicit_symbi_requested &&
        ShouldAllowLegacyNcoreBackendFallback();
    return policy;
}

NinjectorSpawnInjector::SpawnExecutionState
NinjectorSpawnInjector::BuildSpawnExecutionState(const comm::SpawnRequest& request,
                                                 const std::string& agent_path) {
    (void)agent_path;
    SpawnExecutionState state;
    state.phase = SpawnExecutionPhase::kInit;
    state.phase_reason = SpawnExecutionReason::kInitialized;
    state.routing_state = SpawnRoutingState::kNotStarted;
    state.routing_progress = SpawnRoutingProgress::kNotStarted;
    state.current_route_step = SpawnRouteStep::kNone;
    state.zygote_control_route_state = SpawnZygoteControlRouteState::kNotStarted;
    state.routing_windows = SpawnRouteWindows{};
    state.policy = BuildSpawnExecutionPolicy(request);
    return state;
}

bool NinjectorSpawnInjector::ApplySpawnRoutingSnapshot(
    SpawnExecutionState* state,
    const SpawnRoutingSnapshot& snapshot,
    std::string* error_message) const {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }
    if (snapshot.update_routing_state &&
        state->routing_state == SpawnRoutingState::kNotStarted &&
        snapshot.routing_state != SpawnRoutingState::kNotStarted &&
        snapshot.routing_state != SpawnRoutingState::kRunning) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_progress &&
        state->routing_progress == SpawnRoutingProgress::kNotStarted &&
        snapshot.routing_progress != SpawnRoutingProgress::kNotStarted &&
        snapshot.routing_progress != SpawnRoutingProgress::kEnteredRouting) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_current_route_step &&
        state->routing_progress == SpawnRoutingProgress::kNotStarted &&
        !snapshot.update_routing_progress &&
        snapshot.current_route_step != SpawnRouteStep::kNone) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kNotStarted &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kNotStarted &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kSkipped &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kEntered) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kSkipped &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kSkipped) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kEntered &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kEntered &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kCommitted &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kDeferredToFallback &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kAborted) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kCommitted &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kCommitted) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kDeferredToFallback &&
        snapshot.zygote_control_route_state !=
            SpawnZygoteControlRouteState::kDeferredToFallback) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        state->zygote_control_route_state == SpawnZygoteControlRouteState::kAborted &&
        snapshot.zygote_control_route_state != SpawnZygoteControlRouteState::kAborted) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        snapshot.zygote_control_route_state == SpawnZygoteControlRouteState::kEntered &&
        ((snapshot.update_current_route_step
              ? snapshot.current_route_step
              : state->current_route_step) != SpawnRouteStep::kZygoteControl)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        snapshot.zygote_control_route_state == SpawnZygoteControlRouteState::kCommitted &&
        ((snapshot.update_routing_state
              ? snapshot.routing_state
              : state->routing_state) != SpawnRoutingState::kCommittedFromZygoteControl)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        snapshot.zygote_control_route_state == SpawnZygoteControlRouteState::kSkipped &&
        ((snapshot.update_zygote_control_window
              ? snapshot.zygote_control_window
              : state->routing_windows.zygote_control) != SpawnRouteWindowState::kSkippedByPolicy)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        snapshot.zygote_control_route_state ==
            SpawnZygoteControlRouteState::kDeferredToFallback &&
        (((snapshot.update_routing_state
               ? snapshot.routing_state
               : state->routing_state) != SpawnRoutingState::kRunning) ||
         ((snapshot.update_current_route_step
               ? snapshot.current_route_step
               : state->current_route_step) != SpawnRouteStep::kZygoteControl) ||
         ((snapshot.update_zygote_control_window
               ? snapshot.zygote_control_window
               : state->routing_windows.zygote_control) != SpawnRouteWindowState::kEntered))) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_zygote_control_route_state &&
        snapshot.zygote_control_route_state == SpawnZygoteControlRouteState::kAborted &&
        (((snapshot.update_routing_state
               ? snapshot.routing_state
               : state->routing_state) != SpawnRoutingState::kRunning) ||
         ((snapshot.update_current_route_step
               ? snapshot.current_route_step
               : state->current_route_step) != SpawnRouteStep::kZygoteControl) ||
         ((snapshot.update_zygote_control_window
               ? snapshot.zygote_control_window
               : state->routing_windows.zygote_control) != SpawnRouteWindowState::kEntered))) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kCommittedFromZygoteControl &&
        (state->routing_progress != SpawnRoutingProgress::kAfterZygoteControl ||
         state->current_route_step != SpawnRouteStep::kZygoteControl)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kCommittedFromSymbi &&
        (state->routing_progress != SpawnRoutingProgress::kAfterSymbi ||
         state->current_route_step != SpawnRouteStep::kSymbi)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kCommittedFromSymbi &&
        state->ownership_state != SpawnOwnershipState::kSymbiOwned) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kCommittedFromLegacy &&
        (state->routing_progress != SpawnRoutingProgress::kAfterLegacy ||
         state->current_route_step != SpawnRouteStep::kLegacy)) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kCommittedFromLegacy &&
        state->ownership_state != SpawnOwnershipState::kLegacyOwned) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_state &&
        snapshot.routing_state == SpawnRoutingState::kDeferredToTerminal &&
        state->routing_progress != SpawnRoutingProgress::kAfterLegacy) {
        SetError(error_message, "invalid spawn routing snapshot transition");
        return false;
    }
    if (snapshot.update_routing_progress) {
        state->routing_progress = snapshot.routing_progress;
    }
    if (snapshot.update_routing_state) {
        state->routing_state = snapshot.routing_state;
    }
    if (snapshot.update_current_route_step) {
        state->current_route_step = snapshot.current_route_step;
    }
    if (snapshot.update_zygote_control_route_state) {
        state->zygote_control_route_state = snapshot.zygote_control_route_state;
    }
    if (snapshot.update_zygote_control_window) {
        state->routing_windows.zygote_control = snapshot.zygote_control_window;
    }
    if (snapshot.update_symbi_window) {
        state->routing_windows.symbi = snapshot.symbi_window;
    }
    if (snapshot.update_legacy_window) {
        state->routing_windows.legacy = snapshot.legacy_window;
    }
    return true;
}

bool NinjectorSpawnInjector::TransitionSpawnOwnershipState(
    SpawnExecutionState* state,
    SpawnOwnershipState next_state,
    std::string* error_message) const {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }
    if (state->ownership_state != SpawnOwnershipState::kNone &&
        state->ownership_state != next_state) {
        SetError(error_message, "invalid spawn ownership state transition");
        return false;
    }
    state->ownership_state = next_state;
    SetError(error_message, "");
    return true;
}

NinjectorSpawnInjector::SpawnOwnershipState
NinjectorSpawnInjector::ResolveOwnershipStateFromBackend(SpawnBackend backend) const {
    switch (backend) {
    case SpawnBackend::kZygoteControl:
        return SpawnOwnershipState::kZygoteControlOwned;
    case SpawnBackend::kSymbi:
        return SpawnOwnershipState::kSymbiOwned;
    case SpawnBackend::kLegacyNcore:
        return SpawnOwnershipState::kLegacyOwned;
    case SpawnBackend::kNone:
        break;
    }
    return SpawnOwnershipState::kNone;
}

bool NinjectorSpawnInjector::BeginSpawnRouting(SpawnExecutionState* state,
                                               std::string* error_message) const {
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kRouting,
                                       SpawnExecutionReason::kBeginRouting,
                                       error_message)) {
        return false;
    }
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_routing_state = true,
                                         .routing_state = SpawnRoutingState::kRunning,
                                         .update_routing_progress = true,
                                         .routing_progress = SpawnRoutingProgress::kEnteredRouting,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::EnterZygoteControlRoute(SpawnExecutionState* state,
                                                     std::string* error_message) const {
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_current_route_step = true,
                                         .current_route_step = SpawnRouteStep::kZygoteControl,
                                         .update_zygote_control_route_state = true,
                                         .zygote_control_route_state =
                                             SpawnZygoteControlRouteState::kEntered,
                                         .update_zygote_control_window = true,
                                         .zygote_control_window = SpawnRouteWindowState::kEntered,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::SkipZygoteControlRoute(SpawnExecutionState* state,
                                                    std::string* error_message) const {
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_zygote_control_route_state = true,
                                         .zygote_control_route_state =
                                             SpawnZygoteControlRouteState::kSkipped,
                                         .update_zygote_control_window = true,
                                         .zygote_control_window =
                                             SpawnRouteWindowState::kSkippedByPolicy,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::AbortZygoteControlRoute(SpawnExecutionState* state,
                                                     std::string* error_message) const {
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_zygote_control_route_state = true,
                                         .zygote_control_route_state =
                                             SpawnZygoteControlRouteState::kAborted,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::CommitZygoteControlRoute(SpawnExecutionState* state,
                                                      std::string* error_message) const {
    if (!ApplySpawnRoutingSnapshot(state,
                                   SpawnRoutingSnapshot{
                                       .update_routing_progress = true,
                                       .routing_progress = SpawnRoutingProgress::kAfterZygoteControl,
                                   },
                                   error_message)) {
        return false;
    }
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kRouteCommitted,
                                       SpawnExecutionReason::kRouteCommittedFromZygoteControl,
                                       error_message)) {
        return false;
    }
    if (!TransitionSpawnOwnershipState(state,
                                       SpawnOwnershipState::kZygoteControlOwned,
                                       error_message)) {
        return false;
    }
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_routing_state = true,
                                         .routing_state = SpawnRoutingState::kCommittedFromZygoteControl,
                                         .update_zygote_control_route_state = true,
                                         .zygote_control_route_state =
                                             SpawnZygoteControlRouteState::kCommitted,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::CommitNonZygoteControlRoute(SpawnExecutionState* state,
                                                         SpawnBackend backend,
                                                         std::string* error_message) const {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }

    SpawnExecutionReason phase_reason = SpawnExecutionReason::kNone;
    SpawnOwnershipState ownership_state = SpawnOwnershipState::kNone;
    SpawnRoutingState routing_state = SpawnRoutingState::kNotStarted;

    switch (backend) {
    case SpawnBackend::kSymbi:
        phase_reason = SpawnExecutionReason::kRouteCommittedFromSymbi;
        ownership_state = SpawnOwnershipState::kSymbiOwned;
        routing_state = SpawnRoutingState::kCommittedFromSymbi;
        break;
    case SpawnBackend::kLegacyNcore:
        phase_reason = SpawnExecutionReason::kRouteCommittedFromLegacy;
        ownership_state = SpawnOwnershipState::kLegacyOwned;
        routing_state = SpawnRoutingState::kCommittedFromLegacy;
        break;
    case SpawnBackend::kNone:
    case SpawnBackend::kZygoteControl:
        SetError(error_message, "invalid non-zygote route commit backend");
        return false;
    }

    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kRouteCommitted,
                                       phase_reason,
                                       error_message)) {
        return false;
    }
    if (!TransitionSpawnOwnershipState(state, ownership_state, error_message)) {
        return false;
    }
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_routing_state = true,
                                         .routing_state = routing_state,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::ApplyZygoteControlRouting(
    const comm::SpawnRequest& request,
    const std::string& agent_path,
    SpawnExecutionState* state,
    int* pid,
    std::string* error_message) {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }

    if (!EnterZygoteControlRoute(state, error_message)) {
        return false;
    }
    state->zygote_attempt = TrySpawnViaZygoteControl(request, agent_path);
    ZygoteControlAttemptResult& zygote_attempt = state->zygote_attempt;
    if (!ApplyZygoteControlRouteAttempt(request,
                                        zygote_attempt,
                                        state->policy.strict_zygote_control,
                                        &state->outcome,
                                        pid,
                                        error_message)) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("abort",
                                                            request.identifier,
                                                            state->policy.strict_zygote_control,
                                                            "none",
                                                            *error_message)
                            .c_str());
        (void)AbortZygoteControlRoute(state, nullptr);
        return false;
    }
    if (zygote_attempt.success) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("commit",
                                                            request.identifier,
                                                            state->policy.strict_zygote_control,
                                                            "none",
                                                            "zygote-control route committed")
                            .c_str());
        return CommitZygoteControlRoute(state, error_message);
    }
    NOOK_SPAWN_LOGI("%s",
                    FormatZygoteControlSpawnDecisionLog("defer",
                                                        request.identifier,
                                                        state->policy.strict_zygote_control,
                                                        "legacy-or-symbi",
                                                        state->outcome.zygote_control_error)
                        .c_str());
    return DeferZygoteControlRouteToFallback(state, error_message);
}

bool NinjectorSpawnInjector::DeferZygoteControlRouteToFallback(
    SpawnExecutionState* state,
    std::string* error_message) const {
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_zygote_control_route_state = true,
                                         .zygote_control_route_state =
                                             SpawnZygoteControlRouteState::kDeferredToFallback,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::AdvancePastZygoteControlRoute(
    SpawnExecutionState* state,
    std::string* error_message) const {
    return ApplySpawnRoutingSnapshot(state,
                                     SpawnRoutingSnapshot{
                                         .update_routing_progress = true,
                                         .routing_progress = SpawnRoutingProgress::kAfterZygoteControl,
                                     },
                                     error_message);
}

bool NinjectorSpawnInjector::TransitionSpawnExecutionPhase(
    SpawnExecutionState* state,
    SpawnExecutionPhase next_phase,
    SpawnExecutionReason reason,
    std::string* error_message) const {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }

    const SpawnExecutionPhase current_phase = state->phase;
    bool allowed = false;
    switch (current_phase) {
    case SpawnExecutionPhase::kInit:
        allowed = next_phase == SpawnExecutionPhase::kRouting;
        break;
    case SpawnExecutionPhase::kRouting:
        allowed = next_phase == SpawnExecutionPhase::kRouteCommitted ||
                  next_phase == SpawnExecutionPhase::kRouteDeferred;
        break;
    case SpawnExecutionPhase::kRouteCommitted:
        allowed = next_phase == SpawnExecutionPhase::kCompleted;
        break;
    case SpawnExecutionPhase::kRouteDeferred:
        allowed = next_phase == SpawnExecutionPhase::kTerminal;
        break;
    case SpawnExecutionPhase::kTerminal:
        allowed = next_phase == SpawnExecutionPhase::kTerminalResolved;
        break;
    case SpawnExecutionPhase::kTerminalResolved:
        allowed = next_phase == SpawnExecutionPhase::kTerminalFinalized;
        break;
    case SpawnExecutionPhase::kTerminalFinalized:
        allowed = next_phase == SpawnExecutionPhase::kCompleted;
        break;
    case SpawnExecutionPhase::kCompleted:
        allowed = false;
        break;
    }

    if (!allowed) {
        SetError(error_message, "invalid spawn execution phase transition");
        return false;
    }

    state->phase = next_phase;
    state->phase_reason = reason;
    return true;
}

bool NinjectorSpawnInjector::ApplySpawnRoutingAttempts(
    const comm::SpawnRequest& request,
    const std::string& agent_path,
    SpawnExecutionState* state,
    int* pid,
    std::string* error_message) {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }
    if (!BeginSpawnRouting(state, error_message)) {
        return false;
    }
    SpawnExecutionPolicy& policy = state->policy;
    SpawnOutcome& outcome = state->outcome;

    if (policy.primary_route == SpawnPrimaryRoute::kStrictZygoteControl) {
        if (!ApplyZygoteControlRouting(request,
                                       agent_path,
                                       state,
                                       pid,
                                       error_message)) {
            return false;
        }
        if (state->phase == SpawnExecutionPhase::kRouteCommitted) {
            return true;
        }
    } else if (policy.primary_route == SpawnPrimaryRoute::kExplicitSymbi) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("skip-zygote-control-explicit-symbi",
                                                            request.identifier,
                                                            policy.strict_zygote_control,
                                                            "symbi-first",
                                                            "symbi requested via --symbi")
                            .c_str());
        if (!SkipZygoteControlRoute(state, error_message)) {
            return false;
        }
    } else if (policy.primary_route == SpawnPrimaryRoute::kSymbiDefault) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("skip-zygote-control-default-symbi",
                                                            request.identifier,
                                                            policy.strict_zygote_control,
                                                            "symbi-first",
                                                            "default backend prefers symbi")
                            .c_str());
        if (!SkipZygoteControlRoute(state, error_message)) {
            return false;
        }
    } else {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("skip-stable-default",
                                                            request.identifier,
                                                            policy.strict_zygote_control,
                                                            "legacy-default",
                                                            "zygote-control disabled")
                            .c_str());
        if (!SkipZygoteControlRoute(state, error_message)) {
            return false;
        }
    }
    if (!AdvancePastZygoteControlRoute(state, error_message)) {
        return false;
    }

    SpawnOwnedState pending_spawn_state;
    if (policy.allow_symbi_backend && ops_.spawn_symbi) {
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_current_route_step = true,
                                           .current_route_step = SpawnRouteStep::kSymbi,
                                           .update_symbi_window = true,
                                           .symbi_window = SpawnRouteWindowState::kEntered,
                                       },
                                       error_message)) {
            return false;
        }
        pending_spawn_state = {};
        std::string symbi_error;
        const bool symbi_ok =
            SpawnViaSymbi(request, agent_path, pid, &pending_spawn_state, &symbi_error);
        if (!ApplySymbiRouteResult(request,
                                   policy.explicit_symbi_requested,
                                   symbi_ok,
                                   symbi_error,
                                   std::move(pending_spawn_state),
                                   &outcome,
                                   pid,
                                   error_message)) {
            return false;
        }
        if (symbi_ok) {
            if (!ApplySpawnRoutingSnapshot(state,
                                           SpawnRoutingSnapshot{
                                               .update_routing_progress = true,
                                               .routing_progress = SpawnRoutingProgress::kAfterSymbi,
                                           },
                                           error_message)) {
                return false;
            }
            if (!CommitNonZygoteControlRoute(state,
                                             SpawnBackend::kSymbi,
                                             error_message)) {
                return false;
            }
            return true;
        }
    } else if (config_.enable_zygote_control) {
        NOOK_SPAWN_LOGI("%s",
                        FormatZygoteControlSpawnDecisionLog("skip-symbi-fallback",
                                                            request.identifier,
                                                            policy.strict_zygote_control,
                                                            "legacy-default",
                                                            "symbi fallback disabled")
                            .c_str());
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_symbi_window = true,
                                           .symbi_window = SpawnRouteWindowState::kSkippedByPolicy,
                                       },
                                       error_message)) {
            return false;
        }
    } else {
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_symbi_window = true,
                                           .symbi_window = SpawnRouteWindowState::kSkippedByPolicy,
                                       },
                                       error_message)) {
            return false;
        }
    }
    if (!ApplySpawnRoutingSnapshot(state,
                                   SpawnRoutingSnapshot{
                                       .update_routing_progress = true,
                                       .routing_progress = SpawnRoutingProgress::kAfterSymbi,
                                   },
                                   error_message)) {
        return false;
    }

    if (policy.allow_legacy_backend_fallback) {
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_current_route_step = true,
                                           .current_route_step = SpawnRouteStep::kLegacy,
                                           .update_legacy_window = true,
                                           .legacy_window = SpawnRouteWindowState::kEntered,
                                       },
                                       error_message)) {
            return false;
        }
        pending_spawn_state = {};
        std::string legacy_error;
        const bool legacy_ok =
            SpawnViaLegacyNcore(request, agent_path, pid, &pending_spawn_state, &legacy_error);
        if (!ApplyLegacyRouteResult(request,
                                    legacy_ok,
                                    std::move(pending_spawn_state),
                                    legacy_error,
                                    &outcome,
                                    pid,
                                    error_message)) {
            return false;
        }
        if (legacy_ok) {
            if (!ApplySpawnRoutingSnapshot(state,
                                           SpawnRoutingSnapshot{
                                               .update_routing_progress = true,
                                               .routing_progress = SpawnRoutingProgress::kAfterLegacy,
                                           },
                                           error_message)) {
                return false;
            }
            if (!CommitNonZygoteControlRoute(state,
                                             SpawnBackend::kLegacyNcore,
                                             error_message)) {
                return false;
            }
            return true;
        }
    } else if (outcome.zygote_control_error.empty() &&
               (!policy.allow_symbi_backend || !ops_.spawn_symbi)) {
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_current_route_step = true,
                                           .current_route_step = SpawnRouteStep::kLegacy,
                                           .update_legacy_window = true,
                                           .legacy_window = SpawnRouteWindowState::kProbeOnly,
                                       },
                                       error_message)) {
            return false;
        }
        std::string legacy_probe_error;
        (void)SpawnViaLegacyNcore(request, agent_path, pid, nullptr, &legacy_probe_error);
        if (!ApplyLegacyRouteResult(request,
                                    false,
                                    SpawnOwnedState{},
                                    legacy_probe_error,
                                    &outcome,
                                    pid,
                                    error_message)) {
            return false;
        }
    } else if (!policy.allow_legacy_backend_fallback) {
        if (!ApplySpawnRoutingSnapshot(state,
                                       SpawnRoutingSnapshot{
                                           .update_legacy_window = true,
                                           .legacy_window = SpawnRouteWindowState::kSkippedByPolicy,
                                       },
                                       error_message)) {
            return false;
        }
    }
    if (!ApplySpawnRoutingSnapshot(state,
                                   SpawnRoutingSnapshot{
                                       .update_routing_progress = true,
                                       .routing_progress = SpawnRoutingProgress::kAfterLegacy,
                                   },
                                   error_message)) {
        return false;
    }

    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kRouteDeferred,
                                       SpawnExecutionReason::kRouteDeferredForTerminalClassification,
                                       error_message)) {
        return false;
    }
    if (!ApplySpawnRoutingSnapshot(state,
                                   SpawnRoutingSnapshot{
                                       .update_routing_state = true,
                                       .routing_state = SpawnRoutingState::kDeferredToTerminal,
                                   },
                                   error_message)) {
        return false;
    }
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::ApplyTerminalSpawnOutcome(
    const comm::SpawnRequest& request,
    SpawnExecutionState* state,
    std::string* error_message) {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kTerminal,
                                       SpawnExecutionReason::kBeginTerminalClassification,
                                       error_message)) {
        return false;
    }
    SpawnExecutionPolicy& policy = state->policy;
    SpawnOutcome& outcome = state->outcome;

    ClassifyTerminalSpawnOutcome(&outcome,
                                 policy.explicit_symbi_requested,
                                 policy.allow_symbi_backend,
                                 policy.allow_legacy_backend_fallback);
    const bool ok = FinalizeSpawnOutcome(request,
                                         outcome,
                                         policy.explicit_symbi_requested,
                                         policy.allow_symbi_backend,
                                         policy.allow_legacy_backend_fallback,
                                         error_message);
    std::string transition_error;
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kTerminalResolved,
                                       SpawnExecutionReason::kTerminalOutcomeResolved,
                                       &transition_error)) {
        SetError(error_message, transition_error);
        return false;
    }
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kTerminalFinalized,
                                       SpawnExecutionReason::kTerminalOutcomeFinalized,
                                       &transition_error)) {
        SetError(error_message, transition_error);
        return false;
    }
    return ok;
}

bool NinjectorSpawnInjector::CompleteSpawnAfterRouting(
    const comm::SpawnRequest& request,
    const std::string& request_spawn_token,
    SpawnExecutionState* state,
    std::string* error_message) {
    if (state == nullptr) {
        SetError(error_message, "spawn execution state is null");
        return false;
    }

    if (state->outcome.final_status == SpawnFinalStatus::kSuccess) {
        if (!TransitionSpawnExecutionPhase(state,
                                           SpawnExecutionPhase::kCompleted,
                                           SpawnExecutionReason::kCompletedAfterCommittedRoute,
                                           error_message)) {
            return false;
        }
        return true;
    }

    (void)ReleaseActiveOwnerAfterDeferredRouting(request.identifier, request_spawn_token);
    const bool ok = ApplyTerminalSpawnOutcome(request, state, error_message);
    if (!TransitionSpawnExecutionPhase(state,
                                       SpawnExecutionPhase::kCompleted,
                                       SpawnExecutionReason::kCompletedAfterTerminalOutcome,
                                       error_message)) {
        return false;
    }
    return ok;
}

bool NinjectorSpawnInjector::AdmitSpawnRequest(const comm::SpawnRequest& request,
                                               std::string* error_message) {
    std::lock_guard<std::mutex> lock(transaction_mutex_);
    const SpawnOwnedState* authoritative_spawn_owner =
        ResolveAuthoritativeSpawnOwner(active_spawn_owner_);
    const bool has_active_spawn_owner = authoritative_spawn_owner != nullptr;
    const bool has_active_zygote_transaction =
        HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction);
    const std::string active_identifier =
        has_active_spawn_owner
            ? authoritative_spawn_owner->identifier
            : (has_active_zygote_transaction
                   ? active_spawn_owner_.zygote_control_transaction.identifier
                   : std::string());

    if (!active_identifier.empty()) {
        SetError(error_message,
                 active_identifier == request.identifier
                     ? "spawn already active for identifier"
                     : "spawn already active");
        return false;
    }

    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::Spawn(const comm::SpawnRequest& request,
                                   const std::string& agent_path,
                                   int* pid,
                                   std::string* error_message) {
    if (pid != nullptr) {
        *pid = 0;
    }

    if (request.identifier.empty()) {
        SetError(error_message, "spawn identifier is empty");
        return false;
    }
    if (agent_path.empty()) {
        SetError(error_message, "agent path is empty");
        return false;
    }

    const std::string request_spawn_token = ExtractSpawnToken(request);
    if (!AdmitSpawnRequest(request, error_message)) {
        return false;
    }
    SpawnExecutionState state = BuildSpawnExecutionState(request, agent_path);

    if (!ApplySpawnRoutingAttempts(request,
                                   agent_path,
                                   &state,
                                   pid,
                                   error_message)) {
        return false;
    }
    return CompleteSpawnAfterRouting(request,
                                     request_spawn_token,
                                     &state,
                                     error_message);
}

bool NinjectorSpawnInjector::FinalizeSpawn(const comm::SpawnRequest& request,
                                           std::string* error_message) {
    if (request.identifier.empty()) {
        SetError(error_message, "spawn identifier is empty");
        return false;
    }

    FinalizeSession finalize_session = BuildFinalizeSession(request);
    // Finalization follows backend ownership:
    // - zygote-control owns its own teardown
    // - symbi only cleans any materialized agent artifact
    // - legacy ncore clears the zygote prepare/clear state
    if (finalize_session.finalize_owner != SpawnOwnershipState::kNone) {
        const bool finalized = FinalizeOwnedSpawnByOwner(request,
                                                         finalize_session.finalize_owner,
                                                         finalize_session.owned_spawn_state,
                                                         &finalize_session.owned_zygote_transaction,
                                                         error_message);
        if (!finalized) {
            std::lock_guard<std::mutex> lock(transaction_mutex_);
            if (HasZygoteControlTransactionRecord(finalize_session.owned_zygote_transaction) &&
                !HasZygoteControlTransactionRecord(active_spawn_owner_.zygote_control_transaction)) {
                active_spawn_owner_.zygote_control_transaction =
                    finalize_session.owned_zygote_transaction;
            }

            const SpawnOwnedState* authoritative_spawn_owner =
                ResolveAuthoritativeSpawnOwner(active_spawn_owner_);
            if (authoritative_spawn_owner == nullptr &&
                finalize_session.finalize_owner != SpawnOwnershipState::kZygoteControlOwned &&
                HasOwnedSpawnStateForRetry(finalize_session.owned_spawn_state)) {
                RestoreOwnedSpawnStateForRetry(finalize_session.owned_spawn_state);
            }
        }
        return finalized;
    }
    return FinalizeWithoutOwnedBackend(request,
                                       finalize_session.has_foreign_active_owner,
                                       finalize_session.owned_zygote_transaction,
                                       error_message);
}

bool NinjectorSpawnInjector::InjectAgent(int pid,
                                         const std::string& agent_path,
                                         const std::string& ready_token,
                                         std::string* error_message) {
    if (pid <= 0) {
        SetError(error_message, "invalid pid");
        return false;
    }
    if (agent_path.empty()) {
        SetError(error_message, "agent path is empty");
        return false;
    }
    if (!ops_.inject_so_by_pid) {
        SetError(error_message, "inject_so_by_pid op is unavailable");
        return false;
    }
    if (!ops_.inject_embedded_agent_by_pid) {
        SetError(error_message, "inject_embedded_agent_by_pid op is unavailable");
        return false;
    }

    const bool prefer_embedded_attach = ShouldPreferEmbeddedAttachAgent(agent_path, ops_);
    if (prefer_embedded_attach) {
        const std::string runtime_dir = RuntimeDirectoryFromAgentPath(agent_path);
        NOOK_SPAWN_LOGI("attach using embedded agent by default pid=%d runtime_dir=%s",
                        pid,
                        runtime_dir.empty() ? "(empty)" : runtime_dir.c_str());
        if (ops_.inject_embedded_agent_by_pid(pid,
                                              runtime_dir.empty() ? nullptr : runtime_dir.c_str(),
                                              ready_token.empty() ? nullptr : ready_token.c_str())) {
            SetError(error_message, "");
            return true;
        }

        std::string embedded_error = "inject_embedded_agent_by_pid failed";
        std::string embedded_detail;
        if (ops_.get_inject_error) {
            embedded_detail = ops_.get_inject_error();
            if (!embedded_detail.empty()) {
                embedded_error += ": ";
                embedded_error += embedded_detail;
            }
        }

        NOOK_SPAWN_LOGI("attach embedded injection failed, fallback to sidecar pid=%d detail=%s",
                        pid,
                        embedded_detail.empty() ? "(empty)" : embedded_detail.c_str());

        std::string resolved_agent_path;
        bool materialized_embedded_agent = false;
        std::string resolved_agent_error;
        if (IsEmbeddedAgentSentinelPath(agent_path)) {
            SetError(error_message, embedded_error);
            return false;
        }
        if (!EnsureLegacyAgentReady(agent_path,
                                    &resolved_agent_path,
                                    &materialized_embedded_agent,
                                    &resolved_agent_error)) {
            SetError(error_message,
                     embedded_error + "; sidecar fallback prepare failed: " +
                         (resolved_agent_error.empty() ? std::string("agent path is empty")
                                                       : resolved_agent_error));
            return false;
        }

        if (!ops_.inject_so_by_pid(pid,
                                   resolved_agent_path.c_str(),
                                   ready_token.empty() ? nullptr : ready_token.c_str())) {
            std::string sidecar_error = "inject_so_by_pid failed";
            if (ops_.get_inject_error) {
                const std::string sidecar_detail = ops_.get_inject_error();
                if (!sidecar_detail.empty()) {
                    sidecar_error += ": ";
                    sidecar_error += sidecar_detail;
                }
            }
            MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
            SetError(error_message,
                     embedded_error + "; sidecar fallback failed: " + sidecar_error);
            return false;
        }

        MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
        SetError(error_message, "");
        return true;
    }

    std::string resolved_agent_path;
    bool materialized_embedded_agent = false;
    std::string resolved_agent_error;
    if (!EnsureLegacyAgentReady(agent_path,
                                &resolved_agent_path,
                                &materialized_embedded_agent,
                                &resolved_agent_error)) {
        SetError(error_message,
                 resolved_agent_error.empty() ? "inject_so_by_pid failed" : resolved_agent_error);
        return false;
    }

    if (!ops_.inject_so_by_pid(pid,
                               resolved_agent_path.c_str(),
                               ready_token.empty() ? nullptr : ready_token.c_str())) {
        std::string detail;
        std::string error = "inject_so_by_pid failed";
        if (ops_.get_inject_error) {
            detail = ops_.get_inject_error();
            if (!detail.empty()) {
                error += ": ";
                error += detail;
            }
        }

        const bool should_try_embedded_fallback =
            !detail.empty() &&
            (detail.find("remote_dlopen_failed") != std::string::npos ||
             detail.find("undefined symbol: JNI_OnLoad") != std::string::npos ||
             detail.find("classloader-namespace") != std::string::npos);
        if (!should_try_embedded_fallback) {
            SetError(error_message, error);
            return false;
        }

        const std::string runtime_dir = RuntimeDirectoryFromAgentPath(resolved_agent_path);
        NOOK_SPAWN_LOGI("attach sidecar injection failed, fallback to embedded agent pid=%d detail=%s runtime_dir=%s",
                        pid,
                        detail.c_str(),
                        runtime_dir.c_str());
        if (!ops_.inject_embedded_agent_by_pid(pid,
                                               runtime_dir.empty() ? nullptr : runtime_dir.c_str(),
                                               ready_token.empty() ? nullptr : ready_token.c_str())) {
            std::string embedded_error = "inject_embedded_agent_by_pid failed";
            if (ops_.get_inject_error) {
                const std::string embedded_detail = ops_.get_inject_error();
                if (!embedded_detail.empty()) {
                    embedded_error += ": ";
                    embedded_error += embedded_detail;
                }
            }
            SetError(error_message,
                     error + "; embedded fallback failed: " + embedded_error);
            return false;
        }
    }

    MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
    SetError(error_message, "");
    return true;
}

bool NinjectorSpawnInjector::InjectSpawnChildAgent(int pid,
                                                   const std::string& agent_path,
                                                   std::string* error_message) {
    if (pid <= 0) {
        SetError(error_message, "invalid pid");
        return false;
    }
    if (agent_path.empty()) {
        SetError(error_message, "agent path is empty");
        return false;
    }

    const bool prefer_embedded_attach = ShouldPreferEmbeddedAttachAgent(agent_path, ops_);
    if (!prefer_embedded_attach) {
        return InjectAgent(pid, agent_path, "", error_message);
    }

    if (!ops_.inject_embedded_agent_by_pid) {
        SetError(error_message, "inject_embedded_agent_by_pid op is unavailable");
        return false;
    }

    const std::string runtime_dir = RuntimeDirectoryFromAgentPath(agent_path);
    NOOK_SPAWN_LOGI("spawn child using embedded agent by default pid=%d runtime_dir=%s",
                    pid,
                    runtime_dir.empty() ? "(empty)" : runtime_dir.c_str());
    if (ninjector::InjectEmbeddedAgentByPidSuspended(
            pid,
            runtime_dir.empty() ? nullptr : runtime_dir.c_str())) {
        SetError(error_message, "");
        return true;
    }

    std::string embedded_error = "inject_embedded_agent_by_pid_suspended failed";
    if (ops_.get_inject_error) {
        const std::string embedded_detail = ops_.get_inject_error();
        if (!embedded_detail.empty()) {
            embedded_error += ": ";
            embedded_error += embedded_detail;
        }
    }

    if (IsEmbeddedAgentSentinelPath(agent_path)) {
        SetError(error_message, embedded_error);
        return false;
    }

    std::string resolved_agent_path;
    bool materialized_embedded_agent = false;
    std::string resolved_agent_error;
    if (!EnsureLegacyAgentReady(agent_path,
                                &resolved_agent_path,
                                &materialized_embedded_agent,
                                &resolved_agent_error)) {
        SetError(error_message,
                 embedded_error + "; sidecar fallback prepare failed: " +
                     (resolved_agent_error.empty() ? std::string("agent path is empty")
                                                   : resolved_agent_error));
        return false;
    }

    if (!ops_.inject_so_by_pid ||
        !ops_.inject_so_by_pid(pid, resolved_agent_path.c_str(), nullptr)) {
        std::string sidecar_error = "inject_so_by_pid failed";
        if (ops_.get_inject_error) {
            const std::string sidecar_detail = ops_.get_inject_error();
            if (!sidecar_detail.empty()) {
                sidecar_error += ": ";
                sidecar_error += sidecar_detail;
            }
        }
        MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
        SetError(error_message,
                 embedded_error + "; sidecar fallback failed: " + sidecar_error);
        return false;
    }

    MaybeCleanupLegacyAgentArtifact(resolved_agent_path, materialized_embedded_agent);
    SetError(error_message, "");
    return true;
}

}  // namespace server
}  // namespace nook
