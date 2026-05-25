#include "ninjector_compat.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>

#define NOOK_NINJECTOR_TAG "NookNinjector"
#if !defined(__ANDROID__) || !defined(__aarch64__)
#define NOOK_LOGD(...) ((void)0)
#define NOOK_LOGI(...) ((void)0)
#define NOOK_LOGE(...) ((void)0)
#endif

#if defined(__ANDROID__) && defined(__aarch64__)

#include <android/dlext.h>
#include <android/log.h>
#include <cerrno>
#include <cstdarg>
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <linux/uio.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include "generated/nook_embedded_agent_blob.h"
#include "generated/nook_embedded_ncore_blob.h"
#include "generated/nook_embedded_zygote_helper_blob.h"
#include "server_runtime.h"
#include "symbi/symbi_injector.h"

namespace {

#define NOOK_LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, NOOK_NINJECTOR_TAG, __VA_ARGS__))
#define NOOK_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, NOOK_NINJECTOR_TAG, __VA_ARGS__))
#define NOOK_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, NOOK_NINJECTOR_TAG, __VA_ARGS__))

constexpr int kRegsArgNum = 8;

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#if !defined(SYS_memfd_create) && defined(__NR_memfd_create)
#define SYS_memfd_create __NR_memfd_create
#endif

using pt_regs = user_regs_struct;

struct AndroidLoaderApi {
    long dlopen = 0;
    long android_dlopen_ext = 0;
    long dlclose = 0;
    long dlsym = 0;
    long dlerror = 0;
    long pretend_caller = 0;
    std::string linker_path;
};

long GetModuleBase(pid_t pid, const char* module_name);
long GetRemoteAddr(pid_t pid, void* local_func);

template <typename Ret>
Ret CallRemoteCall(pid_t pid, long address, int argc, long* args);

template <typename Ret, typename... Args>
Ret CallRemoteFunction(pid_t pid, void* local_func, Args... args);

void* RemoteAllocBytes(pid_t pid, const void* data, size_t size);
bool RemoteFreeScratch(pid_t pid, void* remote_buf, size_t size);

std::mutex g_last_inject_error_mutex;
std::string g_last_inject_error;
std::mutex g_embedded_ncore_handle_mutex;
void* g_last_embedded_ncore_handle = nullptr;

std::string BuildVersionedEmbeddedName(const char* prefix,
                                       const char* sha256,
                                       size_t blob_size) {
    std::string name = prefix != nullptr && prefix[0] != '\0'
                           ? prefix
                           : "libnook-embedded";
    if (sha256 != nullptr && sha256[0] != '\0') {
        name.push_back('-');
        name.append(sha256, std::min<size_t>(12, std::strlen(sha256)));
    }
    name.push_back('-');
    name.append(std::to_string(blob_size));
    return name;
}
pid_t g_last_embedded_ncore_pid = -1;

long long ElapsedMillis(std::chrono::steady_clock::time_point started_at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started_at)
        .count();
}

void SetLastInjectError(const std::string& error) {
    std::lock_guard<std::mutex> lock(g_last_inject_error_mutex);
    g_last_inject_error = error;
}

void ClearLastInjectError() {
    SetLastInjectError("");
}

std::string GetLastInjectError();

void RememberEmbeddedNcoreHandle(pid_t pid, void* handle) {
    std::lock_guard<std::mutex> lock(g_embedded_ncore_handle_mutex);
    g_last_embedded_ncore_pid = pid;
    g_last_embedded_ncore_handle = handle;
}

void* TakeEmbeddedNcoreHandle(pid_t pid) {
    std::lock_guard<std::mutex> lock(g_embedded_ncore_handle_mutex);
    if (g_last_embedded_ncore_pid != pid || g_last_embedded_ncore_handle == nullptr) {
        return nullptr;
    }
    void* handle = g_last_embedded_ncore_handle;
    g_last_embedded_ncore_handle = nullptr;
    g_last_embedded_ncore_pid = -1;
    return handle;
}

void ClearEmbeddedNcoreHandle(pid_t pid) {
    std::lock_guard<std::mutex> lock(g_embedded_ncore_handle_mutex);
    if (pid <= 0 || g_last_embedded_ncore_pid == pid) {
        g_last_embedded_ncore_pid = -1;
        g_last_embedded_ncore_handle = nullptr;
    }
}

uid_t GetPackageUidFromPackagesList(const char* package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return static_cast<uid_t>(-1);
    }

    std::ifstream pkg_file("/data/system/packages.list");
    if (!pkg_file.is_open()) {
        return static_cast<uid_t>(-1);
    }

    std::string line;
    while (std::getline(pkg_file, line)) {
        std::stringstream ss(line);
        std::string pkg;
        uid_t uid = static_cast<uid_t>(-1);
        if ((ss >> pkg >> uid) && pkg == package_name) {
            return uid;
        }
    }

    return static_cast<uid_t>(-1);
}

std::string BuildSymbiTargetCachePath(const char* package_name, uid_t uid) {
    if (package_name == nullptr || package_name[0] == '\0' || uid == static_cast<uid_t>(-1)) {
        return std::string();
    }

    std::string path("/data/data/");
    path += package_name;
    path += "/cache/lib";
    path += std::to_string(uid);
    path += ".so";
    return path;
}

bool WriteFullyLocalFd(int fd, const uint8_t* data, size_t size) {
    if (fd < 0 || (data == nullptr && size != 0)) {
        return false;
    }

    size_t written_total = 0;
    while (written_total < size) {
        const ssize_t written =
            TEMP_FAILURE_RETRY(write(fd, data + written_total, size - written_total));
        if (written <= 0) {
            return false;
        }
        written_total += static_cast<size_t>(written);
    }

    return true;
}

int CreateLocalMemfdWithBytes(const char* name, const uint8_t* data, size_t size) {
#if !defined(SYS_memfd_create)
    (void)name;
    (void)data;
    (void)size;
    SetLastInjectError("memfd_create_unsupported");
    return -1;
#else
    if (name == nullptr || name[0] == '\0' || (data == nullptr && size != 0)) {
        SetLastInjectError("invalid_args:local_memfd");
        return -1;
    }

    const int fd = static_cast<int>(
        TEMP_FAILURE_RETRY(syscall(SYS_memfd_create, name, static_cast<unsigned int>(MFD_CLOEXEC))));
    if (fd < 0) {
        SetLastInjectError("local_memfd_create_failed");
        return -1;
    }

    if (!WriteFullyLocalFd(fd, data, size)) {
        SetLastInjectError("local_memfd_write_failed");
        close(fd);
        return -1;
    }

    if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) < 0) {
        SetLastInjectError("local_memfd_lseek_failed");
        close(fd);
        return -1;
    }

    return fd;
#endif
}

long XPTrace(int request, ...) {
    va_list args;
    va_start(args, request);

    pid_t pid = va_arg(args, pid_t);
    void* addr = va_arg(args, void*);
    void* data = va_arg(args, void*);

    errno = 0;
    const long result = ptrace(request, pid, addr, data);
    va_end(args);

    if (result == -1 && errno != 0) {
        NOOK_LOGE("ptrace failed: request=%d pid=%d errno=%d (%s)",
                  request, pid, errno, strerror(errno));
    }

    return result;
}

constexpr int kWaitFlags = WUNTRACED | __WALL;
constexpr int kWaitStopPollUs = 10 * 1000;
constexpr int kWaitStopTimeoutMs = 5000;

char QueryProcessState(pid_t pid) {
    if (pid <= 0) {
        return '?';
    }

    char stat_path[64] = {0};
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

    std::ifstream file(stat_path);
    std::string stat;
    if (!file.good() || !std::getline(file, stat)) {
        return '?';
    }

    const std::string::size_type paren_end = stat.rfind(')');
    if (paren_end == std::string::npos || paren_end + 2 >= stat.size()) {
        return '?';
    }

    return stat[paren_end + 2];
}

std::string ReadProcessCmdlineBasename(pid_t pid) {
    if (pid <= 0) {
        return {};
    }

    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    std::ifstream file(path, std::ios::binary);
    std::string cmdline;
    std::getline(file, cmdline, '\0');
    if (cmdline.empty()) {
        return {};
    }

    const std::string::size_type slash = cmdline.find_last_of('/');
    return (slash == std::string::npos) ? cmdline : cmdline.substr(slash + 1);
}

bool ReadProcessRealUid(pid_t pid, uid_t* uid) {
    if (pid <= 0 || uid == nullptr) {
        return false;
    }

    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    std::ifstream file(path);
    if (!file.good()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        constexpr const char* kUidPrefix = "Uid:";
        if (line.rfind(kUidPrefix, 0) != 0) {
            continue;
        }

        std::istringstream stream(line.substr(std::strlen(kUidPrefix)));
        unsigned int real_uid = 0;
        if (!(stream >> real_uid)) {
            return false;
        }

        *uid = static_cast<uid_t>(real_uid);
        return true;
    }

    return false;
}

bool ReadProcessParentPid(pid_t pid, pid_t* parent_pid) {
    if (pid <= 0 || parent_pid == nullptr) {
        return false;
    }

    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    std::ifstream file(path);
    if (!file.good()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        constexpr const char* kPPidPrefix = "PPid:";
        if (line.rfind(kPPidPrefix, 0) != 0) {
            continue;
        }

        std::istringstream stream(line.substr(std::strlen(kPPidPrefix)));
        int parsed_parent_pid = -1;
        if (!(stream >> parsed_parent_pid) || parsed_parent_pid <= 0) {
            return false;
        }

        *parent_pid = static_cast<pid_t>(parsed_parent_pid);
        return true;
    }

    return false;
}

bool IsActualZygoteFamilyProcess(pid_t pid) {
    if (pid <= 0) {
        return false;
    }

    const std::string name = ReadProcessCmdlineBasename(pid);
    if (name.empty()) {
        return false;
    }

    uid_t uid = static_cast<uid_t>(-1);
    if (!ReadProcessRealUid(pid, &uid) || uid != 0) {
        return false;
    }

    pid_t parent_pid = -1;
    if (!ReadProcessParentPid(pid, &parent_pid)) {
        return false;
    }

    if (name == "zygote64" || name == "zygote") {
        return parent_pid == static_cast<pid_t>(1);
    }

    if (name == "usap64" || name == "usap32") {
        const std::string parent_name = ReadProcessCmdlineBasename(parent_pid);
        return parent_name == "zygote64" || parent_name == "zygote";
    }

    return false;
}

bool IsZygoteFamilyProcess(pid_t pid) {
    return IsActualZygoteFamilyProcess(pid);
}

std::string ResolveZygoteRuntimeDirectory(const char* runtime_dir) {
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return std::string(runtime_dir);
    }
    const char* env_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    if (env_runtime_dir != nullptr && env_runtime_dir[0] != '\0') {
        return std::string(env_runtime_dir);
    }
    return "/data/local/tmp/nook";
}

bool WaitForProcessState(pid_t pid, char expected_state, int timeout_ms, const char* stage) {
    if (pid <= 0) {
        return false;
    }

    const int sleep_us = 10 * 1000;
    const int max_attempts = timeout_ms <= 0 ? 1 : ((timeout_ms * 1000) / sleep_us);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const char state = QueryProcessState(pid);
        if (state == expected_state) {
            return true;
        }
        if (state == 'Z' || state == 'X') {
            NOOK_LOGE("%s: target exited before state transition pid=%d state=%c",
                      stage, pid, state);
            return false;
        }
        usleep(sleep_us);
    }

    NOOK_LOGE("%s: timed out waiting for pid=%d state=%c", stage, pid, expected_state);
    return false;
}

bool WaitForProcessStateNot(pid_t pid, char unexpected_state, int timeout_ms, const char* stage) {
    if (pid <= 0) {
        return false;
    }

    const int sleep_us = 10 * 1000;
    const int max_attempts = timeout_ms <= 0 ? 1 : ((timeout_ms * 1000) / sleep_us);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const char state = QueryProcessState(pid);
        if (state != unexpected_state) {
            return true;
        }
        if (state == 'Z' || state == 'X') {
            NOOK_LOGE("%s: target exited before leaving state pid=%d state=%c",
                      stage, pid, state);
            return false;
        }
        usleep(sleep_us);
    }

    NOOK_LOGE("%s: timed out waiting for pid=%d to leave state=%c",
              stage, pid, unexpected_state);
    return false;
}

bool WaitForProcessStop(pid_t pid, int* status, const char* stage) {
    if (pid <= 0 || status == nullptr) {
        return false;
    }

    const int max_attempts =
        kWaitStopTimeoutMs <= 0 ? 1 : ((kWaitStopTimeoutMs * 1000) / kWaitStopPollUs);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const pid_t waited = waitpid(pid, status, kWaitFlags | WNOHANG);
        if (waited == 0) {
            usleep(kWaitStopPollUs);
            continue;
        }
        if (waited == -1) {
            if (errno == EINTR) {
                continue;
            }
            NOOK_LOGE("%s: waitpid failed pid=%d errno=%d (%s)",
                      stage, pid, errno, strerror(errno));
            return false;
        }

        if (WIFSTOPPED(*status)) {
            return true;
        }

        if (WIFEXITED(*status) || WIFSIGNALED(*status)) {
            NOOK_LOGE("%s: target left stop state pid=%d status=0x%x",
                      stage, pid, *status);
            return false;
        }
    }

    NOOK_LOGE("%s: waitpid stop timed out pid=%d timeout_ms=%d state=%c",
              stage,
              pid,
              kWaitStopTimeoutMs,
              QueryProcessState(pid));
    return false;
}

bool AttachProcess(pid_t pid) {
    if (pid <= 0) {
        NOOK_LOGE("AttachProcess: invalid pid=%d", pid);
        return false;
    }

    NOOK_LOGI("AttachProcess: build-marker=2026-05-11-seize-v2 pid=%d", pid);

    int status = 0;
    const bool was_group_stopped = QueryProcessState(pid) == 'T';
    const bool prefer_legacy_attach = IsZygoteFamilyProcess(pid);

    if (prefer_legacy_attach) {
        NOOK_LOGI("AttachProcess: forcing legacy attach for actual zygote-family pid=%d", pid);
    }

#ifdef PTRACE_SEIZE
    if (!prefer_legacy_attach &&
        XPTrace(PTRACE_SEIZE, pid, nullptr, nullptr) != -1) {
        if (was_group_stopped) {
            if (!WaitForProcessStop(pid, &status, "AttachProcess:seize-wait-group-stop")) {
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }
            NOOK_LOGI("AttachProcess: group-stop status pid=%d status=0x%x", pid, status);

#ifdef PTRACE_LISTEN
            if (XPTrace(PTRACE_LISTEN, pid, nullptr, nullptr) == -1) {
                NOOK_LOGE("AttachProcess: PTRACE_LISTEN failed pid=%d", pid);
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }
#endif

            if (kill(pid, SIGCONT) != 0) {
                NOOK_LOGE("AttachProcess: SIGCONT failed pid=%d errno=%d (%s)",
                          pid, errno, strerror(errno));
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }

            if (!WaitForProcessStateNot(pid,
                                        'T',
                                        500,
                                        "AttachProcess:wait-leave-group-stop")) {
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }

            if (XPTrace(PTRACE_INTERRUPT, pid, nullptr, nullptr) == -1) {
                NOOK_LOGE("AttachProcess: ptrace interrupt after SIGCONT failed pid=%d", pid);
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }

            if (!WaitForProcessStop(pid, &status, "AttachProcess:interrupt-after-group-stop")) {
                (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
                return false;
            }

            NOOK_LOGI("AttachProcess: seized already-group-stopped pid=%d and interrupted into ptrace-stop status=0x%x",
                      pid,
                      status);
            return true;
        }

        if (XPTrace(PTRACE_INTERRUPT, pid, nullptr, nullptr) == -1) {
            NOOK_LOGE("AttachProcess: ptrace interrupt failed pid=%d", pid);
            (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
            return false;
        }

        if (!WaitForProcessStop(pid, &status, "AttachProcess:interrupt-wait")) {
            (void) XPTrace(PTRACE_DETACH, pid, nullptr, nullptr);
            return false;
        }

        return true;
    }
#endif

    if (XPTrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        NOOK_LOGE("AttachProcess: ptrace attach failed pid=%d", pid);
        return false;
    }

    if (!WaitForProcessStop(pid, &status, "AttachProcess:attach-wait")) {
        return false;
    }

    NOOK_LOGI("AttachProcess: fallback-attach path pid=%d", pid);

    return true;
}

bool DetachProcess(pid_t pid) {
    if (pid <= 0) {
        NOOK_LOGE("DetachProcess: invalid pid=%d", pid);
        return false;
    }

    return XPTrace(PTRACE_DETACH, pid, nullptr, nullptr) != -1;
}

void PTraceRead(pid_t pid, long address, uint8_t* buffer, size_t size) {
    if (pid <= 0 || address == 0 || buffer == nullptr || size == 0) {
        return;
    }

    memset(buffer, 0, size);
    const size_t word_size = sizeof(unsigned long);
    const size_t full_words = size / word_size;
    const size_t remain = size % word_size;

    for (size_t i = 0; i < full_words; ++i) {
        const unsigned long word = static_cast<unsigned long>(
            XPTrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(address + i * word_size), nullptr));
        memcpy(buffer + i * word_size, &word, word_size);
    }

    if (remain > 0) {
        const unsigned long word = static_cast<unsigned long>(
            XPTrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(address + full_words * word_size), nullptr));
        memcpy(buffer + full_words * word_size, &word, remain);
    }
}

void PTraceWrite(pid_t pid, long address, void* data, size_t size) {
    if (pid <= 0 || address == 0 || data == nullptr || size == 0) {
        return;
    }

    const size_t word_size = sizeof(unsigned long);
    const size_t full_words = size / word_size;
    const size_t remain = size % word_size;
    auto* bytes = reinterpret_cast<unsigned char*>(data);

    for (size_t i = 0; i < full_words; ++i) {
        const unsigned long word = *reinterpret_cast<unsigned long*>(bytes + i * word_size);
        XPTrace(PTRACE_POKEDATA,
                pid,
                reinterpret_cast<void*>(address + i * word_size),
                reinterpret_cast<void*>(word));
    }

    if (remain > 0) {
        const long tail_addr = address + full_words * word_size;
        unsigned long word = static_cast<unsigned long>(
            XPTrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(tail_addr), nullptr));
        memcpy(&word, bytes + full_words * word_size, remain);
        XPTrace(PTRACE_POKEDATA, pid, reinterpret_cast<void*>(tail_addr), reinterpret_cast<void*>(word));
    }
}

std::string ReadRemoteCString(pid_t pid, long address, size_t max_len = 512) {
    if (pid <= 0 || address == 0 || max_len == 0) {
        return {};
    }

    std::string result;
    result.reserve(max_len);

    const size_t chunk_size = sizeof(unsigned long);
    std::vector<uint8_t> chunk(chunk_size);
    for (size_t offset = 0; offset < max_len; offset += chunk_size) {
        PTraceRead(pid, address + static_cast<long>(offset), chunk.data(), chunk.size());
        for (size_t i = 0; i < chunk.size() && result.size() < max_len; ++i) {
            const char ch = static_cast<char>(chunk[i]);
            if (ch == '\0') {
                return result;
            }
            result.push_back(ch);
        }
    }

    return result;
}

bool RemoteModulePathContains(pid_t pid, const char* path) {
    if (pid <= 0 || path == nullptr || path[0] == '\0') {
        return false;
    }

    char maps_path[64] = {0};
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE* fp = fopen(maps_path, "r");
    if (fp == nullptr) {
        return false;
    }

    char line[1024] = {0};
    bool found = false;
    while (fgets(line, sizeof(line), fp) != nullptr) {
        if (std::strstr(line, path) != nullptr) {
            found = true;
            break;
        }
    }

    fclose(fp);
    return found;
}

bool IsLinkerModulePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    const std::string::size_type slash = path.find_last_of('/');
    const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    return name == "linker64" || name == "linker" || name == "ld-android.so";
}

std::string SwapRuntimeLibDirectory(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::string swapped = path;
    const std::string lib64_token = "/lib64/";
    const std::string lib_token = "/lib/";
    const std::string::size_type lib64_pos = swapped.find(lib64_token);
    if (lib64_pos != std::string::npos) {
        swapped.replace(lib64_pos, lib64_token.size(), lib_token);
        return swapped;
    }

    const std::string::size_type lib_pos = swapped.find(lib_token);
    if (lib_pos != std::string::npos) {
        swapped.replace(lib_pos, lib_token.size(), lib64_token);
        return swapped;
    }

    return {};
}

std::vector<std::string> BuildModulePathCandidates(const std::string& module_path) {
    std::vector<std::string> candidates;
    if (module_path.empty()) {
        return candidates;
    }

    candidates.push_back(module_path);
    const std::string swapped = SwapRuntimeLibDirectory(module_path);
    if (!swapped.empty() && swapped != module_path) {
        candidates.push_back(swapped);
    }
    return candidates;
}

std::string GetAndroidLinkerPath(pid_t pid) {
    char maps_path[64] = {0};
    if (pid == -1) {
        snprintf(maps_path, sizeof(maps_path), "/proc/self/maps");
    } else {
        snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    }

    FILE* fp = fopen(maps_path, "r");
    if (fp == nullptr) {
        return {};
    }

    char line[1024] = {0};
    std::string result;
    while (fgets(line, sizeof(line), fp) != nullptr) {
        char* path = strchr(line, '/');
        if (path == nullptr) {
            continue;
        }

        char* newline = strchr(path, '\n');
        if (newline != nullptr) {
            *newline = '\0';
        }

        if (IsLinkerModulePath(path)) {
            result = path;
            break;
        }
    }

    fclose(fp);
    return result;
}

bool DetectRemoteProcess64Bit(pid_t pid, bool* is_64_bit) {
    if (is_64_bit == nullptr || pid <= 0) {
        return false;
    }

    const std::string linker_path = GetAndroidLinkerPath(pid);
    if (linker_path.empty()) {
        return false;
    }

    if (linker_path.find("linker64") != std::string::npos ||
        linker_path.find("/lib64/") != std::string::npos) {
        *is_64_bit = true;
        return true;
    }

    if (linker_path.find("/system/bin/linker") != std::string::npos ||
        linker_path.find("/bin/linker") != std::string::npos ||
        linker_path.find("/lib/") != std::string::npos) {
        *is_64_bit = false;
        return true;
    }

    return false;
}

bool ReadElfImage(const std::string& path, std::vector<uint8_t>* data) {
    if (data == nullptr || path.empty()) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    data->assign(static_cast<size_t>(size), 0);
    file.read(reinterpret_cast<char*>(data->data()), size);
    return file.good();
}

bool FindElfExportOffset(const std::string& path,
                         const std::vector<std::string>& candidates,
                         uint64_t* offset) {
    if (offset == nullptr || path.empty() || candidates.empty()) {
        return false;
    }

    std::vector<uint8_t> image;
    if (!ReadElfImage(path, &image) || image.size() < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(image.data());
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_shoff == 0 ||
        ehdr->e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr->e_shnum == 0) {
        return false;
    }

    const size_t shdr_table_end =
        static_cast<size_t>(ehdr->e_shoff) + static_cast<size_t>(ehdr->e_shnum) * sizeof(Elf64_Shdr);
    if (shdr_table_end > image.size()) {
        return false;
    }

    const auto* shdrs =
        reinterpret_cast<const Elf64_Shdr*>(image.data() + static_cast<size_t>(ehdr->e_shoff));

    for (size_t i = 0; i < ehdr->e_shnum; ++i) {
        const Elf64_Shdr& shdr = shdrs[i];
        if ((shdr.sh_type != SHT_DYNSYM && shdr.sh_type != SHT_SYMTAB) ||
            shdr.sh_entsize != sizeof(Elf64_Sym) ||
            shdr.sh_offset >= image.size() ||
            shdr.sh_link >= ehdr->e_shnum) {
            continue;
        }

        const Elf64_Shdr& strtab = shdrs[shdr.sh_link];
        if (strtab.sh_offset >= image.size() ||
            static_cast<size_t>(strtab.sh_offset + strtab.sh_size) > image.size() ||
            static_cast<size_t>(shdr.sh_offset + shdr.sh_size) > image.size()) {
            continue;
        }

        const auto* syms =
            reinterpret_cast<const Elf64_Sym*>(image.data() + static_cast<size_t>(shdr.sh_offset));
        const size_t sym_count = static_cast<size_t>(shdr.sh_size / sizeof(Elf64_Sym));
        const char* strtab_data =
            reinterpret_cast<const char*>(image.data() + static_cast<size_t>(strtab.sh_offset));

        for (size_t sym_index = 0; sym_index < sym_count; ++sym_index) {
            const Elf64_Sym& sym = syms[sym_index];
            if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_name >= strtab.sh_size) {
                continue;
            }

            const char* name = strtab_data + sym.st_name;
            for (const std::string& candidate : candidates) {
                if (candidate == name) {
                    *offset = sym.st_value;
                    return true;
                }
            }
        }
    }

    return false;
}

bool ResolveAndroidLoaderApi(pid_t pid, AndroidLoaderApi* api) {
    if (pid <= 0 || api == nullptr) {
        return false;
    }

    const std::string local_linker_path = GetAndroidLinkerPath(-1);
    const std::string remote_linker_path = GetAndroidLinkerPath(pid);
    if (local_linker_path.empty() || remote_linker_path.empty()) {
        NOOK_LOGE("ResolveAndroidLoaderApi: linker path missing pid=%d local=%s remote=%s",
                  pid,
                  local_linker_path.c_str(),
                  remote_linker_path.c_str());
        return false;
    }

    const long remote_linker_base = GetModuleBase(pid, remote_linker_path.c_str());
    if (remote_linker_base == 0) {
        NOOK_LOGE("ResolveAndroidLoaderApi: remote linker base missing pid=%d path=%s",
                  pid,
                  remote_linker_path.c_str());
        return false;
    }

    uint64_t dlopen_offset = 0;
    uint64_t android_dlopen_ext_offset = 0;
    uint64_t dlclose_offset = 0;
    uint64_t dlsym_offset = 0;
    uint64_t dlerror_offset = 0;
    if (!FindElfExportOffset(local_linker_path,
                             {"__loader_dlopen", "__dl__Z8__dlopenPKciPKv"},
                             &dlopen_offset) ||
        !FindElfExportOffset(local_linker_path,
                             {"__loader_dlclose", "__dl__Z9__dlclosePv"},
                             &dlclose_offset) ||
        !FindElfExportOffset(local_linker_path,
                             {"__loader_dlsym", "__dl__Z7__dlsymPvPKcPKv"},
                             &dlsym_offset) ||
        !FindElfExportOffset(local_linker_path,
                             {"__loader_dlerror", "__dl__Z9__dlerrorv"},
                             &dlerror_offset)) {
        NOOK_LOGE("ResolveAndroidLoaderApi: linker exports missing pid=%d linker=%s",
                  pid,
                  local_linker_path.c_str());
        return false;
    }
    (void) FindElfExportOffset(local_linker_path,
                               {"__loader_android_dlopen_ext"},
                               &android_dlopen_ext_offset);

    api->dlopen = remote_linker_base + static_cast<long>(dlopen_offset);
    api->android_dlopen_ext =
        android_dlopen_ext_offset != 0
            ? (remote_linker_base + static_cast<long>(android_dlopen_ext_offset))
            : 0;
    api->dlclose = remote_linker_base + static_cast<long>(dlclose_offset);
    api->dlsym = remote_linker_base + static_cast<long>(dlsym_offset);
    api->dlerror = remote_linker_base + static_cast<long>(dlerror_offset);
    api->pretend_caller = GetRemoteAddr(pid, reinterpret_cast<void*>(close));
    api->linker_path = remote_linker_path;
    if (api->pretend_caller == 0) {
        NOOK_LOGE("ResolveAndroidLoaderApi: pretend caller missing pid=%d", pid);
        return false;
    }

    NOOK_LOGI("ResolveAndroidLoaderApi: pid=%d linker=%s dlopen=%lx android_dlopen_ext=%lx dlsym=%lx dlerror=%lx caller=%lx",
              pid,
              api->linker_path.c_str(),
              api->dlopen,
              api->android_dlopen_ext,
              api->dlsym,
              api->dlerror,
              api->pretend_caller);
    return true;
}

void* CallRemoteAndroidDlopen(pid_t pid,
                              const AndroidLoaderApi& api,
                              const char* remote_path,
                              int flags) {
    long params[3] = {
        reinterpret_cast<long>(remote_path),
        static_cast<long>(flags),
        api.pretend_caller,
    };
    return CallRemoteCall<void*>(pid, api.dlopen, 3, params);
}

void* CallRemoteAndroidDlopenExtUseFd(pid_t pid,
                                      const AndroidLoaderApi& api,
                                      const char* remote_path,
                                      int flags,
                                      int library_fd) {
    if (api.android_dlopen_ext == 0 || remote_path == nullptr || library_fd < 0) {
        return nullptr;
    }

    android_dlextinfo extinfo{};
    extinfo.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
    extinfo.library_fd = library_fd;

    void* remote_extinfo = RemoteAllocBytes(pid, &extinfo, sizeof(extinfo));
    if (remote_extinfo == nullptr) {
        return nullptr;
    }

    long params[4] = {
        reinterpret_cast<long>(remote_path),
        static_cast<long>(flags),
        reinterpret_cast<long>(remote_extinfo),
        api.pretend_caller,
    };
    void* handle = CallRemoteCall<void*>(pid, api.android_dlopen_ext, 4, params);
    (void)RemoteFreeScratch(pid, remote_extinfo, sizeof(extinfo));
    return handle;
}

void* CallRemoteAndroidDlsym(pid_t pid,
                             const AndroidLoaderApi& api,
                             void* handle,
                             const char* remote_symbol) {
    long params[3] = {
        reinterpret_cast<long>(handle),
        reinterpret_cast<long>(remote_symbol),
        api.pretend_caller,
    };
    return CallRemoteCall<void*>(pid, api.dlsym, 3, params);
}

std::string CallRemoteAndroidDlerror(pid_t pid, const AndroidLoaderApi& api) {
    const void* remote_dlerror = CallRemoteCall<void*>(pid, api.dlerror, 0, nullptr);
    if (remote_dlerror == nullptr) {
        return {};
    }
    return ReadRemoteCString(pid, reinterpret_cast<long>(remote_dlerror));
}

bool TryParseProcSelfFdPath(const char* path, int* fd) {
    if (path == nullptr || fd == nullptr) {
        return false;
    }

    constexpr const char* kPrefix = "/proc/self/fd/";
    if (std::strncmp(path, kPrefix, std::strlen(kPrefix)) != 0) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(path + std::strlen(kPrefix), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0' || value < 0 || value > INT32_MAX) {
        return false;
    }

    *fd = static_cast<int>(value);
    return true;
}

bool IsShellTmpRuntimePath(const char* path) {
    if (path == nullptr) {
        return false;
    }
    return std::strcmp(path, "/data/local/tmp") == 0 ||
           std::strncmp(path, "/data/local/tmp/", std::strlen("/data/local/tmp/")) == 0;
}

long GetModuleBase(pid_t pid, const char* module_name) {
    if (module_name == nullptr || module_name[0] == '\0') {
        return 0;
    }

    char maps_path[64] = {0};
    if (pid == -1) {
        snprintf(maps_path, sizeof(maps_path), "/proc/self/maps");
    } else {
        snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    }

    FILE* fp = fopen(maps_path, "r");
    if (fp == nullptr) {
        return 0;
    }

    char line[512] = {0};
    long base_addr = 0;
    while (fgets(line, sizeof(line), fp) != nullptr) {
        if (strstr(line, module_name) != nullptr) {
            char* start = strtok(line, "-");
            if (start != nullptr) {
                base_addr = strtoul(start, nullptr, 16);
                break;
            }
        }
    }

    fclose(fp);
    return base_addr;
}

long GetModuleBaseFromCandidates(pid_t pid,
                                 const std::vector<std::string>& module_candidates,
                                 std::string* matched_module_path) {
    if (matched_module_path != nullptr) {
        matched_module_path->clear();
    }

    for (const std::string& candidate : module_candidates) {
        if (candidate.empty()) {
            continue;
        }

        const long base = GetModuleBase(pid, candidate.c_str());
        if (base != 0) {
            if (matched_module_path != nullptr) {
                *matched_module_path = candidate;
            }
            return base;
        }
    }

    return 0;
}

std::string GetModulePathForAddress(pid_t pid, uintptr_t addr) {
    char maps_path[64] = {0};
    if (pid == -1) {
        snprintf(maps_path, sizeof(maps_path), "/proc/self/maps");
    } else {
        snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    }

    FILE* fp = fopen(maps_path, "r");
    if (fp == nullptr) {
        return {};
    }

    char line[1024] = {0};
    while (fgets(line, sizeof(line), fp) != nullptr) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) {
            continue;
        }

        if (addr >= start && addr < end) {
            char* path = strchr(line, '/');
            if (path != nullptr) {
                char* newline = strchr(path, '\n');
                if (newline != nullptr) {
                    *newline = '\0';
                }
                fclose(fp);
                return path;
            }
        }
    }

    fclose(fp);
    return {};
}

long GetRemoteAddr(pid_t pid, void* local_func) {
    if (local_func == nullptr) {
        NOOK_LOGE("GetRemoteAddr: local_func is null pid=%d", pid);
        return 0;
    }

    const std::string module_path = GetModulePathForAddress(-1, reinterpret_cast<uintptr_t>(local_func));
    if (module_path.empty()) {
        NOOK_LOGE("GetRemoteAddr: local module path not found pid=%d local_func=%p", pid, local_func);
        return 0;
    }

    const std::vector<std::string> module_candidates = BuildModulePathCandidates(module_path);
    std::string local_module_path;
    std::string remote_module_path;
    const long local_base = GetModuleBaseFromCandidates(-1, module_candidates, &local_module_path);
    const long remote_base = GetModuleBaseFromCandidates(pid, module_candidates, &remote_module_path);
    if (local_base == 0 || remote_base == 0) {
        const std::string alternate_module_path = SwapRuntimeLibDirectory(module_path);
        NOOK_LOGE("GetRemoteAddr: module base resolve failed pid=%d module=%s alt=%s local_base=%lx remote_base=%lx",
                  pid,
                  module_path.c_str(),
                  alternate_module_path.empty() ? "(none)" : alternate_module_path.c_str(),
                  local_base,
                  remote_base);
        return 0;
    }

    if (local_module_path != remote_module_path) {
        NOOK_LOGI("GetRemoteAddr: module alias resolved pid=%d local=%s remote=%s",
                  pid,
                  local_module_path.c_str(),
                  remote_module_path.c_str());
    }

    return reinterpret_cast<long>(local_func) - local_base + remote_base;
}

template <typename Ret>
Ret CallRemoteCall(pid_t pid, long address, int argc, long* args) {
    pt_regs regs{};
    pt_regs backup_regs{};

    iovec regs_iov{
        .iov_base = &regs,
        .iov_len = sizeof(pt_regs)
    };
    iovec backup_iov{
        .iov_base = &backup_regs,
        .iov_len = sizeof(pt_regs)
    };

    if (XPTrace(PTRACE_GETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &regs_iov) == -1) {
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }
    backup_regs = regs;

    for (int i = 0; i < argc && i < kRegsArgNum; ++i) {
        regs.regs[i] = args[i];
    }

    if (argc > kRegsArgNum) {
        const size_t stack_size = static_cast<size_t>(argc - kRegsArgNum) * sizeof(long);
        regs.sp -= stack_size;
        PTraceWrite(pid, regs.sp, args + kRegsArgNum, stack_size);
    }

    regs.regs[30] = 0;
    regs.pc = address;

    constexpr unsigned int kCpsrTMask = (1u << 5);
    if (regs.pc & 1) {
        regs.pc &= (~1u);
        regs.pstate |= kCpsrTMask;
    } else {
        regs.pstate &= ~kCpsrTMask;
    }

    if (XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &regs_iov) == -1) {
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }
    if (XPTrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
        (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }

    int status = 0;
    if (!WaitForProcessStop(pid, &status, "CallRemoteCall:wait")) {
        (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }
    bool completed = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (XPTrace(PTRACE_GETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &regs_iov) == -1) {
            (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
            if constexpr (std::is_void_v<Ret>) {
                return;
            } else {
                return static_cast<Ret>(0);
            }
        }

        NOOK_LOGI("CallRemoteCall: stop pid=%d attempt=%d status=0x%x sig=%d pc=%llx x0=%llx",
                  pid,
                  attempt,
                  status,
                  WSTOPSIG(status),
                  static_cast<unsigned long long>(regs.pc),
                  static_cast<unsigned long long>(regs.regs[0]));

        if (regs.pc == 0) {
            completed = true;
            break;
        }

        if (XPTrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
            (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
            if constexpr (std::is_void_v<Ret>) {
                return;
            } else {
                return static_cast<Ret>(0);
            }
        }
        if (!WaitForProcessStop(pid, &status, "CallRemoteCall:wait-loop")) {
            (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
            if constexpr (std::is_void_v<Ret>) {
                return;
            } else {
                return static_cast<Ret>(0);
            }
        }
    }

    if (!completed) {
        NOOK_LOGE("CallRemoteCall: remote call did not reach completion pid=%d target=%lx", pid, address);
        (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }

    (void) XPTrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &backup_iov);

    if constexpr (std::is_void_v<Ret>) {
        return;
    } else if constexpr (std::is_pointer_v<Ret>) {
        return reinterpret_cast<Ret>(regs.regs[0]);
    } else {
        return static_cast<Ret>(regs.regs[0]);
    }
}

template <typename Ret, typename... Args>
Ret CallRemoteFunction(pid_t pid, void* local_func, Args... args) {
    if (local_func == nullptr) {
        NOOK_LOGE("CallRemoteFunction: local_func is null pid=%d", pid);
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }

    const long remote_addr = GetRemoteAddr(pid, local_func);
    if (remote_addr == 0) {
        NOOK_LOGE("CallRemoteFunction: remote_addr resolve failed pid=%d local_func=%p", pid, local_func);
        if constexpr (std::is_void_v<Ret>) {
            return;
        } else {
            return static_cast<Ret>(0);
        }
    }

    long params[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = {};
    int index = 0;
    ((params[index++] = (long)args), ...);
    return CallRemoteCall<Ret>(pid, remote_addr, sizeof...(Args), params);
}

void* RemoteAllocString(pid_t pid, const char* str) {
    if (pid <= 0 || str == nullptr || str[0] == '\0') {
        NOOK_LOGE("RemoteAllocString: invalid args pid=%d str=%p", pid, str);
        return nullptr;
    }

    const size_t len = strlen(str) + 1;
    void* remote_buf = RemoteAllocBytes(pid, str, len);
    if (remote_buf == nullptr ||
        reinterpret_cast<uintptr_t>(remote_buf) < static_cast<uintptr_t>(0x10000)) {
        NOOK_LOGE("RemoteAllocString: remote scratch alloc failed pid=%d len=%zu", pid, len);
        return nullptr;
    }
    NOOK_LOGI("RemoteAllocString: pid=%d len=%zu remote=%p", pid, len, remote_buf);
    return remote_buf;
}

bool UseRemoteMmapScratch(pid_t pid) {
    return IsZygoteFamilyProcess(pid);
}

void* RemoteAllocScratch(pid_t pid, size_t size) {
    if (pid <= 0 || size == 0) {
        return nullptr;
    }

    if (UseRemoteMmapScratch(pid)) {
        void* remote_buf = CallRemoteFunction<void*, void*, size_t, int, int, int, long>(
            pid,
            reinterpret_cast<void*>(mmap),
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        if (remote_buf == nullptr ||
            reinterpret_cast<uintptr_t>(remote_buf) < static_cast<uintptr_t>(0x10000)) {
            NOOK_LOGE("RemoteAllocScratch: remote mmap failed pid=%d size=%zu", pid, size);
            return nullptr;
        }
        NOOK_LOGI("RemoteAllocScratch: pid=%d size=%zu remote=%p mode=%s",
                  pid,
                  size,
                  remote_buf,
                  UseRemoteMmapScratch(pid) ? "mmap" : "malloc");
        return remote_buf;
    }

    void* remote_buf = CallRemoteFunction<void*, size_t>(
        pid,
        reinterpret_cast<void*>(malloc),
        size);
    if (remote_buf == nullptr ||
        reinterpret_cast<uintptr_t>(remote_buf) < static_cast<uintptr_t>(0x10000)) {
        NOOK_LOGE("RemoteAllocScratch: remote malloc failed pid=%d size=%zu", pid, size);
        return nullptr;
    }
    NOOK_LOGI("RemoteAllocScratch: pid=%d size=%zu remote=%p mode=%s",
              pid,
              size,
              remote_buf,
              UseRemoteMmapScratch(pid) ? "mmap" : "malloc");
    return remote_buf;
}

bool RemoteFreeScratch(pid_t pid, void* remote_buf, size_t size) {
    if (pid <= 0 || remote_buf == nullptr || size == 0) {
        return false;
    }

    if (UseRemoteMmapScratch(pid)) {
        const long rc = CallRemoteFunction<long, void*, size_t>(
            pid,
            reinterpret_cast<void*>(munmap),
            remote_buf,
            size);
        return rc == 0;
    }

    CallRemoteFunction<void, void*>(pid, reinterpret_cast<void*>(free), remote_buf);
    return true;
}

void* RemoteAllocBytes(pid_t pid, const void* data, size_t size) {
    if (pid <= 0 || data == nullptr || size == 0) {
        NOOK_LOGE("RemoteAllocBytes: invalid args pid=%d data=%p size=%zu", pid, data, size);
        return nullptr;
    }

    void* remote_buf = RemoteAllocScratch(pid, size);
    if (remote_buf == nullptr ||
        reinterpret_cast<uintptr_t>(remote_buf) < static_cast<uintptr_t>(0x10000)) {
        NOOK_LOGE("RemoteAllocBytes: remote scratch alloc failed pid=%d size=%zu", pid, size);
        return nullptr;
    }

    PTraceWrite(pid, reinterpret_cast<long>(remote_buf), const_cast<void*>(data), size);
    NOOK_LOGI("RemoteAllocBytes: pid=%d size=%zu remote=%p", pid, size, remote_buf);
    return remote_buf;
}

bool WriteFully(int fd, const void* data, size_t size) {
    if (fd < 0 || data == nullptr) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    size_t written_total = 0;
    while (written_total < size) {
        const ssize_t written = TEMP_FAILURE_RETRY(write(fd,
                                                         bytes + written_total,
                                                         size - written_total));
        if (written <= 0) {
            return false;
        }
        written_total += static_cast<size_t>(written);
    }

    return true;
}

bool HostWriteFullyToRemoteProcFd(pid_t pid, int remote_fd, const uint8_t* data, size_t size) {
    if (pid <= 0 || remote_fd < 0 || data == nullptr) {
        return false;
    }

    char path[64] = {0};
    std::snprintf(path, sizeof(path), "/proc/%d/fd/%d", static_cast<int>(pid), remote_fd);
    const int local_fd = TEMP_FAILURE_RETRY(open(path, O_WRONLY | O_CLOEXEC));
    if (local_fd < 0) {
        NOOK_LOGI("HostWriteFullyToRemoteProcFd: open failed pid=%d remote_fd=%d errno=%d",
                  static_cast<int>(pid),
                  remote_fd,
                  errno);
        return false;
    }

    bool ok = false;
    if (TEMP_FAILURE_RETRY(lseek(local_fd, 0, SEEK_SET)) < 0) {
        NOOK_LOGI("HostWriteFullyToRemoteProcFd: lseek failed pid=%d remote_fd=%d errno=%d",
                  static_cast<int>(pid),
                  remote_fd,
                  errno);
    } else {
        ok = WriteFully(local_fd, data, size);
        if (!ok) {
            NOOK_LOGI("HostWriteFullyToRemoteProcFd: write failed pid=%d remote_fd=%d errno=%d size=%zu",
                      static_cast<int>(pid),
                      remote_fd,
                      errno,
                      size);
        } else {
            NOOK_LOGI("HostWriteFullyToRemoteProcFd: write ok pid=%d remote_fd=%d size=%zu",
                      static_cast<int>(pid),
                      remote_fd,
                      size);
        }
    }

    TEMP_FAILURE_RETRY(close(local_fd));
    return ok;
}

bool RemoteWriteFullyToFd(pid_t pid, int fd, const uint8_t* data, size_t size) {
    if (pid <= 0 || fd < 0 || data == nullptr) {
        NOOK_LOGE("RemoteWriteFullyToFd: invalid args pid=%d fd=%d data=%p size=%zu",
                  pid,
                  fd,
                  data,
                  size);
        return false;
    }

    if (HostWriteFullyToRemoteProcFd(pid, fd, data, size)) {
        return true;
    }
    NOOK_LOGI("RemoteWriteFullyToFd: host /proc write fallback to ptrace pid=%d fd=%d size=%zu",
              static_cast<int>(pid),
              fd,
              size);

    constexpr size_t kChunkSize = 16 * 1024;
    const size_t buffer_size = size < kChunkSize ? size : kChunkSize;
    void* remote_buf = RemoteAllocScratch(pid, buffer_size);
    if (remote_buf == nullptr) {
        NOOK_LOGE("RemoteWriteFullyToFd: remote scratch alloc failed pid=%d size=%zu", pid, buffer_size);
        return false;
    }

    size_t written_total = 0;
    bool ok = true;
    while (written_total < size) {
        const size_t chunk_size = std::min(kChunkSize, size - written_total);
        PTraceWrite(pid,
                    reinterpret_cast<long>(remote_buf),
                    const_cast<uint8_t*>(data + written_total),
                    chunk_size);
        const long written = CallRemoteFunction<long, int, const void*, size_t>(
            pid,
            reinterpret_cast<void*>(write),
            fd,
            remote_buf,
            chunk_size);
        if (written != static_cast<long>(chunk_size)) {
            NOOK_LOGE("RemoteWriteFullyToFd: remote write failed pid=%d fd=%d want=%zu got=%ld",
                      pid,
                      fd,
                      chunk_size,
                      written);
            ok = false;
            break;
        }
        written_total += chunk_size;
    }

    const bool free_ok = RemoteFreeScratch(pid, remote_buf, buffer_size);
    return ok && free_ok;
}

int RemoteCreateMemfdWithBytes(pid_t pid,
                               const char* name,
                               const uint8_t* data,
                               size_t size) {
    if (pid <= 0 || name == nullptr || name[0] == '\0' || data == nullptr) {
        NOOK_LOGE("RemoteCreateMemfdWithBytes: invalid args pid=%d name=%s data=%p size=%zu",
                  pid,
                  name ? name : "(null)",
                  data,
                  size);
        return -1;
    }

#if !defined(SYS_memfd_create)
    (void) pid;
    (void) name;
    (void) data;
    (void) size;
    SetLastInjectError("memfd_create_unsupported");
    return -1;
#else
    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:remote_memfd_stage");
        NOOK_LOGE("RemoteCreateMemfdWithBytes: AttachProcess failed pid=%d", pid);
        return -1;
    }

    int remote_fd = -1;
    void* remote_name = RemoteAllocString(pid, name);
    if (remote_name == nullptr) {
        SetLastInjectError("remote_alloc_failed:memfd_name");
        NOOK_LOGE("RemoteCreateMemfdWithBytes: RemoteAllocString failed pid=%d", pid);
        goto beach;
    }

    remote_fd = static_cast<int>(CallRemoteFunction<long, long, const char*, unsigned int>(
        pid,
        reinterpret_cast<void*>(syscall),
        static_cast<long>(SYS_memfd_create),
        reinterpret_cast<const char*>(remote_name),
        static_cast<unsigned int>(MFD_CLOEXEC)));
    if (remote_fd < 0) {
        SetLastInjectError("remote_memfd_create_failed");
        NOOK_LOGE("RemoteCreateMemfdWithBytes: remote memfd_create failed pid=%d", pid);
        (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
        goto beach;
    }

    (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    remote_name = nullptr;

    if (!RemoteWriteFullyToFd(pid, remote_fd, data, size)) {
        SetLastInjectError("remote_memfd_write_failed");
        CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), remote_fd);
        remote_fd = -1;
        goto beach;
    }

beach:
    if (remote_name != nullptr) {
        (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    }
    if (!DetachProcess(pid)) {
        if (remote_fd >= 0) {
            SetLastInjectError("detach_process_failed:remote_memfd_stage");
        }
        NOOK_LOGE("RemoteCreateMemfdWithBytes: DetachProcess failed pid=%d", pid);
        return -1;
    }

    return remote_fd;
#endif
}

void CloseRemoteFdBestEffort(pid_t pid, int fd) {
    if (pid <= 0 || fd < 0) {
        return;
    }

    if (!AttachProcess(pid)) {
        NOOK_LOGE("CloseRemoteFdBestEffort: AttachProcess failed pid=%d fd=%d", pid, fd);
        return;
    }

    (void) CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), fd);
    if (!DetachProcess(pid)) {
        NOOK_LOGE("CloseRemoteFdBestEffort: DetachProcess failed pid=%d fd=%d", pid, fd);
    }
}

bool RemoteSetEnv(pid_t pid, const char* name, const char* value) {
    if (pid <= 0 || name == nullptr || name[0] == '\0' || value == nullptr) {
        return false;
    }

    void* remote_name = RemoteAllocString(pid, name);
    void* remote_value = RemoteAllocString(pid, value);
    if (remote_name == nullptr || remote_value == nullptr) {
        if (remote_name != nullptr) {
            (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
        }
        if (remote_value != nullptr) {
            (void)RemoteFreeScratch(pid, remote_value, std::strlen(value) + 1);
        }
        return false;
    }

    const long rc = CallRemoteFunction<long, const char*, const char*, int>(
        pid,
        reinterpret_cast<void*>(setenv),
        reinterpret_cast<const char*>(remote_name),
        reinterpret_cast<const char*>(remote_value),
        1);
    (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    (void)RemoteFreeScratch(pid, remote_value, std::strlen(value) + 1);
    return rc == 0;
}

bool RemoteUnsetEnv(pid_t pid, const char* name) {
    if (pid <= 0 || name == nullptr || name[0] == '\0') {
        return false;
    }

    void* remote_name = RemoteAllocString(pid, name);
    if (remote_name == nullptr) {
        return false;
    }

    const long rc = CallRemoteFunction<long, const char*>(pid,
                                                          reinterpret_cast<void*>(unsetenv),
                                                          reinterpret_cast<const char*>(remote_name));
    (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    return rc == 0;
}

bool RemoteGetEnvEquals(pid_t pid, const char* name, const char* expected_value) {
    if (pid <= 0 || name == nullptr || name[0] == '\0' ||
        expected_value == nullptr || expected_value[0] == '\0') {
        return false;
    }

    void* remote_name = RemoteAllocString(pid, name);
    if (remote_name == nullptr) {
        return false;
    }

    void* remote_value = CallRemoteFunction<void*, const char*>(
        pid,
        reinterpret_cast<void*>(getenv),
        reinterpret_cast<const char*>(remote_name));
    (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    if (remote_value == nullptr) {
        return false;
    }

    const std::string value = ReadRemoteCString(pid, reinterpret_cast<long>(remote_value), 128);
    return value == expected_value;
}

void* InjectSoHandleByPid(pid_t pid, const char* so_path) {
    if (pid <= 0 || so_path == nullptr || so_path[0] == '\0') {
        SetLastInjectError("invalid_args:dlopen_stage");
        NOOK_LOGE("InjectSoHandleByPid: invalid args pid=%d so=%s", pid, so_path ? so_path : "(null)");
        return nullptr;
    }

    bool attached = false;
    void* remote_path = nullptr;
    void* handle = nullptr;
    AndroidLoaderApi loader_api;
    int proc_self_fd = -1;
    int remote_open_fd = -1;

    NOOK_LOGI("InjectSoHandleByPid: begin pid=%d so=%s", pid, so_path);

    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:dlopen_stage");
        NOOK_LOGE("InjectSoHandleByPid: AttachProcess failed pid=%d", pid);
        return nullptr;
    }
    attached = true;
    NOOK_LOGI("InjectSoHandleByPid: attached pid=%d", pid);

    if (!ResolveAndroidLoaderApi(pid, &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed");
        NOOK_LOGE("InjectSoHandleByPid: ResolveAndroidLoaderApi failed pid=%d", pid);
        goto fail;
    }

    remote_path = RemoteAllocString(pid, so_path);
    if (remote_path == nullptr) {
        SetLastInjectError("remote_alloc_failed:so_path");
        NOOK_LOGE("InjectSoHandleByPid: RemoteAllocString failed pid=%d", pid);
        goto fail;
    }

    if (TryParseProcSelfFdPath(so_path, &proc_self_fd)) {
        handle = CallRemoteAndroidDlopenExtUseFd(pid,
                                                 loader_api,
                                                 reinterpret_cast<const char*>(remote_path),
                                                 RTLD_NOW | RTLD_GLOBAL,
                                                 proc_self_fd);
        if (handle == nullptr) {
            NOOK_LOGI("InjectSoHandleByPid: android_dlopen_ext fallback pid=%d so=%s",
                      pid,
                      so_path);
        }
    } else {
        int (*openat_func)(int, const char*, int, ...) = ::openat;
        remote_open_fd = static_cast<int>(
            CallRemoteFunction<long, int, const char*, int, int>(
                pid,
                reinterpret_cast<void*>(openat_func),
                AT_FDCWD,
                reinterpret_cast<const char*>(remote_path),
                O_RDONLY | O_CLOEXEC,
                0));
        if (remote_open_fd >= 0) {
            NOOK_LOGI("InjectSoHandleByPid: remote open ok pid=%d so=%s fd=%d",
                      pid,
                      so_path,
                      remote_open_fd);
            handle = CallRemoteAndroidDlopenExtUseFd(pid,
                                                     loader_api,
                                                     reinterpret_cast<const char*>(remote_path),
                                                     RTLD_NOW | RTLD_GLOBAL,
                                                     remote_open_fd);
            if (handle == nullptr) {
                NOOK_LOGI("InjectSoHandleByPid: android_dlopen_ext remote-fd fallback pid=%d so=%s fd=%d",
                          pid,
                          so_path,
                          remote_open_fd);
            }
        } else {
            if (IsZygoteFamilyProcess(pid) && IsShellTmpRuntimePath(so_path)) {
                SetLastInjectError("remote_open_failed:zygote_shell_tmp_search_denied");
                NOOK_LOGE("InjectSoHandleByPid: remote open denied for zygote shell tmp pid=%d so=%s",
                          pid,
                          so_path);
            }
            NOOK_LOGI("InjectSoHandleByPid: remote open failed pid=%d so=%s",
                      pid,
                      so_path);
        }
    }
    if (handle == nullptr) {
        handle = CallRemoteAndroidDlopen(pid,
                                         loader_api,
                                         reinterpret_cast<const char*>(remote_path),
                                         RTLD_NOW | RTLD_GLOBAL);
    }
    if (handle != nullptr &&
        reinterpret_cast<uintptr_t>(handle) < static_cast<uintptr_t>(0x10000)) {
        NOOK_LOGE("InjectSoHandleByPid: suspicious remote dlopen handle pid=%d so=%s handle=%p",
                  pid,
                  so_path,
                  handle);
        handle = nullptr;
    }
    if (handle == nullptr) {
        std::string detail = "remote_dlopen_failed";
        std::string remote_error = CallRemoteAndroidDlerror(pid, loader_api);
        if (!remote_error.empty()) {
            detail += ":";
            detail += remote_error;
        }
        SetLastInjectError(detail);
        NOOK_LOGE("InjectSoHandleByPid: remote dlopen failed pid=%d so=%s detail=%s",
                  pid,
                  so_path,
                  detail.c_str());

        if (RemoteModulePathContains(pid, so_path)) {
            NOOK_LOGI("InjectSoHandleByPid: remote module present despite null dlopen return pid=%d so=%s",
                      pid,
                      so_path);
            handle = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
        } else {
            goto fail;
        }
    }
    NOOK_LOGI("InjectSoHandleByPid: remote dlopen ok pid=%d handle=%p", pid, handle);

    (void)RemoteFreeScratch(pid, remote_path, std::strlen(so_path) + 1);
    remote_path = nullptr;
    if (remote_open_fd >= 0) {
        (void)CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), remote_open_fd);
        remote_open_fd = -1;
    }

    if (!DetachProcess(pid)) {
        SetLastInjectError("detach_process_failed:dlopen_stage");
        NOOK_LOGE("InjectSoHandleByPid: DetachProcess failed pid=%d", pid);
        return nullptr;
    }

    NOOK_LOGI("InjectSoHandleByPid: success pid=%d handle=%p", pid, handle);

    return handle;

fail:
    if (remote_path != nullptr) {
        (void)RemoteFreeScratch(pid, remote_path, std::strlen(so_path) + 1);
    }
    if (remote_open_fd >= 0) {
        (void)CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), remote_open_fd);
    }
    if (attached) {
        DetachProcess(pid);
    }
    return nullptr;
}

bool InvokeRemoteInitSymbolByHandle(pid_t pid,
                                    void* handle,
                                    const char* init_symbol,
                                    const char* runtime_dir) {
    if (pid <= 0 || handle == nullptr || init_symbol == nullptr || init_symbol[0] == '\0') {
        SetLastInjectError("invalid_args:init_stage");
        return false;
    }

    AndroidLoaderApi loader_api;
    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:init_stage");
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: AttachProcess failed pid=%d", pid);
        return false;
    }

    if (!ResolveAndroidLoaderApi(pid, &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:init_stage");
        DetachProcess(pid);
        return false;
    }

    const bool zygote_control_init =
        std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0;
    if (zygote_control_init) {
        NOOK_LOGI("InvokeRemoteInitSymbolByHandle: zygote-control init skips remote env prewarm pid=%d",
                  pid);
    }

    if (!zygote_control_init &&
        runtime_dir != nullptr && runtime_dir[0] != '\0' &&
        !RemoteSetEnv(pid, "NOOK_RUNTIME_DIR", runtime_dir)) {
        SetLastInjectError("remote_setenv_failed:NOOK_RUNTIME_DIR");
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: RemoteSetEnv failed pid=%d runtime_dir=%s",
                  pid,
                  runtime_dir);
        DetachProcess(pid);
        return false;
    }

    void* remote_sym_name = RemoteAllocString(pid, init_symbol);
    if (remote_sym_name == nullptr) {
        SetLastInjectError(std::string("remote_alloc_failed:") + init_symbol);
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: RemoteAllocString failed pid=%d symbol=%s",
                  pid,
                  init_symbol);
        DetachProcess(pid);
        return false;
    }

    void* remote_init = CallRemoteAndroidDlsym(pid,
                                               loader_api,
                                               handle,
                                               reinterpret_cast<const char*>(remote_sym_name));
    if (remote_init == nullptr) {
        SetLastInjectError(std::string("dlsym_failed:") + init_symbol);
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: remote dlsym failed pid=%d handle=%p symbol=%s",
                  pid,
                  handle,
                  init_symbol);
        (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);
        DetachProcess(pid);
        return false;
    }
    NOOK_LOGI("InvokeRemoteInitSymbolByHandle: remote dlsym ok pid=%d init=%p", pid, remote_init);

    const intptr_t init_status = reinterpret_cast<intptr_t>(
        CallRemoteCall<void*>(pid, reinterpret_cast<long>(remote_init), 0, nullptr));
    NOOK_LOGI("InvokeRemoteInitSymbolByHandle: remote init returned pid=%d status=%ld",
              pid,
              static_cast<long>(init_status));

    (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);

    const bool detached = DetachProcess(pid);
    if (!detached) {
        SetLastInjectError("detach_process_failed:init_stage");
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: final DetachProcess failed pid=%d", pid);
    }
    if (init_status != 0) {
        SetLastInjectError(std::string("remote_init_failed:status=") +
                           std::to_string(static_cast<long>(init_status)));
        NOOK_LOGE("InvokeRemoteInitSymbolByHandle: init failed pid=%d symbol=%s status=%ld",
                  pid,
                  init_symbol,
                  static_cast<long>(init_status));
    }
    return init_status == 0 && detached;
}

bool InvokeRemoteInitSymbolByHandleWithSpawnContext(pid_t pid,
                                                    void* handle,
                                                    const char* init_symbol,
                                                    const char* runtime_dir,
                                                    const char* spawn_token) {
    if (pid <= 0 || handle == nullptr || init_symbol == nullptr || init_symbol[0] == '\0') {
        SetLastInjectError("invalid_args:init_stage_with_spawn_context");
        return false;
    }

    AndroidLoaderApi loader_api;
    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:init_stage_with_spawn_context");
        NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: AttachProcess failed pid=%d", pid);
        return false;
    }

    if (!ResolveAndroidLoaderApi(pid, &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:init_stage_with_spawn_context");
        DetachProcess(pid);
        return false;
    }

    const bool zygote_control_init =
        std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0;
    const bool spawn_child_init =
        std::strcmp(init_symbol, "NookAgentInitializeForSpawnChild") == 0;
    bool runtime_dir_env_set = false;
    bool spawn_token_env_set = false;

    if (zygote_control_init) {
        NOOK_LOGI("InvokeRemoteInitSymbolByHandleWithSpawnContext: zygote-control init skips remote env prewarm pid=%d",
                  pid);
    }

    if (!zygote_control_init &&
        runtime_dir != nullptr && runtime_dir[0] != '\0') {
        if (!RemoteSetEnv(pid, "NOOK_RUNTIME_DIR", runtime_dir)) {
            SetLastInjectError("remote_setenv_failed:NOOK_RUNTIME_DIR");
            NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: RemoteSetEnv failed pid=%d runtime_dir=%s",
                      pid,
                      runtime_dir);
            DetachProcess(pid);
            return false;
        }
        runtime_dir_env_set = true;
    }

    if (!zygote_control_init &&
        spawn_token != nullptr && spawn_token[0] != '\0') {
        if (!RemoteSetEnv(pid, "NOOK_SPAWN_TOKEN", spawn_token)) {
            SetLastInjectError("remote_setenv_failed:NOOK_SPAWN_TOKEN");
            NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: RemoteSetEnv failed pid=%d token=%s",
                      pid,
                      spawn_token);
            DetachProcess(pid);
            return false;
        }
        spawn_token_env_set = true;
    }

    void* remote_sym_name = RemoteAllocString(pid, init_symbol);
    if (remote_sym_name == nullptr) {
        SetLastInjectError(std::string("remote_alloc_failed:") + init_symbol);
        NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: RemoteAllocString failed pid=%d symbol=%s",
                  pid,
                  init_symbol);
        DetachProcess(pid);
        return false;
    }

    void* remote_init = CallRemoteAndroidDlsym(pid,
                                               loader_api,
                                               handle,
                                               reinterpret_cast<const char*>(remote_sym_name));
    if (remote_init == nullptr) {
        SetLastInjectError(std::string("dlsym_failed:") + init_symbol);
        NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: remote dlsym failed pid=%d handle=%p symbol=%s",
                  pid,
                  handle,
                  init_symbol);
        (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);
        if (spawn_token_env_set && !spawn_child_init) {
            (void)RemoteUnsetEnv(pid, "NOOK_SPAWN_TOKEN");
        }
        if (runtime_dir_env_set) {
            (void)RemoteUnsetEnv(pid, "NOOK_RUNTIME_DIR");
        }
        DetachProcess(pid);
        return false;
    }
    NOOK_LOGI("InvokeRemoteInitSymbolByHandleWithSpawnContext: remote dlsym ok pid=%d init=%p", pid, remote_init);

    const intptr_t init_status = reinterpret_cast<intptr_t>(
        CallRemoteCall<void*>(pid, reinterpret_cast<long>(remote_init), 0, nullptr));
    NOOK_LOGI("InvokeRemoteInitSymbolByHandleWithSpawnContext: remote init returned pid=%d status=%ld",
              pid,
              static_cast<long>(init_status));

    (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);

    if (spawn_token_env_set && !spawn_child_init) {
        (void)RemoteUnsetEnv(pid, "NOOK_SPAWN_TOKEN");
    }

    const bool detached = DetachProcess(pid);
    if (!detached) {
        SetLastInjectError("detach_process_failed:init_stage_with_spawn_context");
        NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: final DetachProcess failed pid=%d", pid);
    }
    if (init_status != 0) {
        SetLastInjectError(std::string("remote_init_failed:status=") +
                           std::to_string(static_cast<long>(init_status)));
        NOOK_LOGE("InvokeRemoteInitSymbolByHandleWithSpawnContext: init failed pid=%d symbol=%s status=%ld",
                  pid,
                  init_symbol,
                  static_cast<long>(init_status));
    }
    return init_status == 0 && detached;
}

bool InvokeRemoteInitSymbolByPath(pid_t pid,
                                  const char* so_path,
                                  const char* init_symbol,
                                  const char* runtime_dir) {
    if (pid <= 0 || so_path == nullptr || so_path[0] == '\0' ||
        init_symbol == nullptr || init_symbol[0] == '\0') {
        SetLastInjectError("invalid_args:init_stage_by_path");
        return false;
    }

    uint64_t symbol_offset = 0;
    if (!FindElfExportOffset(so_path, {init_symbol}, &symbol_offset)) {
        SetLastInjectError(std::string("find_export_failed:") + init_symbol);
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: export not found pid=%d so=%s symbol=%s",
                  pid,
                  so_path,
                  init_symbol);
        return false;
    }

    const long remote_base = GetModuleBase(pid, so_path);
    if (remote_base == 0) {
        SetLastInjectError("remote_module_base_failed");
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: module base not found pid=%d so=%s",
                  pid,
                  so_path);
        return false;
    }

    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:init_stage_by_path");
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: AttachProcess failed pid=%d", pid);
        return false;
    }

    const auto detach_guard = [&]() {
        const bool detached = DetachProcess(pid);
        if (!detached) {
            SetLastInjectError("detach_process_failed:init_stage_by_path");
            NOOK_LOGE("InvokeRemoteInitSymbolByPath: DetachProcess failed pid=%d", pid);
        }
        return detached;
    };

    if (std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0) {
        const bool strict_helper_only =
            so_path != nullptr &&
            std::strstr(so_path, "zygote-helper") != nullptr;
        const char* desired_native_hooks = "0";
        const char* desired_wrapper_hooks = "0";
        if (!RemoteSetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS", desired_native_hooks)) {
            SetLastInjectError("remote_setenv_failed:NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
            NOOK_LOGE("InvokeRemoteInitSymbolByPath: RemoteSetEnv failed pid=%d name=%s",
                      pid,
                      "NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
            (void)detach_guard();
            return false;
        }
        if (!RemoteSetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS", desired_wrapper_hooks)) {
            SetLastInjectError("remote_setenv_failed:NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
            NOOK_LOGE("InvokeRemoteInitSymbolByPath: RemoteSetEnv failed pid=%d name=%s",
                      pid,
                      "NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
            (void)detach_guard();
            return false;
        }
        NOOK_LOGI("InvokeRemoteInitSymbolByPath: zygote-control init hook env native=%s wrapper=%s helper_only=%d pid=%d",
                  desired_native_hooks,
                  desired_wrapper_hooks,
                  strict_helper_only ? 1 : 0,
                  pid);
    }

    if (runtime_dir != nullptr && runtime_dir[0] != '\0' &&
        !RemoteSetEnv(pid, "NOOK_RUNTIME_DIR", runtime_dir)) {
        SetLastInjectError("remote_setenv_failed:NOOK_RUNTIME_DIR");
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: RemoteSetEnv failed pid=%d runtime_dir=%s",
                  pid,
                  runtime_dir);
        (void)detach_guard();
        return false;
    }

    const uintptr_t remote_init =
        static_cast<uintptr_t>(remote_base) + static_cast<uintptr_t>(symbol_offset);
    NOOK_LOGI("InvokeRemoteInitSymbolByPath: pid=%d so=%s symbol=%s base=%lx offset=%llx init=%p",
              pid,
              so_path,
              init_symbol,
              remote_base,
              static_cast<unsigned long long>(symbol_offset),
              reinterpret_cast<void*>(remote_init));

    const intptr_t init_status = reinterpret_cast<intptr_t>(
        CallRemoteCall<void*>(pid, static_cast<long>(remote_init), 0, nullptr));
    NOOK_LOGI("InvokeRemoteInitSymbolByPath: remote init returned pid=%d status=%ld",
              pid,
              static_cast<long>(init_status));

    const bool unset_ok = RemoteUnsetEnv(pid, "NOOK_SKIP_AUTO_INIT");
    if (!unset_ok) {
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: RemoteUnsetEnv failed pid=%d name=%s",
                  pid,
                  "NOOK_SKIP_AUTO_INIT");
    }

    const bool detached = detach_guard();
    if (init_status != 0) {
        SetLastInjectError(std::string("remote_init_failed:status=") +
                           std::to_string(static_cast<long>(init_status)));
        NOOK_LOGE("InvokeRemoteInitSymbolByPath: init failed pid=%d symbol=%s status=%ld",
                  pid,
                  init_symbol,
                  static_cast<long>(init_status));
    }
    return init_status == 0 && detached;
}

bool InvokeRemoteInitSymbolByEmbeddedImage(pid_t pid,
                                           const char* remote_module_hint,
                                           const uint8_t* embedded_blob,
                                           size_t embedded_blob_size,
                                           const char* init_symbol,
                                           const char* runtime_dir) {
    if (pid <= 0 || remote_module_hint == nullptr || remote_module_hint[0] == '\0' ||
        embedded_blob == nullptr || embedded_blob_size == 0 ||
        init_symbol == nullptr || init_symbol[0] == '\0') {
        SetLastInjectError("invalid_args:init_stage_embedded");
        return false;
    }

    uint64_t symbol_offset = 0;
    std::vector<uint8_t> image(embedded_blob, embedded_blob + embedded_blob_size);
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(image.data());
    if (image.size() < sizeof(Elf64_Ehdr) ||
        memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_shoff == 0 ||
        ehdr->e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr->e_shnum == 0) {
        SetLastInjectError("embedded_image_invalid");
        return false;
    }
    if (!FindElfExportOffset("/proc/self/exe", {init_symbol}, &symbol_offset)) {
        symbol_offset = 0;
    }
    if (symbol_offset == 0) {
        const auto* shdrs =
            reinterpret_cast<const Elf64_Shdr*>(image.data() + static_cast<size_t>(ehdr->e_shoff));
        for (size_t i = 0; i < ehdr->e_shnum; ++i) {
            const Elf64_Shdr& shdr = shdrs[i];
            if ((shdr.sh_type != SHT_DYNSYM && shdr.sh_type != SHT_SYMTAB) ||
                shdr.sh_entsize != sizeof(Elf64_Sym) ||
                shdr.sh_offset >= image.size() ||
                shdr.sh_link >= ehdr->e_shnum) {
                continue;
            }

            const Elf64_Shdr& strtab = shdrs[shdr.sh_link];
            if (strtab.sh_offset >= image.size() ||
                static_cast<size_t>(strtab.sh_offset + strtab.sh_size) > image.size() ||
                static_cast<size_t>(shdr.sh_offset + shdr.sh_size) > image.size()) {
                continue;
            }

            const auto* syms = reinterpret_cast<const Elf64_Sym*>(
                image.data() + static_cast<size_t>(shdr.sh_offset));
            const size_t sym_count = static_cast<size_t>(shdr.sh_size / sizeof(Elf64_Sym));
            const char* strtab_data =
                reinterpret_cast<const char*>(image.data() + static_cast<size_t>(strtab.sh_offset));
            for (size_t sym_index = 0; sym_index < sym_count; ++sym_index) {
                const Elf64_Sym& sym = syms[sym_index];
                if (sym.st_name >= strtab.sh_size) {
                    continue;
                }
                const char* name = strtab_data + sym.st_name;
                if (std::strcmp(name, init_symbol) == 0) {
                    symbol_offset = sym.st_value;
                    break;
                }
            }
            if (symbol_offset != 0) {
                break;
            }
        }
    }

    if (symbol_offset == 0) {
        SetLastInjectError(std::string("find_export_failed:embedded:") + init_symbol);
        return false;
    }

    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:init_stage_embedded");
        return false;
    }

    const auto detach_guard = [&]() {
        const bool detached = DetachProcess(pid);
        if (!detached) {
            SetLastInjectError("detach_process_failed:init_stage_embedded");
        }
        return detached;
    };

    AndroidLoaderApi loader_api;
    if (!ResolveAndroidLoaderApi(pid, &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:init_stage_embedded");
        (void)detach_guard();
        return false;
    }

    if (std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0) {
        const bool strict_helper_only =
            remote_module_hint != nullptr &&
            std::strstr(remote_module_hint, "zygote-helper") != nullptr;
        const char* desired_native_hooks = "0";
        const char* desired_wrapper_hooks = "0";
        if (!RemoteSetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS", desired_native_hooks)) {
            SetLastInjectError("remote_setenv_failed:NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
            (void)detach_guard();
            return false;
        }
        if (!RemoteSetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS", desired_wrapper_hooks)) {
            SetLastInjectError("remote_setenv_failed:NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
            (void)detach_guard();
            return false;
        }
        NOOK_LOGI("InvokeRemoteInitSymbolByEmbeddedImage: zygote-control init hook env native=%s wrapper=%s helper_only=%d pid=%d",
                  desired_native_hooks,
                  desired_wrapper_hooks,
                  strict_helper_only ? 1 : 0,
                  pid);
    }

    if (runtime_dir != nullptr && runtime_dir[0] != '\0' &&
        !RemoteSetEnv(pid, "NOOK_RUNTIME_DIR", runtime_dir)) {
        SetLastInjectError("remote_setenv_failed:NOOK_RUNTIME_DIR");
        (void)detach_guard();
        return false;
    }

    const bool force_reinit =
        std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0 &&
        GetModuleBase(pid, remote_module_hint) != 0;
    if (force_reinit &&
        !RemoteSetEnv(pid, "NOOK_ZYGOTE_FORCE_REINIT", "1")) {
        SetLastInjectError("remote_setenv_failed:NOOK_ZYGOTE_FORCE_REINIT");
        (void)detach_guard();
        return false;
    }

    void* remote_sym_name = RemoteAllocString(pid, init_symbol);
    if (remote_sym_name == nullptr) {
        SetLastInjectError(std::string("remote_alloc_failed:embedded:") + init_symbol);
        (void)detach_guard();
        return false;
    }

    void* remote_init =
        CallRemoteAndroidDlsym(pid,
                               loader_api,
                               nullptr,
                               reinterpret_cast<const char*>(remote_sym_name));
    (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);
    if (remote_init == nullptr) {
        const long remote_base = GetModuleBase(pid, remote_module_hint);
        if (remote_base == 0) {
            SetLastInjectError("remote_module_base_failed:embedded");
            (void)detach_guard();
            return false;
        }
        remote_init = reinterpret_cast<void*>(remote_base + static_cast<long>(symbol_offset));
        NOOK_LOGI("InvokeRemoteInitSymbolByEmbeddedImage: dlsym(NULL,%s) missed, fallback base=%lx symbol=%p pid=%d",
                  init_symbol,
                  remote_base,
                  remote_init,
                  pid);
    } else {
        NOOK_LOGI("InvokeRemoteInitSymbolByEmbeddedImage: dlsym(NULL,%s) resolved symbol=%p pid=%d",
                  init_symbol,
                  remote_init,
                  pid);
    }

    const intptr_t init_status = reinterpret_cast<intptr_t>(
        CallRemoteCall<void*>(pid, reinterpret_cast<long>(remote_init), 0, nullptr));
    if (force_reinit) {
        (void)RemoteUnsetEnv(pid, "NOOK_ZYGOTE_FORCE_REINIT");
    }
    const bool detached = detach_guard();
    if (init_status != 0) {
        SetLastInjectError(std::string("remote_init_failed:embedded:status=") +
                           std::to_string(static_cast<long>(init_status)));
        return false;
    }
    return detached;
}

int ParseCallbackPidFromPayload(const std::string& payload) {
    if (payload.empty()) {
        return -1;
    }

    const char* pid_key = "\"pid\":";
    const char* pid_pos = strstr(payload.c_str(), pid_key);
    if (pid_pos == nullptr) {
        return -1;
    }
    return atoi(pid_pos + strlen(pid_key));
}

}  // namespace

#endif  // defined(__ANDROID__) && defined(__aarch64__)

namespace nook {
namespace server {
namespace ninjector {

bool PrepareSpawnInZygote(int zygote_pid,
                          const char* ncore_path,
                          const char* package_name,
                          const char* so_path,
                          const char* spawn_token);
std::string TrimAsciiWhitespace(std::string value);
std::string ReadCommandOutputFirstNonEmptyLine(const std::string& command);
std::string ResolveLaunchComponent(const char* package_name);
bool WaitForProcessExitByName(const char* process_name, uint32_t timeout_ms);
bool WaitForProcessStartByName(const char* process_name, uint32_t timeout_ms);

#if !defined(__ANDROID__) || !defined(__aarch64__)
namespace {
std::mutex g_host_last_inject_error_mutex;
std::string g_host_last_inject_error;
}

void SetLastInjectError(const std::string& error) {
    std::lock_guard<std::mutex> lock(g_host_last_inject_error_mutex);
    g_host_last_inject_error = error;
}

void ClearLastInjectError() {
    SetLastInjectError("");
}
#endif

int GetPid(const char* process_name) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (process_name == nullptr || process_name[0] == '\0') {
        return -1;
    }

    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return -1;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const int pid = atoi(entry->d_name);
        if (pid <= 0) {
            continue;
        }

        char path[256] = {0};
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* fp = fopen(path, "r");
        if (fp == nullptr) {
            continue;
        }

        char cmdline[256] = {0};
        fgets(cmdline, sizeof(cmdline), fp);
        fclose(fp);

        char* name = strrchr(cmdline, '/');
        name = (name == nullptr) ? cmdline : name + 1;
        if (strcmp(process_name, name) == 0) {
            closedir(dir);
            return pid;
        }
    }

    closedir(dir);
    return -1;
#else
    (void)process_name;
    return -1;
#endif
}

bool InjectSoByPidWithEntry(int pid, const char* so_path, const char* init_symbol) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0 || so_path == nullptr || so_path[0] == '\0' ||
        init_symbol == nullptr || init_symbol[0] == '\0') {
        SetLastInjectError("invalid_args");
        NOOK_LOGE("InjectSoByPidWithEntry: invalid args pid=%d so=%s init=%s",
                  pid,
                  so_path ? so_path : "(null)",
                  init_symbol ? init_symbol : "(null)");
        return false;
    }

    NOOK_LOGI("InjectSoByPidWithEntry: begin pid=%d so=%s init=%s", pid, so_path, init_symbol);
    void* handle = InjectSoHandleByPid(static_cast<pid_t>(pid), so_path);
    if (handle == nullptr) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("inject_handle_failed");
        }
        NOOK_LOGE("InjectSoByPidWithEntry: InjectSoHandleByPid failed pid=%d so=%s", pid, so_path);
        return false;
    }
    NOOK_LOGI("InjectSoByPidWithEntry: remote handle ready pid=%d handle=%p", pid, handle);
    return InvokeRemoteInitSymbolByHandle(static_cast<pid_t>(pid), handle, init_symbol, nullptr);
#else
    (void)pid;
    (void)so_path;
    (void)init_symbol;
    return false;
#endif
}

bool InjectSoByPid(int pid, const char* so_path, const char* ready_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0 || so_path == nullptr || so_path[0] == '\0') {
        SetLastInjectError("invalid_args");
        NOOK_LOGE("InjectSoByPid: invalid args pid=%d so=%s",
                  pid,
                  so_path ? so_path : "(null)");
        return false;
    }

    NOOK_LOGI("InjectSoByPid: begin pid=%d so=%s", pid, so_path);
    if (!AttachProcess(static_cast<pid_t>(pid))) {
        SetLastInjectError("attach_process_failed:preload_stage");
        NOOK_LOGE("InjectSoByPid: AttachProcess failed before preload env pid=%d", pid);
        return false;
    }
    const bool skip_env_set = RemoteSetEnv(static_cast<pid_t>(pid), "NOOK_SKIP_AUTO_INIT", "1");
    const bool detached_after_env = DetachProcess(static_cast<pid_t>(pid));
    if (!skip_env_set || !detached_after_env) {
        if (!skip_env_set) {
            SetLastInjectError("remote_setenv_failed:NOOK_SKIP_AUTO_INIT");
            NOOK_LOGE("InjectSoByPid: RemoteSetEnv failed pid=%d name=%s",
                      pid,
                      "NOOK_SKIP_AUTO_INIT");
        } else {
            SetLastInjectError("detach_process_failed:preload_stage");
            NOOK_LOGE("InjectSoByPid: DetachProcess failed after preload env pid=%d", pid);
        }
        return false;
    }

    void* handle = InjectSoHandleByPid(static_cast<pid_t>(pid), so_path);
    if (handle == nullptr) {
        if (AttachProcess(static_cast<pid_t>(pid))) {
            (void)RemoteUnsetEnv(static_cast<pid_t>(pid), "NOOK_SKIP_AUTO_INIT");
            (void)DetachProcess(static_cast<pid_t>(pid));
        }
        if (GetLastInjectError().empty()) {
            SetLastInjectError("inject_handle_failed");
        }
        NOOK_LOGE("InjectSoByPid: InjectSoHandleByPid failed pid=%d so=%s", pid, so_path);
        return false;
    }
    NOOK_LOGI("InjectSoByPid: remote handle ready pid=%d handle=%p", pid, handle);
    const std::string runtime_dir = [] (const char* path) {
        if (path == nullptr) {
            return std::string();
        }
        const std::string agent_path(path);
        const std::string::size_type slash = agent_path.find_last_of("/\\");
        if (slash == std::string::npos) {
            return std::string();
        }
        if (slash == 0) {
            return agent_path.substr(0, 1);
        }
        return agent_path.substr(0, slash);
    }(so_path);
    if (ready_token != nullptr && ready_token[0] != '\0') {
        return InvokeRemoteInitSymbolByHandleWithSpawnContext(static_cast<pid_t>(pid),
                                                              handle,
                                                              "NookAgentInitialize",
                                                              runtime_dir.empty() ? nullptr : runtime_dir.c_str(),
                                                              ready_token);
    }
    return InvokeRemoteInitSymbolByPath(static_cast<pid_t>(pid),
                                        so_path,
                                        "NookAgentInitialize",
                                        runtime_dir.empty() ? nullptr : runtime_dir.c_str());
#else
    (void)pid;
    (void)so_path;
    (void)ready_token;
    return false;
#endif
}

// Atomic memfd+dlopen: single ptrace attach for the entire
// memfd_create → write → dlopen → optional init sequence.
// This eliminates the detach/re-attach gap that allows the remote fd
// to become stale while the target runs freely between calls.
bool InjectEmbeddedSoByPidAtomic(pid_t pid,
                                 const char* memfd_name,
                                 const uint8_t* blob,
                                 size_t blob_size,
                                 const char* init_symbol,
                                 const char* runtime_dir,
                                 const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__) && defined(SYS_memfd_create)
    if (pid <= 0 || blob == nullptr || blob_size == 0) {
        SetLastInjectError("invalid_args:atomic_inject");
        return false;
    }

    NOOK_LOGI("InjectEmbeddedSoByPidAtomic: begin pid=%d name=%s size=%zu init=%s",
              pid,
              memfd_name ? memfd_name : "agent",
              blob_size,
              init_symbol ? init_symbol : "(none)");

    // --- single attach for the entire operation ---
    if (!AttachProcess(pid)) {
        SetLastInjectError("attach_process_failed:atomic_inject");
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: AttachProcess failed pid=%d", pid);
        return false;
    }

    int remote_fd = -1;
    void* remote_name = nullptr;
    void* remote_path = nullptr;
    void* remote_sym_name = nullptr;
    void* handle = nullptr;
    bool success = false;
    bool runtime_dir_env_set = false;
    bool spawn_token_env_set = false;
    bool skip_auto_init_env_set = false;
    bool zygote_java_native_hooks_env_set = false;
    bool zygote_java_wrapper_hooks_env_set = false;
    const bool zygote_control_init =
        init_symbol != nullptr &&
        std::strcmp(init_symbol, "NookAgentInitializeForZygoteControl") == 0;
    const bool spawn_child_init =
        init_symbol != nullptr &&
        std::strcmp(init_symbol, "NookAgentInitializeForSpawnChild") == 0;
    AndroidLoaderApi loader_api;

    if (!zygote_control_init &&
        runtime_dir != nullptr && runtime_dir[0] != '\0') {
        if (!RemoteSetEnv(pid, "NOOK_RUNTIME_DIR", runtime_dir)) {
            SetLastInjectError("remote_setenv_failed:NOOK_RUNTIME_DIR");
            NOOK_LOGE("InjectEmbeddedSoByPidAtomic: set runtime dir failed pid=%d dir=%s",
                      pid,
                      runtime_dir);
            goto cleanup;
        }
        runtime_dir_env_set = true;
    }

    if (!zygote_control_init &&
        spawn_token != nullptr && spawn_token[0] != '\0') {
        if (!RemoteSetEnv(pid, "NOOK_SPAWN_TOKEN", spawn_token)) {
            SetLastInjectError("remote_setenv_failed:NOOK_SPAWN_TOKEN");
            NOOK_LOGE("InjectEmbeddedSoByPidAtomic: set spawn token failed pid=%d token=%s",
                      pid,
                      spawn_token);
            goto cleanup;
        }
        spawn_token_env_set = true;
    }

    if (zygote_control_init) {
        NOOK_LOGI("InjectEmbeddedSoByPidAtomic: zygote-control init skips remote env prewarm pid=%d",
                  pid);
    }

    if (!zygote_control_init &&
        init_symbol != nullptr && init_symbol[0] != '\0') {
        if (!RemoteSetEnv(pid, "NOOK_SKIP_AUTO_INIT", "1")) {
            SetLastInjectError("remote_setenv_failed:NOOK_SKIP_AUTO_INIT");
            NOOK_LOGE("InjectEmbeddedSoByPidAtomic: set skip auto init failed pid=%d", pid);
            goto cleanup;
        }
        skip_auto_init_env_set = true;
        NOOK_LOGI("InjectEmbeddedSoByPidAtomic: verify env pid=%d name=%s ok=%d",
                  pid,
                  "NOOK_SKIP_AUTO_INIT",
                  RemoteGetEnvEquals(pid, "NOOK_SKIP_AUTO_INIT", "1") ? 1 : 0);
    }

    // 1. memfd_create
    remote_name = RemoteAllocString(pid, memfd_name ? memfd_name : "nook-agent");
    if (remote_name == nullptr) {
        SetLastInjectError("remote_alloc_failed:atomic_memfd_name");
        goto cleanup;
    }

    remote_fd = static_cast<int>(CallRemoteFunction<long, long, const char*, unsigned int>(
        pid,
        reinterpret_cast<void*>(syscall),
        static_cast<long>(SYS_memfd_create),
        reinterpret_cast<const char*>(remote_name),
        static_cast<unsigned int>(MFD_CLOEXEC)));
    (void)RemoteFreeScratch(pid, remote_name, std::strlen(memfd_name ? memfd_name : "nook-agent") + 1);
    remote_name = nullptr;

    if (remote_fd < 0) {
        SetLastInjectError("remote_memfd_create_failed:atomic");
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: remote memfd_create failed pid=%d", pid);
        goto cleanup;
    }

    // 2. write blob into memfd (still attached)
    if (!RemoteWriteFullyToFd(pid, remote_fd, blob, blob_size)) {
        SetLastInjectError("remote_memfd_write_failed:atomic");
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: write failed pid=%d fd=%d", pid, remote_fd);
        goto cleanup;
    }

    // 3. dlopen("/proc/self/fd/<remote_fd>")
    if (!ResolveAndroidLoaderApi(pid, &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:atomic");
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: ResolveAndroidLoaderApi failed pid=%d", pid);
        goto cleanup;
    }

    {
        std::string proc_fd_path("/proc/self/fd/");
        proc_fd_path.append(std::to_string(remote_fd));

        remote_path = RemoteAllocString(pid, proc_fd_path.c_str());
        if (remote_path == nullptr) {
            SetLastInjectError("remote_alloc_failed:atomic_so_path");
            goto cleanup;
        }

        NOOK_LOGI("InjectEmbeddedSoByPidAtomic: dlopen pid=%d path=%s", pid, proc_fd_path.c_str());

        handle = CallRemoteAndroidDlopenExtUseFd(pid,
                                                 loader_api,
                                                 reinterpret_cast<const char*>(remote_path),
                                                 RTLD_NOW | RTLD_GLOBAL,
                                                 remote_fd);
        if (handle == nullptr) {
            NOOK_LOGI("InjectEmbeddedSoByPidAtomic: android_dlopen_ext fallback pid=%d fd=%d",
                      pid,
                      remote_fd);
            handle = CallRemoteAndroidDlopen(pid,
                                             loader_api,
                                             reinterpret_cast<const char*>(remote_path),
                                             RTLD_NOW | RTLD_GLOBAL);
        }
        (void)RemoteFreeScratch(pid, remote_path, proc_fd_path.size() + 1);
        remote_path = nullptr;
    }

    if (handle != nullptr &&
        reinterpret_cast<uintptr_t>(handle) < static_cast<uintptr_t>(0x10000)) {
        handle = nullptr;
    }
    if (handle == nullptr) {
        std::string detail = "remote_dlopen_failed";
        std::string remote_error = CallRemoteAndroidDlerror(pid, loader_api);
        if (!remote_error.empty()) {
            detail += ":";
            detail += remote_error;
        }
        SetLastInjectError(detail);
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: dlopen failed pid=%d detail=%s", pid, detail.c_str());
        goto cleanup;
    }
    NOOK_LOGI("InjectEmbeddedSoByPidAtomic: dlopen ok pid=%d handle=%p", pid, handle);

    // 4. close the memfd now that dlopen succeeded (still attached)
    CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), remote_fd);
    remote_fd = -1;

    // 5. optional: dlsym(init_symbol) + call
    if (init_symbol != nullptr && init_symbol[0] != '\0') {
        remote_sym_name = RemoteAllocString(pid, init_symbol);
        if (remote_sym_name == nullptr) {
            SetLastInjectError("remote_alloc_failed:atomic_init_sym");
            goto cleanup;
        }

        void* remote_init = CallRemoteAndroidDlsym(pid,
                                                    loader_api,
                                                    handle,
                                                    reinterpret_cast<const char*>(remote_sym_name));
        (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);
        remote_sym_name = nullptr;

        if (remote_init == nullptr) {
            SetLastInjectError(std::string("dlsym_failed:atomic:") + init_symbol);
            NOOK_LOGE("InjectEmbeddedSoByPidAtomic: dlsym failed pid=%d sym=%s", pid, init_symbol);
            goto cleanup;
        }

        const intptr_t init_status = reinterpret_cast<intptr_t>(
            CallRemoteCall<void*>(pid, reinterpret_cast<long>(remote_init), 0, nullptr));
        NOOK_LOGI("InjectEmbeddedSoByPidAtomic: init returned pid=%d status=%ld", pid, (long)init_status);

        if (init_status != 0) {
            SetLastInjectError(std::string("remote_init_failed:atomic:status=") +
                               std::to_string(static_cast<long>(init_status)));
            goto cleanup;
        }

        if (skip_auto_init_env_set) {
            if (!RemoteUnsetEnv(pid, "NOOK_SKIP_AUTO_INIT")) {
                NOOK_LOGE("InjectEmbeddedSoByPidAtomic: unset skip auto init failed pid=%d; continuing",
                          pid);
            } else {
                skip_auto_init_env_set = false;
            }
        }
    }

    success = true;

cleanup:
    if (remote_name != nullptr) {
        (void)RemoteFreeScratch(pid, remote_name, std::strlen(memfd_name ? memfd_name : "nook-agent") + 1);
    }
    if (remote_path != nullptr) {
        const std::string proc_fd_cleanup = std::string("/proc/self/fd/") + std::to_string(remote_fd);
        (void)RemoteFreeScratch(pid, remote_path, proc_fd_cleanup.size() + 1);
    }
    if (remote_sym_name != nullptr) {
        (void)RemoteFreeScratch(pid, remote_sym_name, std::strlen(init_symbol) + 1);
    }
    if (skip_auto_init_env_set) {
        (void)RemoteUnsetEnv(pid, "NOOK_SKIP_AUTO_INIT");
    }
    if (spawn_token_env_set && !spawn_child_init) {
        (void)RemoteUnsetEnv(pid, "NOOK_SPAWN_TOKEN");
    }
    if (zygote_java_wrapper_hooks_env_set) {
        (void)RemoteUnsetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS");
    }
    if (zygote_java_native_hooks_env_set) {
        (void)RemoteUnsetEnv(pid, "NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS");
    }
    if (remote_fd >= 0) {
        CallRemoteFunction<long, int>(pid, reinterpret_cast<void*>(close), remote_fd);
    }
    if (!DetachProcess(pid)) {
        NOOK_LOGE("InjectEmbeddedSoByPidAtomic: DetachProcess failed pid=%d", pid);
        if (success) {
            SetLastInjectError("detach_process_failed:atomic_inject");
            success = false;
        }
    }

    return success;
#else
    (void)pid;
    (void)memfd_name;
    (void)blob;
    (void)blob_size;
    (void)init_symbol;
    (void)runtime_dir;
    (void)spawn_token;
    SetLastInjectError("atomic_inject_unsupported");
    return false;
#endif
}

bool InjectEmbeddedAgentByPid(int pid, const char* runtime_dir, const char* ready_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_agent");
        return false;
    }

    return InjectEmbeddedSoByPidAtomic(static_cast<pid_t>(pid),
                                       "libnook-agent",
                                       kNookEmbeddedAgentBlob,
                                       static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                       "NookAgentInitialize",
                                       runtime_dir,
                                       ready_token);
#else
    (void)pid;
    (void)runtime_dir;
    (void)ready_token;
    return false;
#endif
}

bool InjectEmbeddedAgentByPidSuspended(int pid, const char* runtime_dir) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_agent_suspended");
        return false;
    }

    return InjectEmbeddedSoByPidAtomic(static_cast<pid_t>(pid),
                                       "libnook-agent",
                                       kNookEmbeddedAgentBlob,
                                       static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                       "NookAgentInitializeForSpawnChild",
                                       runtime_dir,
                                       nullptr);
#else
    (void)pid;
    (void)runtime_dir;
    return false;
#endif
}

bool InjectEmbeddedAgentByPidSuspendedWithSpawnContext(int pid,
                                                       const char* runtime_dir,
                                                       const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_agent_suspended");
        return false;
    }

    return InjectEmbeddedSoByPidAtomic(static_cast<pid_t>(pid),
                                       "libnook-agent",
                                       kNookEmbeddedAgentBlob,
                                       static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                       "NookAgentInitializeForSpawnChild",
                                       runtime_dir,
                                       spawn_token);
#else
    (void)pid;
    (void)runtime_dir;
    (void)spawn_token;
    return false;
#endif
}

bool RemoteGetEnvTruthy(pid_t pid, const char* name) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (pid <= 0 || name == nullptr || name[0] == '\0') {
        return false;
    }

    void* remote_name = RemoteAllocString(pid, name);
    if (remote_name == nullptr) {
        return false;
    }

    void* remote_value = CallRemoteFunction<void*, const char*>(
        pid,
        reinterpret_cast<void*>(getenv),
        reinterpret_cast<const char*>(remote_name));
    (void)RemoteFreeScratch(pid, remote_name, std::strlen(name) + 1);
    if (remote_value == nullptr) {
        return false;
    }

    const std::string value = ReadRemoteCString(pid, reinterpret_cast<long>(remote_value), 16);
    return value == "1" || value == "true";
#else
    (void)pid;
    (void)name;
    return false;
#endif
}

bool InjectEmbeddedZygoteAgentByPid(int pid, const char* runtime_dir) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_zygote_agent");
        return false;
    }

    const std::string resolved_runtime_dir = ResolveZygoteRuntimeDirectory(runtime_dir);
    const long existing_agent_base = GetModuleBase(static_cast<pid_t>(pid), "libnook-agent");
    if (existing_agent_base != 0) {
        NOOK_LOGI("InjectEmbeddedZygoteAgentByPid: existing zygote agent base pid=%d base=%lx",
                  pid,
                  existing_agent_base);
        if (AttachProcess(static_cast<pid_t>(pid))) {
            const bool reinit_capable =
                RemoteGetEnvTruthy(static_cast<pid_t>(pid), "NOOK_ZYGOTE_REINIT_CAPABLE");
            if (!DetachProcess(static_cast<pid_t>(pid))) {
                SetLastInjectError("detach_process_failed:zygote_reinit_probe");
                return false;
            }
            if (reinit_capable) {
                NOOK_LOGI("InjectEmbeddedZygoteAgentByPid: reuse already-loaded zygote agent pid=%d runtime_dir=%s",
                          pid,
                          resolved_runtime_dir.c_str());
                return ReinitializeEmbeddedZygoteAgentByPid(
                    pid,
                    resolved_runtime_dir.empty() ? nullptr : resolved_runtime_dir.c_str());
            }
        } else {
            NOOK_LOGE("InjectEmbeddedZygoteAgentByPid: reinit capability probe attach failed pid=%d",
                      pid);
        }
    }

    const std::string sidecar_path =
        nook::server::BuildEmbeddedAgentPathForRuntimeDirectory(
            resolved_runtime_dir,
            kNookEmbeddedAgentBlob,
            static_cast<size_t>(kNookEmbeddedAgentBlobSize));
    nook::server::EmbeddedFileMaterializationResult materialize_result =
        nook::server::EmbeddedFileMaterializationResult::kError;
    std::string materialize_error;
    if (!nook::server::EnsureEmbeddedFileAtPath(sidecar_path.c_str(),
                                                kNookEmbeddedAgentBlob,
                                                static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                                &materialize_result,
                                                &materialize_error)) {
        if (!materialize_error.empty()) {
            SetLastInjectError("embedded_zygote_agent_materialize_failed:" + materialize_error);
        } else {
            SetLastInjectError("embedded_zygote_agent_materialize_failed");
        }
        NOOK_LOGE("InjectEmbeddedZygoteAgentByPid: materialize sidecar failed pid=%d path=%s detail=%s",
                  pid,
                  sidecar_path.c_str(),
                  materialize_error.empty() ? "(empty)" : materialize_error.c_str());
        return false;
    }

    NOOK_LOGI("InjectEmbeddedZygoteAgentByPid: first-load sidecar zygote inject pid=%d runtime_dir=%s path=%s",
              pid,
              resolved_runtime_dir.c_str(),
              sidecar_path.c_str());
    return InjectSoByPidWithEntry(pid,
                                  sidecar_path.c_str(),
                                  "NookAgentInitializeForZygoteControl");
#else
    (void)pid;
    (void)runtime_dir;
    return false;
#endif
}

bool InjectEmbeddedZygoteHelperByPid(int pid, const char* runtime_dir) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_zygote_helper");
        return false;
    }

    const std::string resolved_runtime_dir = ResolveZygoteRuntimeDirectory(runtime_dir);
    const std::string helper_memfd_name =
        BuildVersionedEmbeddedName("libnook-zygote-helper",
                                   kNookEmbeddedZygoteHelperSourceSha256,
                                   static_cast<size_t>(kNookEmbeddedZygoteHelperBlobSize));
    const long existing_helper_base =
        GetModuleBase(static_cast<pid_t>(pid), helper_memfd_name.c_str());
    if (existing_helper_base != 0) {
        NOOK_LOGI("InjectEmbeddedZygoteHelperByPid: existing current zygote helper base pid=%d name=%s base=%lx",
                  pid,
                  helper_memfd_name.c_str(),
                  existing_helper_base);
        return InvokeRemoteInitSymbolByEmbeddedImage(static_cast<pid_t>(pid),
                                                     helper_memfd_name.c_str(),
                                                     kNookEmbeddedZygoteHelperBlob,
                                                     static_cast<size_t>(kNookEmbeddedZygoteHelperBlobSize),
                                                     "NookAgentReinitializeForZygoteControl",
                                                     resolved_runtime_dir.empty()
                                                         ? nullptr
                                                         : resolved_runtime_dir.c_str());
    }

    const long stale_helper_base =
        GetModuleBase(static_cast<pid_t>(pid), "libnook-zygote-helper");
    if (stale_helper_base != 0) {
        NOOK_LOGI("InjectEmbeddedZygoteHelperByPid: ignore stale zygote helper base pid=%d current_name=%s stale_base=%lx",
                  pid,
                  helper_memfd_name.c_str(),
                  stale_helper_base);
    }

    if (!AttachProcess(static_cast<pid_t>(pid))) {
        SetLastInjectError("attach_process_failed:embedded_zygote_helper_env");
        return false;
    }

    bool env_ok = true;
    if (!resolved_runtime_dir.empty()) {
        env_ok = RemoteSetEnv(static_cast<pid_t>(pid),
                              "NOOK_RUNTIME_DIR",
                              resolved_runtime_dir.c_str());
    }
    if (env_ok) {
        env_ok = RemoteSetEnv(static_cast<pid_t>(pid),
                              "NOOK_STRICT_ZYGOTE_CONTROL",
                              "1");
    }
    if (!env_ok && GetLastInjectError().empty()) {
        SetLastInjectError("remote_setenv_failed:embedded_zygote_helper");
    }
    if (!DetachProcess(static_cast<pid_t>(pid))) {
        if (env_ok) {
            SetLastInjectError("detach_process_failed:embedded_zygote_helper_env");
        }
        return false;
    }
    if (!env_ok) {
        return false;
    }

    return InjectEmbeddedSoByPidAtomic(static_cast<pid_t>(pid),
                                       helper_memfd_name.c_str(),
                                       kNookEmbeddedZygoteHelperBlob,
                                       static_cast<size_t>(kNookEmbeddedZygoteHelperBlobSize),
                                       "NookAgentInitializeForZygoteControl",
                                       resolved_runtime_dir.empty() ? nullptr : resolved_runtime_dir.c_str(),
                                       nullptr);
#else
    (void)pid;
    (void)runtime_dir;
    return false;
#endif
}

bool ReinitializeEmbeddedZygoteAgentByPid(int pid, const char* runtime_dir) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    return InvokeRemoteInitSymbolByEmbeddedImage(static_cast<pid_t>(pid),
                                                 "libnook-agent",
                                                 kNookEmbeddedAgentBlob,
                                                 static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                                 "NookAgentReinitializeForZygoteControl",
                                                 runtime_dir);
#else
    (void)pid;
    (void)runtime_dir;
    return false;
#endif
}

bool UninstallEmbeddedZygoteControlHooksByPid(int pid) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:embedded_zygote_uninstall");
        return false;
    }

    if (InvokeRemoteInitSymbolByEmbeddedImage(static_cast<pid_t>(pid),
                                              "libnook-zygote-helper",
                                              kNookEmbeddedZygoteHelperBlob,
                                              static_cast<size_t>(kNookEmbeddedZygoteHelperBlobSize),
                                              "NookAgentUninstallZygoteControlHooks",
                                              nullptr)) {
        return true;
    }

    const std::string helper_error = GetLastInjectError();
    if (InvokeRemoteInitSymbolByEmbeddedImage(static_cast<pid_t>(pid),
                                              "libnook-agent",
                                              kNookEmbeddedAgentBlob,
                                              static_cast<size_t>(kNookEmbeddedAgentBlobSize),
                                              "NookAgentUninstallZygoteControlHooks",
                                              nullptr)) {
        return true;
    }

    if (GetLastInjectError().empty() && !helper_error.empty()) {
        SetLastInjectError(helper_error);
    } else if (!helper_error.empty() && GetLastInjectError() != helper_error) {
        SetLastInjectError(helper_error + "; fallback failed: " + GetLastInjectError());
    }
    return false;
#else
    (void)pid;
    return false;
#endif
}

bool IsZygoteMonitorReady(int pid) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:zygote_monitor_ready");
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(pid))) {
        SetLastInjectError("attach_process_failed:zygote_monitor_ready");
        return false;
    }

    const bool ready = RemoteGetEnvEquals(static_cast<pid_t>(pid),
                                          "NOOK_ZYGOTE_MONITOR_READY",
                                          "1");
    if (!DetachProcess(static_cast<pid_t>(pid))) {
        if (ready) {
            SetLastInjectError("detach_process_failed:zygote_monitor_ready");
        }
        return false;
    }

    return ready;
#else
    if (pid <= 0) {
        SetLastInjectError("invalid_args:zygote_monitor_ready");
        return false;
    }
    return false;
#endif
}

bool HasEmbeddedZygoteControlResidue(int pid) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (pid <= 0) {
        SetLastInjectError("invalid_args:zygote_control_residue");
        return false;
    }

    const bool has_helper =
        GetModuleBase(static_cast<pid_t>(pid), "libnook-zygote-helper") != 0 ||
        RemoteModulePathContains(static_cast<pid_t>(pid), "libnook-zygote-helper");
    const bool has_agent =
        GetModuleBase(static_cast<pid_t>(pid), "libnook-agent") != 0 ||
        RemoteModulePathContains(static_cast<pid_t>(pid), "libnook-agent");

    if (has_helper || has_agent) {
        return true;
    }

    SetLastInjectError("zygote_control_residue_not_present");
    return false;
#else
    if (pid <= 0) {
        SetLastInjectError("invalid_args:zygote_control_residue");
        return false;
    }
    return false;
#endif
}

bool SpawnViaSymbi(int zygote_pid,
                   const char* package_name,
                   const char* so_path,
                   const char* runtime_dir,
                   const char* spawn_token,
                   int* child_pid) {
#if defined(__ANDROID__) && defined(__aarch64__)
    const auto started_at = std::chrono::steady_clock::now();
    ClearLastInjectError();
    if (zygote_pid <= 0 ||
        package_name == nullptr || package_name[0] == '\0' ||
        so_path == nullptr || so_path[0] == '\0' ||
        runtime_dir == nullptr || runtime_dir[0] == '\0') {
        SetLastInjectError("invalid_args:spawn_symbi");
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:spawn_symbi");
        return false;
    }

    bool detach_ok = DetachProcess(static_cast<pid_t>(zygote_pid));
    if (!detach_ok) {
        SetLastInjectError("detach_process_failed:spawn_symbi");
    }
    if (!detach_ok) {
        return false;
    }

    SpawnSymbiResult spawn_result{};
    const bool ok = inject_spawn_symbi_by_package(static_cast<pid_t>(zygote_pid),
                                                  package_name,
                                                  &spawn_result);
    if (!ok && GetLastInjectError().empty()) {
        const char* symbi_error = get_last_spawn_symbi_error();
        if (symbi_error != nullptr && symbi_error[0] != '\0') {
            SetLastInjectError(std::string("spawn_symbi_failed:") + symbi_error);
        } else {
            SetLastInjectError("spawn_symbi_failed");
        }
    }

    if (!ok) {
        return false;
    }

    if (spawn_result.child_pid <= 0) {
        SetLastInjectError("spawn_symbi_failed:invalid_child_pid");
        return false;
    }

    if (!WaitForProcessState(static_cast<pid_t>(spawn_result.child_pid),
                             'T',
                             3000,
                             "SpawnViaSymbi:wait-child-stop")) {
        SetLastInjectError("spawn_symbi_failed:child_stop_timeout");
        return false;
    }
    NOOK_LOGI("SpawnViaSymbi: child stop ready child_pid=%d total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(started_at));

    NOOK_LOGI("SpawnViaSymbi: zygote gate installed without zygote env prewarm child_pid=%d",
              static_cast<int>(spawn_result.child_pid));
    // Child runtime delivery begins here. The local symbi gate path has already
    // finished its zygote-side work and only handed us the stopped child.
    NOOK_LOGI("SpawnViaSymbi: child-owned host-side inject begin child_pid=%d so=%s runtime_dir=%s token_set=%d",
              static_cast<int>(spawn_result.child_pid),
              so_path,
              runtime_dir,
              (spawn_token != nullptr && spawn_token[0] != '\0') ? 1 : 0);
    const auto inject_started_at = std::chrono::steady_clock::now();
    void* child_handle = InjectSoHandleByPid(static_cast<pid_t>(spawn_result.child_pid), so_path);
    if (child_handle == nullptr) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("spawn_symbi_failed:host_dlopen_failed");
        }
        NOOK_LOGE("SpawnViaSymbi: host-side inject failed child_pid=%d so=%s detail=%s",
                  static_cast<int>(spawn_result.child_pid),
                  so_path,
                  GetLastInjectError().c_str());
        (void)kill(spawn_result.child_pid, SIGCONT);
        return false;
    }
    if (!InvokeRemoteInitSymbolByHandleWithSpawnContext(static_cast<pid_t>(spawn_result.child_pid),
                                                        child_handle,
                                                        "NookAgentInitializeForSpawnChild",
                                                        runtime_dir,
                                                        spawn_token)) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("spawn_symbi_failed:host_init_failed");
        }
        NOOK_LOGE("SpawnViaSymbi: child-owned host-side init failed child_pid=%d so=%s detail=%s",
                  static_cast<int>(spawn_result.child_pid),
                  so_path,
                  GetLastInjectError().c_str());
        (void)kill(spawn_result.child_pid, SIGCONT);
        return false;
    }
    NOOK_LOGI("SpawnViaSymbi: child-owned host-side inject ok child_pid=%d handle=%p",
              static_cast<int>(spawn_result.child_pid),
              child_handle);
    NOOK_LOGI("SpawnViaSymbi: child-owned host-side inject timing child_pid=%d inject_ms=%lld total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(inject_started_at),
              ElapsedMillis(started_at));

    if (kill(spawn_result.child_pid, SIGCONT) != 0) {
        SetLastInjectError("spawn_symbi_failed:resume_child_failed");
        return false;
    }
    NOOK_LOGI("SpawnViaSymbi: child resume child_pid=%d total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(started_at));

    if (child_pid != nullptr) {
        *child_pid = static_cast<int>(spawn_result.child_pid);
    }
    return true;
#else
    (void)zygote_pid;
    (void)package_name;
    (void)so_path;
    (void)runtime_dir;
    (void)spawn_token;
    (void)child_pid;
    return false;
#endif
}

bool SpawnViaSymbiEmbedded(int zygote_pid,
                           const char* package_name,
                           const char* runtime_dir,
                           const char* spawn_token,
                           int* child_pid) {
#if defined(__ANDROID__) && defined(__aarch64__)
    const auto started_at = std::chrono::steady_clock::now();
    ClearLastInjectError();
    if (zygote_pid <= 0 ||
        package_name == nullptr || package_name[0] == '\0' ||
        runtime_dir == nullptr || runtime_dir[0] == '\0') {
        SetLastInjectError("invalid_args:spawn_symbi_embedded");
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:spawn_symbi_embedded");
        return false;
    }

    const bool detach_ok = DetachProcess(static_cast<pid_t>(zygote_pid));
    if (!detach_ok) {
        SetLastInjectError("detach_process_failed:spawn_symbi_embedded");
    }
    if (!detach_ok) {
        return false;
    }

    SpawnSymbiResult spawn_result{};
    const bool ok = inject_spawn_symbi_by_package(static_cast<pid_t>(zygote_pid),
                                                  package_name,
                                                  &spawn_result);

    if (!ok && GetLastInjectError().empty()) {
        const char* symbi_error = get_last_spawn_symbi_error();
        if (symbi_error != nullptr && symbi_error[0] != '\0') {
            SetLastInjectError(std::string("spawn_symbi_failed:") + symbi_error);
        } else {
            SetLastInjectError("spawn_symbi_failed");
        }
    }
    if (!ok) {
        return false;
    }

    if (spawn_result.child_pid <= 0) {
        SetLastInjectError("spawn_symbi_failed:invalid_child_pid");
        return false;
    }

    if (!WaitForProcessState(static_cast<pid_t>(spawn_result.child_pid),
                             'T',
                             3000,
                             "SpawnViaSymbiEmbedded:wait-child-stop")) {
        SetLastInjectError("spawn_symbi_failed:child_stop_timeout");
        return false;
    }
    NOOK_LOGI("SpawnViaSymbiEmbedded: child stop ready child_pid=%d total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(started_at));

    NOOK_LOGI("SpawnViaSymbiEmbedded: zygote gate installed without zygote env prewarm child_pid=%d",
              static_cast<int>(spawn_result.child_pid));

    // Child runtime delivery begins here. The local symbi gate path has already
    // returned a stopped child and restored the zygote-side gate state.
    NOOK_LOGI("SpawnViaSymbiEmbedded: child-owned memfd inject begin child_pid=%d runtime_dir=%s token_set=%d",
              static_cast<int>(spawn_result.child_pid),
              runtime_dir,
              (spawn_token != nullptr && spawn_token[0] != '\0') ? 1 : 0);
    const auto inject_started_at = std::chrono::steady_clock::now();
    if (!InjectEmbeddedAgentByPidSuspendedWithSpawnContext(static_cast<pid_t>(spawn_result.child_pid),
                                                           runtime_dir,
                                                           spawn_token)) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("spawn_symbi_failed:host_dlopen_failed");
        }
        NOOK_LOGE("SpawnViaSymbiEmbedded: child-owned memfd inject failed child_pid=%d detail=%s",
                  static_cast<int>(spawn_result.child_pid),
                  GetLastInjectError().c_str());
        (void)kill(spawn_result.child_pid, SIGCONT);
        return false;
    }
    NOOK_LOGI("SpawnViaSymbiEmbedded: child-owned memfd inject ok child_pid=%d",
              static_cast<int>(spawn_result.child_pid));
    NOOK_LOGI("SpawnViaSymbiEmbedded: child-owned memfd inject timing child_pid=%d inject_ms=%lld total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(inject_started_at),
              ElapsedMillis(started_at));

    if (kill(spawn_result.child_pid, SIGCONT) != 0) {
        SetLastInjectError("spawn_symbi_failed:resume_child_failed");
        return false;
    }
    NOOK_LOGI("SpawnViaSymbiEmbedded: child resume child_pid=%d total_ms=%lld",
              static_cast<int>(spawn_result.child_pid),
              ElapsedMillis(started_at));

    if (child_pid != nullptr) {
        *child_pid = static_cast<int>(spawn_result.child_pid);
    }
    return true;
#else
    (void)zygote_pid;
    (void)package_name;
    (void)runtime_dir;
    (void)spawn_token;
    (void)child_pid;
    return false;
#endif
}

bool PrepareSpawnInZygoteEmbedded(int zygote_pid,
                                  const char* package_name,
                                  const char* so_path,
                                  const char* runtime_dir,
                                  const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    ClearEmbeddedNcoreHandle(static_cast<pid_t>(zygote_pid));
    if (zygote_pid <= 0 ||
        package_name == nullptr || package_name[0] == '\0' ||
        so_path == nullptr || so_path[0] == '\0') {
        SetLastInjectError("invalid_args:prepare_spawn_embedded");
        return false;
    }

#if !defined(SYS_memfd_create)
    (void)spawn_token;
    SetLastInjectError("memfd_create_unsupported:ncore");
    return false;
#else
    const int remote_fd = RemoteCreateMemfdWithBytes(static_cast<pid_t>(zygote_pid),
                                                     "libncore",
                                                     kNookEmbeddedNcoreBlob,
                                                     static_cast<size_t>(kNookEmbeddedNcoreBlobSize));
    if (remote_fd < 0) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("remote_memfd_stage_failed:ncore");
        }
        NOOK_LOGE("PrepareSpawnInZygoteEmbedded: RemoteCreateMemfdWithBytes failed zygote_pid=%d detail=%s",
                  zygote_pid,
                  GetLastInjectError().c_str());
        return false;
    }

    std::string proc_fd_path;
    proc_fd_path.reserve(32);
    proc_fd_path.append("/proc/self/fd/");
    proc_fd_path.append(std::to_string(remote_fd));

    NOOK_LOGI("PrepareSpawnInZygoteEmbedded: begin zygote_pid=%d path=%s package=%s so=%s",
              zygote_pid,
              proc_fd_path.c_str(),
              package_name,
              so_path);

    void* handle = InjectSoHandleByPid(static_cast<pid_t>(zygote_pid), proc_fd_path.c_str());
    if (handle == nullptr) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("inject_handle_failed:prepare_spawn_embedded");
        }
        CloseRemoteFdBestEffort(static_cast<pid_t>(zygote_pid), remote_fd);
        return false;
    }

    RememberEmbeddedNcoreHandle(static_cast<pid_t>(zygote_pid), handle);
    if (!PrepareSpawnInZygote(zygote_pid,
                              "__embedded_ncore__",
                              package_name,
                              so_path,
                              runtime_dir,
                              spawn_token)) {
        ClearEmbeddedNcoreHandle(static_cast<pid_t>(zygote_pid));
        CloseRemoteFdBestEffort(static_cast<pid_t>(zygote_pid), remote_fd);
        return false;
    }
    CloseRemoteFdBestEffort(static_cast<pid_t>(zygote_pid), remote_fd);
    return true;
#endif
#else
    (void)zygote_pid;
    (void)package_name;
    (void)so_path;
    (void)runtime_dir;
    (void)spawn_token;
    return false;
#endif
}

bool ClearSpawnInZygoteEmbedded(int zygote_pid,
                                const char* runtime_dir,
                                const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (zygote_pid <= 0) {
        SetLastInjectError("invalid_args:clear_spawn_embedded");
        return false;
    }

    void* handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_embedded_ncore_handle_mutex);
        if (g_last_embedded_ncore_pid == static_cast<pid_t>(zygote_pid)) {
            handle = g_last_embedded_ncore_handle;
        }
    }
    if (handle == nullptr) {
        SetLastInjectError("embedded_ncore_handle_missing");
        return false;
    }

    AndroidLoaderApi loader_api;
    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:clear_spawn_embedded");
        return false;
    }

    if (!ResolveAndroidLoaderApi(static_cast<pid_t>(zygote_pid), &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:clear_spawn_embedded");
        DetachProcess(static_cast<pid_t>(zygote_pid));
        return false;
    }

    void* remote_sym_name = RemoteAllocString(static_cast<pid_t>(zygote_pid), "aclear");
    if (remote_sym_name == nullptr) {
        SetLastInjectError("remote_alloc_failed:clear_spawn_embedded");
        DetachProcess(static_cast<pid_t>(zygote_pid));
        return false;
    }

    void* remote_aclear = CallRemoteAndroidDlsym(static_cast<pid_t>(zygote_pid),
                                                 loader_api,
                                                 handle,
                                                 reinterpret_cast<const char*>(remote_sym_name));
    if (remote_aclear == nullptr) {
        SetLastInjectError("dlsym_failed:aclear_embedded");
        (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("aclear") + 1);
        DetachProcess(static_cast<pid_t>(zygote_pid));
        return false;
    }

    CallRemoteCall<void>(static_cast<pid_t>(zygote_pid), reinterpret_cast<long>(remote_aclear), 0, nullptr);
    (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("aclear") + 1);
    if (spawn_token != nullptr && spawn_token[0] != '\0') {
        (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_SPAWN_TOKEN");
    }
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_RUNTIME_DIR");
    }
    (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_TARGET_PACKAGE");
    (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_REQUEST");
    (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_CONTROL");
    if (!DetachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("detach_process_failed:clear_spawn_embedded");
        return false;
    }
    ClearEmbeddedNcoreHandle(static_cast<pid_t>(zygote_pid));
    return true;
#else
    (void)zygote_pid;
    (void)spawn_token;
    return false;
#endif
}

bool InjectZygoteAgentByPid(int pid, const char* so_path) {
    if (so_path != nullptr &&
        std::strncmp(so_path,
                     "__embedded_zygote_helper_runtime_dir__:",
                     std::strlen("__embedded_zygote_helper_runtime_dir__:")) == 0) {
        const char* runtime_dir =
            so_path + std::strlen("__embedded_zygote_helper_runtime_dir__:");
        return InjectEmbeddedZygoteHelperByPid(pid,
                                               (runtime_dir != nullptr && runtime_dir[0] != '\0')
                                                   ? runtime_dir
                                                   : nullptr);
    }

    if (so_path != nullptr &&
        std::strcmp(so_path, "__embedded_zygote_helper__") == 0) {
        const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
        return InjectEmbeddedZygoteHelperByPid(pid, runtime_dir);
    }

    if (so_path != nullptr &&
        std::strcmp(so_path, "__embedded_agent__") == 0) {
        const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
        return InjectEmbeddedZygoteAgentByPid(pid, runtime_dir);
    }

    if (so_path != nullptr &&
        so_path[0] == '/' &&
        std::strstr(so_path, ".so") == nullptr) {
        return InjectEmbeddedZygoteAgentByPid(pid, so_path);
    }

    std::string runtime_dir;
    if (so_path != nullptr && so_path[0] != '\0') {
        const std::string path(so_path);
        const std::string::size_type slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            runtime_dir = (slash == 0) ? path.substr(0, 1) : path.substr(0, slash);
        }
    }
    return InjectEmbeddedZygoteAgentByPid(pid, runtime_dir.empty() ? nullptr : runtime_dir.c_str());
}

bool SetZygoteSpawnControl(int zygote_pid,
                           const char* package_name,
                           const char* spawn_token,
                           bool strict_request) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (zygote_pid <= 0 || package_name == nullptr || package_name[0] == '\0') {
        SetLastInjectError("invalid_args:zygote_spawn_control");
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:zygote_spawn_control");
        return false;
    }

    bool ok = RemoteSetEnv(static_cast<pid_t>(zygote_pid), "NOOK_TARGET_PACKAGE", package_name);
    if (ok && spawn_token != nullptr && spawn_token[0] != '\0') {
        ok = RemoteSetEnv(static_cast<pid_t>(zygote_pid), "NOOK_SPAWN_TOKEN", spawn_token);
    }
    if (ok && strict_request) {
        ok = RemoteSetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_REQUEST", "1");
    }
    if (!ok && GetLastInjectError().empty()) {
        SetLastInjectError("remote_setenv_failed:zygote_spawn_control");
    }

    if (!DetachProcess(static_cast<pid_t>(zygote_pid))) {
        if (ok) {
            SetLastInjectError("detach_process_failed:zygote_spawn_control");
        }
        return false;
    }
    return ok;
#else
    ClearLastInjectError();
    if (zygote_pid <= 0 || package_name == nullptr || package_name[0] == '\0') {
        SetLastInjectError("invalid_args:zygote_spawn_control");
        return false;
    }
    (void)spawn_token;
    return true;
#endif
}

bool ClearZygoteSpawnControl(int zygote_pid,
                             const char* spawn_token,
                             bool strict_request) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (zygote_pid <= 0) {
        SetLastInjectError("invalid_args:clear_zygote_spawn_control");
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:clear_zygote_spawn_control");
        return false;
    }

    bool ok = RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_TARGET_PACKAGE");
    if (spawn_token != nullptr && spawn_token[0] != '\0') {
        ok = RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_SPAWN_TOKEN") && ok;
    }
    ok = RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_REQUEST") && ok;
    ok = RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_CONTROL") && ok;
    if (!ok && GetLastInjectError().empty()) {
        SetLastInjectError("remote_unsetenv_failed:clear_zygote_spawn_control");
    }

    if (!DetachProcess(static_cast<pid_t>(zygote_pid))) {
        if (ok) {
            SetLastInjectError("detach_process_failed:clear_zygote_spawn_control");
        }
        return false;
    }
    return ok;
#else
    ClearLastInjectError();
    if (zygote_pid <= 0) {
        SetLastInjectError("invalid_args:clear_zygote_spawn_control");
        return false;
    }
    (void)spawn_token;
    (void)strict_request;
    return true;
#endif
}

std::string GetLastInjectError() {
#if defined(__ANDROID__) && defined(__aarch64__)
    std::lock_guard<std::mutex> lock(g_last_inject_error_mutex);
    return g_last_inject_error;
#else
    return {};
#endif
}

bool PrepareSpawnInZygote(int zygote_pid,
                          const char* ncore_path,
                          const char* package_name,
                          const char* so_path,
                          const char* runtime_dir,
                          const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    ClearLastInjectError();
    if (zygote_pid <= 0 ||
        ncore_path == nullptr || ncore_path[0] == '\0' ||
        package_name == nullptr || package_name[0] == '\0' ||
        so_path == nullptr || so_path[0] == '\0') {
        SetLastInjectError("invalid_args:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: invalid args zygote_pid=%d ncore=%s package=%s so=%s",
                  zygote_pid,
                  ncore_path ? ncore_path : "(null)",
                  package_name ? package_name : "(null)",
                  so_path ? so_path : "(null)");
        return false;
    }

    bool attached = false;
    void* handle = nullptr;
    AndroidLoaderApi loader_api;
    const bool use_embedded_handle =
        ncore_path != nullptr && std::strcmp(ncore_path, "__embedded_ncore__") == 0;
    if (use_embedded_handle) {
        {
            std::lock_guard<std::mutex> lock(g_embedded_ncore_handle_mutex);
            if (g_last_embedded_ncore_pid == static_cast<pid_t>(zygote_pid)) {
                handle = g_last_embedded_ncore_handle;
            }
        }
        if (handle == nullptr) {
            SetLastInjectError("embedded_ncore_handle_missing");
            NOOK_LOGE("PrepareSpawnInZygote: embedded ncore handle missing zygote_pid=%d",
                      zygote_pid);
            return false;
        }
    } else {
        handle = InjectSoHandleByPid(static_cast<pid_t>(zygote_pid), ncore_path);
    }
    if (handle == nullptr) {
        if (GetLastInjectError().empty()) {
            SetLastInjectError("inject_handle_failed:prepare_spawn");
        }
        NOOK_LOGE("PrepareSpawnInZygote: InjectSoHandleByPid failed zygote_pid=%d ncore=%s detail=%s",
                  zygote_pid,
                  ncore_path,
                  GetLastInjectError().c_str());
        return false;
    }
    NOOK_LOGI("PrepareSpawnInZygote: ncore handle ready zygote_pid=%d handle=%p", zygote_pid, handle);

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("attach_process_failed:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: AttachProcess failed zygote_pid=%d", zygote_pid);
        return false;
    }
    attached = true;
    NOOK_LOGI("PrepareSpawnInZygote: attach ok zygote_pid=%d", zygote_pid);

    if (!ResolveAndroidLoaderApi(static_cast<pid_t>(zygote_pid), &loader_api)) {
        SetLastInjectError("resolve_android_loader_api_failed:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: ResolveAndroidLoaderApi failed zygote_pid=%d", zygote_pid);
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_REQUEST");
    (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_STRICT_ZYGOTE_CONTROL");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0' &&
        !RemoteSetEnv(static_cast<pid_t>(zygote_pid), "NOOK_RUNTIME_DIR", runtime_dir)) {
        SetLastInjectError("remote_setenv_failed:prepare_spawn_runtime_dir");
        NOOK_LOGE("PrepareSpawnInZygote: RemoteSetEnv runtime dir failed zygote_pid=%d runtime_dir=%s",
                  zygote_pid,
                  runtime_dir);
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    if (spawn_token != nullptr && spawn_token[0] != '\0' &&
        !RemoteSetEnv(static_cast<pid_t>(zygote_pid), "NOOK_SPAWN_TOKEN", spawn_token)) {
        SetLastInjectError("remote_setenv_failed:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: RemoteSetEnv failed zygote_pid=%d", zygote_pid);
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    void* remote_sym_name = RemoteAllocString(static_cast<pid_t>(zygote_pid), "ainject");
    void* remote_pkg = RemoteAllocString(static_cast<pid_t>(zygote_pid), package_name);
    void* remote_so = RemoteAllocString(static_cast<pid_t>(zygote_pid), so_path);
    if (remote_sym_name == nullptr || remote_pkg == nullptr || remote_so == nullptr) {
        SetLastInjectError("remote_alloc_failed:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: RemoteAllocString failed zygote_pid=%d sym=%p pkg=%p so=%p",
                  zygote_pid,
                  remote_sym_name,
                  remote_pkg,
                  remote_so);
        if (remote_sym_name != nullptr) {
            (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("ainject") + 1);
        }
        if (remote_pkg != nullptr) {
            (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_pkg, std::strlen(package_name) + 1);
        }
        if (remote_so != nullptr) {
            (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_so, std::strlen(so_path) + 1);
        }
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    void* remote_ainject = CallRemoteAndroidDlsym(static_cast<pid_t>(zygote_pid),
                                                  loader_api,
                                                  handle,
                                                  reinterpret_cast<const char*>(remote_sym_name));
    if (remote_ainject == nullptr) {
        SetLastInjectError("dlsym_failed:ainject");
        NOOK_LOGE("PrepareSpawnInZygote: remote dlsym failed zygote_pid=%d handle=%p symbol=%s",
                  zygote_pid,
                  handle,
                  "ainject");
        (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("ainject") + 1);
        (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_pkg, std::strlen(package_name) + 1);
        (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_so, std::strlen(so_path) + 1);
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    long params[2] = {
        reinterpret_cast<long>(remote_pkg),
        reinterpret_cast<long>(remote_so),
    };
    CallRemoteCall<void>(static_cast<pid_t>(zygote_pid),
                         reinterpret_cast<long>(remote_ainject),
                         2,
                         params);
    NOOK_LOGI("PrepareSpawnInZygote: ainject invoked zygote_pid=%d", zygote_pid);

    (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("ainject") + 1);
    (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_pkg, std::strlen(package_name) + 1);
    (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_so, std::strlen(so_path) + 1);

    if (!DetachProcess(static_cast<pid_t>(zygote_pid))) {
        SetLastInjectError("detach_process_failed:prepare_spawn");
        NOOK_LOGE("PrepareSpawnInZygote: DetachProcess failed zygote_pid=%d", zygote_pid);
        return false;
    }
    return true;
#else
    (void)zygote_pid;
    (void)ncore_path;
    (void)package_name;
    (void)so_path;
    (void)runtime_dir;
    (void)spawn_token;
    return false;
#endif
}

bool ClearSpawnInZygote(int zygote_pid,
                        const char* ncore_path,
                        const char* runtime_dir,
                        const char* spawn_token) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (zygote_pid <= 0 || ncore_path == nullptr || ncore_path[0] == '\0') {
        return false;
    }

    if (std::strcmp(ncore_path, "__embedded_ncore__") == 0) {
        return ClearSpawnInZygoteEmbedded(zygote_pid, runtime_dir, spawn_token);
    }

    bool attached = false;
    void* handle = InjectSoHandleByPid(static_cast<pid_t>(zygote_pid), ncore_path);
    AndroidLoaderApi loader_api;
    if (handle == nullptr) {
        return false;
    }

    if (!AttachProcess(static_cast<pid_t>(zygote_pid))) {
        return false;
    }
    attached = true;

    if (!ResolveAndroidLoaderApi(static_cast<pid_t>(zygote_pid), &loader_api)) {
        DetachProcess(static_cast<pid_t>(zygote_pid));
        return false;
    }

    void* remote_sym_name = RemoteAllocString(static_cast<pid_t>(zygote_pid), "aclear");
    if (remote_sym_name == nullptr) {
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    void* remote_aclear = CallRemoteAndroidDlsym(static_cast<pid_t>(zygote_pid),
                                                 loader_api,
                                                 handle,
                                                 reinterpret_cast<const char*>(remote_sym_name));
    if (remote_aclear == nullptr) {
        (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("aclear") + 1);
        if (attached) {
            DetachProcess(static_cast<pid_t>(zygote_pid));
        }
        return false;
    }

    CallRemoteCall<void>(static_cast<pid_t>(zygote_pid), reinterpret_cast<long>(remote_aclear), 0, nullptr);
    (void)RemoteFreeScratch(static_cast<pid_t>(zygote_pid), remote_sym_name, std::strlen("aclear") + 1);
    if (spawn_token != nullptr && spawn_token[0] != '\0') {
        (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_SPAWN_TOKEN");
    }
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        (void)RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), "NOOK_RUNTIME_DIR");
    }
    return DetachProcess(static_cast<pid_t>(zygote_pid));
#else
    (void)zygote_pid;
    (void)ncore_path;
    (void)runtime_dir;
    (void)spawn_token;
    return false;
#endif
}

bool StartTargetApp(const char* package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return false;
    }

#if defined(__ANDROID__)
    const std::string component = ResolveLaunchComponent(package_name);
    if (component.empty()) {
        NOOK_LOGE("StartTargetApp: resolve launch component failed package=%s",
                  package_name);
        return false;
    }

    std::string force_stop_cmd = std::string("am force-stop ") + package_name;
    std::string start_cmd = std::string("am start -S -n ") + component;
    const int force_stop_ret = system(force_stop_cmd.c_str());
    const bool exited = WaitForProcessExitByName(package_name, 2000);
    const int start_ret = system(start_cmd.c_str());
    const bool started = WaitForProcessStartByName(package_name, 10000);
    NOOK_LOGI("StartTargetApp: package=%s component=%s force-stop ret=%d exited=%d start ret=%d started=%d",
              package_name,
              component.c_str(),
              force_stop_ret,
              exited ? 1 : 0,
              start_ret,
              started ? 1 : 0);
    return start_ret == 0 && started;
#else
    return false;
#endif
}

int WaitForSpawnCallback(const char* result_file) {
    if (result_file == nullptr || result_file[0] == '\0') {
        return -1;
    }

#if defined(__ANDROID__)
    unlink(result_file);
    std::string payload;

    for (int i = 0; i < 80; ++i) {
        struct stat st{};
        if (stat(result_file, &st) == 0 && st.st_size > 0) {
            std::ifstream file(result_file);
            if (file.good()) {
                std::getline(file, payload, '\0');
            }
            break;
        }
        usleep(100000);
    }

    const int pid = ParseCallbackPidFromPayload(payload);
    unlink(result_file);
    return pid;
#else
    return -1;
#endif
}

bool IsRemoteProcess64Bit(int pid, bool* is_64_bit) {
#if defined(__ANDROID__) && defined(__aarch64__)
    return DetectRemoteProcess64Bit(static_cast<pid_t>(pid), is_64_bit);
#else
    (void)pid;
    (void)is_64_bit;
    return false;
#endif
}

std::string GetDefaultSpawnSourceProcess() {
#if defined(__aarch64__)
    return "zygote64";
#else
    return "zygote";
#endif
}

std::string GetDefaultCallbackFile() {
    return "/data/local/tmp/nook/spawn_result.json";
}

std::string GetDefaultNcorePath() {
    return "/data/local/tmp/nook/libncore.so";
}

std::string TrimAsciiWhitespace(std::string value) {
    const auto not_space = [](unsigned char ch) {
        return ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n';
    };
    while (!value.empty() && !not_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && !not_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string ReadCommandOutputFirstNonEmptyLine(const std::string& command) {
    if (command.empty()) {
        return {};
    }

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }

    std::string last_non_empty_line;
    char buffer[512] = {};
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line = TrimAsciiWhitespace(buffer);
        if (!line.empty()) {
            last_non_empty_line = std::move(line);
        }
    }
    (void)pclose(pipe);
    return last_non_empty_line;
}

std::string ResolveLaunchComponent(const char* package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return {};
    }

    const std::string resolve_cmd =
        std::string("cmd package resolve-activity --brief ") + package_name;
    const std::string component = ReadCommandOutputFirstNonEmptyLine(resolve_cmd);
    if (!component.empty()) {
        NOOK_LOGI("ResolveLaunchComponent: package=%s component=%s",
                  package_name,
                  component.c_str());
    } else {
        NOOK_LOGE("ResolveLaunchComponent: resolve failed package=%s",
                  package_name);
    }
    return component;
}

bool WaitForProcessExitByName(const char* process_name, uint32_t timeout_ms) {
#if defined(__ANDROID__)
    if (process_name == nullptr || process_name[0] == '\0') {
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (GetPid(process_name) <= 0) {
            return true;
        }
        usleep(50 * 1000);
    }

    return GetPid(process_name) <= 0;
#else
    (void)process_name;
    (void)timeout_ms;
    return true;
#endif
}

bool WaitForProcessStartByName(const char* process_name, uint32_t timeout_ms) {
#if defined(__ANDROID__)
    if (process_name == nullptr || process_name[0] == '\0') {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (GetPid(process_name) > 0) {
            return true;
        }
        usleep(50 * 1000);
    }

    return GetPid(process_name) > 0;
#else
    (void)process_name;
    (void)timeout_ms;
    return false;
#endif
}

}  // namespace ninjector
}  // namespace server
}  // namespace nook
