#include "native_hook/core/native_hook_symbol_resolver.h"
#include "native_hook/inline_hook/inline_hook_impl.h"
#include "generated/nook_embedded_agent_blob.h"

#include <jni.h>
#include <sys/types.h>
#include <unistd.h>

#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/syscall.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr const char* kTag = "NookNcore";
constexpr const char* kResultFile = "/data/local/tmp/nook/spawn_result.json";
constexpr const char* kLibcModule = "libc.so";
constexpr const char* kAndroidRuntimeModule = "libandroid_runtime.so";
constexpr const char* kSelinuxModule = "libselinux.so";
constexpr const char* kEmbeddedAgentSentinel = "__embedded_agent__";
constexpr const char* kProcessSetArgSymbol =
    "_Z27android_os_Process_setArgV0P7_JNIEnvP8_jobjectP8_jstring";

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#if !defined(SYS_memfd_create) && defined(__NR_memfd_create)
#define SYS_memfd_create __NR_memfd_create
#endif

#define NOOK_NCORE_LOGI(...) \
    ((void)__android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__))
#define NOOK_NCORE_LOGD(...) \
    ((void)__android_log_print(ANDROID_LOG_DEBUG, kTag, __VA_ARGS__))
#define NOOK_NCORE_LOGE(...) \
    ((void)__android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__))

using ForkFn = pid_t (*)();
using VForkFn = pid_t (*)();
using ProcessSetArgFn = void (*)(JNIEnv*, jobject, jstring);
using SelinuxSetContextFn = int (*)(uid_t, bool, const char*, const char*);
using AgentInitializeFn = int (*)();

char* g_target_package = nullptr;
char* g_target_so = nullptr;
bool g_payload_loaded = false;
bool g_spawn_hooks_installed = false;

ForkFn g_orig_fork = nullptr;
VForkFn g_orig_vfork = nullptr;
ProcessSetArgFn g_orig_process_set_arg = nullptr;
SelinuxSetContextFn g_orig_selinux_setcontext = nullptr;

void* g_fork_hook_handle = nullptr;
void* g_vfork_hook_handle = nullptr;
void* g_process_set_arg_hook_handle = nullptr;
void* g_selinux_setcontext_hook_handle = nullptr;

struct ScopedSkipAutoInitEnv {
    bool had_original = false;
    std::string original_value;

    ScopedSkipAutoInitEnv() {
        const char* current = std::getenv("NOOK_SKIP_AUTO_INIT");
        if (current != nullptr) {
            had_original = true;
            original_value = current;
        }
        setenv("NOOK_SKIP_AUTO_INIT", "1", 1);
    }

    ~ScopedSkipAutoInitEnv() {
        if (had_original) {
            setenv("NOOK_SKIP_AUTO_INIT", original_value.c_str(), 1);
        } else {
            unsetenv("NOOK_SKIP_AUTO_INIT");
        }
    }
};

bool WriteFullyToFd(int fd, const uint8_t* data, size_t size) {
    if (fd < 0 || (data == nullptr && size != 0)) {
        return false;
    }

    size_t total_written = 0;
    while (total_written < size) {
        const ssize_t written = TEMP_FAILURE_RETRY(
            write(fd, data + total_written, size - total_written));
        if (written <= 0) {
            return false;
        }
        total_written += static_cast<size_t>(written);
    }

    return true;
}

void* DlopenEmbeddedAgentFromMemfd() {
#if !defined(SYS_memfd_create)
    NOOK_NCORE_LOGE("embedded agent memfd unavailable");
    return nullptr;
#else
    if (nook::server::kNookEmbeddedAgentBlobSize == 0) {
        NOOK_NCORE_LOGE("embedded agent blob is empty");
        return nullptr;
    }

    const int fd = static_cast<int>(TEMP_FAILURE_RETRY(
        syscall(SYS_memfd_create, "libnook-agent", static_cast<unsigned int>(MFD_CLOEXEC))));
    if (fd < 0) {
        NOOK_NCORE_LOGE("memfd_create failed errno=%d", errno);
        return nullptr;
    }

    if (!WriteFullyToFd(fd,
                        nook::server::kNookEmbeddedAgentBlob,
                        static_cast<size_t>(nook::server::kNookEmbeddedAgentBlobSize))) {
        NOOK_NCORE_LOGE("write embedded agent to memfd failed");
        close(fd);
        return nullptr;
    }

    if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) < 0) {
        NOOK_NCORE_LOGE("memfd lseek failed errno=%d", errno);
        close(fd);
        return nullptr;
    }

    android_dlextinfo extinfo{};
    extinfo.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
    extinfo.library_fd = fd;

    char proc_fd_path[64] = {};
    std::snprintf(proc_fd_path, sizeof(proc_fd_path), "/proc/self/fd/%d", fd);

    void* handle = android_dlopen_ext(proc_fd_path,
                                      RTLD_NOW | RTLD_NODELETE | RTLD_GLOBAL,
                                      &extinfo);
    if (handle == nullptr) {
        NOOK_NCORE_LOGE("android_dlopen_ext embedded agent failed: %s",
                        dlerror());
    }

    close(fd);
    return handle;
#endif
}

void* LoadAgentLibraryHandle(const char* so_path) {
    if (so_path == nullptr || so_path[0] == '\0') {
        return nullptr;
    }

    if (std::strcmp(so_path, kEmbeddedAgentSentinel) == 0) {
        return DlopenEmbeddedAgentFromMemfd();
    }

    return dlopen(so_path, RTLD_NOW | RTLD_NODELETE | RTLD_GLOBAL);
}

void SendStatusToInjector(const char* package_name, const char* so_path) {
    char payload[512] = {0};
    std::snprintf(payload,
                  sizeof(payload),
                  "{\"pid\":%d,\"pkg\":\"%s\",\"so\":\"%s\"}",
                  getpid(),
                  package_name != nullptr ? package_name : "",
                  so_path != nullptr ? so_path : "");

    const int fd = open(kResultFile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        NOOK_NCORE_LOGE("open result file failed");
        return;
    }

    const ssize_t written = write(fd, payload, std::strlen(payload));
    if (written < 0) {
        NOOK_NCORE_LOGE("write result file failed");
        close(fd);
        return;
    }

    NOOK_NCORE_LOGI("sent callback payload=%s", payload);
    close(fd);
}

bool MatchesTarget(const char* name) {
    const bool matched = g_target_package != nullptr &&
                         g_target_so != nullptr &&
                         name != nullptr &&
                         std::strcmp(name, g_target_package) == 0;
    if (name != nullptr) {
        NOOK_NCORE_LOGD("compare target current=%s target=%s matched=%d",
                        name,
                        g_target_package != nullptr ? g_target_package : "(null)",
                        matched ? 1 : 0);
    }
    return matched;
}

void UnloadTargetState() {
    NOOK_NCORE_LOGI("unload target state package=%s so=%s loaded=%d",
                    g_target_package != nullptr ? g_target_package : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)",
                    g_payload_loaded ? 1 : 0);
    if (g_target_package != nullptr) {
        std::free(g_target_package);
        g_target_package = nullptr;
    }
    if (g_target_so != nullptr) {
        std::free(g_target_so);
        g_target_so = nullptr;
    }
    g_payload_loaded = false;
}

void UninstallHook(void** hook_handle, const char* name) {
    if (hook_handle == nullptr || *hook_handle == nullptr) {
        return;
    }

    if (!NookInlineHookInternal::UninstallInlineHook(*hook_handle)) {
        NOOK_NCORE_LOGE("unhook failed name=%s handle=%p", name, *hook_handle);
        return;
    }

    NOOK_NCORE_LOGI("unhooked name=%s handle=%p", name, *hook_handle);
    *hook_handle = nullptr;
}

void UnhookAll() {
    NOOK_NCORE_LOGI("unhook all fork_handle=%p vfork_handle=%p process_arg_handle=%p selinux_handle=%p",
                    g_fork_hook_handle,
                    g_vfork_hook_handle,
                    g_process_set_arg_hook_handle,
                    g_selinux_setcontext_hook_handle);
    UninstallHook(&g_fork_hook_handle, "fork");
    UninstallHook(&g_vfork_hook_handle, "vfork");
    UninstallHook(&g_process_set_arg_hook_handle, "android_os_Process_setArgV0");
    UninstallHook(&g_selinux_setcontext_hook_handle, "selinux_android_setcontext");

    g_orig_fork = nullptr;
    g_orig_vfork = nullptr;
    g_orig_process_set_arg = nullptr;
    g_orig_selinux_setcontext = nullptr;
    g_spawn_hooks_installed = false;
}

bool InstallHookBySymbol(const char* module_name,
                         const char* symbol_name,
                         void* replacement,
                         void** original,
                         void** hook_handle) {
    void* target = nullptr;
    if (!NookNativeHookInternal::ResolveSymbolAddress(module_name, symbol_name, &target) ||
        target == nullptr) {
        NOOK_NCORE_LOGE("resolve failed module=%s symbol=%s",
                        module_name,
                        symbol_name);
        return false;
    }

    if (!NookInlineHookInternal::InstallInlineHook(target,
                                                   replacement,
                                                   original,
                                                   hook_handle)) {
        NOOK_NCORE_LOGE("install hook failed module=%s symbol=%s target=%p",
                        module_name,
                        symbol_name,
                        target);
        return false;
    }

    NOOK_NCORE_LOGI("hook installed module=%s symbol=%s target=%p handle=%p original=%p",
                    module_name,
                    symbol_name,
                    target,
                    hook_handle != nullptr ? *hook_handle : nullptr,
                    original != nullptr ? *original : nullptr);
    return true;
}

bool LoadPayloadIfNeeded(const char* package_name) {
    if (!MatchesTarget(package_name)) {
        return false;
    }

    if (g_payload_loaded) {
        NOOK_NCORE_LOGI("payload already loaded for %s", package_name);
        return true;
    }

    NOOK_NCORE_LOGI("target matched, loading payload package=%s so=%s",
                    package_name != nullptr ? package_name : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)");

    UnhookAll();

    ScopedSkipAutoInitEnv scoped_skip_auto_init;
    void* handle = LoadAgentLibraryHandle(g_target_so);
    if (handle == nullptr) {
        NOOK_NCORE_LOGE("dlopen failed for %s: %s",
                        g_target_so,
                        dlerror());
        return false;
    }

    dlerror();
    void* init_symbol = dlsym(handle, "NookAgentInitializeForSpawnChild");
    const char* dlsym_error = dlerror();
    if (dlsym_error != nullptr || init_symbol == nullptr) {
        NOOK_NCORE_LOGE("dlsym NookAgentInitializeForSpawnChild failed for %s: %s symbol=%p",
                        g_target_so,
                        dlsym_error != nullptr ? dlsym_error : "(null)",
                        init_symbol);
        return false;
    }

    NOOK_NCORE_LOGI("calling NookAgentInitializeForSpawnChild symbol=%p so=%s",
                    init_symbol,
                    g_target_so);
    const int init_status =
        reinterpret_cast<AgentInitializeFn>(init_symbol)();
    NOOK_NCORE_LOGI("NookAgentInitializeForSpawnChild returned status=%d so=%s",
                    init_status,
                    g_target_so);

    g_payload_loaded = true;
    NOOK_NCORE_LOGI("payload loaded for %s => %s", package_name, g_target_so);
    SendStatusToInjector(package_name, g_target_so);
    return true;
}

void InstallChildHooks();

pid_t NookNcoreFakeFork() {
    if (g_orig_fork == nullptr) {
        NOOK_NCORE_LOGE("orig_fork unavailable");
        return -1;
    }

    NOOK_NCORE_LOGI("fork intercepted pid=%d ppid=%d target=%s so=%s",
                    getpid(),
                    getppid(),
                    g_target_package != nullptr ? g_target_package : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)");
    const pid_t pid = g_orig_fork();
    if (pid == 0) {
        NOOK_NCORE_LOGI("child forked pid=%d", getpid());
        InstallChildHooks();
    } else if (pid > 0) {
        NOOK_NCORE_LOGD("parent observed fork child=%d", pid);
    }
    return pid;
}

pid_t NookNcoreFakeVfork() {
    NOOK_NCORE_LOGI("vfork intercepted pid=%d ppid=%d target=%s so=%s",
                    getpid(),
                    getppid(),
                    g_target_package != nullptr ? g_target_package : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)");
    return NookNcoreFakeFork();
}

void NookNcoreFakeProcessSetArg(JNIEnv* env, jobject obj, jstring arg) {
    const char* package_name =
        (env != nullptr && arg != nullptr) ? env->GetStringUTFChars(arg, nullptr) : nullptr;

    if (g_orig_process_set_arg != nullptr) {
        g_orig_process_set_arg(env, obj, arg);
    }

    NOOK_NCORE_LOGD("android_os_Process_setArgV0 arg=%s",
                    package_name != nullptr ? package_name : "(null)");
    LoadPayloadIfNeeded(package_name);

    if (env != nullptr && arg != nullptr && package_name != nullptr) {
        env->ReleaseStringUTFChars(arg, package_name);
    }
}

int NookNcoreFakeSelinuxSetContext(uid_t uid,
                                   bool is_system_server,
                                   const char* seinfo,
                                   const char* name) {
    int result = -1;
    if (g_orig_selinux_setcontext != nullptr) {
        result = g_orig_selinux_setcontext(uid, is_system_server, seinfo, name);
    }

    NOOK_NCORE_LOGD("selinux_android_setcontext name=%s",
                    name != nullptr ? name : "(null)");
    LoadPayloadIfNeeded(name);
    return result;
}

void InstallChildHooks() {
    NOOK_NCORE_LOGI("installing child hooks pid=%d ppid=%d target=%s so=%s",
                    getpid(),
                    getppid(),
                    g_target_package != nullptr ? g_target_package : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)");

    const bool process_arg_ok =
        InstallHookBySymbol(kAndroidRuntimeModule,
                            kProcessSetArgSymbol,
                            reinterpret_cast<void*>(&NookNcoreFakeProcessSetArg),
                            reinterpret_cast<void**>(&g_orig_process_set_arg),
                            &g_process_set_arg_hook_handle);
    const bool selinux_ok =
        InstallHookBySymbol(kSelinuxModule,
                            "selinux_android_setcontext",
                            reinterpret_cast<void*>(&NookNcoreFakeSelinuxSetContext),
                            reinterpret_cast<void**>(&g_orig_selinux_setcontext),
                            &g_selinux_setcontext_hook_handle);
    NOOK_NCORE_LOGI("child hooks installed process_set_arg=%d selinux_setcontext=%d",
                    process_arg_ok ? 1 : 0,
                    selinux_ok ? 1 : 0);
}

}  // namespace

extern "C" void aclear() {
    NOOK_NCORE_LOGI("aclear");
    UnhookAll();
    UnloadTargetState();
}

extern "C" void ainject(const char* package_name, const char* so_path) {
    NOOK_NCORE_LOGI("ainject begin pid=%d ppid=%d package=%s so=%s hooks_installed=%d",
                    getpid(),
                    getppid(),
                    package_name != nullptr ? package_name : "(null)",
                    so_path != nullptr ? so_path : "(null)",
                    g_spawn_hooks_installed ? 1 : 0);
    UnloadTargetState();

    if (package_name != nullptr && package_name[0] != '\0') {
        g_target_package = ::strdup(package_name);
    }
    if (so_path != nullptr && so_path[0] != '\0') {
        g_target_so = ::strdup(so_path);
    }

    NOOK_NCORE_LOGI("ainject package=%s so=%s",
                    g_target_package != nullptr ? g_target_package : "(null)",
                    g_target_so != nullptr ? g_target_so : "(null)");

    if (g_spawn_hooks_installed) {
        NOOK_NCORE_LOGI("spawn hooks already installed, skip");
        return;
    }

    const bool fork_ok = InstallHookBySymbol(kLibcModule,
                                             "fork",
                                             reinterpret_cast<void*>(&NookNcoreFakeFork),
                                             reinterpret_cast<void**>(&g_orig_fork),
                                             &g_fork_hook_handle);
    const bool vfork_ok = InstallHookBySymbol(kLibcModule,
                                              "vfork",
                                              reinterpret_cast<void*>(&NookNcoreFakeVfork),
                                              reinterpret_cast<void**>(&g_orig_vfork),
                                              &g_vfork_hook_handle);
    g_spawn_hooks_installed = fork_ok || vfork_ok;
    NOOK_NCORE_LOGI("spawn hooks installed fork=%d vfork=%d active=%d",
                    fork_ok ? 1 : 0,
                    vfork_ok ? 1 : 0,
                    g_spawn_hooks_installed ? 1 : 0);
    if (!g_spawn_hooks_installed) {
        NOOK_NCORE_LOGE("ainject failed to install any spawn hook package=%s so=%s",
                        g_target_package != nullptr ? g_target_package : "(null)",
                        g_target_so != nullptr ? g_target_so : "(null)");
    }
}

__attribute__((destructor()))
static void NookNcoreCleanup() {
    NOOK_NCORE_LOGD("cleanup");
    UnhookAll();
    UnloadTargetState();
}
