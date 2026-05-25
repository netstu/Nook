#include "native_hook/inline_hook/inline_hook_module_observer.h"

#include "agent_runtime/nook_native_js_bridge.h"
#include "native_hook/core/module_info.h"
#include "native_hook/core/native_hook_symbol_resolver.h"
#include "native_hook/inline_hook/inline_hook_impl.h"
#include "native_hook/inline_hook/pending_inline_hook_registry.h"

#include "nook/NookInlineHook.h"

#include <atomic>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include "xdl.h"
#endif

namespace NookInlineHookInternal {

namespace {

#if defined(__ANDROID__)

constexpr char kObserverTag[] = "NookInlineObserver";

#if defined(__LP64__)
constexpr char kLinkerModuleName[] = "linker64";
#else
constexpr char kLinkerModuleName[] = "linker";
#endif

constexpr char kLinkerCallConstructorsSymbolLower[] = "__dl__ZN6soinfo17call_constructorsEv";
constexpr char kLinkerCallConstructorsSymbolUpper[] = "__dl__ZN6soinfo16CallConstructorsEv";
constexpr size_t kSoinfoScanWordCount = 96u;
constexpr char kProbeLibraryName[] = "libnook_inline_observer_probe.so";

using LinkerCallConstructorsFn = void (*)(void*);

std::once_flag g_module_observer_once;
NookStatus g_module_observer_status = NOOK_STATUS_INTERNAL_ERROR;
std::mutex g_module_observer_async_mutex;
bool g_module_observer_async_started = false;
NookStatus g_module_observer_async_schedule_status = NOOK_STATUS_INTERNAL_ERROR;

void* g_original_linker_call_constructors = nullptr;
void* g_linker_call_constructors_handle = nullptr;

pthread_key_t g_inline_hook_module_notification_key = 0;
bool g_inline_hook_module_notification_key_ready = false;

std::string g_probe_module_path;
std::string g_probe_module_basename;

size_t g_soinfo_offset_phdr = SIZE_MAX;
size_t g_soinfo_offset_phnum = SIZE_MAX;
size_t g_soinfo_offset_load_bias = SIZE_MAX;
size_t g_soinfo_offset_name = SIZE_MAX;
size_t g_soinfo_offset_constructors_called = SIZE_MAX;

std::atomic<bool> g_soinfo_offsets_ready{false};
std::atomic<bool> g_soinfo_scan_requested{false};

void LogObserverEvent(int priority, const char* format, ...);

bool EndsWith(const char* value, const char* suffix) {
    if (value == nullptr || suffix == nullptr) {
        return false;
    }
    const size_t value_length = std::strlen(value);
    const size_t suffix_length = std::strlen(suffix);
    if (value_length < suffix_length) {
        return false;
    }
    return std::memcmp(value + value_length - suffix_length, suffix, suffix_length) == 0;
}

std::string GetBasename(const std::string& path) {
    const size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos) {
        return path;
    }
    return path.substr(separator + 1u);
}

std::string JoinSiblingPath(const std::string& path, const char* sibling_basename) {
    if (path.empty() || sibling_basename == nullptr || sibling_basename[0] == '\0') {
        return std::string();
    }

    const size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos) {
        return std::string(sibling_basename);
    }
    return path.substr(0u, separator + 1u) + sibling_basename;
}

bool IsReadableAddress(const void* address) {
    if (address == nullptr) {
        return false;
    }
    int protection = 0;
    return ElfHooker::get_address_protection(const_cast<void*>(address), &protection) &&
           ((protection & PROT_READ) != 0);
}

bool IsModuleNotificationActive() {
    if (!g_inline_hook_module_notification_key_ready) {
        return false;
    }
    return pthread_getspecific(g_inline_hook_module_notification_key) != nullptr;
}

void SetModuleNotificationActive(bool active) {
    if (!g_inline_hook_module_notification_key_ready) {
        return;
    }
    pthread_setspecific(g_inline_hook_module_notification_key,
                        active ? reinterpret_cast<void*>(1) : nullptr);
}

NookStatus InstallPendingInlineSymbolHook(const char* module_path,
                                          const char* symbol_name,
                                          void* replacement,
                                          void** original,
                                          void** hook_handle,
                                          void*) {
    void* target_address = nullptr;
    NookStatus status = NOOK_STATUS_INTERNAL_ERROR;
    if (NookNativeHookInternal::ResolveSymbolAddressInLoadedModule(module_path,
                                                                   symbol_name,
                                                                   &target_address)) {
        status = NookInlineHookAddress(target_address, replacement, original, hook_handle);
    }
    LogObserverEvent(status == NOOK_STATUS_OK ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                     "pending install module=%s symbol=%s status=%d",
                     module_path,
                     symbol_name,
                     status);
    return status;
}

void LogObserverEvent(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);
    __android_log_vprint(priority, kObserverTag, format, args);
    va_end(args);
}

bool NotifyModuleLoaded(const char* module_path) {
    if (module_path == nullptr || module_path[0] == '\0' ||
        IsModuleNotificationActive()) {
        return false;
    }

    SetModuleNotificationActive(true);
    PendingInlineHookInstallerDependencies dependencies = {};
    dependencies.install_symbol_hook = &InstallPendingInlineSymbolHook;
    const size_t installed = TryInstallPendingInlineHooksForModule(module_path, dependencies);
    std::string native_js_error;
    size_t native_js_installed = 0u;
#if !defined(NOOK_ZYGOTE_HELPER_ONLY)
    native_js_installed =
            nook::agent_runtime::NotifyNativeJsHookModuleLoaded(module_path, &native_js_error);
#else
    native_js_error = "disabled in zygote-helper-only build";
#endif
    SetModuleNotificationActive(false);
    LogObserverEvent(ANDROID_LOG_INFO,
                     "module notify path=%s installed=%zu native_js_installed=%zu native_js_error=%s",
                     module_path,
                     installed,
                     native_js_installed,
                     native_js_error.empty() ? "(none)" : native_js_error.c_str());
    return installed > 0u || native_js_installed > 0u;
}

bool TryGetProbeModuleInfo(xdl_info_t* probe_info) {
    if (probe_info == nullptr || g_probe_module_path.empty()) {
        return false;
    }

    void* handle = xdl_open(g_probe_module_path.c_str(), XDL_DEFAULT);
    if (handle == nullptr) {
        return false;
    }

    xdl_info(handle, XDL_DI_DLINFO, probe_info);
    xdl_close(handle);
    return true;
}

uintptr_t ComputeProbeDynamicAddress(const xdl_info_t& probe_info) {
    for (size_t index = 0u; index < probe_info.dlpi_phnum; ++index) {
        if (probe_info.dlpi_phdr[index].p_type == PT_DYNAMIC) {
            return reinterpret_cast<uintptr_t>(probe_info.dli_fbase) +
                   static_cast<uintptr_t>(probe_info.dlpi_phdr[index].p_vaddr);
        }
    }
    return UINTPTR_MAX;
}

bool TryDiscoverSoinfoOffsets(void* soinfo) {
    if (soinfo == nullptr) {
        return false;
    }

    xdl_info_t probe_info;
    if (!TryGetProbeModuleInfo(&probe_info)) {
        LogObserverEvent(ANDROID_LOG_ERROR, "probe info lookup failed path=%s", g_probe_module_path.c_str());
        return false;
    }

    const uintptr_t probe_dynamic = ComputeProbeDynamicAddress(probe_info);
    if (probe_dynamic == UINTPTR_MAX) {
        return false;
    }

    for (size_t offset = 0u; offset < sizeof(uintptr_t) * kSoinfoScanWordCount; offset += sizeof(uintptr_t)) {
        uintptr_t value_0 = *(reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(soinfo) + offset));
        uintptr_t value_1 = *(reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(soinfo) + offset + sizeof(uintptr_t)));
        uintptr_t value_2 = *(reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(soinfo) + offset + sizeof(uintptr_t) * 2u));
        uintptr_t value_5 = *(reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(soinfo) + offset + sizeof(uintptr_t) * 5u));
        uintptr_t value_6 = *(reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(soinfo) + offset + sizeof(uintptr_t) * 6u));

        if (g_soinfo_offset_phdr == SIZE_MAX &&
            value_0 == reinterpret_cast<uintptr_t>(probe_info.dlpi_phdr) &&
            value_1 == static_cast<uintptr_t>(probe_info.dlpi_phnum)) {
            g_soinfo_offset_phdr = offset;
            g_soinfo_offset_phnum = offset + sizeof(uintptr_t);
            continue;
        }

        if (g_soinfo_offset_load_bias == SIZE_MAX &&
            value_0 == reinterpret_cast<uintptr_t>(probe_info.dli_fbase) &&
            value_2 == probe_dynamic &&
            value_5 == 0u &&
            value_6 == value_0) {
            const char* candidate_name = reinterpret_cast<const char*>(value_1);
            if (!IsReadableAddress(candidate_name)) {
                continue;
            }
            if (!EndsWith(candidate_name, g_probe_module_basename.c_str())) {
                continue;
            }

            g_soinfo_offset_load_bias = offset;
            g_soinfo_offset_name = offset + sizeof(uintptr_t);
            g_soinfo_offset_constructors_called = offset + sizeof(uintptr_t) * 5u;
            continue;
        }

        if (g_soinfo_offset_phdr != SIZE_MAX &&
            g_soinfo_offset_phnum != SIZE_MAX &&
            g_soinfo_offset_load_bias != SIZE_MAX &&
            g_soinfo_offset_name != SIZE_MAX &&
            g_soinfo_offset_constructors_called != SIZE_MAX) {
            break;
        }
    }

    return g_soinfo_offset_phdr != SIZE_MAX &&
           g_soinfo_offset_phnum != SIZE_MAX &&
           g_soinfo_offset_load_bias != SIZE_MAX &&
           g_soinfo_offset_name != SIZE_MAX &&
           g_soinfo_offset_constructors_called != SIZE_MAX;
}

bool FinalizeSoinfoOffsetDiscovery(void* soinfo) {
    if (soinfo == nullptr || g_soinfo_offset_constructors_called == SIZE_MAX) {
        return false;
    }

    const int constructors_called =
            *(reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(soinfo) +
                                           g_soinfo_offset_constructors_called));
    if (constructors_called == 0) {
        return false;
    }

    g_soinfo_offsets_ready.store(true, std::memory_order_release);
    LogObserverEvent(ANDROID_LOG_INFO,
                     "soinfo offsets ready load_bias=%zu name=%zu phdr=%zu phnum=%zu called=%zu",
                     g_soinfo_offset_load_bias,
                     g_soinfo_offset_name,
                     g_soinfo_offset_phdr,
                     g_soinfo_offset_phnum,
                     g_soinfo_offset_constructors_called);
    return true;
}

bool IsSoinfoLoading(const void* soinfo) {
    if (soinfo == nullptr || !g_soinfo_offsets_ready.load(std::memory_order_acquire)) {
        return false;
    }
    return *(reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(soinfo) +
                                          g_soinfo_offset_constructors_called)) == 0;
}

const char* GetLoadedModulePathFromSoinfo(const void* soinfo) {
    if (soinfo == nullptr || !g_soinfo_offsets_ready.load(std::memory_order_acquire)) {
        return nullptr;
    }

    const char* module_path =
            *(reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(soinfo) +
                                                   g_soinfo_offset_name));
    if (module_path == nullptr || module_path[0] == '\0' || !IsReadableAddress(module_path)) {
        return nullptr;
    }
    return module_path;
}

bool TryInstallObserverHook(void* target_address,
                            void* replacement,
                            void** original,
                            void** hook_handle) {
    if (target_address == nullptr) {
        return false;
    }
    return InstallInlineHook(target_address, replacement, original, hook_handle);
}

extern "C" void HookedLinkerCallConstructors(void* soinfo) {
    bool scan_started = false;
    if (!g_soinfo_offsets_ready.load(std::memory_order_acquire) &&
        g_soinfo_scan_requested.load(std::memory_order_acquire)) {
        scan_started = TryDiscoverSoinfoOffsets(soinfo);
    }

    const char* module_path = nullptr;
    if (g_soinfo_offsets_ready.load(std::memory_order_acquire) && IsSoinfoLoading(soinfo)) {
        module_path = GetLoadedModulePathFromSoinfo(soinfo);
        if (module_path != nullptr && !g_probe_module_basename.empty() &&
            !EndsWith(module_path, g_probe_module_basename.c_str())) {
            (void)NotifyModuleLoaded(module_path);
        }
    }

    auto* original = reinterpret_cast<LinkerCallConstructorsFn>(g_original_linker_call_constructors);
    if (original != nullptr) {
        original(soinfo);
    }

    if (scan_started) {
        (void)FinalizeSoinfoOffsetDiscovery(soinfo);
    }
}

void InitializeInlineHookModuleObserverOnce() {
    g_module_observer_status = NOOK_STATUS_INTERNAL_ERROR;

    g_inline_hook_module_notification_key_ready =
            pthread_key_create(&g_inline_hook_module_notification_key, nullptr) == 0;
    if (!g_inline_hook_module_notification_key_ready) {
        LogObserverEvent(ANDROID_LOG_ERROR, "pthread_key_create failed");
        return;
    }

    Dl_info payload_info = {};
    if (dladdr(reinterpret_cast<void*>(&InitializeInlineHookModuleObserverOnce), &payload_info) == 0 ||
        payload_info.dli_fname == nullptr) {
        LogObserverEvent(ANDROID_LOG_ERROR, "dladdr payload path failed");
        return;
    }

    g_probe_module_path = JoinSiblingPath(payload_info.dli_fname, kProbeLibraryName);
    g_probe_module_basename = GetBasename(g_probe_module_path);
    if (g_probe_module_path.empty()) {
        LogObserverEvent(ANDROID_LOG_ERROR, "probe path resolve failed");
        return;
    }

    void* linker_handle = xdl_open(kLinkerModuleName, XDL_DEFAULT);
    if (linker_handle == nullptr) {
        LogObserverEvent(ANDROID_LOG_ERROR, "xdl_open linker failed module=%s", kLinkerModuleName);
        return;
    }

    void* call_constructors_address = xdl_dsym(linker_handle, kLinkerCallConstructorsSymbolLower, nullptr);
    if (call_constructors_address == nullptr) {
        call_constructors_address = xdl_dsym(linker_handle, kLinkerCallConstructorsSymbolUpper, nullptr);
    }
    xdl_close(linker_handle);

    const bool observer_installed =
            TryInstallObserverHook(call_constructors_address,
                                   reinterpret_cast<void*>(HookedLinkerCallConstructors),
                                   &g_original_linker_call_constructors,
                                   &g_linker_call_constructors_handle);
    LogObserverEvent(ANDROID_LOG_INFO,
                     "install call_constructors observer target=%p status=%d original=%p handle=%p probe=%s",
                     call_constructors_address,
                     observer_installed ? 1 : 0,
                     g_original_linker_call_constructors,
                     g_linker_call_constructors_handle,
                     g_probe_module_path.c_str());
    if (!observer_installed) {
        return;
    }

    g_soinfo_scan_requested.store(true, std::memory_order_release);
    void* probe_handle = dlopen(g_probe_module_path.c_str(), RTLD_NOW);
    if (probe_handle != nullptr) {
        dlclose(probe_handle);
    } else {
        LogObserverEvent(ANDROID_LOG_ERROR,
                         "probe dlopen failed path=%s error=%s",
                         g_probe_module_path.c_str(),
                         dlerror());
    }
    g_soinfo_scan_requested.store(false, std::memory_order_release);

    g_module_observer_status = g_soinfo_offsets_ready.load(std::memory_order_acquire)
                                       ? NOOK_STATUS_OK
                                       : NOOK_STATUS_INTERNAL_ERROR;
    LogObserverEvent(ANDROID_LOG_INFO, "observer init complete status=%d", g_module_observer_status);
}

void* InitializeInlineHookModuleObserverThreadMain(void*) {
    LogObserverEvent(ANDROID_LOG_INFO, "observer async thread start");
    const NookStatus status = InitializeInlineHookModuleObserver();
    LogObserverEvent(ANDROID_LOG_INFO, "observer async thread init status=%d", status);
    return nullptr;
}

#endif

}  // namespace

NookStatus InitializeInlineHookModuleObserver(void) {
#if defined(__ANDROID__)
    std::call_once(g_module_observer_once, &InitializeInlineHookModuleObserverOnce);
    return g_module_observer_status;
#else
    return NOOK_STATUS_OK;
#endif
}

NookStatus EnsureInlineHookModuleObserverAsync(void) {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_module_observer_async_mutex);
    if (g_module_observer_async_started) {
        return g_module_observer_async_schedule_status;
    }

    pthread_t thread = 0;
    if (pthread_create(&thread, nullptr, &InitializeInlineHookModuleObserverThreadMain, nullptr) != 0) {
        g_module_observer_async_schedule_status = NOOK_STATUS_INTERNAL_ERROR;
        LogObserverEvent(ANDROID_LOG_ERROR, "observer async thread create failed");
        return g_module_observer_async_schedule_status;
    }

    pthread_detach(thread);
    g_module_observer_async_started = true;
    g_module_observer_async_schedule_status = NOOK_STATUS_OK;
    LogObserverEvent(ANDROID_LOG_INFO, "observer async scheduled");
    return g_module_observer_async_schedule_status;
#else
    return NOOK_STATUS_OK;
#endif
}

}  // namespace NookInlineHookInternal
