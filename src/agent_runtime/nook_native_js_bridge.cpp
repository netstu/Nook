#include "agent_runtime/nook_native_js_bridge.h"
#include "agent_runtime/js_runtime.h"

#include "nook/Nook.h"
#include "native_hook/core/module_info.h"
#include "native_hook/core/native_hook_symbol_resolver.h"
#if defined(__ANDROID__)
#include "native_hook/inline_hook/inline_hook_module_observer.h"
#endif

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <sstream>
#include <vector>

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <atomic>

namespace nook {
namespace agent_runtime {
namespace {

#if defined(__ANDROID__)
constexpr const char* kNativeJsBridgeTag = "NookCommApi";
#define NOOK_NATIVE_JS_BRIDGE_LOGI(...) \
    ((void)__android_log_print(ANDROID_LOG_INFO, kNativeJsBridgeTag, __VA_ARGS__))
#define NOOK_NATIVE_JS_BRIDGE_LOGW(...) \
    ((void)__android_log_print(ANDROID_LOG_WARN, kNativeJsBridgeTag, __VA_ARGS__))
#else
#define NOOK_NATIVE_JS_BRIDGE_LOGI(...) ((void)0)
#define NOOK_NATIVE_JS_BRIDGE_LOGW(...) ((void)0)
#endif

using ResolveLoadedSymbolAddressFn = bool (*)(const char* module_name,
                                              const char* symbol_name,
                                              void** symbol_address);
using InlineHookSymbolSafetyChecker = bool (*)(const char* module_name,
                                               const char* symbol_name,
                                               void* symbol_address);

using InlineHookAddressInvoker = NookStatus (*)(void* target_address,
                                                void* replacement,
                                                void** original,
                                                void** hook_handle);
using InlineHookUnhookInvoker = NookStatus (*)(void* hook_handle);
using EnsureInlineHookModuleObserverAsyncFn = NookStatus (*)();

using InlineHookReplacementFunction = uint64_t (*)(uint64_t,
                                                   uint64_t,
                                                   uint64_t,
                                                   uint64_t,
                                                   uint64_t,
                                                   uint64_t,
                                                   uint64_t,
                                                   uint64_t);

constexpr size_t kMaxNativeJsInlineHookSlots = 16u;
constexpr size_t kMaxCStringSnapshotLength = 256u;

struct NativeJsHookRegistryState {
    std::mutex mutex;
    uint32_t next_hook_id = 1;
    std::unordered_map<uint32_t, NativeJsHookRecord> records;
};

struct NativeJsPendingHookRegistryState {
    std::mutex mutex;
    std::unordered_map<uint32_t, NativeJsPendingHookRecord> records;
};

struct NativeJsInlineHookSlotState {
    bool in_use = false;
    uint32_t hook_id = 0;
    bool blocking = true;
    uint64_t target_address = 0u;
    void* original_function = nullptr;
    void* native_hook_handle = nullptr;
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
};

struct NativeJsInlineHookRuntimeSnapshot {
    bool in_use = false;
    uint32_t hook_id = 0;
    bool blocking = true;
    uint64_t target_address = 0u;
    void* original_function = nullptr;
    const std::vector<NativeJsArgumentSnapshotRequest>* snapshots = nullptr;
};

struct NativeJsHookEventQueueState {
    std::mutex mutex;
    std::deque<HookEvent> pending_events;
    NativeJsHookEventNotifier notifier = nullptr;
};

struct NativeJsHookStatusEventQueueState {
    std::mutex mutex;
    std::deque<NativeJsHookStatusEvent> pending_events;
};

struct NativeJsHookInvocationState {
    struct PendingInvocationRecord {
        bool enter_completed = false;
        HookInvocationMutationResult enter_result = {};
        bool leave_completed = false;
        HookInvocationMutationResult leave_result = {};
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<uint64_t, PendingInvocationRecord> records;
};

std::atomic<uint64_t> g_next_native_js_invocation_id{1u};

struct NativeJsInlineHookThreadState {
    uint32_t dispatch_depth = 0u;
    uint32_t ignore_level = 0u;
};

#if defined(_WIN32)
thread_local NativeJsInlineHookThreadState g_native_js_inline_hook_thread_state = {};

NativeJsInlineHookThreadState& GetNativeJsInlineHookThreadState() {
    return g_native_js_inline_hook_thread_state;
}
#else
pthread_key_t& GetNativeJsInlineHookThreadStateKey() {
    static pthread_key_t key = 0;
    return key;
}

pthread_once_t& GetNativeJsInlineHookThreadStateOnce() {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    return once;
}

void DestroyNativeJsInlineHookThreadState(void* value) {
    delete static_cast<NativeJsInlineHookThreadState*>(value);
}

void InitNativeJsInlineHookThreadStateKey() {
    (void)pthread_key_create(&GetNativeJsInlineHookThreadStateKey(),
                             &DestroyNativeJsInlineHookThreadState);
}

NativeJsInlineHookThreadState& GetNativeJsInlineHookThreadState() {
    (void)pthread_once(&GetNativeJsInlineHookThreadStateOnce(),
                       &InitNativeJsInlineHookThreadStateKey);
    void* value = pthread_getspecific(GetNativeJsInlineHookThreadStateKey());
    if (value == nullptr) {
        auto* state = new NativeJsInlineHookThreadState();
        (void)pthread_setspecific(GetNativeJsInlineHookThreadStateKey(), state);
        value = state;
    }
    return *static_cast<NativeJsInlineHookThreadState*>(value);
}
#endif

struct NativeJsInlineHookBridgeState {
    std::mutex mutex;
    ResolveLoadedSymbolAddressFn resolve_loaded_symbol_address = nullptr;
    ResolveLoadedSymbolAddressFn resolve_symbol_address = nullptr;
    InlineHookSymbolSafetyChecker inline_hook_symbol_safety_checker = nullptr;
    InlineHookAddressInvoker inline_hook_address_invoker = nullptr;
    InlineHookUnhookInvoker inline_hook_unhook_invoker = nullptr;
    EnsureInlineHookModuleObserverAsyncFn ensure_inline_hook_module_observer_async = nullptr;
    std::array<NativeJsInlineHookSlotState, kMaxNativeJsInlineHookSlots> slots = {};
    std::array<std::atomic<bool>, kMaxNativeJsInlineHookSlots> slot_runtime_in_use = {};
    std::array<NativeJsInlineHookRuntimeSnapshot, kMaxNativeJsInlineHookSlots> slot_runtime_snapshots = {};
};

struct NativeJsInlineHookPerfState {
    std::atomic<uint64_t> dispatch_count{0u};
    std::atomic<uint64_t> enter_callback_ns_total{0u};
    std::atomic<uint64_t> leave_callback_ns_total{0u};
    std::atomic<uint64_t> original_call_ns_total{0u};
    std::atomic<uint64_t> total_dispatch_ns_total{0u};
};

NativeJsHookRegistryState& GetRegistryState() {
    static NativeJsHookRegistryState state;
    return state;
}

NativeJsInlineHookBridgeState& GetInlineHookBridgeState() {
    static NativeJsInlineHookBridgeState state;
    return state;
}

NativeJsPendingHookRegistryState& GetPendingHookRegistryState() {
    static NativeJsPendingHookRegistryState state;
    return state;
}

NativeJsHookEventQueueState& GetEventQueueState() {
    static NativeJsHookEventQueueState state;
    return state;
}

NativeJsHookStatusEventQueueState& GetStatusEventQueueState() {
    static NativeJsHookStatusEventQueueState state;
    return state;
}

NativeJsHookInvocationState& GetInvocationState() {
    static NativeJsHookInvocationState state;
    return state;
}

NativeJsInlineHookPerfState& GetInlineHookPerfState() {
    static NativeJsInlineHookPerfState state;
    return state;
}

void SetError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

void NotifyHookEventConsumer() {
    NativeJsHookEventNotifier notifier = nullptr;
    {
        NativeJsHookEventQueueState& state = GetEventQueueState();
        std::lock_guard<std::mutex> lock(state.mutex);
        notifier = state.notifier;
    }
    if (notifier != nullptr) {
        notifier();
    }
}

void EnqueueNativeJsHookStatusEvent(uint32_t hook_id,
                                    NativeJsHookStatusState status,
                                    const std::string& module_name,
                                    const std::string& symbol_name,
                                    const std::string& error_message) {
    if (hook_id == 0u) {
        return;
    }

    NativeJsHookStatusEventQueueState& state = GetStatusEventQueueState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        NativeJsHookStatusEvent event = {};
        event.hook_id = hook_id;
        event.state = status;
        event.module_name = module_name;
        event.symbol_name = symbol_name;
        event.error_message = error_message;
        state.pending_events.push_back(std::move(event));
    }
    NOOK_NATIVE_JS_BRIDGE_LOGI("native hook status enqueued hook_id=%u state=%d module=%s symbol=%s error=%s",
                               hook_id,
                               static_cast<int>(status),
                               module_name.c_str(),
                               symbol_name.c_str(),
                               error_message.empty() ? "" : error_message.c_str());
    NotifyHookEventConsumer();
}

bool AddOverflows(uintptr_t value, size_t offset, uintptr_t* result) {
    if (result == nullptr) {
        return true;
    }
    const uintptr_t max_value = static_cast<uintptr_t>(~static_cast<uintptr_t>(0));
    if (offset > static_cast<size_t>(max_value - value)) {
        return true;
    }
    *result = value + static_cast<uintptr_t>(offset);
    return false;
}

void PushNativeJsInlineHookIgnore() {
    ++GetNativeJsInlineHookThreadState().ignore_level;
}

void PopNativeJsInlineHookIgnore() {
    NativeJsInlineHookThreadState& state = GetNativeJsInlineHookThreadState();
    if (state.ignore_level > 0u) {
        --state.ignore_level;
    }
}

struct ScopedNativeJsInlineHookIgnore {
    ScopedNativeJsInlineHookIgnore() {
        PushNativeJsInlineHookIgnore();
    }
    ~ScopedNativeJsInlineHookIgnore() {
        PopNativeJsInlineHookIgnore();
    }
};

bool IsNativeJsInlineHookIgnoredOnCurrentThread() {
    return GetNativeJsInlineHookThreadState().ignore_level > 0u;
}

uintptr_t NormalizeProcessAddressForRangeCheck(uintptr_t address) {
    if (sizeof(uintptr_t) >= sizeof(uint64_t)) {
        return static_cast<uintptr_t>(static_cast<uint64_t>(address) & 0x00FFFFFFFFFFFFFFull);
    }
    return address;
}

#if defined(_WIN32)
bool IsReadableProtection(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    const DWORD base_protect = protect & 0xffu;
    return base_protect == PAGE_READONLY ||
           base_protect == PAGE_READWRITE ||
           base_protect == PAGE_WRITECOPY ||
           base_protect == PAGE_EXECUTE_READ ||
           base_protect == PAGE_EXECUTE_READWRITE ||
           base_protect == PAGE_EXECUTE_WRITECOPY;
}

bool FindReadableMappingEnd(uintptr_t address, uintptr_t* mapping_end) {
    if (mapping_end == nullptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
        return false;
    }

    const uintptr_t region_start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    uintptr_t region_end = 0;
    if (AddOverflows(region_start, info.RegionSize, &region_end) || region_end <= address) {
        return false;
    }
    *mapping_end = region_end;
    return true;
}

bool IsReadableMemoryRange(uintptr_t address, size_t length) {
    address = NormalizeProcessAddressForRangeCheck(address);
    if (address == 0) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    uintptr_t end = 0;
    if (AddOverflows(address, length, &end)) {
        return false;
    }

    uintptr_t current = address;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) == 0) {
            return false;
        }
        if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
            return false;
        }

        const uintptr_t region_start = reinterpret_cast<uintptr_t>(info.BaseAddress);
        uintptr_t region_end = 0;
        if (AddOverflows(region_start, info.RegionSize, &region_end) || region_end <= current) {
            return false;
        }
        current = region_end < end ? region_end : end;
    }
    return true;
}
#else
bool FindReadableMappingEnd(uintptr_t address, uintptr_t* mapping_end) {
    if (mapping_end == nullptr) {
        return false;
    }

    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return false;
    }

    char line[512] = {};
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5] = {};
        if (std::sscanf(line, "%llx-%llx %4s", &start, &end, permissions) != 3) {
            continue;
        }
        if (permissions[0] != 'r') {
            continue;
        }
        if (static_cast<unsigned long long>(address) >= start &&
            static_cast<unsigned long long>(address) < end) {
            std::fclose(maps);
            *mapping_end = static_cast<uintptr_t>(end);
            return true;
        }
    }

    std::fclose(maps);
    return false;
}

bool IsReadableMemoryRange(uintptr_t address, size_t length) {
    address = NormalizeProcessAddressForRangeCheck(address);
    if (address == 0) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    uintptr_t end = 0;
    if (AddOverflows(address, length, &end)) {
        return false;
    }

    uintptr_t current = address;
    while (current < end) {
        uintptr_t mapping_end = 0;
        if (!FindReadableMappingEnd(current, &mapping_end) || mapping_end <= current) {
            return false;
        }
        current = mapping_end < end ? mapping_end : end;
    }
    return true;
}
#endif

bool ReadUtf8StringFromReadableMemory(uintptr_t address,
                                      size_t max_length,
                                      std::string* utf8_out) {
    if (utf8_out == nullptr) {
        return false;
    }
    utf8_out->clear();

    address = NormalizeProcessAddressForRangeCheck(address);
    if (address == 0) {
        return false;
    }
    if (max_length == 0u) {
        return true;
    }

    uintptr_t current = address;
    uintptr_t mapping_end = 0;
    if (!FindReadableMappingEnd(current, &mapping_end) || mapping_end <= current) {
        return false;
    }

    utf8_out->reserve(max_length);
    for (size_t index = 0; index < max_length; ++index) {
        if (current >= mapping_end) {
            if (!FindReadableMappingEnd(current, &mapping_end) || mapping_end <= current) {
                return false;
            }
        }

        const char ch = *(reinterpret_cast<const char*>(current));
        if (ch == '\0') {
            return true;
        }
        utf8_out->push_back(ch);

        uintptr_t next = 0;
        if (AddOverflows(current, 1u, &next)) {
            return false;
        }
        current = next;
    }

    return true;
}

bool IsModuleLoadedByName(const std::string& module_name) {
    if (module_name.empty()) {
        return false;
    }
#if defined(__ANDROID__) || defined(__linux__)
    void* module_base = nullptr;
    return ElfHooker::get_module_info(0, module_name.c_str(), &module_base, nullptr) &&
           module_base != nullptr;
#else
    return false;
#endif
}

void RegisterPendingInvocation(uint64_t invocation_id) {
    if (invocation_id == 0u) {
        return;
    }
    NativeJsHookInvocationState& state = GetInvocationState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.records.erase(invocation_id);
    state.records.emplace(invocation_id, NativeJsHookInvocationState::PendingInvocationRecord{});
}

bool WaitForInvocationCompletion(uint64_t invocation_id,
                                 HookEventPhase phase,
                                 HookInvocationMutationResult* result_out,
                                 uint32_t timeout_ms) {
    if (invocation_id == 0u) {
        return false;
    }
    NativeJsHookInvocationState& state = GetInvocationState();
    std::unique_lock<std::mutex> lock(state.mutex);
    auto predicate = [&]() {
        const auto it = state.records.find(invocation_id);
        if (it == state.records.end()) {
            return true;
        }
        return phase == HookEventPhase::kEnter ? it->second.enter_completed
                                               : it->second.leave_completed;
    };
    if (!state.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate)) {
        if (phase == HookEventPhase::kLeave) {
            state.records.erase(invocation_id);
        }
        return false;
    }

    const auto it = state.records.find(invocation_id);
    if (it == state.records.end()) {
        return false;
    }
    if (result_out != nullptr) {
        *result_out = phase == HookEventPhase::kEnter ? it->second.enter_result
                                                      : it->second.leave_result;
    }
    if (phase == HookEventPhase::kLeave) {
        state.records.erase(it);
    }
    return true;
}

uint32_t GetCurrentNativeThreadId() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentThreadId());
#elif defined(__ANDROID__) || defined(__linux__)
    return static_cast<uint32_t>(::syscall(SYS_gettid));
#else
    return 0u;
#endif
}

uint64_t GetCurrentReturnAddress() {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
#else
    return 0u;
#endif
}

uint64_t GetCurrentFramePointer() {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
#else
    return 0u;
#endif
}

uint64_t GetCurrentStackPointer() {
#if defined(__aarch64__)
    uintptr_t value = 0u;
    asm volatile("mov %0, sp" : "=r"(value));
    return static_cast<uint64_t>(value);
#else
    return 0u;
#endif
}

uint64_t GetCurrentLinkRegister() {
#if defined(__aarch64__)
    uintptr_t value = 0u;
    asm volatile("mov %0, x30" : "=r"(value));
    return static_cast<uint64_t>(value);
#else
    return GetCurrentReturnAddress();
#endif
}

bool MatchesModulePath(const char* module_path, const char* module_name) {
    if (module_path == nullptr || module_name == nullptr) {
        return false;
    }
    const size_t module_path_length = std::strlen(module_path);
    const size_t module_name_length = std::strlen(module_name);
    if (module_name_length == 0 || module_path_length < module_name_length) {
        return false;
    }
    return std::memcmp(module_path + module_path_length - module_name_length,
                       module_name,
                       module_name_length) == 0;
}

const char* GetStatusString(NookStatus status) {
    switch (status) {
        case NOOK_STATUS_OK:
            return "ok";
        case NOOK_STATUS_NOT_IMPLEMENTED:
            return "not implemented";
        case NOOK_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case NOOK_STATUS_INTERNAL_ERROR:
        default:
            return "internal error";
    }
}

ResolveLoadedSymbolAddressFn ResolveLoadedSymbolAddress() {
#if defined(__ANDROID__) || defined(__linux__)
    return &NookNativeHookInternal::ResolveSymbolAddressInLoadedModule;
#else
    return nullptr;
#endif
}

ResolveLoadedSymbolAddressFn ResolveSymbolAddressFallback() {
#if defined(__ANDROID__) || defined(__linux__)
    return &NookNativeHookInternal::ResolveSymbolAddress;
#else
    return nullptr;
#endif
}

InlineHookSymbolSafetyChecker ResolveInlineHookSymbolSafetyChecker() {
#if defined(__ANDROID__) || defined(__linux__)
    return &NookNativeHookInternal::IsSymbolInlineHookSafeInLoadedModule;
#else
    return nullptr;
#endif
}

bool IsResolvedInlineHookSymbolSafe(const char* module_name,
                                    const char* symbol_name,
                                    void* symbol_address) {
    InlineHookSymbolSafetyChecker checker = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        checker = state.inline_hook_symbol_safety_checker;
    }
    if (checker == nullptr) {
        checker = ResolveInlineHookSymbolSafetyChecker();
    }
    return checker == nullptr || checker(module_name, symbol_name, symbol_address);
}

bool TryResolveInlineHookTargetAddress(const char* module_name,
                                       const char* symbol_name,
                                       void** target_address,
                                       bool* used_fallback) {
    if (target_address == nullptr) {
        return false;
    }
    *target_address = nullptr;
    if (used_fallback != nullptr) {
        *used_fallback = false;
    }

    ResolveLoadedSymbolAddressFn loaded_resolver = nullptr;
    ResolveLoadedSymbolAddressFn fallback_resolver = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        loaded_resolver = state.resolve_loaded_symbol_address;
        fallback_resolver = state.resolve_symbol_address;
    }
    if (loaded_resolver == nullptr) {
        loaded_resolver = ResolveLoadedSymbolAddress();
    }
    if (fallback_resolver == nullptr) {
        fallback_resolver = ResolveSymbolAddressFallback();
    }

    if (loaded_resolver != nullptr &&
        loaded_resolver(module_name, symbol_name, target_address) &&
        *target_address != nullptr) {
        return true;
    }

    if (fallback_resolver != nullptr &&
        fallback_resolver(module_name, symbol_name, target_address) &&
        *target_address != nullptr) {
        if (used_fallback != nullptr) {
            *used_fallback = true;
        }
        return true;
    }

    *target_address = nullptr;
    return false;
}

InlineHookAddressInvoker ResolveInlineHookAddressInvoker() {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<InlineHookAddressInvoker>(
            GetProcAddress(module, "NookInlineHookAddress"));
#else
    return reinterpret_cast<InlineHookAddressInvoker>(
            dlsym(RTLD_DEFAULT, "NookInlineHookAddress"));
#endif
}

InlineHookUnhookInvoker ResolveInlineHookUnhookInvoker() {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<InlineHookUnhookInvoker>(
            GetProcAddress(module, "NookInlineUnhook"));
#else
    return reinterpret_cast<InlineHookUnhookInvoker>(
            dlsym(RTLD_DEFAULT, "NookInlineUnhook"));
#endif
}

EnsureInlineHookModuleObserverAsyncFn ResolveEnsureInlineHookModuleObserverAsync() {
#if defined(__ANDROID__)
    return &NookInlineHookInternal::EnsureInlineHookModuleObserverAsync;
#else
    return nullptr;
#endif
}

bool FillHookEventArguments(HookEvent* event,
                            uint64_t x0,
                            uint64_t x1,
                            uint64_t x2,
                            uint64_t x3,
                            uint64_t x4,
                            uint64_t x5,
                            uint64_t x6,
                            uint64_t x7) {
    if (event == nullptr) {
        return false;
    }
    event->argument_count = 8u;
    event->argument_values[0] = x0;
    event->argument_values[1] = x1;
    event->argument_values[2] = x2;
    event->argument_values[3] = x3;
    event->argument_values[4] = x4;
    event->argument_values[5] = x5;
    event->argument_values[6] = x6;
    event->argument_values[7] = x7;
    return true;
}

bool ReserveInlineHookSlot(uint32_t hook_id, size_t* slot_index, std::string* error_message) {
    if (slot_index == nullptr) {
        SetError(error_message, "slot_index is null");
        return false;
    }

    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (size_t index = 0; index < state.slots.size(); ++index) {
        if (state.slots[index].in_use) {
            continue;
        }
        state.slots[index] = {};
        state.slots[index].in_use = true;
        state.slots[index].hook_id = hook_id;
        state.slot_runtime_snapshots[index] = {};
        state.slot_runtime_in_use[index].store(false, std::memory_order_release);
        *slot_index = index;
        return true;
    }

    SetError(error_message, "no free native js inline hook slots");
    return false;
}

void ReleaseInlineHookSlot(size_t slot_index) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (slot_index >= state.slots.size()) {
        return;
    }
    state.slot_runtime_in_use[slot_index].store(false, std::memory_order_release);
    state.slot_runtime_snapshots[slot_index] = {};
    state.slots[slot_index] = {};
}

bool ActivateInlineHookSlot(size_t slot_index,
                            const NativeJsHookRequest& request,
                            uint64_t resolved_target_address,
                            void* original_function,
                            void* native_hook_handle,
                            std::string* error_message) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (slot_index >= state.slots.size() || !state.slots[slot_index].in_use) {
        SetError(error_message, "inline hook slot not reserved");
        return false;
    }
    state.slots[slot_index].blocking = request.blocking;
    state.slots[slot_index].target_address =
        resolved_target_address != 0u ? resolved_target_address : request.target_address;
    state.slots[slot_index].original_function = original_function;
    state.slots[slot_index].native_hook_handle = native_hook_handle;
    state.slots[slot_index].snapshots = request.snapshots;
    NativeJsInlineHookRuntimeSnapshot runtime_snapshot = {};
    runtime_snapshot.in_use = true;
    runtime_snapshot.hook_id = state.slots[slot_index].hook_id;
    runtime_snapshot.blocking = state.slots[slot_index].blocking;
    runtime_snapshot.target_address = state.slots[slot_index].target_address;
    runtime_snapshot.original_function = state.slots[slot_index].original_function;
    runtime_snapshot.snapshots = &state.slots[slot_index].snapshots;
    state.slot_runtime_snapshots[slot_index] = runtime_snapshot;
    state.slot_runtime_in_use[slot_index].store(true, std::memory_order_release);
    return true;
}

bool StorePendingHookRecord(uint32_t hook_id,
                            const NativeJsHookRequest& request,
                            size_t slot_index,
                            std::string* error_message) {
    if (hook_id == 0) {
        SetError(error_message, "hook_id is required");
        return false;
    }

    NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    NativeJsPendingHookRecord record = {};
    record.hook_id = hook_id;
    record.module_name = request.module_name;
    record.symbol_name = request.symbol_name;
    record.slot_index = slot_index;
    record.native_hook_handle = nullptr;
    record.installed = false;
    state.records[hook_id] = record;
    return true;
}

void ErasePendingHookRecord(uint32_t hook_id) {
    NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.records.erase(hook_id);
}

bool GetInlineHookSlotSnapshot(size_t slot_index, NativeJsInlineHookSlotState* slot_state) {
    if (slot_state == nullptr) {
        return false;
    }
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (slot_index >= state.slots.size() || !state.slots[slot_index].in_use) {
        return false;
    }
    *slot_state = state.slots[slot_index];
    return true;
}

bool GetInlineHookRuntimeSnapshot(size_t slot_index, NativeJsInlineHookRuntimeSnapshot* slot_state) {
    if (slot_state == nullptr) {
        return false;
    }
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    if (slot_index >= state.slot_runtime_snapshots.size()) {
        return false;
    }
    if (!state.slot_runtime_in_use[slot_index].load(std::memory_order_acquire)) {
        return false;
    }
    *slot_state = state.slot_runtime_snapshots[slot_index];
    if (!slot_state->in_use) {
        return false;
    }
    return true;
}

bool FindInlineHookSlotIndexByHookId(uint32_t hook_id, size_t* slot_index) {
    if (hook_id == 0 || slot_index == nullptr) {
        return false;
    }
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (size_t index = 0; index < state.slots.size(); ++index) {
        if (!state.slots[index].in_use) {
            continue;
        }
        if (state.slots[index].hook_id != hook_id) {
            continue;
        }
        *slot_index = index;
        return true;
    }
    return false;
}

bool GetNativeJsHookRecordSnapshot(uint32_t hook_id, NativeJsHookRecord* out_record) {
    if (hook_id == 0 || out_record == nullptr) {
        return false;
    }

    NativeJsHookRegistryState& state = GetRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto it = state.records.find(hook_id);
    if (it == state.records.end()) {
        return false;
    }
    *out_record = it->second;
    return true;
}

void RemoveQueuedHookEvents(uint32_t hook_id) {
    if (hook_id == 0) {
        return;
    }
    NativeJsHookEventQueueState& state = GetEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.pending_events.begin();
    while (it != state.pending_events.end()) {
        if (it->hook_id == hook_id) {
            it = state.pending_events.erase(it);
            continue;
        }
        ++it;
    }
}

bool AppendUtf8Snapshot(HookEvent* event,
                        uint32_t argument_index,
                        const char* property_name,
                        const std::string& utf8,
                        std::string* error_message) {
    if (event == nullptr) {
        SetError(error_message, "event is null");
        return false;
    }
    if (event->jni_utf8_snapshot_count >= event->jni_utf8_snapshots.size()) {
        SetError(error_message, "too many jni utf8 snapshots");
        return false;
    }

    HookEvent::JniUtf8ArgumentSnapshot& snapshot =
        event->jni_utf8_snapshots[event->jni_utf8_snapshot_count++];
    snapshot.argument_index = argument_index;
    snapshot.property_name = property_name != nullptr ? property_name : "";
    snapshot.utf8 = utf8;
    return true;
}

#if defined(__ANDROID__)
bool ReadJniStringUtf8OnHookThread(JNIEnv* env,
                                   jstring value,
                                   std::string* utf8_out,
                                   std::string* error_message) {
    if (utf8_out == nullptr) {
        SetError(error_message, "utf8_out is null");
        return false;
    }
    utf8_out->clear();
    if (env == nullptr || value == nullptr) {
        SetError(error_message, "JNI env and jstring must be non-null");
        return false;
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "GetStringUTFChars failed");
        return false;
    }

    *utf8_out = chars;
    env->ReleaseStringUTFChars(value, chars);
    return true;
}
#endif

bool ReadCStringUtf8OnHookThread(const char* value,
                                 std::string* utf8_out,
                                 std::string* error_message) {
    if (utf8_out == nullptr) {
        SetError(error_message, "utf8_out is null");
        return false;
    }
    utf8_out->clear();
    if (value == nullptr) {
        SetError(error_message, "cstring must be non-null");
        return false;
    }

    if (!ReadUtf8StringFromReadableMemory(reinterpret_cast<uintptr_t>(value),
                                          kMaxCStringSnapshotLength,
                                          utf8_out)) {
        SetError(error_message, "cstring unreadable");
        return false;
    }
    return true;
}

void MaybeCaptureJniUtf8Snapshots(const NativeJsHookRecord& record,
                                  uint64_t x0,
                                  uint64_t x1,
                                  uint64_t x2,
                                  uint64_t x3,
                                  uint64_t x4,
                                  uint64_t x5,
                                  uint64_t x6,
                                  uint64_t x7,
                                  HookEvent* event) {
    if (event == nullptr || event->phase != HookEventPhase::kEnter) {
        return;
    }

#if defined(__ANDROID__)
    if (record.snapshots.empty()) {
        return;
    }

    const uint64_t args[8] = {x0, x1, x2, x3, x4, x5, x6, x7};
    for (const NativeJsArgumentSnapshotRequest& snapshot_request : record.snapshots) {
        if (snapshot_request.argument_index >= 8u ||
            snapshot_request.env_index >= 8u) {
            continue;
        }
        if (snapshot_request.type == "jstringUtf8") {
            JNIEnv* env = reinterpret_cast<JNIEnv*>(static_cast<uintptr_t>(args[snapshot_request.env_index]));
            jstring value = reinterpret_cast<jstring>(static_cast<uintptr_t>(args[snapshot_request.argument_index]));
            std::string utf8;
            if (!ReadJniStringUtf8OnHookThread(env, value, &utf8, nullptr)) {
                continue;
            }
            (void)AppendUtf8Snapshot(event,
                                     snapshot_request.argument_index,
                                     "$jniUtf8",
                                     utf8,
                                     nullptr);
            continue;
        }
        if (snapshot_request.type == "cstringUtf8") {
            const char* value =
                reinterpret_cast<const char*>(static_cast<uintptr_t>(args[snapshot_request.argument_index]));
            std::string utf8;
            if (!ReadCStringUtf8OnHookThread(value, &utf8, nullptr)) {
                continue;
            }
            (void)AppendUtf8Snapshot(event,
                                     snapshot_request.argument_index,
                                     "$utf8",
                                     utf8,
                                     nullptr);
        }
    }
#else
    (void)record;
    (void)x0;
    (void)x1;
    (void)x2;
    (void)x3;
    (void)x4;
    (void)x5;
    (void)x6;
    (void)x7;
    (void)event;
#endif
}

uint64_t DispatchInlineHookSlot(size_t slot_index,
                                uint64_t x0,
                                uint64_t x1,
                                uint64_t x2,
                                uint64_t x3,
                                uint64_t x4,
                                uint64_t x5,
                                uint64_t x6,
                                uint64_t x7) {
    NativeJsInlineHookRuntimeSnapshot slot = {};
    if (!GetInlineHookRuntimeSnapshot(slot_index, &slot)) {
        return 0;
    }

    if (GetNativeJsInlineHookThreadState().dispatch_depth > 0u ||
        IsNativeJsInlineHookIgnoredOnCurrentThread()) {
        if (slot.original_function != nullptr) {
            const auto original =
                reinterpret_cast<InlineHookReplacementFunction>(slot.original_function);
            return original(x0, x1, x2, x3, x4, x5, x6, x7);
        }
        return 0;
    }

    struct DispatchGuard {
        DispatchGuard() {
            ++GetNativeJsInlineHookThreadState().dispatch_depth;
        }
        ~DispatchGuard() {
            NativeJsInlineHookThreadState& state = GetNativeJsInlineHookThreadState();
            if (state.dispatch_depth > 0u) {
                --state.dispatch_depth;
            }
        }
    } dispatch_guard;

    HookEvent enter_event = {};
    const auto dispatch_started_at = std::chrono::steady_clock::now();
    enter_event.hook_id = slot.hook_id;
    enter_event.invocation_id = g_next_native_js_invocation_id.fetch_add(1u);
    enter_event.phase = HookEventPhase::kEnter;
    FillHookEventArguments(&enter_event, x0, x1, x2, x3, x4, x5, x6, x7);
    enter_event.thread_id = GetCurrentNativeThreadId();
    enter_event.return_address = GetCurrentReturnAddress();
    enter_event.stack_pointer = GetCurrentStackPointer();
    enter_event.frame_pointer = GetCurrentFramePointer();
    enter_event.link_register = GetCurrentLinkRegister();
    NativeJsHookRecord hook_record = {};
    hook_record.hook_id = slot.hook_id;
    hook_record.target_address = slot.target_address;
    hook_record.blocking = slot.blocking;
    if (slot.snapshots != nullptr) {
        hook_record.snapshots = *slot.snapshots;
    }
    enter_event.program_counter = slot.target_address;
    MaybeCaptureJniUtf8Snapshots(hook_record, x0, x1, x2, x3, x4, x5, x6, x7, &enter_event);
    uint64_t enter_callback_ns = 0u;
    if (slot.blocking) {
        HookInvocationMutationResult enter_result = {};
        std::string invoke_error;
        const auto enter_started_at = std::chrono::steady_clock::now();
        ScopedNativeJsInlineHookIgnore ignore_scope;
        if (JsRuntime::InvokeNativeHookCallbackSync(enter_event, &enter_result, &invoke_error)) {
            if (enter_result.argument_overrides[0]) x0 = enter_result.argument_values[0];
            if (enter_result.argument_overrides[1]) x1 = enter_result.argument_values[1];
            if (enter_result.argument_overrides[2]) x2 = enter_result.argument_values[2];
            if (enter_result.argument_overrides[3]) x3 = enter_result.argument_values[3];
            if (enter_result.argument_overrides[4]) x4 = enter_result.argument_values[4];
            if (enter_result.argument_overrides[5]) x5 = enter_result.argument_values[5];
            if (enter_result.argument_overrides[6]) x6 = enter_result.argument_values[6];
            if (enter_result.argument_overrides[7]) x7 = enter_result.argument_values[7];
        } else if (!invoke_error.empty()) {
            NOOK_NATIVE_JS_BRIDGE_LOGI("native hook sync enter callback failed hook_id=%u error=%s",
                                       enter_event.hook_id,
                                       invoke_error.c_str());
        }
        enter_callback_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - enter_started_at).count());
    } else {
        EnqueueNativeJsHookEvent(enter_event, nullptr);
    }

    uint64_t return_value = 0;
    const auto original_started_at = std::chrono::steady_clock::now();
    if (slot.original_function != nullptr) {
        const auto original =
                reinterpret_cast<InlineHookReplacementFunction>(slot.original_function);
        return_value = original(x0, x1, x2, x3, x4, x5, x6, x7);
    }
    const uint64_t original_call_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - original_started_at).count());

    HookEvent leave_event = {};
    leave_event.hook_id = slot.hook_id;
    leave_event.invocation_id = enter_event.invocation_id;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.thread_id = enter_event.thread_id;
    leave_event.return_address = enter_event.return_address;
    FillHookEventArguments(&leave_event, x0, x1, x2, x3, x4, x5, x6, x7);
    leave_event.stack_pointer = enter_event.stack_pointer;
    leave_event.frame_pointer = enter_event.frame_pointer;
    leave_event.link_register = enter_event.link_register;
    leave_event.program_counter = enter_event.program_counter;
    leave_event.return_value = return_value;
    uint64_t leave_callback_ns = 0u;
    if (slot.blocking) {
        HookInvocationMutationResult leave_result = {};
        std::string invoke_error;
        const auto leave_started_at = std::chrono::steady_clock::now();
        ScopedNativeJsInlineHookIgnore ignore_scope;
        if (JsRuntime::InvokeNativeHookCallbackSync(leave_event, &leave_result, &invoke_error)) {
            if (leave_result.has_return_value_override) {
                return_value = leave_result.return_value;
            }
        } else if (!invoke_error.empty()) {
            NOOK_NATIVE_JS_BRIDGE_LOGI("native hook sync leave callback failed hook_id=%u error=%s",
                                       leave_event.hook_id,
                                       invoke_error.c_str());
        }
        leave_callback_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - leave_started_at).count());
    } else {
        EnqueueNativeJsHookEvent(leave_event, nullptr);
    }

    const uint64_t total_dispatch_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - dispatch_started_at).count());
    NativeJsInlineHookPerfState& perf = GetInlineHookPerfState();
    const uint64_t dispatch_count =
        perf.dispatch_count.fetch_add(1u, std::memory_order_relaxed) + 1u;
    perf.enter_callback_ns_total.fetch_add(enter_callback_ns, std::memory_order_relaxed);
    perf.leave_callback_ns_total.fetch_add(leave_callback_ns, std::memory_order_relaxed);
    perf.original_call_ns_total.fetch_add(original_call_ns, std::memory_order_relaxed);
    perf.total_dispatch_ns_total.fetch_add(total_dispatch_ns, std::memory_order_relaxed);
    if ((dispatch_count % 256u) == 0u) {
        const uint64_t enter_total =
            perf.enter_callback_ns_total.load(std::memory_order_relaxed);
        const uint64_t leave_total =
            perf.leave_callback_ns_total.load(std::memory_order_relaxed);
        const uint64_t original_total =
            perf.original_call_ns_total.load(std::memory_order_relaxed);
        const uint64_t dispatch_total =
            perf.total_dispatch_ns_total.load(std::memory_order_relaxed);
        NOOK_NATIVE_JS_BRIDGE_LOGI(
            "native hook perf hook_id=%u count=%llu avg_enter_us=%llu avg_leave_us=%llu avg_original_us=%llu avg_total_us=%llu",
            slot.hook_id,
            static_cast<unsigned long long>(dispatch_count),
            static_cast<unsigned long long>(enter_total / dispatch_count / 1000u),
            static_cast<unsigned long long>(leave_total / dispatch_count / 1000u),
            static_cast<unsigned long long>(original_total / dispatch_count / 1000u),
            static_cast<unsigned long long>(dispatch_total / dispatch_count / 1000u));
    }
    return return_value;
}

template <size_t SlotIndex>
uint64_t InlineHookReplacementEntry(uint64_t x0,
                                    uint64_t x1,
                                    uint64_t x2,
                                    uint64_t x3,
                                    uint64_t x4,
                                    uint64_t x5,
                                    uint64_t x6,
                                    uint64_t x7) {
    return DispatchInlineHookSlot(SlotIndex, x0, x1, x2, x3, x4, x5, x6, x7);
}

constexpr std::array<InlineHookReplacementFunction, kMaxNativeJsInlineHookSlots>
        kInlineHookReplacementEntries = {
                &InlineHookReplacementEntry<0>,  &InlineHookReplacementEntry<1>,
                &InlineHookReplacementEntry<2>,  &InlineHookReplacementEntry<3>,
                &InlineHookReplacementEntry<4>,  &InlineHookReplacementEntry<5>,
                &InlineHookReplacementEntry<6>,  &InlineHookReplacementEntry<7>,
                &InlineHookReplacementEntry<8>,  &InlineHookReplacementEntry<9>,
                &InlineHookReplacementEntry<10>, &InlineHookReplacementEntry<11>,
                &InlineHookReplacementEntry<12>, &InlineHookReplacementEntry<13>,
                &InlineHookReplacementEntry<14>, &InlineHookReplacementEntry<15>};

bool InstallInlineHookWithDefaultAdapter(const NativeJsHookRequest& request,
                                         void** hook_handle,
                                         bool* deferred,
                                         uint64_t* resolved_target_address,
                                         std::string* error_message) {
    if (hook_handle == nullptr) {
        SetError(error_message, "hook_handle is null");
        return false;
    }
    if (deferred == nullptr) {
        SetError(error_message, "deferred is null");
        return false;
    }
    if (request.hook_id == 0) {
        SetError(error_message, "hook_id is required");
        return false;
    }

    *deferred = false;
    if (resolved_target_address != nullptr) {
        *resolved_target_address = 0u;
    }

    size_t slot_index = 0u;
    if (!ReserveInlineHookSlot(request.hook_id, &slot_index, error_message)) {
        return false;
    }

    InlineHookAddressInvoker invoker = nullptr;
    EnsureInlineHookModuleObserverAsyncFn ensure_observer_async = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        invoker = state.inline_hook_address_invoker;
        ensure_observer_async = state.ensure_inline_hook_module_observer_async;
    }
    if (invoker == nullptr) {
        invoker = ResolveInlineHookAddressInvoker();
    }
    if (ensure_observer_async == nullptr) {
        ensure_observer_async = ResolveEnsureInlineHookModuleObserverAsync();
    }
    if (invoker == nullptr) {
        ReleaseInlineHookSlot(slot_index);
        SetError(error_message, "inline hook address invoker is null");
        return false;
    }

    if (request.has_target_address) {
        if (request.target_address == 0) {
            ReleaseInlineHookSlot(slot_index);
            SetError(error_message, "target address is required");
            return false;
        }

        void* original_function = nullptr;
        void* native_hook_handle = nullptr;
        const NookStatus status =
                invoker(reinterpret_cast<void*>(static_cast<uintptr_t>(request.target_address)),
                        reinterpret_cast<void*>(kInlineHookReplacementEntries[slot_index]),
                        &original_function,
                        &native_hook_handle);
        if (status != NOOK_STATUS_OK) {
            ReleaseInlineHookSlot(slot_index);
            if (error_message != nullptr && error_message->empty()) {
                std::ostringstream stream;
                stream << "inline hook install failed: " << GetStatusString(status);
                *error_message = stream.str();
            }
            return false;
        }

        if (!ActivateInlineHookSlot(slot_index,
                                    request,
                                    request.target_address,
                                    original_function,
                                    native_hook_handle,
                                    error_message)) {
            ReleaseInlineHookSlot(slot_index);
            return false;
        }

        if (resolved_target_address != nullptr) {
            *resolved_target_address = request.target_address;
        }
        *hook_handle = native_hook_handle;
        return true;
    }

    void* target_address = nullptr;
    bool used_fallback_resolver = false;
    if (!TryResolveInlineHookTargetAddress(request.module_name.c_str(),
                                           request.symbol_name.c_str(),
                                           &target_address,
                                           &used_fallback_resolver)) {
        if (!IsModuleLoadedByName(request.module_name)) {
            if (!StorePendingHookRecord(request.hook_id, request, slot_index, error_message)) {
                ReleaseInlineHookSlot(slot_index);
                return false;
            }
            if (ensure_observer_async != nullptr) {
                const NookStatus observer_status = ensure_observer_async();
                if (observer_status != NOOK_STATUS_OK) {
                    ErasePendingHookRecord(request.hook_id);
                    ReleaseInlineHookSlot(slot_index);
                    std::ostringstream stream;
                    stream << "inline hook module observer async schedule failed: "
                           << GetStatusString(observer_status);
                    SetError(error_message, stream.str());
                    return false;
                }
            }
            *hook_handle = nullptr;
            *deferred = true;
            EnqueueNativeJsHookStatusEvent(request.hook_id,
                                           NativeJsHookStatusState::kPending,
                                           request.module_name,
                                           request.symbol_name,
                                           "");
            return true;
        }
        ReleaseInlineHookSlot(slot_index);
        SetError(error_message, "failed to resolve loaded symbol address");
        return false;
    }
    NOOK_NATIVE_JS_BRIDGE_LOGI("native inline hook resolved hook_id=%u module=%s symbol=%s target=%p fallback=%d",
                               request.hook_id,
                               request.module_name.c_str(),
                               request.symbol_name.c_str(),
                               target_address,
                               used_fallback_resolver ? 1 : 0);
    if (!IsResolvedInlineHookSymbolSafe(request.module_name.c_str(),
                                        request.symbol_name.c_str(),
                                        target_address)) {
        ReleaseInlineHookSlot(slot_index);
        SetError(error_message, "unsafe inline hook target symbol");
        return false;
    }

    void* original_function = nullptr;
    void* native_hook_handle = nullptr;
    const NookStatus status = invoker(target_address,
                                      reinterpret_cast<void*>(kInlineHookReplacementEntries[slot_index]),
                                      &original_function,
                                      &native_hook_handle);
    if (status != NOOK_STATUS_OK) {
        ReleaseInlineHookSlot(slot_index);
        if (error_message != nullptr && error_message->empty()) {
            std::ostringstream stream;
            stream << "inline hook install failed: " << GetStatusString(status);
            *error_message = stream.str();
        }
        return false;
    }

    if (!ActivateInlineHookSlot(slot_index,
                                request,
                                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target_address)),
                                original_function,
                                native_hook_handle,
                                error_message)) {
        ReleaseInlineHookSlot(slot_index);
        return false;
    }

    if (resolved_target_address != nullptr) {
        *resolved_target_address =
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target_address));
    }
    *hook_handle = native_hook_handle;
    return true;
}

}  // namespace

bool InstallNativeJsHook(const NativeJsHookRequest& request,
                         const NativeJsHookInstallerDependencies& dependencies,
                         NativeJsHookRecord* out_record,
                         std::string* error_message) {
    if (out_record == nullptr) {
        SetError(error_message, "out_record is null");
        return false;
    }

    if (request.type != "inline") {
        SetError(error_message, "not implemented yet");
        return false;
    }

    uint32_t hook_id = 0;
    {
        NativeJsHookRegistryState& state = GetRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        hook_id = state.next_hook_id++;
    }

    void* hook_handle = nullptr;
    NativeJsHookRequest install_request = request;
    install_request.hook_id = hook_id;
    bool deferred = false;
    uint64_t resolved_target_address = 0u;
    if (dependencies.install_inline_hook != nullptr) {
        if (!dependencies.install_inline_hook(install_request, &hook_handle, error_message)) {
            if (error_message != nullptr && error_message->empty()) {
                *error_message = "inline hook install failed";
            }
            return false;
        }
    } else {
        if (!InstallInlineHookWithDefaultAdapter(install_request,
                                                &hook_handle,
                                                &deferred,
                                                &resolved_target_address,
                                                error_message)) {
            if (error_message != nullptr && error_message->empty()) {
                *error_message = "inline hook install failed";
            }
            return false;
        }
    }

    NativeJsHookRegistryState& state = GetRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);

    NativeJsHookRecord record = {};
    record.hook_id = hook_id;
    record.type = request.type;
    record.module_name = request.module_name;
    record.symbol_name = request.symbol_name;
    record.has_target_address = request.has_target_address;
    record.target_address = request.target_address;
    record.blocking = request.blocking;
    if (resolved_target_address != 0u) {
        record.target_address = resolved_target_address;
    }
    record.hook_handle = hook_handle;
    record.deferred = deferred;
    record.snapshots = request.snapshots;
    state.records[record.hook_id] = record;
    NOOK_NATIVE_JS_BRIDGE_LOGI("native hook record hook_id=%u module=%s symbol=%s target=%p blocking=%d deferred=%d",
                               record.hook_id,
                               record.module_name.empty() ? "(pointer)" : record.module_name.c_str(),
                               record.symbol_name.empty() ? "(pointer)" : record.symbol_name.c_str(),
                               reinterpret_cast<void*>(static_cast<uintptr_t>(record.target_address)),
                               record.blocking ? 1 : 0,
                               record.deferred ? 1 : 0);
    *out_record = record;
    return true;
}

bool FindNativeJsExportByName(const char* module_name,
                              const char* symbol_name,
                              uint64_t* target_address,
                              std::string* error_message) {
    if (target_address == nullptr) {
        SetError(error_message, "target_address is null");
        return false;
    }
    *target_address = 0;
    if (module_name == nullptr || module_name[0] == '\0') {
        SetError(error_message, "module_name is required");
        return false;
    }
    if (symbol_name == nullptr || symbol_name[0] == '\0') {
        SetError(error_message, "symbol_name is required");
        return false;
    }

    void* address = nullptr;
    bool used_fallback_resolver = false;
    if (!TryResolveInlineHookTargetAddress(module_name,
                                           symbol_name,
                                           &address,
                                           &used_fallback_resolver)) {
        *target_address = 0u;
        return true;
    }
    NOOK_NATIVE_JS_BRIDGE_LOGI("native export resolved module=%s symbol=%s target=%p fallback=%d",
                               module_name,
                               symbol_name,
                               address,
                               used_fallback_resolver ? 1 : 0);
    *target_address = reinterpret_cast<uint64_t>(address);
    return true;
}

bool UninstallNativeJsHook(uint32_t hook_id, std::string* error_message) {
    if (hook_id == 0) {
        SetError(error_message, "hook_id is required");
        return false;
    }

    NativeJsHookRecord record = {};
    {
        NativeJsHookRegistryState& state = GetRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const auto it = state.records.find(hook_id);
        if (it == state.records.end()) {
            SetError(error_message, "native js hook not found");
            return false;
        }
        record = it->second;
    }

    size_t slot_index = 0u;
    const bool has_slot = FindInlineHookSlotIndexByHookId(hook_id, &slot_index);
    if (record.hook_handle != nullptr) {
        InlineHookUnhookInvoker unhook_invoker = nullptr;
        {
            NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            unhook_invoker = state.inline_hook_unhook_invoker;
        }
        if (unhook_invoker == nullptr) {
            unhook_invoker = ResolveInlineHookUnhookInvoker();
        }
        if (unhook_invoker == nullptr) {
            SetError(error_message, "inline hook unhook invoker is null");
            return false;
        }
        const NookStatus status = unhook_invoker(record.hook_handle);
        if (status != NOOK_STATUS_OK) {
            std::ostringstream stream;
            stream << "inline hook unhook failed: " << GetStatusString(status);
            SetError(error_message, stream.str());
            return false;
        }
    }

    {
        NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.records.erase(hook_id);
    }
    {
        NativeJsHookRegistryState& state = GetRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.records.erase(hook_id);
    }
    if (has_slot) {
        ReleaseInlineHookSlot(slot_index);
    }
    RemoveQueuedHookEvents(hook_id);
    return true;
}

bool InstallNativeJsReplacementHook(uint64_t target_address,
                                    uint64_t replacement_address,
                                    uint64_t* original_address,
                                    void** hook_handle,
                                    std::string* error_message) {
    if (hook_handle == nullptr) {
        SetError(error_message, "hook_handle is null");
        return false;
    }
    *hook_handle = nullptr;
    if (original_address == nullptr) {
        SetError(error_message, "original_address is null");
        return false;
    }
    *original_address = 0u;
    if (target_address == 0u) {
        SetError(error_message, "target_address is required");
        return false;
    }
    if (replacement_address == 0u) {
        SetError(error_message, "replacement_address is required");
        return false;
    }

    InlineHookAddressInvoker invoker = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        invoker = state.inline_hook_address_invoker;
    }
    if (invoker == nullptr) {
        invoker = ResolveInlineHookAddressInvoker();
    }
    if (invoker == nullptr) {
        SetError(error_message, "inline hook address invoker is null");
        return false;
    }

    void* original_function = nullptr;
    const NookStatus status =
        invoker(reinterpret_cast<void*>(static_cast<uintptr_t>(target_address)),
                reinterpret_cast<void*>(static_cast<uintptr_t>(replacement_address)),
                &original_function,
                hook_handle);
    if (status != NOOK_STATUS_OK) {
        std::ostringstream stream;
        stream << "inline hook install failed: " << GetStatusString(status);
        SetError(error_message, stream.str());
        return false;
    }

    *original_address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(original_function));
    return true;
}

bool UninstallNativeJsReplacementHook(void* hook_handle, std::string* error_message) {
    if (hook_handle == nullptr) {
        SetError(error_message, "hook_handle is required");
        return false;
    }

    InlineHookUnhookInvoker unhook_invoker = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        unhook_invoker = state.inline_hook_unhook_invoker;
    }
    if (unhook_invoker == nullptr) {
        unhook_invoker = ResolveInlineHookUnhookInvoker();
    }
    if (unhook_invoker == nullptr) {
        SetError(error_message, "inline hook unhook invoker is null");
        return false;
    }

    const NookStatus status = unhook_invoker(hook_handle);
    if (status != NOOK_STATUS_OK) {
        std::ostringstream stream;
        stream << "inline hook unhook failed: " << GetStatusString(status);
        SetError(error_message, stream.str());
        return false;
    }

    return true;
}

size_t NotifyNativeJsHookModuleLoaded(const char* module_path, std::string* error_message) {
    if (module_path == nullptr || module_path[0] == '\0') {
        SetError(error_message, "module_path is required");
        return 0u;
    }

    std::string module_observer_error;
    if (!JsRuntime::NotifyModuleLoaded(module_path, &module_observer_error)) {
        NOOK_NATIVE_JS_BRIDGE_LOGW("module observer notify failed path=%s error=%s",
                                   module_path,
                                   module_observer_error.c_str());
        if (error_message != nullptr && error_message->empty()) {
            *error_message = module_observer_error;
        }
    }

    struct PendingInstallCandidate {
        uint32_t hook_id = 0;
        std::string module_name;
        std::string symbol_name;
        size_t slot_index = 0u;
    };

    std::vector<PendingInstallCandidate> candidates;
    {
        NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        for (const auto& pair : state.records) {
            const NativeJsPendingHookRecord& record = pair.second;
            if (record.installed) {
                continue;
            }
            if (!MatchesModulePath(module_path, record.module_name.c_str())) {
                continue;
            }
            PendingInstallCandidate candidate = {};
            candidate.hook_id = record.hook_id;
            candidate.module_name = record.module_name;
            candidate.symbol_name = record.symbol_name;
            candidate.slot_index = record.slot_index;
            candidates.push_back(std::move(candidate));
        }
    }

    InlineHookAddressInvoker invoker = nullptr;
    {
        NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        invoker = state.inline_hook_address_invoker;
    }
    if (invoker == nullptr) {
        invoker = ResolveInlineHookAddressInvoker();
    }
    if (candidates.empty()) {
        return 0u;
    }

    if (invoker == nullptr) {
        SetError(error_message, "deferred install dependencies are unavailable");
        return 0u;
    }

    size_t installed_count = 0u;
    for (const PendingInstallCandidate& candidate : candidates) {
        void* target_address = nullptr;
        bool used_fallback_resolver = false;
        if (!TryResolveInlineHookTargetAddress(candidate.module_name.c_str(),
                                               candidate.symbol_name.c_str(),
                                               &target_address,
                                               &used_fallback_resolver)) {
            EnqueueNativeJsHookStatusEvent(candidate.hook_id,
                                           NativeJsHookStatusState::kFailed,
                                           candidate.module_name,
                                           candidate.symbol_name,
                                           "deferred install failed: resolve loaded symbol address failed");
            continue;
        }
        NOOK_NATIVE_JS_BRIDGE_LOGI("native deferred hook resolved hook_id=%u module=%s symbol=%s target=%p fallback=%d",
                                   candidate.hook_id,
                                   candidate.module_name.c_str(),
                                   candidate.symbol_name.c_str(),
                                   target_address,
                                   used_fallback_resolver ? 1 : 0);
        if (!IsResolvedInlineHookSymbolSafe(candidate.module_name.c_str(),
                                            candidate.symbol_name.c_str(),
                                            target_address)) {
            EnqueueNativeJsHookStatusEvent(candidate.hook_id,
                                           NativeJsHookStatusState::kFailed,
                                           candidate.module_name,
                                           candidate.symbol_name,
                                           "deferred install failed: unsafe inline hook target symbol");
            continue;
        }

        void* original_function = nullptr;
        void* native_hook_handle = nullptr;
        const NookStatus status = invoker(target_address,
                                          reinterpret_cast<void*>(
                                                  kInlineHookReplacementEntries[candidate.slot_index]),
                                          &original_function,
                                          &native_hook_handle);
        if (status != NOOK_STATUS_OK) {
            std::ostringstream stream;
            stream << "deferred install failed: inline hook install failed: "
                   << GetStatusString(status);
            EnqueueNativeJsHookStatusEvent(candidate.hook_id,
                                           NativeJsHookStatusState::kFailed,
                                           candidate.module_name,
                                           candidate.symbol_name,
                                           stream.str());
            continue;
        }
        NativeJsHookRequest request = {};
        {
            NativeJsHookRegistryState& state = GetRegistryState();
            std::lock_guard<std::mutex> lock(state.mutex);
            auto it = state.records.find(candidate.hook_id);
            if (it != state.records.end()) {
                request.type = it->second.type;
                request.module_name = it->second.module_name;
                request.symbol_name = it->second.symbol_name;
                request.has_target_address = it->second.has_target_address;
                request.target_address = it->second.target_address;
                request.blocking = it->second.blocking;
                request.snapshots = it->second.snapshots;
            }
        }
        if (!ActivateInlineHookSlot(candidate.slot_index,
                                    request,
                                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target_address)),
                                    original_function,
                                    native_hook_handle,
                                    error_message)) {
            EnqueueNativeJsHookStatusEvent(candidate.hook_id,
                                           NativeJsHookStatusState::kFailed,
                                           candidate.module_name,
                                           candidate.symbol_name,
                                           error_message != nullptr ? *error_message
                                                                    : "deferred install failed: activate slot failed");
            continue;
        }

        {
            NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
            std::lock_guard<std::mutex> lock(state.mutex);
            auto it = state.records.find(candidate.hook_id);
            if (it != state.records.end()) {
                it->second.installed = true;
                it->second.native_hook_handle = native_hook_handle;
            }
        }
        {
            NativeJsHookRegistryState& state = GetRegistryState();
            std::lock_guard<std::mutex> lock(state.mutex);
            auto it = state.records.find(candidate.hook_id);
            if (it != state.records.end()) {
                it->second.hook_handle = native_hook_handle;
                it->second.deferred = false;
            }
        }
        EnqueueNativeJsHookStatusEvent(candidate.hook_id,
                                       NativeJsHookStatusState::kInstalled,
                                       candidate.module_name,
                                       candidate.symbol_name,
                                       "");
        ++installed_count;
    }
    return installed_count;
}

bool EnqueueNativeJsHookEvent(const HookEvent& event, std::string* error_message) {
    if (event.hook_id == 0) {
        SetError(error_message, "hook_id is required");
        return false;
    }
    if (event.argument_count > event.argument_values.size()) {
        SetError(error_message, "too many hook arguments");
        return false;
    }
    if (event.jni_utf8_snapshot_count > event.jni_utf8_snapshots.size()) {
        SetError(error_message, "too many jni utf8 snapshots");
        return false;
    }
    for (uint32_t index = 0; index < event.jni_utf8_snapshot_count; ++index) {
        if (event.jni_utf8_snapshots[index].argument_index >= event.argument_count) {
            SetError(error_message, "jni utf8 snapshot argument index out of range");
            return false;
        }
        if (event.jni_utf8_snapshots[index].property_name.empty()) {
            SetError(error_message, "jni utf8 snapshot property name is required");
            return false;
        }
    }

    NativeJsHookEventQueueState& state = GetEventQueueState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending_events.push_back(event);
    }
    NotifyHookEventConsumer();
    return true;
}

bool TryDequeueNativeJsHookEvent(HookEvent* out_event) {
    if (out_event == nullptr) {
        return false;
    }

    NativeJsHookEventQueueState& state = GetEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.pending_events.empty()) {
        return false;
    }

    *out_event = state.pending_events.front();
    state.pending_events.pop_front();
    return true;
}

bool TryDequeueNativeJsHookStatusEvent(NativeJsHookStatusEvent* out_event) {
    if (out_event == nullptr) {
        return false;
    }

    NativeJsHookStatusEventQueueState& state = GetStatusEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.pending_events.empty()) {
        return false;
    }

    *out_event = state.pending_events.front();
    state.pending_events.pop_front();
    return true;
}

bool CompleteNativeJsHookInvocation(uint64_t invocation_id,
                                    HookEventPhase phase,
                                    const HookInvocationMutationResult& result,
                                    std::string* error_message) {
    if (invocation_id == 0u) {
        SetError(error_message, "invocation_id is required");
        return false;
    }

    NativeJsHookInvocationState& state = GetInvocationState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.records.find(invocation_id);
        if (it == state.records.end()) {
            SetError(error_message, "pending invocation not found");
            return false;
        }
        if (phase == HookEventPhase::kEnter) {
            it->second.enter_result = result;
            it->second.enter_completed = true;
        } else {
            it->second.leave_result = result;
            it->second.leave_completed = true;
        }
    }
    state.cv.notify_all();
    return true;
}

std::size_t GetInstalledNativeJsHookCountForTesting() {
    NativeJsHookRegistryState& state = GetRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.records.size();
}

bool GetNativeJsHookRecordForTesting(uint32_t hook_id, NativeJsHookRecord* out_record) {
    if (out_record == nullptr) {
        return false;
    }
    NativeJsHookRegistryState& state = GetRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto it = state.records.find(hook_id);
    if (it == state.records.end()) {
        return false;
    }
    *out_record = it->second;
    return true;
}

std::size_t GetPendingNativeJsHookCountForTesting() {
    NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    size_t count = 0u;
    for (const auto& pair : state.records) {
        if (!pair.second.installed) {
            ++count;
        }
    }
    return count;
}

bool GetPendingNativeJsHookRecordForTesting(uint32_t hook_id, NativeJsPendingHookRecord* out_record) {
    if (out_record == nullptr) {
        return false;
    }
    NativeJsPendingHookRegistryState& state = GetPendingHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto it = state.records.find(hook_id);
    if (it == state.records.end()) {
        return false;
    }
    *out_record = it->second;
    return true;
}

bool InvokeInstalledNativeJsHookForTesting(uint32_t hook_id,
                                           const std::array<uint64_t, 8>& arguments,
                                           uint64_t* return_value_out) {
    if (hook_id == 0u || return_value_out == nullptr) {
        return false;
    }

    size_t slot_index = 0u;
    bool found = false;
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        for (size_t candidate = 0u; candidate < state.slots.size(); ++candidate) {
            const NativeJsInlineHookSlotState& slot = state.slots[candidate];
            if (slot.in_use && slot.hook_id == hook_id) {
                slot_index = candidate;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }

    *return_value_out = DispatchInlineHookSlot(slot_index,
                                               arguments[0],
                                               arguments[1],
                                               arguments[2],
                                               arguments[3],
                                               arguments[4],
                                               arguments[5],
                                               arguments[6],
                                               arguments[7]);
    return true;
}

void PushNativeJsInlineHookIgnoreForTesting() {
    PushNativeJsInlineHookIgnore();
}

void PopNativeJsInlineHookIgnoreForTesting() {
    PopNativeJsInlineHookIgnore();
}

void RunWithInlineHookBridgeMutexHeldForTesting(const std::function<void()>& callback) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (callback != nullptr) {
        callback();
    }
}

void ResetNativeJsHookRegistryForTesting() {
    NativeJsHookRegistryState& state = GetRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.next_hook_id = 1;
    state.records.clear();

    NativeJsPendingHookRegistryState& pending_state = GetPendingHookRegistryState();
    std::lock_guard<std::mutex> pending_lock(pending_state.mutex);
    pending_state.records.clear();

    NativeJsInlineHookBridgeState& bridge_state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> bridge_lock(bridge_state.mutex);
    bridge_state.resolve_loaded_symbol_address = nullptr;
    bridge_state.resolve_symbol_address = nullptr;
    bridge_state.inline_hook_symbol_safety_checker = nullptr;
    bridge_state.inline_hook_address_invoker = nullptr;
    bridge_state.inline_hook_unhook_invoker = nullptr;
    bridge_state.ensure_inline_hook_module_observer_async = nullptr;
    for (auto& slot : bridge_state.slots) {
        slot = {};
    }
    for (auto& runtime_snapshot : bridge_state.slot_runtime_snapshots) {
        runtime_snapshot = {};
    }
    for (auto& runtime_in_use : bridge_state.slot_runtime_in_use) {
        runtime_in_use.store(false, std::memory_order_release);
    }
}

void ResetNativeJsHookEventQueueForTesting() {
    NativeJsHookEventQueueState& state = GetEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.pending_events.clear();
    state.notifier = nullptr;

    NativeJsHookStatusEventQueueState& status_state = GetStatusEventQueueState();
    {
        std::lock_guard<std::mutex> status_lock(status_state.mutex);
        status_state.pending_events.clear();
    }

    NativeJsHookInvocationState& invocation_state = GetInvocationState();
    {
        std::lock_guard<std::mutex> invocation_lock(invocation_state.mutex);
        invocation_state.records.clear();
    }
    invocation_state.cv.notify_all();
}

void ResetNativeJsHookStatusEventQueueForTesting() {
    NativeJsHookStatusEventQueueState& state = GetStatusEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.pending_events.clear();
}

void SetNativeJsHookEventNotifier(NativeJsHookEventNotifier notifier) {
    NativeJsHookEventQueueState& state = GetEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.notifier = notifier;
}

void ResetNativeJsHookEventNotifier() {
    NativeJsHookEventQueueState& state = GetEventQueueState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.notifier = nullptr;
}

void SetNativeJsResolveLoadedSymbolAddressForTesting(ResolveLoadedSymbolAddressFn resolver) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.resolve_loaded_symbol_address = resolver;
}

void ResetNativeJsResolveLoadedSymbolAddressForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.resolve_loaded_symbol_address = nullptr;
}

void SetNativeJsResolveSymbolAddressForTesting(ResolveLoadedSymbolAddressFn resolver) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.resolve_symbol_address = resolver;
}

void ResetNativeJsResolveSymbolAddressForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.resolve_symbol_address = nullptr;
}

void SetNativeJsInlineHookSymbolSafetyCheckerForTesting(InlineHookSymbolSafetyChecker checker) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_symbol_safety_checker = checker;
}

void ResetNativeJsInlineHookSymbolSafetyCheckerForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_symbol_safety_checker = nullptr;
}

void SetNativeJsInlineHookAddressInvokerForTesting(InlineHookAddressInvoker invoker) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_address_invoker = invoker;
}

void ResetNativeJsInlineHookAddressInvokerForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_address_invoker = nullptr;
    for (auto& slot : state.slots) {
        slot = {};
    }
}

void SetNativeJsInlineHookUnhookInvokerForTesting(InlineHookUnhookInvoker invoker) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_unhook_invoker = invoker;
}

void ResetNativeJsInlineHookUnhookInvokerForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.inline_hook_unhook_invoker = nullptr;
}

void SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
        EnsureInlineHookModuleObserverAsyncFn invoker) {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.ensure_inline_hook_module_observer_async = invoker;
}

void ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting() {
    NativeJsInlineHookBridgeState& state = GetInlineHookBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.ensure_inline_hook_module_observer_async = nullptr;
}

}  // namespace agent_runtime
}  // namespace nook
