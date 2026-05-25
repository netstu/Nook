#include "symbi/symbi_injector.h"
#include "symbi/symbi_stub.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#define SYMBI_TARGET_SYMBOL "_Z27android_os_Process_setArgV0P7_JNIEnvP8_jobjectP8_jstring"

namespace {

constexpr int kSymbiCallbackTimeoutMs = 8000;
thread_local std::string g_last_spawn_symbi_error;

void set_spawn_symbi_error(const std::string& error) {
    g_last_spawn_symbi_error = error;
}

struct MemoryMap {
    uintptr_t start = 0;
    uintptr_t end = 0;
    char perms[5] = {0};
    size_t offset = 0;
    std::string pathname;
};

struct SymbiContext {
    pid_t zygote_pid = -1;
    std::string target_package;
    uintptr_t shellcode_base = 0;
    uintptr_t set_argv0_address = 0;
    uintptr_t art_method_slot = 0;
    uintptr_t original_ptr = 0;
    uintptr_t remote_socket = 0;
    uintptr_t remote_connect = 0;
    uintptr_t remote_write = 0;
    uintptr_t remote_close = 0;
    uintptr_t remote_getpid = 0;
    uintptr_t remote_raise = 0;
    std::string libandroid_runtime_path;
    std::string shellcode_map_path;
    uintptr_t shellcode_map_start = 0;
    size_t shellcode_map_offset = 0;
    std::string callback_socket_name;
    std::vector<uint8_t> original_shellcode_area;
};

struct SymbiCallbackHeader {
    uint32_t pid = 0;
    uint32_t load_ok = 0;
};

struct SymbiCallbackListener {
    int fd = -1;
    std::string socket_name;
};

struct SymbiCallbackResult {
    pid_t pid = -1;
    bool load_ok = false;
};

std::string trim_ascii_whitespace(std::string value) {
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

std::string read_command_output_last_non_empty_line(const std::string& command) {
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
        std::string line = trim_ascii_whitespace(buffer);
        if (!line.empty()) {
            last_non_empty_line = std::move(line);
        }
    }
    (void)pclose(pipe);
    return last_non_empty_line;
}

std::string resolve_launch_component(const char* package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return {};
    }

    const std::string resolve_cmd =
        std::string("cmd package resolve-activity --brief ") + package_name;
    const std::string component = read_command_output_last_non_empty_line(resolve_cmd);
    if (!component.empty()) {
        LOGI("symbi: resolved launch component package=%s component=%s",
             package_name,
             component.c_str());
    } else {
        LOGE("symbi: resolve launch component failed package=%s", package_name);
    }
    return component;
}

int get_pid_by_name(const char* process_name) {
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
        std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* fp = std::fopen(path, "r");
        if (fp == nullptr) {
            continue;
        }

        char cmdline[256] = {0};
        std::fgets(cmdline, sizeof(cmdline), fp);
        std::fclose(fp);
        if (std::strcmp(cmdline, process_name) == 0) {
            closedir(dir);
            return pid;
        }
    }

    closedir(dir);
    return -1;
}

bool wait_for_process_start_by_name(const char* process_name, uint32_t timeout_ms) {
    if (process_name == nullptr || process_name[0] == '\0') {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (get_pid_by_name(process_name) > 0) {
            return true;
        }
        usleep(50 * 1000);
    }

    return get_pid_by_name(process_name) > 0;
}

struct PreparedStubPatch {
    std::vector<uint8_t> stub_copy;
    uintptr_t new_ptr = 0;
};

enum class RestoreStage {
    kAfterStartFailure,
    kAfterCallback,
};

enum class RestoreFailureReason {
    kNone,
    kStopProcessFailed,
    kOpenRemoteMemFailed,
    kWriteMemFailed,
    kPtraceAttachFailed,
    kPtraceWaitpidFailed,
};

struct RestoreAttemptResult {
    bool restored = false;
    bool primary_attempted = false;
    bool ptrace_fallback_attempted = false;
    RestoreFailureReason primary_failure = RestoreFailureReason::kNone;
    RestoreFailureReason ptrace_failure = RestoreFailureReason::kNone;
};

struct RestoreDriverOps {
    const char* name = nullptr;
    RestoreFailureReason (*attach_or_stop)(const SymbiContext& ctx) = nullptr;
    void (*detach_or_resume)(const SymbiContext& ctx) = nullptr;
};

struct SymbiGateInstallResult {
    bool installed = false;
};

struct SymbiChildHandoffResult {
    bool callback_ok = false;
    bool restored = false;
    SymbiCallbackResult callback_result{};
    RestoreAttemptResult restore_result{};
};

enum class SymbiHandoffState {
    kNone = 0,
    kGateInstalled,
    kTargetAppStarted,
    kCallbackObserved,
    kPrimaryRestoreAttempted,
    kPtraceRestoreAttempted,
    kRestoreCompleted,
};

std::vector<MemoryMap> get_process_maps(pid_t pid) {
    std::vector<MemoryMap> maps;
    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        MemoryMap map{};
        char perms[5] = {0};
        char dev[16] = {0};
        char path_buf[512] = {0};
        unsigned long inode = 0;
        if (sscanf(line.c_str(),
                   "%lx-%lx %4s %lx %15s %lu %511s",
                   &map.start,
                   &map.end,
                   perms,
                   &map.offset,
                   dev,
                   &inode,
                   path_buf) >= 6) {
            memcpy(map.perms, perms, sizeof(map.perms));
            map.pathname = path_buf;
            maps.push_back(map);
        }
    }
    return maps;
}

bool RecvAll(int fd, void* buffer, size_t size) {
    auto* cursor = static_cast<uint8_t*>(buffer);
    size_t received = 0;
    while (received < size) {
        ssize_t chunk = read(fd, cursor + received, size - received);
        if (chunk <= 0) {
            return false;
        }
        received += static_cast<size_t>(chunk);
    }
    return true;
}

char query_process_state(pid_t pid) {
    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    std::ifstream stat_file(path);
    std::string stat_line;
    if (!std::getline(stat_file, stat_line)) {
        return '\0';
    }

    const std::string::size_type paren_end = stat_line.rfind(')');
    if (paren_end == std::string::npos || paren_end + 2 >= stat_line.size()) {
        return '\0';
    }

    return stat_line[paren_end + 2];
}

bool wait_for_process_state(pid_t pid, char expected_state, int timeout_ms, const char* stage) {
    const int sleep_us = 10 * 1000;
    const int max_attempts = timeout_ms <= 0 ? 1 : ((timeout_ms * 1000) / sleep_us);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const char state = query_process_state(pid);
        if (state == expected_state) {
            return true;
        }
        if (state == 'Z' || state == 'X') {
            LOGE("symbi: %s failed pid=%d state=%c", stage, pid, state);
            return false;
        }
        usleep(sleep_us);
    }

    LOGE("symbi: %s timeout pid=%d expected=%c last=%c", stage, pid, expected_state, query_process_state(pid));
    return false;
}

bool wait_for_process_state_not(pid_t pid, char unexpected_state, int timeout_ms, const char* stage) {
    const int sleep_us = 10 * 1000;
    const int max_attempts = timeout_ms <= 0 ? 1 : ((timeout_ms * 1000) / sleep_us);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const char state = query_process_state(pid);
        if (state != unexpected_state) {
            return true;
        }
        if (state == 'Z' || state == 'X') {
            LOGE("symbi: %s failed pid=%d state=%c", stage, pid, state);
            return false;
        }
        usleep(sleep_us);
    }

    LOGE("symbi: %s timeout pid=%d waiting-not=%c last=%c",
         stage,
         pid,
         unexpected_state,
         query_process_state(pid));
    return false;
}

void CloseSymbiCallbackListener(SymbiCallbackListener* listener) {
    if (listener == nullptr || listener->fd < 0) {
        return;
    }
    close(listener->fd);
    listener->fd = -1;
    listener->socket_name.clear();
}

bool OpenSymbiCallbackListener(SymbiCallbackListener* listener) {
    if (listener == nullptr) {
        return false;
    }

    listener->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener->fd < 0) {
        LOGE("symbi: callback socket create failed errno=%d", errno);
        return false;
    }

    char name[64] = {0};
    snprintf(name, sizeof(name), "ninjector-symbi-%d-%llu",
             getpid(),
             static_cast<unsigned long long>(time(nullptr)));
    listener->socket_name = name;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    size_t name_len = std::min(listener->socket_name.size(), sizeof(addr.sun_path) - 2);
    memcpy(addr.sun_path + 1, listener->socket_name.data(), name_len);
    socklen_t addr_len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
    if (bind(listener->fd, reinterpret_cast<const struct sockaddr*>(&addr), addr_len) != 0) {
        LOGE("symbi: callback socket bind failed errno=%d", errno);
        CloseSymbiCallbackListener(listener);
        return false;
    }

    if (listen(listener->fd, 1) != 0) {
        LOGE("symbi: callback socket listen failed errno=%d", errno);
        CloseSymbiCallbackListener(listener);
        return false;
    }

    LOGI("symbi: callback listener ready name=%s", listener->socket_name.c_str());
    return true;
}

bool WaitForSymbiCallback(const SymbiCallbackListener& listener,
                          int timeout_ms,
                          SymbiCallbackResult* result) {
    if (listener.fd < 0 || result == nullptr) {
        return false;
    }

    struct pollfd pfd{};
    pfd.fd = listener.fd;
    pfd.events = POLLIN;
    int poll_ret = poll(&pfd, 1, timeout_ms);
    if (poll_ret <= 0) {
        return false;
    }

    int client_fd = accept(listener.fd, nullptr, nullptr);
    if (client_fd < 0) {
        LOGE("symbi: callback accept failed errno=%d", errno);
        return false;
    }

    SymbiCallbackHeader header{};
    bool ok = RecvAll(client_fd, &header, sizeof(header));
    if (!ok) {
        close(client_fd);
        return false;
    }

    close(client_fd);

    result->pid = static_cast<pid_t>(header.pid);
    result->load_ok = header.load_ok != 0;
    LOGI("symbi: callback handshake received pid=%d load_ok=%d",
         result->pid,
         result->load_ok ? 1 : 0);
    return true;
}

const char* SymbiHandoffStateName(SymbiHandoffState state) {
    switch (state) {
        case SymbiHandoffState::kNone:
            return "none";
        case SymbiHandoffState::kGateInstalled:
            return "gate-installed";
        case SymbiHandoffState::kTargetAppStarted:
            return "target-app-started";
        case SymbiHandoffState::kCallbackObserved:
            return "callback-observed";
        case SymbiHandoffState::kPrimaryRestoreAttempted:
            return "primary-restore-attempted";
        case SymbiHandoffState::kPtraceRestoreAttempted:
            return "ptrace-restore-attempted";
        case SymbiHandoffState::kRestoreCompleted:
            return "restore-completed";
    }
    return "unknown";
}

void AdvanceSymbiHandoffState(SymbiHandoffState* state, SymbiHandoffState next) {
    if (state == nullptr) {
        return;
    }
    *state = next;
    LOGI("symbi: handoff state=%s", SymbiHandoffStateName(next));
}

uintptr_t get_symbol_offset_from_elf(const std::string& elf_path, const char* symbol_name) {
    int fd = open(elf_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return 0;
    }

    void* map_base = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_base == MAP_FAILED) {
        return 0;
    }

    auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(map_base);
    auto* shdr = reinterpret_cast<Elf64_Shdr*>(reinterpret_cast<uintptr_t>(map_base) + ehdr->e_shoff);

    uintptr_t symbol_offset = 0;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdr[i].sh_type != SHT_DYNSYM) {
            continue;
        }

        auto* syms = reinterpret_cast<Elf64_Sym*>(reinterpret_cast<uintptr_t>(map_base) + shdr[i].sh_offset);
        int count = static_cast<int>(shdr[i].sh_size / sizeof(Elf64_Sym));
        auto* strtab = reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(map_base) + shdr[shdr[i].sh_link].sh_offset);
        for (int j = 0; j < count; ++j) {
            if (strcmp(strtab + syms[j].st_name, symbol_name) == 0) {
                symbol_offset = syms[j].st_value;
                break;
            }
        }
        if (symbol_offset != 0) {
            break;
        }
    }

    uintptr_t load_bias = 0;
    auto* phdr = reinterpret_cast<Elf64_Phdr*>(reinterpret_cast<uintptr_t>(map_base) + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) {
            load_bias = phdr[i].p_vaddr;
            break;
        }
    }

    munmap(map_base, static_cast<size_t>(st.st_size));
    if (symbol_offset < load_bias) {
        return 0;
    }
    return symbol_offset - load_bias;
}

uintptr_t get_module_base(pid_t pid, const std::string& lib_name) {
    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    std::ifstream maps(path);
    std::string line;

    while (std::getline(maps, line)) {
        if (line.find(lib_name) == std::string::npos) {
            continue;
        }

        uintptr_t start = 0;
        uintptr_t offset = 0;
        char perms[5] = {0};
        if (sscanf(line.c_str(), "%lx-%*x %4s %lx", &start, perms, &offset) == 3) {
            if (offset == 0 && perms[3] != 's') {
                return start;
            }
        }
    }

    return 0;
}

uintptr_t get_remote_symbol(pid_t pid, const std::string& lib_name, const char* symbol) {
    uintptr_t base = get_module_base(pid, lib_name);
    if (base == 0) {
        return 0;
    }

    auto maps = get_process_maps(pid);
    std::string local_path;
    for (const auto& map : maps) {
        if (map.pathname.find(lib_name) != std::string::npos) {
            local_path = map.pathname;
            break;
        }
    }
    if (local_path.empty()) {
        return 0;
    }

    uintptr_t offset = get_symbol_offset_from_elf(local_path, symbol);
    if (offset == 0) {
        return 0;
    }
    return base + offset;
}

bool stop_process(pid_t pid) {
    if (kill(pid, SIGSTOP) != 0) {
        LOGE("symbi: SIGSTOP failed pid=%d errno=%d", pid, errno);
        return false;
    }

    if (!wait_for_process_state(pid, 'T', 1000, "wait-stop")) {
        return false;
    }

    LOGI("symbi: stopped pid=%d via SIGSTOP", pid);
    return true;
}

void resume_process(pid_t pid) {
    if (kill(pid, SIGCONT) != 0) {
        LOGE("symbi: SIGCONT failed pid=%d errno=%d", pid, errno);
        return;
    }
    (void) wait_for_process_state_not(pid, 'T', 1000, "wait-resume");
    LOGI("symbi: resumed pid=%d via SIGCONT", pid);
}

int open_remote_mem(pid_t pid) {
    char path[64] = {0};
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        LOGE("symbi: failed to open %s errno=%d", path, errno);
    }
    return fd;
}

bool load_original_shellcode_page(const SymbiContext& ctx, std::vector<uint8_t>* buffer) {
    if (ctx.shellcode_map_path.empty() || ctx.shellcode_base == 0) {
        return false;
    }

    buffer->assign(stub_binary_size, 0);
    size_t file_offset = ctx.shellcode_map_offset +
                         static_cast<size_t>(ctx.shellcode_base - ctx.shellcode_map_start);
    int fd = open(ctx.shellcode_map_path.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGE("symbi: failed to open shellcode backing file %s", ctx.shellcode_map_path.c_str());
        return false;
    }

    ssize_t read_size = pread(fd, buffer->data(), buffer->size(), static_cast<off_t>(file_offset));
    close(fd);
    if (read_size != static_cast<ssize_t>(buffer->size())) {
        LOGE("symbi: failed to read original shellcode page from file=%s offset=0x%zx",
             ctx.shellcode_map_path.c_str(), file_offset);
        return false;
    }

    return true;
}

bool collect_symbi_context(pid_t zygote_pid,
                           const char* package_name,
                           SymbiContext* ctx) {
    ctx->zygote_pid = zygote_pid;
    if (package_name == nullptr || package_name[0] == '\0') {
        LOGE("symbi: empty target package");
        set_spawn_symbi_error("resolve_target_package_failed");
        return false;
    }
    ctx->target_package = package_name;

    auto maps = get_process_maps(zygote_pid);
    std::vector<MemoryMap> heap_candidates;
    for (const auto& map : maps) {
        if (ctx->libandroid_runtime_path.empty() &&
            map.pathname.find("libandroid_runtime.so") != std::string::npos) {
            ctx->libandroid_runtime_path = map.pathname;
        }
        if (ctx->shellcode_base == 0 &&
            map.pathname.find("libstagefright.so") != std::string::npos &&
            map.perms[2] == 'x') {
            ctx->shellcode_base = map.end - static_cast<uintptr_t>(getpagesize());
            ctx->shellcode_map_path = map.pathname;
            ctx->shellcode_map_start = map.start;
            ctx->shellcode_map_offset = map.offset;
        }
        if ((map.pathname.find("boot.art") != std::string::npos ||
             map.pathname.find("boot-framework.art") != std::string::npos ||
             map.pathname.find("dalvik-LinearAlloc") != std::string::npos) &&
            map.perms[0] == 'r' && map.perms[1] == 'w') {
            heap_candidates.push_back(map);
        }
    }

    if (ctx->libandroid_runtime_path.empty() || ctx->shellcode_base == 0) {
        LOGE("symbi: failed to locate libandroid_runtime/libstagefright");
        set_spawn_symbi_error("locate_runtime_or_stagefright_failed");
        return false;
    }

    uintptr_t libandroid_runtime_base = get_module_base(zygote_pid, ctx->libandroid_runtime_path);
    uintptr_t symbol_offset = get_symbol_offset_from_elf(ctx->libandroid_runtime_path, SYMBI_TARGET_SYMBOL);
    if (libandroid_runtime_base == 0 || symbol_offset == 0) {
        LOGE("symbi: failed to resolve setArgV0");
        set_spawn_symbi_error("resolve_setargv0_failed");
        return false;
    }
    ctx->set_argv0_address = libandroid_runtime_base + symbol_offset;

    ctx->remote_socket = get_remote_symbol(zygote_pid, "libc.so", "socket");
    ctx->remote_connect = get_remote_symbol(zygote_pid, "libc.so", "connect");
    ctx->remote_write = get_remote_symbol(zygote_pid, "libc.so", "write");
    ctx->remote_close = get_remote_symbol(zygote_pid, "libc.so", "close");
    ctx->remote_getpid = get_remote_symbol(zygote_pid, "libc.so", "getpid");
    ctx->remote_raise = get_remote_symbol(zygote_pid, "libc.so", "raise");
    if (ctx->remote_socket == 0 || ctx->remote_connect == 0 ||
        ctx->remote_write == 0 || ctx->remote_close == 0 ||
        ctx->remote_getpid == 0 || ctx->remote_raise == 0) {
        LOGE("symbi: failed to resolve remote helper symbols");
        set_spawn_symbi_error("resolve_remote_helper_symbols_failed");
        return false;
    }

    int mem_fd = open_remote_mem(zygote_pid);
    if (mem_fd < 0) {
        set_spawn_symbi_error("open_remote_mem_failed");
        return false;
    }

    for (const auto& heap : heap_candidates) {
        size_t region_size = static_cast<size_t>(heap.end - heap.start);
        if (region_size < sizeof(uintptr_t)) {
            continue;
        }

        std::vector<uint8_t> buffer(region_size);
        ssize_t read_size = pread(mem_fd, buffer.data(), region_size, static_cast<off_t>(heap.start));
        if (read_size != static_cast<ssize_t>(region_size)) {
            continue;
        }

        auto* found = reinterpret_cast<uint8_t*>(
            memmem(buffer.data(), region_size, &ctx->set_argv0_address, sizeof(ctx->set_argv0_address)));
        if (found != nullptr) {
            ctx->art_method_slot = heap.start + static_cast<uintptr_t>(found - buffer.data());
            break;
        }
    }

    if (ctx->art_method_slot == 0) {
        close(mem_fd);
        LOGE("symbi: failed to find art_method_slot for setArgV0=0x%lx", ctx->set_argv0_address);
        set_spawn_symbi_error("find_art_method_slot_failed");
        return false;
    }

    if (pread(mem_fd, &ctx->original_ptr, sizeof(ctx->original_ptr), static_cast<off_t>(ctx->art_method_slot)) !=
        static_cast<ssize_t>(sizeof(ctx->original_ptr))) {
        close(mem_fd);
        LOGE("symbi: failed to read original art_method slot");
        set_spawn_symbi_error("read_original_art_method_slot_failed");
        return false;
    }

    if (!load_original_shellcode_page(*ctx, &ctx->original_shellcode_area)) {
        close(mem_fd);
        if (g_last_spawn_symbi_error.empty()) {
            set_spawn_symbi_error("load_original_shellcode_page_failed");
        }
        return false;
    }

    close(mem_fd);
    LOGI("symbi: prepared zygote=%d target=%s setArgV0=0x%lx slot=0x%lx original=0x%lx shellcode=0x%lx",
         zygote_pid,
         ctx->target_package.c_str(),
         ctx->set_argv0_address,
         ctx->art_method_slot,
         ctx->original_ptr,
         ctx->shellcode_base);
    return true;
}

bool prepare_stub_patch(const SymbiContext& ctx, PreparedStubPatch* prepared) {
    if (prepared == nullptr) {
        set_spawn_symbi_error("prepare_stub_patch_invalid_args");
        return false;
    }

    char remote_pattern[] = "/ningningning123123";
    uintptr_t marker = reinterpret_cast<uintptr_t>(
        memmem(stub_binary, stub_binary_size, remote_pattern, sizeof(remote_pattern)));
    if (marker == 0) {
        LOGE("symbi: failed to locate stub marker");
        set_spawn_symbi_error("locate_stub_marker_failed");
        return false;
    }

    prepared->stub_copy.assign(stub_binary, stub_binary + stub_binary_size);
    uintptr_t offset = marker - reinterpret_cast<uintptr_t>(stub_binary);
    auto* stub_cfg = reinterpret_cast<TStub*>(prepared->stub_copy.data() + offset);

    memset(stub_cfg->socket_name, 0, sizeof(stub_cfg->socket_name));
    strncpy(stub_cfg->socket_name, ctx.callback_socket_name.c_str(), sizeof(stub_cfg->socket_name) - 1);
    memset(stub_cfg->target_package, 0, sizeof(stub_cfg->target_package));
    strncpy(stub_cfg->target_package, ctx.target_package.c_str(), sizeof(stub_cfg->target_package) - 1);
    stub_cfg->original_set_argv0 =
        reinterpret_cast<int (*)(JNIEnv*, jobject, jstring)>(ctx.set_argv0_address);
    stub_cfg->slot_addr = ctx.art_method_slot;
    stub_cfg->socket = reinterpret_cast<int (*)(int, int, int)>(ctx.remote_socket);
    stub_cfg->connect = reinterpret_cast<int (*)(int, const struct sockaddr*, socklen_t)>(ctx.remote_connect);
    stub_cfg->write = reinterpret_cast<ssize_t (*)(int, const void*, size_t)>(ctx.remote_write);
    stub_cfg->close = reinterpret_cast<int (*)(int)>(ctx.remote_close);
    stub_cfg->getpid = reinterpret_cast<pid_t (*)()>(ctx.remote_getpid);
    stub_cfg->raise = reinterpret_cast<int (*)(int)>(ctx.remote_raise);
    prepared->new_ptr = ctx.shellcode_base;

    return true;
}

bool apply_prepared_stub_patch(int mem_fd, const SymbiContext& ctx, const PreparedStubPatch& prepared) {
    ssize_t written_code = pwrite(mem_fd,
                                  prepared.stub_copy.data(),
                                  prepared.stub_copy.size(),
                                  static_cast<off_t>(ctx.shellcode_base));
    if (written_code != static_cast<ssize_t>(prepared.stub_copy.size())) {
        LOGE("symbi: failed to write stub to shellcode_base=0x%lx", ctx.shellcode_base);
        set_spawn_symbi_error("write_stub_failed");
        return false;
    }

    ssize_t written_ptr = pwrite(mem_fd,
                                 &prepared.new_ptr,
                                 sizeof(prepared.new_ptr),
                                 static_cast<off_t>(ctx.art_method_slot));
    if (written_ptr != static_cast<ssize_t>(sizeof(prepared.new_ptr))) {
        LOGE("symbi: failed to patch art_method_slot=0x%lx", ctx.art_method_slot);
        set_spawn_symbi_error("patch_art_method_slot_failed");
        return false;
    }

    LOGI("symbi: wrote stub to 0x%lx and patched slot=0x%lx -> 0x%lx",
         ctx.shellcode_base, ctx.art_method_slot, prepared.new_ptr);
    return true;
}

RestoreFailureReason restore_write_mem(pid_t pid, const SymbiContext& ctx) {
    int mem_fd = open_remote_mem(pid);
    if (mem_fd < 0) {
        LOGE("symbi: restore open_remote_mem failed pid=%d errno=%d", pid, errno);
        return RestoreFailureReason::kOpenRemoteMemFailed;
    }

    ssize_t slot_written = pwrite(mem_fd,
                                  &ctx.original_ptr,
                                  sizeof(ctx.original_ptr),
                                  static_cast<off_t>(ctx.art_method_slot));
    ssize_t code_written = pwrite(mem_fd,
                                  ctx.original_shellcode_area.data(),
                                  ctx.original_shellcode_area.size(),
                                  static_cast<off_t>(ctx.shellcode_base));
    bool ok = slot_written == static_cast<ssize_t>(sizeof(ctx.original_ptr)) &&
              code_written == static_cast<ssize_t>(ctx.original_shellcode_area.size());
    LOGI("symbi: restore writes pid=%d slot=%zd/%zu code=%zd/%zu",
         pid,
         slot_written,
         sizeof(ctx.original_ptr),
         code_written,
         ctx.original_shellcode_area.size());
    close(mem_fd);
    return ok ? RestoreFailureReason::kNone : RestoreFailureReason::kWriteMemFailed;
}

RestoreFailureReason stop_restore_target(const SymbiContext& ctx) {
    if (!stop_process(ctx.zygote_pid)) {
        LOGE("symbi: restore stop_process failed zygote=%d", ctx.zygote_pid);
        return RestoreFailureReason::kStopProcessFailed;
    }
    return RestoreFailureReason::kNone;
}

void resume_restore_target(const SymbiContext& ctx) {
    resume_process(ctx.zygote_pid);
}

RestoreFailureReason ptrace_attach_restore_target(const SymbiContext& ctx) {
    LOGI("symbi: attempting ptrace restore zygote=%d", ctx.zygote_pid);

    if (ptrace(PTRACE_ATTACH, ctx.zygote_pid, nullptr, nullptr) != 0) {
        LOGE("symbi: ptrace attach failed zygote=%d errno=%d", ctx.zygote_pid, errno);
        return RestoreFailureReason::kPtraceAttachFailed;
    }

    int status = 0;
    pid_t waited = waitpid(ctx.zygote_pid, &status, 0);
    if (waited != ctx.zygote_pid) {
        LOGE("symbi: ptrace waitpid failed zygote=%d errno=%d", ctx.zygote_pid, errno);
        ptrace(PTRACE_DETACH, ctx.zygote_pid, nullptr, nullptr);
        return RestoreFailureReason::kPtraceWaitpidFailed;
    }
    return RestoreFailureReason::kNone;
}

void ptrace_detach_restore_target(const SymbiContext& ctx) {
    ptrace(PTRACE_DETACH, ctx.zygote_pid, nullptr, nullptr);
}

RestoreFailureReason run_restore_attempt(const SymbiContext& ctx, const RestoreDriverOps& ops) {
    const RestoreFailureReason entry = ops.attach_or_stop(ctx);
    if (entry != RestoreFailureReason::kNone) {
        return entry;
    }

    const RestoreFailureReason reason = restore_write_mem(ctx.zygote_pid, ctx);
    if (ops.detach_or_resume != nullptr) {
        ops.detach_or_resume(ctx);
    }

    if (reason == RestoreFailureReason::kNone) {
        LOGI("symbi: %s restore complete slot=0x%lx shellcode=0x%lx",
             ops.name,
             ctx.art_method_slot,
             ctx.shellcode_base);
    } else {
        LOGE("symbi: %s restore failed reason=%d",
             ops.name,
             static_cast<int>(reason));
    }
    return reason;
}

RestoreFailureReason restore_original_slot(const SymbiContext& ctx) {
    const RestoreDriverOps primary_restore_ops{
        .name = "primary",
        .attach_or_stop = stop_restore_target,
        .detach_or_resume = resume_restore_target,
    };
    return run_restore_attempt(ctx, primary_restore_ops);
}

// Fallback: use ptrace(PTRACE_ATTACH) to guarantee the process is stopped,
// then write via /proc/pid/mem. This is the last resort to prevent leaving
// zygote in a corrupted state with a dangling ART method slot.
RestoreFailureReason restore_original_slot_ptrace(const SymbiContext& ctx) {
    const RestoreDriverOps ptrace_restore_ops{
        .name = "ptrace",
        .attach_or_stop = ptrace_attach_restore_target,
        .detach_or_resume = ptrace_detach_restore_target,
    };
    const RestoreFailureReason reason = run_restore_attempt(ctx, ptrace_restore_ops);
    if (reason == RestoreFailureReason::kNone) {
        return reason;
    } else {
        LOGE("symbi: CRITICAL ptrace restore also failed zygote=%d reason=%d",
             ctx.zygote_pid,
             static_cast<int>(reason));
    }
    return reason;
}

const char* RestoreStageName(RestoreStage stage) {
    switch (stage) {
        case RestoreStage::kAfterStartFailure:
            return "after-start-failure";
        case RestoreStage::kAfterCallback:
            return "after-callback";
    }
    return "unknown";
}

const char* RestoreFailureReasonName(RestoreFailureReason reason) {
    switch (reason) {
        case RestoreFailureReason::kNone:
            return "none";
        case RestoreFailureReason::kStopProcessFailed:
            return "primary_stop_failed";
        case RestoreFailureReason::kOpenRemoteMemFailed:
            return "write_open_remote_mem_failed";
        case RestoreFailureReason::kWriteMemFailed:
            return "write_mem_failed";
        case RestoreFailureReason::kPtraceAttachFailed:
            return "ptrace_attach_failed";
        case RestoreFailureReason::kPtraceWaitpidFailed:
            return "ptrace_waitpid_failed";
    }
    return "unknown";
}

std::string BuildRestoreError(const RestoreAttemptResult& result, RestoreStage stage) {
    std::string error = "restore_original_slot_failed:";
    error += RestoreStageName(stage);
    error += ":";
    error += RestoreFailureReasonName(result.primary_failure);
    if (result.ptrace_fallback_attempted) {
        error += ":";
        error += RestoreFailureReasonName(result.ptrace_failure);
    }
    return error;
}

RestoreAttemptResult restore_with_fallback(const SymbiContext& ctx, RestoreStage stage) {
    RestoreAttemptResult result{};
    result.primary_attempted = true;
    SymbiHandoffState handoff_state = SymbiHandoffState::kPrimaryRestoreAttempted;
    LOGI("symbi: handoff state=%s", SymbiHandoffStateName(handoff_state));
    result.primary_failure = restore_original_slot(ctx);
    result.restored = result.primary_failure == RestoreFailureReason::kNone;
    if (result.restored) {
        AdvanceSymbiHandoffState(&handoff_state, SymbiHandoffState::kRestoreCompleted);
        LOGI("symbi: restore stage=%s completed via primary path", RestoreStageName(stage));
        return result;
    }

    LOGE("symbi: restore stage=%s primary path failed reason=%s, trying ptrace fallback",
         RestoreStageName(stage),
         RestoreFailureReasonName(result.primary_failure));
    result.ptrace_fallback_attempted = true;
    AdvanceSymbiHandoffState(&handoff_state, SymbiHandoffState::kPtraceRestoreAttempted);
    result.ptrace_failure = restore_original_slot_ptrace(ctx);
    result.restored = result.ptrace_failure == RestoreFailureReason::kNone;
    if (result.restored) {
        AdvanceSymbiHandoffState(&handoff_state, SymbiHandoffState::kRestoreCompleted);
        LOGI("symbi: restore stage=%s completed via ptrace fallback", RestoreStageName(stage));
    } else {
        LOGE("symbi: restore stage=%s failed on all restore paths primary=%s ptrace=%s",
             RestoreStageName(stage),
             RestoreFailureReasonName(result.primary_failure),
             RestoreFailureReasonName(result.ptrace_failure));
    }
    return result;
}

bool start_target_app_symbi(const char* package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return false;
    }

    const std::string component = resolve_launch_component(package_name);
    if (component.empty()) {
        LOGE("symbi: resolve launch component failed package=%s", package_name);
        return false;
    }

    std::string force_stop_cmd = std::string("am force-stop ") + package_name;
    std::string start_cmd = std::string("am start -S -n ") + component;
    int force_stop_ret = system(force_stop_cmd.c_str());
    int start_ret = system(start_cmd.c_str());
    const bool started = wait_for_process_start_by_name(package_name, 10000);
    LOGI("symbi: start app package=%s component=%s force-stop ret=%d start ret=%d started=%d",
         package_name,
         component.c_str(),
         force_stop_ret,
         start_ret,
         started ? 1 : 0);
    return start_ret == 0 && started;
}

bool install_zygote_gate(const SymbiContext& ctx,
                         const PreparedStubPatch& prepared_patch,
                         SymbiGateInstallResult* result) {
    if (result == nullptr) {
        set_spawn_symbi_error("zygote_gate_install_invalid_args");
        return false;
    }

    int mem_fd = open_remote_mem(ctx.zygote_pid);
    if (mem_fd < 0) {
        set_spawn_symbi_error("open_remote_mem_failed");
        return false;
    }

    if (!stop_process(ctx.zygote_pid)) {
        set_spawn_symbi_error("stop_zygote_failed");
        close(mem_fd);
        return false;
    }

    // This is the zygote-side gate stage only. No child runtime delivery happens here.
    // While the zygote is stopped, we keep the critical section limited to the two writes
    // needed to install the temporary gate.
    result->installed = apply_prepared_stub_patch(mem_fd, ctx, prepared_patch);
    resume_process(ctx.zygote_pid);
    close(mem_fd);

    if (!result->installed) {
        set_spawn_symbi_error("write_stub_or_patch_slot_failed");
        return false;
    }

    return true;
}

bool complete_child_delivery_handoff(const SymbiContext& ctx,
                                     const char* package_name,
                                     SymbiCallbackListener* callback_listener,
                                     SymbiChildHandoffResult* result) {
    if (package_name == nullptr || callback_listener == nullptr || result == nullptr) {
        set_spawn_symbi_error("child_handoff_invalid_args");
        return false;
    }

    SymbiHandoffState handoff_state = SymbiHandoffState::kGateInstalled;
    LOGI("symbi: handoff state=%s", SymbiHandoffStateName(handoff_state));

    // From this point on, the zygote-side gate is already installed. The remaining work is a
    // child-side handoff: start target app, wait for child callback, then restore zygote state.
    if (!start_target_app_symbi(package_name)) {
        set_spawn_symbi_error("start_target_app_failed");
        CloseSymbiCallbackListener(callback_listener);
        result->restore_result = restore_with_fallback(ctx, RestoreStage::kAfterStartFailure);
        result->restored = result->restore_result.restored;
        if (!result->restored) {
            set_spawn_symbi_error(BuildRestoreError(result->restore_result, RestoreStage::kAfterStartFailure));
        }
        return false;
    }
    AdvanceSymbiHandoffState(&handoff_state, SymbiHandoffState::kTargetAppStarted);

    result->callback_ok = WaitForSymbiCallback(*callback_listener,
                                               kSymbiCallbackTimeoutMs,
                                               &result->callback_result);
    if (result->callback_ok) {
        AdvanceSymbiHandoffState(&handoff_state, SymbiHandoffState::kCallbackObserved);
    }
    CloseSymbiCallbackListener(callback_listener);

    result->restore_result = restore_with_fallback(ctx, RestoreStage::kAfterCallback);
    result->restored = result->restore_result.restored;
    if (!result->restored) {
        LOGE("symbi: CRITICAL all restore attempts failed zygote=%d - zygote may be corrupted",
             ctx.zygote_pid);
        set_spawn_symbi_error(BuildRestoreError(result->restore_result, RestoreStage::kAfterCallback));
        return false;
    }

    if (!result->callback_ok) {
        LOGE("symbi: callback wait timed out or failed");
        set_spawn_symbi_error("callback_wait_failed");
        return false;
    }

    return true;
}

} // namespace

bool inject_spawn_symbi_by_pids(const std::vector<pid_t>& pids,
                                const char* package_name,
                                SpawnSymbiResult* result) {
    g_last_spawn_symbi_error.clear();
    if (pids.empty() ||
        package_name == nullptr || package_name[0] == '\0') {
        LOGE("symbi: invalid spawn-symbi args");
        set_spawn_symbi_error("invalid_args");
        return false;
    }

    pid_t zygote_pid = -1;
    for (pid_t pid : pids) {
        if (pid > 0) {
            zygote_pid = pid;
            break;
        }
    }
    if (zygote_pid <= 0) {
        LOGE("symbi: no valid zygote pid");
        set_spawn_symbi_error("no_valid_zygote_pid");
        return false;
    }

    // Gate phase: collect zygote-only state needed to install the temporary fork gate.
    // Child runtime delivery is intentionally handled by the caller after the child
    // stops and reports back through the callback handshake.
    SymbiContext ctx{};
    if (!collect_symbi_context(zygote_pid, package_name, &ctx)) {
        if (g_last_spawn_symbi_error.empty()) {
            set_spawn_symbi_error("collect_context_failed");
        }
        return false;
    }

    SymbiCallbackListener callback_listener{};
    if (!OpenSymbiCallbackListener(&callback_listener)) {
        set_spawn_symbi_error("open_callback_listener_failed");
        return false;
    }
    ctx.callback_socket_name = callback_listener.socket_name;

    PreparedStubPatch prepared_patch{};
    if (!prepare_stub_patch(ctx, &prepared_patch)) {
        if (g_last_spawn_symbi_error.empty()) {
            set_spawn_symbi_error("prepare_stub_patch_failed");
        }
        CloseSymbiCallbackListener(&callback_listener);
        return false;
    }

    SymbiGateInstallResult gate_install_result{};
    if (!install_zygote_gate(ctx, prepared_patch, &gate_install_result)) {
        CloseSymbiCallbackListener(&callback_listener);
        return false;
    }

    // Handoff phase: start app, wait for child callback, restore zygote state, then
    // return child identity to the caller so it can perform child runtime delivery.
    SymbiChildHandoffResult handoff_result{};
    if (!complete_child_delivery_handoff(ctx, package_name, &callback_listener, &handoff_result)) {
        return false;
    }
    if (result != nullptr) {
        result->child_pid = handoff_result.callback_result.pid;
        result->package_name.clear();
    }

    LOGI("symbi: auto restore complete after callback child_pid=%d",
         handoff_result.callback_result.pid);
    return true;
}

bool inject_spawn_symbi_by_package(pid_t zygote_pid,
                                   const char* package_name,
                                   SpawnSymbiResult* result) {
    return inject_spawn_symbi_by_pids(std::vector<pid_t>{zygote_pid}, package_name, result);
}

const char* get_last_spawn_symbi_error() {
    return g_last_spawn_symbi_error.c_str();
}
