#include "agent_runtime/js_runtime.h"
#include "agent_runtime/js_runtime_test_api.h"
#include "agent_runtime/nook_java_js_bridge.h"
#include "agent_runtime/nook_native_js_bridge.h"
#include "gadget/nook_gadget_runtime.h"

#include "../../third_party/quickjs/quickjs-2025-09-13/quickjs.h"

#include <mutex>
#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <csetjmp>
#include <exception>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#ifdef DispatchMessage
#undef DispatchMessage
#endif
#endif

#if !defined(_WIN32)
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/uio.h>
#include <unwind.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "native_hook/core/module_info.h"
#include "native_hook/plt_hook/elfio_image_parser.h"
#if defined(__ANDROID__)
#include "xdl.h"
#endif
#endif

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#include "nook/NookJavaHook.h"
#include "java_hook/JavaHook.h"
#include "java_hook/JVM.h"
#include "java_hook/deferred/java_hook_loader_resolver.h"
#define JS_RT_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookCommApi", __VA_ARGS__))
#else
#define JS_RT_LOGI(...) ((void)0)
#endif

#if defined(_WIN32)
#define NOOK_NATIVE_FUNCTION_SMOKE_EXPORT __declspec(dllexport)
#else
#define NOOK_NATIVE_FUNCTION_SMOKE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT uint32_t
NookNativeFunctionSmokeAdd(uint32_t left, uint32_t right) {
    return left + right;
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT bool
NookNativeFunctionSmokeBoolNot(bool value) {
    return !value;
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT int16_t
NookNativeFunctionSmokeAddS16(int16_t left, int16_t right) {
    return static_cast<int16_t>(left + right);
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT uint64_t
NookNativeFunctionSmokeAddU64(uint64_t left, uint64_t right) {
    return left + right;
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT float
NookNativeFunctionSmokeAddFloat(float left, float right) {
    return left + right;
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT double
NookNativeFunctionSmokeAddDouble(double left, double right) {
    return left + right;
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT uint64_t
NookNativeFunctionSmokeMixU64Double(uint64_t left, double right) {
    return left + static_cast<uint64_t>(right * 10.0);
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT float
NookNativeFunctionSmokeMixFloatU32(float left, uint32_t right) {
    return left + static_cast<float>(right);
}

extern "C" NOOK_NATIVE_FUNCTION_SMOKE_EXPORT double
NookNativeFunctionSmokeMixDoubleU32(double left, uint32_t right) {
    return left + static_cast<double>(right) + 0.25;
}

namespace nook {
namespace agent_runtime {

bool DispatchJavaHookInvocationToRuntime(uint32_t hook_id,
                                         uint64_t receiver_handle,
                                         const JavaJsValue* args,
                                         size_t arg_count,
                                         JavaJsValue* result,
                                         std::string* error_message);
bool DispatchJavaRegisteredClassInvocationToRuntime(uint32_t callback_id,
                                                    uint64_t receiver_handle,
                                                    const std::string& receiver_class_name,
                                                    const std::string& method_name,
                                                    const std::string& method_signature,
                                                    const JavaJsValue* args,
                                                    size_t arg_count,
                                                    JavaJsValue* result,
                                                    std::string* error_message);

namespace {

std::atomic<uint64_t>& GetReadableMemoryProbeCount() {
    static std::atomic<uint64_t> count{0};
    return count;
}

std::atomic<uint64_t>& GetReadableMappingLookupCount() {
    static std::atomic<uint64_t> count{0};
    return count;
}

struct ReadableMappingCacheEntry {
    uintptr_t start = 0u;
    uintptr_t end = 0u;
    bool valid = false;
};

#if defined(_WIN32)
thread_local ReadableMappingCacheEntry g_readable_mapping_cache = {};

ReadableMappingCacheEntry& GetReadableMappingCacheEntry() {
    return g_readable_mapping_cache;
}
#else
pthread_key_t& GetReadableMappingCacheKey() {
    static pthread_key_t key = 0;
    return key;
}

pthread_once_t& GetReadableMappingCacheKeyOnce() {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    return once;
}

void DestroyReadableMappingCacheEntry(void* value) {
    delete static_cast<ReadableMappingCacheEntry*>(value);
}

void InitReadableMappingCacheKey() {
    (void)pthread_key_create(&GetReadableMappingCacheKey(),
                             &DestroyReadableMappingCacheEntry);
}

ReadableMappingCacheEntry& GetReadableMappingCacheEntry() {
    (void)pthread_once(&GetReadableMappingCacheKeyOnce(),
                       &InitReadableMappingCacheKey);
    void* value = pthread_getspecific(GetReadableMappingCacheKey());
    if (value == nullptr) {
        auto* entry = new ReadableMappingCacheEntry();
        (void)pthread_setspecific(GetReadableMappingCacheKey(), entry);
        value = entry;
    }
    return *static_cast<ReadableMappingCacheEntry*>(value);
}
#endif

struct ReadableMappingRecord {
    uintptr_t start = 0u;
    uintptr_t end = 0u;
};

std::mutex& GetReadableMappingSnapshotMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<ReadableMappingRecord>& GetReadableMappingSnapshot() {
    static std::vector<ReadableMappingRecord> snapshot;
    return snapshot;
}

bool IsReadableMemoryRange(uintptr_t address, size_t length);
bool TryReadMemoryBytesSafely(uint64_t address, size_t size, void* bytes_out);

struct SafeReadSignalState {
    bool active = false;
    bool installed = false;
    sigjmp_buf jump_buffer = {};
    struct sigaction old_sigbus = {};
    struct sigaction old_sigsegv = {};
};

#if defined(_WIN32)
thread_local SafeReadSignalState g_safe_read_signal_state = {};

SafeReadSignalState& GetSafeReadSignalState() {
    return g_safe_read_signal_state;
}
#else
pthread_key_t& GetSafeReadSignalStateKey() {
    static pthread_key_t key = 0;
    return key;
}

pthread_once_t& GetSafeReadSignalStateKeyOnce() {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    return once;
}

void DestroySafeReadSignalState(void* value) {
    delete static_cast<SafeReadSignalState*>(value);
}

void InitSafeReadSignalStateKey() {
    (void)pthread_key_create(&GetSafeReadSignalStateKey(),
                             &DestroySafeReadSignalState);
}

SafeReadSignalState& GetSafeReadSignalState() {
    (void)pthread_once(&GetSafeReadSignalStateKeyOnce(),
                       &InitSafeReadSignalStateKey);
    void* value = pthread_getspecific(GetSafeReadSignalStateKey());
    if (value == nullptr) {
        auto* state = new SafeReadSignalState();
        (void)pthread_setspecific(GetSafeReadSignalStateKey(), state);
        value = state;
    }
    return *static_cast<SafeReadSignalState*>(value);
}
#endif

#if !defined(_WIN32)
void SafeReadSignalHandler(int signal_number) {
    SafeReadSignalState& state = GetSafeReadSignalState();
    if (!state.active) {
        signal(signal_number, SIG_DFL);
        raise(signal_number);
        return;
    }
    siglongjmp(state.jump_buffer, signal_number);
}

bool InstallSafeReadSignalHandlers(SafeReadSignalState* state) {
    if (state == nullptr) {
        return false;
    }
    struct sigaction action = {};
    sigemptyset(&action.sa_mask);
    action.sa_handler = &SafeReadSignalHandler;
    action.sa_flags = 0;
    if (sigaction(SIGBUS, &action, &state->old_sigbus) != 0) {
        return false;
    }
    if (sigaction(SIGSEGV, &action, &state->old_sigsegv) != 0) {
        (void)sigaction(SIGBUS, &state->old_sigbus, nullptr);
        return false;
    }
    state->installed = true;
    return true;
}

void RestoreSafeReadSignalHandlers(SafeReadSignalState* state) {
    if (state == nullptr || !state->installed) {
        return;
    }
    (void)sigaction(SIGBUS, &state->old_sigbus, nullptr);
    (void)sigaction(SIGSEGV, &state->old_sigsegv, nullptr);
    state->installed = false;
}

bool TryDirectReadWithSignalGuard(uint64_t address,
                                  size_t size,
                                  void* bytes_out) {
    if (bytes_out == nullptr) {
        return false;
    }
    if (!IsReadableMemoryRange(static_cast<uintptr_t>(address), size)) {
        return false;
    }
    SafeReadSignalState& state = GetSafeReadSignalState();
    if (!InstallSafeReadSignalHandlers(&state)) {
        return false;
    }

    state.active = true;
    const int jump_result = sigsetjmp(state.jump_buffer, 1);
    if (jump_result == 0) {
        volatile const uint8_t* source =
            reinterpret_cast<volatile const uint8_t*>(static_cast<uintptr_t>(address));
        uint8_t* destination = static_cast<uint8_t*>(bytes_out);
        for (size_t index = 0; index < size; ++index) {
            destination[index] = source[index];
        }
        state.active = false;
        RestoreSafeReadSignalHandlers(&state);
        return true;
    }

    state.active = false;
    RestoreSafeReadSignalHandlers(&state);
    return false;
}
#endif

using JavaRegisteredClassSignatureCallbackMap = std::unordered_map<std::string, JSValue>;
using JavaRegisteredClassMethodCallbackMap =
    std::unordered_map<std::string, JavaRegisteredClassSignatureCallbackMap>;
constexpr const char* kJavaInvokeNullTypeCandidate = "__nook_null__";

#if defined(__ANDROID__)
constexpr const char* kJsRuntimeTag = "NookCommApi";
#define NOOK_JS_RUNTIME_LOGI(...) \
    ((void)__android_log_print(ANDROID_LOG_INFO, kJsRuntimeTag, __VA_ARGS__))
#define NOOK_JS_RUNTIME_LOGE(...) \
    ((void)__android_log_print(ANDROID_LOG_ERROR, kJsRuntimeTag, __VA_ARGS__))
#else
#define NOOK_JS_RUNTIME_LOGI(...) ((void)0)
#define NOOK_JS_RUNTIME_LOGE(...) ((void)0)
#endif

enum class NativeFunctionValueType : uint32_t {
    kVoid = 0u,
    kBool = 1u,
    kInt8 = 2u,
    kUInt8 = 3u,
    kInt16 = 4u,
    kUInt16 = 5u,
    kInt32 = 6u,
    kUInt32 = 7u,
    kInt64 = 8u,
    kUInt64 = 9u,
    kFloat = 10u,
    kDouble = 11u,
    kPointer = 12u,
};

struct NativeCallValue {
    uint64_t raw = 0u;
    double number = 0.0;
};

struct NativeModuleRecord {
    std::string name;
    uint64_t base = 0u;
    uint64_t size = 0u;
    std::string path;
    uint64_t load_bias = 0u;
#if !defined(_WIN32)
    const ElfW(Phdr)* phdr = nullptr;
    size_t phnum = 0u;
#endif
};

struct NativeModuleMapping {
    std::string path;
    uint64_t start = 0u;
    uint64_t end = 0u;
};

struct NativeModuleExportRecord {
    std::string type;
    std::string name;
    uint64_t address = 0u;
};

constexpr size_t kMaxNativeCallbackSlots = 32u;
using NativeCallbackTrampoline = uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t);
extern const NativeCallbackTrampoline kNativeCallbackTrampolines[kMaxNativeCallbackSlots];

struct RuntimeState {
    struct NativeJsCallbackRecord {
        JSValue on_enter = JS_UNDEFINED;
        JSValue on_leave = JS_UNDEFINED;
        bool blocking = true;
        JSValue cached_invocation_receiver = JS_UNDEFINED;
        JSValue cached_invocation_args = JS_UNDEFINED;
        JSValue cached_invocation_retval = JS_UNDEFINED;
        JSValue active_sync_invocation_receiver = JS_UNDEFINED;
        uint64_t active_sync_invocation_id = 0u;
        bool cached_invocation_receiver_in_use = false;
        bool cached_invocation_args_in_use = false;
        bool cached_invocation_retval_in_use = false;
        uint32_t cached_invocation_receiver_property_count = 0u;
    };

    struct NativeCallbackRecord {
        JSValue function = JS_UNDEFINED;
        NativeFunctionValueType return_type = NativeFunctionValueType::kVoid;
        std::vector<NativeFunctionValueType> arg_types;
        uint32_t slot = UINT32_MAX;
    };

    struct ReplaceHookRecord {
        uint64_t target_address = 0u;
        uint64_t replacement_address = 0u;
        uint64_t original_address = 0u;
        uint32_t hook_id = 0u;
        void* hook_handle = nullptr;
    };

    std::recursive_mutex runtime_mutex;
    std::mutex callback_mutex;
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JsRuntime::SendCallback send_callback;
    uint32_t current_script_id = 0;
    std::unordered_map<uint32_t, JSValue> recv_callbacks;
    std::unordered_map<uint32_t, std::unordered_map<std::string, JSValue>> rpc_exports;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, NativeJsCallbackRecord>> native_hook_callbacks;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, JSValue>> java_hook_callbacks;
    std::unordered_map<uint32_t,
                       std::unordered_map<uint32_t, JavaRegisteredClassMethodCallbackMap>>
        java_registered_class_callbacks;
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, JSValue>> active_native_invocations;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, NativeCallbackRecord>> native_callback_records;
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, ReplaceHookRecord>> replace_hook_records;
    struct ModuleObserverRecord {
        uint32_t script_id = 0u;
        JSValue on_added = JS_UNDEFINED;
        JSValue on_removed = JS_UNDEFINED;
    };
    std::unordered_map<uint32_t, ModuleObserverRecord> module_observers;
    struct PendingModuleEventRecord {
        bool added = true;
        NativeModuleRecord module = {};
    };
    std::vector<PendingModuleEventRecord> pending_module_events;
    std::array<bool, kMaxNativeCallbackSlots> native_callback_slot_used = {};
    std::unordered_map<uint32_t, std::vector<void*>> owned_allocations;
    std::unordered_map<uint32_t, std::unordered_set<uint64_t>> owned_java_handles;
    std::unordered_map<uint32_t, uint32_t> script_pin_counts;
    struct WeakBindingRecord {
        uint32_t script_id = 0u;
        JSValue callback = JS_UNDEFINED;
        JSValue unregister_token = JS_UNDEFINED;
        bool fired = false;
        bool enqueued = false;
    };
    struct TimerRecord {
        uint32_t script_id = 0u;
        uint32_t timer_id = 0u;
        JSValue callback = JS_UNDEFINED;
        std::vector<JSValue> args;
        std::chrono::steady_clock::time_point due_at = {};
        uint32_t delay_ms = 0u;
        bool repeat = false;
        bool canceled = false;
    };
    std::unordered_map<uint64_t, WeakBindingRecord> weak_bindings;
    std::vector<uint64_t> pending_weak_binding_ids;
    std::unordered_map<uint32_t, TimerRecord> timers;
    uint32_t next_timer_id = 1u;
    std::condition_variable_any timer_cv;
    bool timer_thread_running = false;
    bool stop_timer_thread = false;
    std::thread timer_thread;
    bool debug_symbol_modules_valid = false;
    std::vector<NativeModuleRecord> debug_symbol_modules;
    std::unordered_map<uint64_t, std::vector<NativeModuleExportRecord>> debug_symbol_exports_by_base;
    uint32_t next_native_callback_id = 1u;
    uint32_t next_replace_hook_id = 1u;
    uint32_t next_java_registered_class_callback_id = 1u;
    uint64_t next_weak_binding_id = 1u;
    NativeJsHookInstallerDependencies native_hook_installer_dependencies;
    JavaJsHookInstallerDependencies java_hook_installer_dependencies;
};

struct NativeThreadRecord {
    uint32_t id = 0u;
    std::string state;
    std::string name;
};

struct NativeModuleImportRecord {
    std::string type;
    std::string name;
    std::string module;
    uint64_t slot = 0u;
    uint64_t address = 0u;
};

constexpr unsigned char kElfSymbolTypeObject = 1u;
constexpr unsigned char kElfSymbolTypeFunction = 2u;

enum class BacktracerMode {
    kAccurate,
    kFuzzy,
};

struct JniBridgeState {
    std::mutex mutex;
    JsRuntimeReadJStringUtf8ForTesting read_jstring_utf8 = nullptr;
    JsRuntimeGetJavaEnvPointerForTesting get_java_env_pointer = nullptr;
    JsRuntimeJavaEnvExceptionCheckForTesting java_env_exception_check = nullptr;
    JsRuntimeJavaEnvExceptionOccurredForTesting java_env_exception_occurred = nullptr;
    JsRuntimeJavaEnvExceptionClearForTesting java_env_exception_clear = nullptr;
    JsRuntimeJavaEnvFindClassForTesting java_env_find_class = nullptr;
    JsRuntimeJavaEnvGetObjectClassForTesting java_env_get_object_class = nullptr;
    JsRuntimeJavaEnvIsSameObjectForTesting java_env_is_same_object = nullptr;
    JsRuntimeJavaEnvIsInstanceOfForTesting java_env_is_instance_of = nullptr;
    JsRuntimeJavaEnvNewStringUtfForTesting java_env_new_string_utf = nullptr;
    JsRuntimeJavaEnvGetStringUtfCharsForTesting java_env_get_string_utf_chars = nullptr;
    JsRuntimeJavaEnvReleaseStringUtfCharsForTesting java_env_release_string_utf_chars = nullptr;
    JsRuntimeJavaEnvNewGlobalRefForTesting java_env_new_global_ref = nullptr;
    JsRuntimeJavaEnvDeleteGlobalRefForTesting java_env_delete_global_ref = nullptr;
    JsRuntimeJavaEnvNewWeakGlobalRefForTesting java_env_new_weak_global_ref = nullptr;
    JsRuntimeJavaEnvDeleteWeakGlobalRefForTesting java_env_delete_weak_global_ref = nullptr;
    JsRuntimeJavaEnvGetObjectRefTypeForTesting java_env_get_object_ref_type = nullptr;
    JsRuntimeJavaEnvGetSuperclassForTesting java_env_get_superclass = nullptr;
    JsRuntimeJavaEnvIsAssignableFromForTesting java_env_is_assignable_from = nullptr;
};

struct ScopedJavaEnvOverrideState {
    uint64_t pointer = 0u;
    size_t depth = 0u;
};

constexpr const char* kModuleMapSnapshotProperty = "__nookModuleMapSnapshot";
constexpr const char* kModuleObjectNameProperty = "__nookModuleName";
constexpr const char* kNativeFunctionTargetProperty = "__nookNativeFunctionTarget";
constexpr const char* kNativeFunctionReturnTypeProperty = "__nookNativeFunctionReturnType";
constexpr const char* kNativeFunctionArgTypesProperty = "__nookNativeFunctionArgTypes";
constexpr const char* kNativePointerValueProperty = "__nookPointerValue";
constexpr const char* kDebugSymbolModuleBaseProperty = "__nookDebugSymbolModuleBase";
constexpr const char* kDebugSymbolSymbolAddressProperty = "__nookDebugSymbolSymbolAddress";
constexpr const char* kNativeHookArgumentReplacementProperty = "__nookArgumentReplacement";
constexpr const char* kNativeHookReturnValueReplacementProperty = "__nookReturnValueReplacement";
constexpr const char* kNativeHookBlockingProperty = "__nookHookBlocking";
constexpr const char* kNativeHookMutationArgumentMaskProperty = "__nookMutationArgumentMask";
constexpr const char* kNativeHookMutationContextMaskProperty = "__nookMutationContextMask";
constexpr const char* kNativeHookMutationReturnValueDirtyProperty = "__nookMutationReturnValueDirty";
constexpr const char* kNativeHookMutationReturnAddressDirtyProperty = "__nookMutationReturnAddressDirty";
constexpr const char* kNativeHookContextReceiverProperty = "__nookContextReceiver";
constexpr const char* kNativeHookReturnAddressValueProperty = "__nookReturnAddressValue";
constexpr const char* kNativeHookContextX0ValueProperty = "__nookContextX0Value";
constexpr const char* kNativeHookContextX1ValueProperty = "__nookContextX1Value";
constexpr const char* kNativeHookContextX2ValueProperty = "__nookContextX2Value";
constexpr const char* kNativeHookContextX3ValueProperty = "__nookContextX3Value";
constexpr const char* kNativeHookContextX4ValueProperty = "__nookContextX4Value";
constexpr const char* kNativeHookContextX5ValueProperty = "__nookContextX5Value";
constexpr const char* kNativeHookContextX6ValueProperty = "__nookContextX6Value";
constexpr const char* kNativeHookContextX7ValueProperty = "__nookContextX7Value";
constexpr const char* kNativeHookContextSpValueProperty = "__nookContextSpValue";
constexpr const char* kNativeHookContextFpValueProperty = "__nookContextFpValue";
constexpr const char* kNativeHookContextLrValueProperty = "__nookContextLrValue";
constexpr const char* kNativeHookContextPcValueProperty = "__nookContextPcValue";
constexpr const char* kNativeHookReturnAddressBaselineProperty = "__nookReturnAddressBaseline";
constexpr const char* kNativeHookContextX0BaselineProperty = "__nookContextX0Baseline";
constexpr const char* kNativeHookContextX1BaselineProperty = "__nookContextX1Baseline";
constexpr const char* kNativeHookContextX2BaselineProperty = "__nookContextX2Baseline";
constexpr const char* kNativeHookContextX3BaselineProperty = "__nookContextX3Baseline";
constexpr const char* kNativeHookContextX4BaselineProperty = "__nookContextX4Baseline";
constexpr const char* kNativeHookContextX5BaselineProperty = "__nookContextX5Baseline";
constexpr const char* kNativeHookContextX6BaselineProperty = "__nookContextX6Baseline";
constexpr const char* kNativeHookContextX7BaselineProperty = "__nookContextX7Baseline";
constexpr const char* kNativeHookContextSpBaselineProperty = "__nookContextSpBaseline";
constexpr const char* kNativeHookContextFpBaselineProperty = "__nookContextFpBaseline";
constexpr const char* kNativeHookContextLrBaselineProperty = "__nookContextLrBaseline";
constexpr const char* kNativeHookContextPcBaselineProperty = "__nookContextPcBaseline";
constexpr const char* kJavaInstallImplementationFunctionName = "__nookJavaInstallImplementation";
constexpr const char* kJavaInvokeFunctionName = "__nookJavaInvoke";
constexpr const char* kJavaReleaseFunctionName = "__nookJavaRelease";
constexpr const char* kJavaResolveOverloadSignatureFunctionName = "__nookJavaResolveOverloadSignature";
constexpr const char* kJavaResolveFieldFunctionName = "__nookJavaResolveField";
constexpr const char* kJavaReadFieldFunctionName = "__nookJavaReadField";
constexpr const char* kJavaWriteFieldFunctionName = "__nookJavaWriteField";
constexpr const char* kJavaUseWithLoaderFunctionName = "__nookJavaUseWithLoader";
constexpr const char* kJavaGetClassWrapperFunctionName = "__nookJavaGetClassWrapper";
constexpr const char* kJavaRegisterClassFunctionName = "__nookJavaRegisterClass";
constexpr const char* kGetCurrentScriptIdFunctionName = "__nookGetCurrentScriptId";
constexpr const char* kRunInScriptFunctionName = "__nookRunInScript";
constexpr const char* kJavaInvokeResolverVersion = "2026-04-26-numcand-v2";
constexpr const char* kJavaReceiverHandleProperty = "__nookJavaReceiverHandle";
constexpr const char* kJavaLoaderHandleProperty = "__nookJavaLoaderHandle";
constexpr const char* kJavaOwnedHandleProperty = "__nookJavaOwnedHandle";
constexpr const char* kJavaEnvHandleProperty = "__nookJavaEnvHandle";
constexpr const char* kWeakBindingRegistryProperty = "__nookWeakBindingRegistry";
constexpr const char* kJavaObjectPointerProperty = "__jptr";
constexpr const char* kJavaArrayTypeProperty = "__nookJavaArrayType";

RuntimeState& GetRuntimeState() {
    static RuntimeState state;
    return state;
}

JniBridgeState& GetJniBridgeState() {
    static JniBridgeState state;
    return state;
}

#if defined(_WIN32)
thread_local ScopedJavaEnvOverrideState g_scoped_java_env_override_state = {};

ScopedJavaEnvOverrideState& GetScopedJavaEnvOverrideState() {
    return g_scoped_java_env_override_state;
}
#else
pthread_key_t& GetScopedJavaEnvOverrideStateKey() {
    static pthread_key_t key = 0;
    return key;
}

pthread_once_t& GetScopedJavaEnvOverrideStateKeyOnce() {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    return once;
}

void DestroyScopedJavaEnvOverrideState(void* value) {
    delete static_cast<ScopedJavaEnvOverrideState*>(value);
}

void InitScopedJavaEnvOverrideStateKey() {
    (void)pthread_key_create(&GetScopedJavaEnvOverrideStateKey(),
                             &DestroyScopedJavaEnvOverrideState);
}

ScopedJavaEnvOverrideState& GetScopedJavaEnvOverrideState() {
    (void)pthread_once(&GetScopedJavaEnvOverrideStateKeyOnce(),
                       &InitScopedJavaEnvOverrideStateKey);
    void* value = pthread_getspecific(GetScopedJavaEnvOverrideStateKey());
    if (value == nullptr) {
        auto* state = new ScopedJavaEnvOverrideState();
        (void)pthread_setspecific(GetScopedJavaEnvOverrideStateKey(), state);
        value = state;
    }
    return *static_cast<ScopedJavaEnvOverrideState*>(value);
}
#endif

JSValue MakeNativePointer(JSContext* ctx, uint64_t value);
JSValue MakeJavaEnvWrapper(JSContext* ctx, uint64_t env_ptr);
bool ParseInteger64Value(JSContext* ctx,
                         JSValueConst value,
                         uint64_t* raw_value,
                         bool* is_signed_out);
JSValue MakeInteger64Object(JSContext* ctx, uint64_t raw_value, bool is_signed);
bool ParsePointerValue(JSContext* ctx, JSValueConst value, uint64_t* value_out);
bool ParseJavaEnvWrapperHandle(JSContext* ctx, JSValueConst value, uint64_t* env_ptr_out);
bool ParseNativeFunctionValueType(const std::string& text,
                                  NativeFunctionValueType* type_out);
bool ParseNativeFunctionValueTypeArray(JSContext* ctx,
                                       JSValueConst value,
                                       JSValue* array_out);
bool ReadNativeFunctionValueTypeMetadataArray(JSContext* ctx,
                                              JSValueConst value,
                                              std::vector<NativeFunctionValueType>* types_out);
JSValue CreateNativeFunctionValue(JSContext* ctx,
                                  uint64_t target_address,
                                  NativeFunctionValueType return_type,
                                  JSValueConst arg_types_value);
JSValue JsNativeFunctionInvoke(JSContext* ctx,
                               JSValueConst this_val,
                               int argc,
                               JSValueConst* argv,
                               int magic,
                               JSValue* func_data);
JSValue JsNativeFunctionConstructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsNativeCallbackConstructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsInterceptorReplace(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsInterceptorRevert(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaPerform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaVmPerform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaVmGetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaVmTryGetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvExceptionCheck(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvExceptionOccurred(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvExceptionClear(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvFindClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvGetObjectClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvIsSameObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvIsInstanceOf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvNewStringUtf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvGetStringUtfChars(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvReleaseStringUtfChars(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv);
JSValue JsJavaEnvNewGlobalRef(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvDeleteGlobalRef(JSContext* ctx,
                                 JSValueConst this_val,
                                 int argc,
                                 JSValueConst* argv);
JSValue JsJavaEnvNewWeakGlobalRef(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv);
JSValue JsJavaEnvDeleteWeakGlobalRef(JSContext* ctx,
                                     JSValueConst this_val,
                                     int argc,
                                     JSValueConst* argv);
JSValue JsJavaEnvGetObjectRefType(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv);
JSValue JsJavaEnvGetSuperclass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnvIsAssignableFrom(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv);
JSValue InvokeJavaVmPerformWithEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaUse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaUseWithLoader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaGetClassWrapper(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaRegisterClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsGetCurrentScriptId(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsRunInScript(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaChoose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnumerateLoadedClasses(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaEnumerateClassLoaders(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaInstallImplementation(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaInvoke(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaResolveOverloadSignature(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaResolveField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaReadField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaWriteField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaUpdateClassLoader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaIsClassLoaderReady(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaCallOriginal(JSContext* ctx,
                           JSValueConst this_val,
                           int argc,
                           JSValueConst* argv,
                           int magic,
                           JSValue* func_data);
bool ParseJavaJsValue(JSContext* ctx,
                      JSValueConst value,
                      JavaJsValue* out_value,
                      std::string* error_message);
bool ParseJavaMethodMetadata(JSContext* ctx,
                             JSValueConst value,
                             JavaJsMethodRecord* out_record,
                             uint64_t* receiver_handle_out,
                             std::string* error_message);
JSValue CreateJavaUseWrapper(JSContext* ctx,
                             const char* class_name,
                             uint64_t receiver_handle = 0u,
                             uint64_t loader_handle = 0u,
                             bool owns_handle = false);
JSValue JsJavaRelease(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsNookJniReadJStringUtf8(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue MakeModuleObject(JSContext* ctx, const NativeModuleRecord& module);
JSValue MakeThreadObject(JSContext* ctx, const NativeThreadRecord& thread);
JSValue CloneModuleObject(JSContext* ctx, JSValueConst module_value);
JSValue CloneModuleArray(JSContext* ctx, JSValueConst modules);
JSValue GetModuleMapSnapshot(JSContext* ctx, JSValueConst this_val, const char* method_name);
bool ReplaceModuleMapSnapshot(JSContext* ctx, JSValueConst this_val, JSValue snapshot);
bool TryGetModuleNameFromReceiver(JSContext* ctx,
                                  JSValueConst this_val,
                                  std::string* module_name_out);
JSValue JsModuleEnumerateModules(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsProcessEnumerateModules(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsProcessAttachModuleObserver(JSContext* ctx,
                                      JSValueConst this_val,
                                      int argc,
                                      JSValueConst* argv);
JSValue JsProcessGetCurrentThreadId(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsProcessEnumerateThreads(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsProcessFindModuleByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsProcessGetModuleByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleFindBaseAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleGetBaseAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleLoad(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleEnsureInitialized(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleEnumerateExports(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleEnumerateImports(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleFindImportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleGetImportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleFindExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleGetExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleEnumerateSymbols(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleFindSymbolByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleGetSymbolByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleFindGlobalExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsModuleGetGlobalExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsDebugSymbolFromAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsDebugSymbolToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsThreadBacktrace(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsThreadSleep(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaDeopt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaSetForcedInterpretOnly(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsJavaArtRouterDebug(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptBindWeak(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptUnbindWeak(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptPin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptUnpin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptRunGcForTesting(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsSetImmediate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsSetTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsSetInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsClearTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsClearInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsScriptOnWeakBindingCollected(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
#if defined(__ANDROID__)
bool EnsureJavaHookReadyForJs(std::string* error_message);
void ReleaseTemporaryJavaChooseMatches(const std::vector<JavaJsValue>& matches);
#endif
uintptr_t NormalizeProcessAddressForRangeCheck(uintptr_t address);
bool CollectLoadedNativeModules(std::vector<NativeModuleRecord>* modules,
                                std::string* error_message);
bool CollectModuleExports(const NativeModuleRecord& module,
                          std::vector<NativeModuleExportRecord>* exports_out,
                          std::string* error_message);
bool TryGetNativeFunctionMetadata(JSContext* ctx,
                                  JSValueConst value,
                                  uint64_t* target_address_out,
                                  NativeFunctionValueType* return_type_out,
                                  std::vector<NativeFunctionValueType>* arg_types_out);
bool ReadJStringUtf8(uint64_t env_ptr,
                     uint64_t jstring_ptr,
                     std::string* text_out,
                     std::string* error_message);
bool QueryJavaEnvNewGlobalRef(uint64_t env_ptr,
                              uint64_t object_handle,
                              uint64_t* ref_ptr_out,
                              std::string* error_message);
bool TryGetMutableNativePointerValue(JSContext* ctx,
                                     JSValueConst this_val,
                                     uint64_t* value_out);
bool ResolveNativePointerValue(JSContext* ctx,
                               JSValueConst this_val,
                               JSValue* func_data,
                               uint64_t* value_out);
JSValue ResolveNativePointerDisplayValue(JSContext* ctx,
                                         JSValueConst this_val,
                                         JSValue* func_data);
bool UpdateNativePointerValue(JSContext* ctx,
                              JSValueConst pointer_object,
                              uint64_t value,
                              std::string* error_message);
bool SetOrUpdateNativePointerProperty(JSContext* ctx,
                                      JSValue object,
                                      const char* name,
                                      uint64_t value,
                                      std::string* error_message);
bool SetUint32Property(JSContext* ctx,
                       JSValueConst object,
                       const char* name,
                       uint32_t value,
                       std::string* error_message);
bool SetBoolProperty(JSContext* ctx,
                     JSValueConst object,
                     const char* name,
                     bool value,
                     std::string* error_message);
bool GetUint32Property(JSContext* ctx,
                       JSValueConst object,
                       const char* name,
                       uint32_t* value_out,
                       std::string* error_message);
bool GetBoolProperty(JSContext* ctx,
                     JSValueConst object,
                     const char* name,
                     bool* value_out,
                     std::string* error_message);
bool DispatchJavaReadyCallbacksLocked(RuntimeState& state, std::string* error_message);
bool MarkNativeHookArgumentDirty(JSContext* ctx,
                                 JSValueConst receiver,
                                 uint32_t argument_index,
                                 std::string* error_message);
bool MarkNativeHookContextDirty(JSContext* ctx,
                                JSValueConst receiver,
                                uint32_t context_index,
                                std::string* error_message);
bool MarkNativeHookReturnValueDirty(JSContext* ctx,
                                    JSValueConst receiver,
                                    std::string* error_message);
bool MarkNativeHookReturnAddressDirty(JSContext* ctx,
                                      JSValueConst receiver,
                                      std::string* error_message);
bool ResetNativeHookMutationState(JSContext* ctx,
                                  JSValueConst receiver,
                                  std::string* error_message);
bool SetBaselinePointerProperty(JSContext* ctx,
                                JSValue object,
                                const char* name,
                                uint64_t value,
                                std::string* error_message);
bool ParseBaselinePointerProperty(JSContext* ctx,
                                  JSValue object,
                                  const char* name,
                                  uint64_t* value_out,
                                  std::string* error_message);
JSValue JsNativeHookReceiverPointerGetter(JSContext* ctx,
                                          JSValueConst this_val,
                                          int argc,
                                          JSValueConst* argv,
                                          int magic,
                                          JSValue* func_data);
JSValue JsNativeHookReceiverPointerSetter(JSContext* ctx,
                                          JSValueConst this_val,
                                          int argc,
                                          JSValueConst* argv,
                                          int magic,
                                          JSValue* func_data);
JSValue JsNativeHookContextPointerGetter(JSContext* ctx,
                                         JSValueConst this_val,
                                         int argc,
                                         JSValueConst* argv,
                                         int magic,
                                         JSValue* func_data);
JSValue JsNativeHookContextPointerSetter(JSContext* ctx,
                                         JSValueConst this_val,
                                         int argc,
                                         JSValueConst* argv,
                                         int magic,
                                         JSValue* func_data);
bool SetNativeHookContextPointerProperty(JSContext* ctx,
                                         JSValue context,
                                         const char* name,
                                         uint64_t value,
                                         std::string* error_message);
std::string GetExceptionString(JSContext* ctx);
RuntimeState::NativeJsCallbackRecord* FindNativeHookCallbacksLocked(RuntimeState& state,
                                                                    uint32_t hook_id,
                                                                    uint32_t* script_id_out);
JSValue BuildNativeHookArgsArray(JSContext* ctx, const HookEvent& event, JSValueConst receiver);
JSValue BuildNativeHookInvocationContextObject(JSContext* ctx, const HookEvent& event, bool blocking);
JSValue BuildNativeHookReturnValue(JSContext* ctx, uint64_t value, JSValueConst receiver);
bool ResetNativeHookArgsArray(JSContext* ctx,
                              JSValue args,
                              const HookEvent& event,
                              JSValueConst receiver,
                              std::string* error_message);
bool ResetNativeHookInvocationContextObject(JSContext* ctx,
                                            JSValue receiver,
                                            const HookEvent& event,
                                            bool blocking,
                                            std::string* error_message);
bool ResetNativeHookReturnValue(JSContext* ctx,
                                JSValue retval,
                                uint64_t value,
                                JSValueConst receiver,
                                std::string* error_message);
bool NativeHookInvocationContextIsDirty(JSContext* ctx,
                                        RuntimeState::NativeJsCallbackRecord* callbacks);
JSValue ObtainNativeHookInvocationReceiver(JSContext* ctx,
                                           RuntimeState::NativeJsCallbackRecord* callbacks,
                                           const HookEvent& event,
                                           std::string* error_message);
void ReleaseNativeHookInvocationReceiver(JSContext* ctx,
                                         RuntimeState::NativeJsCallbackRecord* callbacks,
                                         JSValue receiver);
JSValue ObtainNativeHookArgsArray(JSContext* ctx,
                                  RuntimeState::NativeJsCallbackRecord* callbacks,
                                  const HookEvent& event,
                                  JSValueConst receiver,
                                  std::string* error_message);
void ReleaseNativeHookArgsArray(JSContext* ctx,
                                RuntimeState::NativeJsCallbackRecord* callbacks,
                                JSValue args);
JSValue ObtainNativeHookReturnValue(JSContext* ctx,
                                    RuntimeState::NativeJsCallbackRecord* callbacks,
                                    uint64_t value,
                                    JSValueConst receiver,
                                    std::string* error_message);
void ReleaseNativeHookReturnValue(JSContext* ctx,
                                  RuntimeState::NativeJsCallbackRecord* callbacks,
                                  JSValue retval);
bool RefreshNativeHookInvocationReceiverForLeave(JSContext* ctx,
                                                 JSValue receiver,
                                                 const HookEvent& event,
                                                 std::string* error_message);
bool CaptureNativeHookInvocationMutations(JSContext* ctx,
                                          const HookEvent& event,
                                          JSValueConst receiver,
                                          JSValueConst callback_argument,
                                          HookInvocationMutationResult* result_out,
                                          std::string* error_message);
bool FreeModuleObserverCallbacksLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id);
bool EnqueueModuleEventLocked(RuntimeState& state,
                              const NativeModuleRecord& module,
                              bool added);
bool ForwardPendingModuleEventsLocked(RuntimeState& state, std::string* error_message);
bool NotifyModuleObserverModuleLoaded(const char* module_path, std::string* error_message);
void ReportNativeHookCallbackExceptionLocked(JSContext* ctx,
                                             uint32_t hook_id,
                                             HookEventPhase phase,
                                             const std::string& message_text);
void DrainPendingWeakBindingsLocked(RuntimeState& state);
void FreeTimerRecordLocked(JSContext* ctx, RuntimeState::TimerRecord* record);
void FreeTimersForScriptLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id);
void FreeAllTimersLocked(JSContext* ctx, RuntimeState& state);
bool DrainDueTimersLocked(RuntimeState& state, std::string* error_message);
uint64_t InvokeNativeCallbackSlot(uint32_t slot,
                                  uint64_t arg0,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3);

class ScopedCurrentScriptId {
public:
    ScopedCurrentScriptId(RuntimeState& state, uint32_t script_id)
        : state_(state), previous_script_id_(state.current_script_id) {
        state_.current_script_id = script_id;
    }

    ~ScopedCurrentScriptId() {
        state_.current_script_id = previous_script_id_;
    }

private:
    RuntimeState& state_;
    uint32_t previous_script_id_;
};

void SetError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
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

bool AppendUtf8CodePoint(uint32_t code_point, std::string* output) {
    if (output == nullptr || code_point > 0x10ffffu ||
        (code_point >= 0xd800u && code_point <= 0xdfffu)) {
        return false;
    }

    if (code_point <= 0x7fu) {
        output->push_back(static_cast<char>(code_point));
        return true;
    }
    if (code_point <= 0x7ffu) {
        output->push_back(static_cast<char>(0xc0u | (code_point >> 6)));
        output->push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
        return true;
    }
    if (code_point <= 0xffffu) {
        output->push_back(static_cast<char>(0xe0u | (code_point >> 12)));
        output->push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3fu)));
        output->push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
        return true;
    }

    output->push_back(static_cast<char>(0xf0u | (code_point >> 18)));
    output->push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3fu)));
    output->push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3fu)));
    output->push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    return true;
}

bool DecodeUtf8CodePoints(const char* text,
                          size_t length,
                          std::vector<uint32_t>* code_points) {
    if (text == nullptr || code_points == nullptr) {
        return false;
    }

    code_points->clear();
    for (size_t index = 0; index < length;) {
        const uint8_t lead = static_cast<uint8_t>(text[index]);
        uint32_t code_point = 0u;
        size_t extra = 0u;
        uint32_t minimum = 0u;

        if ((lead & 0x80u) == 0u) {
            code_point = lead;
        } else if ((lead & 0xe0u) == 0xc0u) {
            code_point = lead & 0x1fu;
            extra = 1u;
            minimum = 0x80u;
        } else if ((lead & 0xf0u) == 0xe0u) {
            code_point = lead & 0x0fu;
            extra = 2u;
            minimum = 0x800u;
        } else if ((lead & 0xf8u) == 0xf0u) {
            code_point = lead & 0x07u;
            extra = 3u;
            minimum = 0x10000u;
        } else {
            return false;
        }

        if (index + extra >= length) {
            return false;
        }

        for (size_t i = 1; i <= extra; ++i) {
            const uint8_t byte = static_cast<uint8_t>(text[index + i]);
            if ((byte & 0xc0u) != 0x80u) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3fu);
        }

        if ((extra != 0u && code_point < minimum) ||
            code_point > 0x10ffffu ||
            (code_point >= 0xd800u && code_point <= 0xdfffu)) {
            return false;
        }

        code_points->push_back(code_point);
        index += extra + 1u;
    }

    return true;
}

bool InvokeNativeHookCallbackLocked(RuntimeState& state,
                                    const HookEvent& event,
                                    HookInvocationMutationResult* mutation_result,
                                    std::string* error_message) {
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }

    uint32_t script_id = 0;
    RuntimeState::NativeJsCallbackRecord* callbacks =
        FindNativeHookCallbacksLocked(state, event.hook_id, &script_id);
    if (callbacks == nullptr) {
        if (mutation_result != nullptr) {
            *mutation_result = HookInvocationMutationResult{};
        }
        return true;
    }

    HookInvocationMutationResult local_mutation = {};
    JSValue callback = JS_DupValue(
        state.context,
        event.phase == HookEventPhase::kEnter ? callbacks->on_enter : callbacks->on_leave);
    JSValue receiver = JS_UNDEFINED;
    if (event.phase == HookEventPhase::kEnter) {
        receiver = ObtainNativeHookInvocationReceiver(state.context,
                                                      callbacks,
                                                      event,
                                                      error_message);
        if (JS_IsException(receiver)) {
            JS_FreeValue(state.context, callback);
            return false;
        }
        if (callbacks->blocking && event.invocation_id != 0u) {
            if (JS_IsObject(callbacks->active_sync_invocation_receiver)) {
                JS_FreeValue(state.context, callbacks->active_sync_invocation_receiver);
                callbacks->active_sync_invocation_receiver = JS_UNDEFINED;
            }
            callbacks->active_sync_invocation_receiver = JS_DupValue(state.context, receiver);
            callbacks->active_sync_invocation_id = event.invocation_id;
        }
    } else if (callbacks->blocking &&
               event.invocation_id != 0u &&
               callbacks->active_sync_invocation_id == event.invocation_id &&
               JS_IsObject(callbacks->active_sync_invocation_receiver)) {
        receiver = JS_DupValue(state.context, callbacks->active_sync_invocation_receiver);
    } else if (event.invocation_id != 0u) {
        auto script_it = state.active_native_invocations.find(script_id);
        if (script_it != state.active_native_invocations.end()) {
            auto invocation_it = script_it->second.find(event.invocation_id);
            if (invocation_it != script_it->second.end()) {
                receiver = JS_DupValue(state.context, invocation_it->second);
            }
        }
    }
    if (JS_IsUndefined(receiver)) {
        receiver = ObtainNativeHookInvocationReceiver(state.context,
                                                      callbacks,
                                                      event,
                                                      error_message);
        if (JS_IsException(receiver)) {
            JS_FreeValue(state.context, callback);
            return false;
        }
    }
    if (event.phase == HookEventPhase::kLeave) {
        std::string refresh_error;
        if (!RefreshNativeHookInvocationReceiverForLeave(state.context,
                                                         receiver,
                                                         event,
                                                         &refresh_error)) {
            JS_FreeValue(state.context, callback);
            JS_FreeValue(state.context, receiver);
            SetError(error_message, refresh_error);
            return false;
        }
    }

    if (!JS_IsFunction(state.context, callback)) {
        JS_FreeValue(state.context, callback);
        ReleaseNativeHookInvocationReceiver(state.context, callbacks, receiver);
        if (mutation_result != nullptr) {
            *mutation_result = local_mutation;
        }
        return true;
    }

    JSValue argument = JS_UNDEFINED;
    if (event.phase == HookEventPhase::kEnter) {
        argument = ObtainNativeHookArgsArray(state.context,
                                             callbacks,
                                             event,
                                             receiver,
                                             error_message);
    } else {
        argument = ObtainNativeHookReturnValue(state.context,
                                               callbacks,
                                               event.return_value,
                                               receiver,
                                               error_message);
    }

    if (JS_IsException(argument)) {
        JS_FreeValue(state.context, callback);
        ReleaseNativeHookInvocationReceiver(state.context, callbacks, receiver);
        return false;
    }

    JSValue argv[1] = {argument};
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context, callback, receiver, 1, argv);
    if (!JS_IsException(result)) {
        std::string mutation_error;
        if (!CaptureNativeHookInvocationMutations(state.context,
                                                  event,
                                                  receiver,
                                                  argument,
                                                  &local_mutation,
                                                  &mutation_error)) {
            ReportNativeHookCallbackExceptionLocked(state.context,
                                                    event.hook_id,
                                                    event.phase,
                                                    mutation_error);
        }
    }
    if (event.phase == HookEventPhase::kEnter) {
        ReleaseNativeHookArgsArray(state.context, callbacks, argument);
    } else {
        ReleaseNativeHookReturnValue(state.context, callbacks, argument);
    }
    JS_FreeValue(state.context, callback);
    ReleaseNativeHookInvocationReceiver(state.context, callbacks, receiver);
    if (event.phase == HookEventPhase::kLeave) {
        if (callbacks->blocking &&
            callbacks->active_sync_invocation_id == event.invocation_id &&
            JS_IsObject(callbacks->active_sync_invocation_receiver)) {
            JS_FreeValue(state.context, callbacks->active_sync_invocation_receiver);
            callbacks->active_sync_invocation_receiver = JS_UNDEFINED;
            callbacks->active_sync_invocation_id = 0u;
        } else {
            auto script_it = state.active_native_invocations.find(script_id);
            if (script_it != state.active_native_invocations.end()) {
                auto invocation_it = script_it->second.find(event.invocation_id);
                if (invocation_it != script_it->second.end()) {
                    JS_FreeValue(state.context, invocation_it->second);
                    script_it->second.erase(invocation_it);
                }
                if (script_it->second.empty()) {
                    state.active_native_invocations.erase(script_it);
                }
            }
        }
        if (NativeHookInvocationContextIsDirty(state.context, callbacks)) {
            JS_FreeValue(state.context, callbacks->cached_invocation_receiver);
            callbacks->cached_invocation_receiver = JS_UNDEFINED;
            callbacks->cached_invocation_receiver_property_count = 0u;
        }
    }
    if (JS_IsException(result)) {
        const std::string exception_text = GetExceptionString(state.context);
        ReportNativeHookCallbackExceptionLocked(state.context,
                                                event.hook_id,
                                                event.phase,
                                                exception_text);
        JS_FreeValue(state.context, result);
        if (mutation_result != nullptr) {
            *mutation_result = HookInvocationMutationResult{};
        }
        return true;
    }
    JS_FreeValue(state.context, result);

    if (mutation_result != nullptr) {
        *mutation_result = local_mutation;
    }
    return true;
}

bool Utf8ToUtf16(const char* text, size_t length, std::vector<uint16_t>* units_out) {
    if (units_out == nullptr) {
        return false;
    }

    std::vector<uint32_t> code_points;
    if (!DecodeUtf8CodePoints(text, length, &code_points)) {
        return false;
    }

    units_out->clear();
    for (uint32_t code_point : code_points) {
        if (code_point <= 0xffffu) {
            units_out->push_back(static_cast<uint16_t>(code_point));
            continue;
        }

        code_point -= 0x10000u;
        units_out->push_back(static_cast<uint16_t>(0xd800u + (code_point >> 10)));
        units_out->push_back(static_cast<uint16_t>(0xdc00u + (code_point & 0x3ffu)));
    }

    return true;
}

bool Utf16ToUtf8(const uint16_t* units, size_t length, std::string* text_out) {
    if (text_out == nullptr || (units == nullptr && length != 0u)) {
        return false;
    }

    text_out->clear();
    for (size_t index = 0; index < length; ++index) {
        uint32_t code_point = units[index];
        if (code_point >= 0xd800u && code_point <= 0xdbffu) {
            if (index + 1u >= length) {
                return false;
            }
            const uint32_t low = units[index + 1u];
            if (low < 0xdc00u || low > 0xdfffu) {
                return false;
            }
            code_point = 0x10000u + (((code_point - 0xd800u) << 10) | (low - 0xdc00u));
            ++index;
        } else if (code_point >= 0xdc00u && code_point <= 0xdfffu) {
            return false;
        }

        if (!AppendUtf8CodePoint(code_point, text_out)) {
            return false;
        }
    }

    return true;
}

template <typename To, typename From>
To BitCastValue(const From& from) {
    static_assert(sizeof(To) == sizeof(From), "bit cast size mismatch");
    To to;
    std::memcpy(&to, &from, sizeof(To));
    return to;
}

bool IsSignedIntegerType(NativeFunctionValueType type) {
    return type == NativeFunctionValueType::kInt8 ||
           type == NativeFunctionValueType::kInt16 ||
           type == NativeFunctionValueType::kInt32 ||
           type == NativeFunctionValueType::kInt64;
}

bool IsUnsignedIntegerType(NativeFunctionValueType type) {
    return type == NativeFunctionValueType::kBool ||
           type == NativeFunctionValueType::kUInt8 ||
           type == NativeFunctionValueType::kUInt16 ||
           type == NativeFunctionValueType::kUInt32 ||
           type == NativeFunctionValueType::kUInt64;
}

bool ParseJsNativeCallValue(JSContext* ctx,
                            JSValueConst value,
                            NativeFunctionValueType type,
                            NativeCallValue* result_out,
                            const char** error_message_out) {
    if (result_out == nullptr) {
        return false;
    }
    result_out->raw = 0u;
    result_out->number = 0.0;

    switch (type) {
        case NativeFunctionValueType::kBool:
            result_out->raw = JS_ToBool(ctx, value) != 0 ? 1u : 0u;
            result_out->number = result_out->raw != 0u ? 1.0 : 0.0;
            return true;
        case NativeFunctionValueType::kInt8:
        case NativeFunctionValueType::kInt16:
        case NativeFunctionValueType::kInt32: {
            int32_t parsed = 0;
            if (JS_ToInt32(ctx, &parsed, value) < 0) {
                if (error_message_out != nullptr) {
                    *error_message_out = "NativeFunction signed integer argument must be an integer";
                }
                return false;
            }
            if (type == NativeFunctionValueType::kInt8) {
                const int8_t narrowed = static_cast<int8_t>(parsed);
                result_out->raw = static_cast<uint64_t>(static_cast<int64_t>(narrowed));
                result_out->number = static_cast<double>(narrowed);
            } else if (type == NativeFunctionValueType::kInt16) {
                const int16_t narrowed = static_cast<int16_t>(parsed);
                result_out->raw = static_cast<uint64_t>(static_cast<int64_t>(narrowed));
                result_out->number = static_cast<double>(narrowed);
            } else {
                result_out->raw = static_cast<uint64_t>(static_cast<int64_t>(parsed));
                result_out->number = static_cast<double>(parsed);
            }
            return true;
        }
        case NativeFunctionValueType::kUInt8:
        case NativeFunctionValueType::kUInt16:
        case NativeFunctionValueType::kUInt32: {
            uint32_t parsed = 0u;
            if (JS_ToUint32(ctx, &parsed, value) < 0) {
                if (error_message_out != nullptr) {
                    *error_message_out = "NativeFunction unsigned integer argument must be an integer";
                }
                return false;
            }
            if (type == NativeFunctionValueType::kUInt8) {
                const uint8_t narrowed = static_cast<uint8_t>(parsed);
                result_out->raw = narrowed;
                result_out->number = static_cast<double>(narrowed);
            } else if (type == NativeFunctionValueType::kUInt16) {
                const uint16_t narrowed = static_cast<uint16_t>(parsed);
                result_out->raw = narrowed;
                result_out->number = static_cast<double>(narrowed);
            } else {
                result_out->raw = parsed;
                result_out->number = static_cast<double>(parsed);
            }
            return true;
        }
        case NativeFunctionValueType::kInt64:
        case NativeFunctionValueType::kUInt64: {
            uint64_t raw_value = 0u;
            bool is_signed = false;
            if (!ParseInteger64Value(ctx, value, &raw_value, &is_signed)) {
                if (error_message_out != nullptr) {
                    *error_message_out = "NativeFunction 64-bit integer argument must be a number, string, or Int64/UInt64";
                }
                return false;
            }
            result_out->raw = raw_value;
            result_out->number = is_signed ? static_cast<double>(static_cast<int64_t>(raw_value))
                                           : static_cast<double>(raw_value);
            return true;
        }
        case NativeFunctionValueType::kFloat:
        case NativeFunctionValueType::kDouble: {
            double parsed = 0.0;
            if (JS_ToFloat64(ctx, &parsed, value) < 0) {
                if (error_message_out != nullptr) {
                    *error_message_out = "NativeFunction floating-point argument must be a number";
                }
                return false;
            }
            if (type == NativeFunctionValueType::kFloat) {
                const float narrowed = static_cast<float>(parsed);
                result_out->raw = BitCastValue<uint32_t>(narrowed);
                result_out->number = static_cast<double>(narrowed);
            } else {
                result_out->raw = BitCastValue<uint64_t>(parsed);
                result_out->number = parsed;
            }
            return true;
        }
        case NativeFunctionValueType::kPointer: {
            uint64_t parsed = 0u;
            if (!ParsePointerValue(ctx, value, &parsed)) {
                if (error_message_out != nullptr) {
                    *error_message_out = "NativeFunction pointer argument must be a pointer value";
                }
                return false;
            }
            result_out->raw = parsed;
            result_out->number = static_cast<double>(parsed);
            return true;
        }
        default:
            return false;
    }
}

JSValue NativeCallValueToJs(JSContext* ctx,
                            NativeFunctionValueType type,
                            const NativeCallValue& value) {
    switch (type) {
        case NativeFunctionValueType::kVoid:
            return JS_UNDEFINED;
        case NativeFunctionValueType::kBool:
            return JS_NewBool(ctx, value.raw != 0u ? 1 : 0);
        case NativeFunctionValueType::kInt8:
            return JS_NewInt32(ctx, static_cast<int8_t>(value.raw));
        case NativeFunctionValueType::kUInt8:
            return JS_NewUint32(ctx, static_cast<uint8_t>(value.raw));
        case NativeFunctionValueType::kInt16:
            return JS_NewInt32(ctx, static_cast<int16_t>(value.raw));
        case NativeFunctionValueType::kUInt16:
            return JS_NewUint32(ctx, static_cast<uint16_t>(value.raw));
        case NativeFunctionValueType::kInt32:
            return JS_NewInt32(ctx, static_cast<int32_t>(value.raw));
        case NativeFunctionValueType::kUInt32:
            return JS_NewUint32(ctx, static_cast<uint32_t>(value.raw));
        case NativeFunctionValueType::kInt64:
            return MakeInteger64Object(ctx, value.raw, true);
        case NativeFunctionValueType::kUInt64:
            return MakeInteger64Object(ctx, value.raw, false);
        case NativeFunctionValueType::kFloat:
        case NativeFunctionValueType::kDouble:
            return JS_NewFloat64(ctx, value.number);
        case NativeFunctionValueType::kPointer:
            return MakeNativePointer(ctx, value.raw);
        default:
            return JS_EXCEPTION;
    }
}

uint64_t NativeCallValueToRawResult(NativeFunctionValueType type, const NativeCallValue& value) {
    switch (type) {
        case NativeFunctionValueType::kVoid:
            return 0u;
        case NativeFunctionValueType::kBool:
        case NativeFunctionValueType::kUInt8:
        case NativeFunctionValueType::kUInt16:
        case NativeFunctionValueType::kUInt32:
        case NativeFunctionValueType::kUInt64:
        case NativeFunctionValueType::kPointer:
            return value.raw;
        case NativeFunctionValueType::kInt8:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(value.raw)));
        case NativeFunctionValueType::kInt16:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(value.raw)));
        case NativeFunctionValueType::kInt32:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value.raw)));
        case NativeFunctionValueType::kInt64:
            return value.raw;
        case NativeFunctionValueType::kFloat: {
            const float narrowed = static_cast<float>(value.number);
            return BitCastValue<uint32_t>(narrowed);
        }
        case NativeFunctionValueType::kDouble:
            return BitCastValue<uint64_t>(value.number);
        default:
            return 0u;
    }
}

template <typename T>
T NativeCallValueAs(const NativeCallValue& value);

template <>
bool NativeCallValueAs<bool>(const NativeCallValue& value) {
    return value.raw != 0u;
}

template <>
int8_t NativeCallValueAs<int8_t>(const NativeCallValue& value) {
    return static_cast<int8_t>(value.raw);
}

template <>
uint8_t NativeCallValueAs<uint8_t>(const NativeCallValue& value) {
    return static_cast<uint8_t>(value.raw);
}

template <>
int16_t NativeCallValueAs<int16_t>(const NativeCallValue& value) {
    return static_cast<int16_t>(value.raw);
}

template <>
uint16_t NativeCallValueAs<uint16_t>(const NativeCallValue& value) {
    return static_cast<uint16_t>(value.raw);
}

template <>
int32_t NativeCallValueAs<int32_t>(const NativeCallValue& value) {
    return static_cast<int32_t>(value.raw);
}

template <>
uint32_t NativeCallValueAs<uint32_t>(const NativeCallValue& value) {
    return static_cast<uint32_t>(value.raw);
}

template <>
int64_t NativeCallValueAs<int64_t>(const NativeCallValue& value) {
    return static_cast<int64_t>(value.raw);
}

template <>
uint64_t NativeCallValueAs<uint64_t>(const NativeCallValue& value) {
    return value.raw;
}

template <>
float NativeCallValueAs<float>(const NativeCallValue& value) {
    return static_cast<float>(value.number);
}

template <>
double NativeCallValueAs<double>(const NativeCallValue& value) {
    return value.number;
}

template <typename T>
void StoreTypedNativeCallValue(T typed_value, NativeCallValue* out) {
    out->raw = static_cast<uint64_t>(typed_value);
    out->number = static_cast<double>(typed_value);
}

template <>
void StoreTypedNativeCallValue<bool>(bool typed_value, NativeCallValue* out) {
    out->raw = typed_value ? 1u : 0u;
    out->number = typed_value ? 1.0 : 0.0;
}

template <>
void StoreTypedNativeCallValue<float>(float typed_value, NativeCallValue* out) {
    out->raw = BitCastValue<uint32_t>(typed_value);
    out->number = static_cast<double>(typed_value);
}

template <>
void StoreTypedNativeCallValue<double>(double typed_value, NativeCallValue* out) {
    out->raw = BitCastValue<uint64_t>(typed_value);
    out->number = typed_value;
}

template <>
void StoreTypedNativeCallValue<uint64_t>(uint64_t typed_value, NativeCallValue* out) {
    out->raw = typed_value;
    out->number = static_cast<double>(out->raw);
}

uint64_t CallNativeFunctionRawU64(uint64_t target_address,
                                  const uint64_t* args,
                                  uint32_t argc) {
    switch (argc) {
        case 0:
            return reinterpret_cast<uint64_t(*)()>(static_cast<uintptr_t>(target_address))();
        case 1:
            return reinterpret_cast<uint64_t(*)(uint64_t)>(static_cast<uintptr_t>(target_address))(args[0]);
        case 2:
            return reinterpret_cast<uint64_t(*)(uint64_t, uint64_t)>(
                static_cast<uintptr_t>(target_address))(args[0], args[1]);
        case 3:
            return reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t)>(
                static_cast<uintptr_t>(target_address))(args[0], args[1], args[2]);
        case 4:
            return reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(
                static_cast<uintptr_t>(target_address))(args[0], args[1], args[2], args[3]);
        default:
            return 0u;
    }
}

void CallNativeFunctionRawVoid(uint64_t target_address,
                               const uint64_t* args,
                               uint32_t argc) {
    switch (argc) {
        case 0:
            reinterpret_cast<void(*)()>(static_cast<uintptr_t>(target_address))();
            return;
        case 1:
            reinterpret_cast<void(*)(uint64_t)>(static_cast<uintptr_t>(target_address))(args[0]);
            return;
        case 2:
            reinterpret_cast<void(*)(uint64_t, uint64_t)>(static_cast<uintptr_t>(target_address))(
                args[0], args[1]);
            return;
        case 3:
            reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t)>(
                static_cast<uintptr_t>(target_address))(args[0], args[1], args[2]);
            return;
        case 4:
            reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(
                static_cast<uintptr_t>(target_address))(args[0], args[1], args[2], args[3]);
            return;
        default:
            return;
    }
}

bool NativeFunctionTypeUsesFpAbi(NativeFunctionValueType type) {
    return type == NativeFunctionValueType::kFloat || type == NativeFunctionValueType::kDouble;
}

enum class NativeAbiArgKind : uint8_t {
    kRaw = 0u,
    kFloat = 1u,
    kDouble = 2u,
};

NativeAbiArgKind GetNativeAbiArgKind(NativeFunctionValueType type) {
    switch (type) {
        case NativeFunctionValueType::kFloat:
            return NativeAbiArgKind::kFloat;
        case NativeFunctionValueType::kDouble:
            return NativeAbiArgKind::kDouble;
        default:
            return NativeAbiArgKind::kRaw;
    }
}

void StoreRawNativeCallValue(NativeFunctionValueType return_type,
                             uint64_t raw_value,
                             NativeCallValue* out) {
    out->raw = raw_value;
    switch (return_type) {
        case NativeFunctionValueType::kBool:
            out->number = (raw_value & 1u) != 0u ? 1.0 : 0.0;
            break;
        case NativeFunctionValueType::kInt8:
            out->number = static_cast<double>(static_cast<int8_t>(raw_value));
            break;
        case NativeFunctionValueType::kUInt8:
            out->number = static_cast<double>(static_cast<uint8_t>(raw_value));
            break;
        case NativeFunctionValueType::kInt16:
            out->number = static_cast<double>(static_cast<int16_t>(raw_value));
            break;
        case NativeFunctionValueType::kUInt16:
            out->number = static_cast<double>(static_cast<uint16_t>(raw_value));
            break;
        case NativeFunctionValueType::kInt32:
            out->number = static_cast<double>(static_cast<int32_t>(raw_value));
            break;
        case NativeFunctionValueType::kUInt32:
            out->number = static_cast<double>(static_cast<uint32_t>(raw_value));
            break;
        case NativeFunctionValueType::kInt64:
            out->number = static_cast<double>(static_cast<int64_t>(raw_value));
            break;
        case NativeFunctionValueType::kUInt64:
        case NativeFunctionValueType::kPointer:
            out->number = static_cast<double>(raw_value);
            break;
        case NativeFunctionValueType::kVoid:
        case NativeFunctionValueType::kFloat:
        case NativeFunctionValueType::kDouble:
        default:
            out->number = 0.0;
            break;
    }
}

template <typename Ret>
void StoreDispatchedNativeCallValue(NativeFunctionValueType return_type,
                                    Ret typed_value,
                                    NativeCallValue* out) {
    if constexpr (std::is_same_v<Ret, uint64_t>) {
        StoreRawNativeCallValue(return_type, typed_value, out);
    } else {
        StoreTypedNativeCallValue<Ret>(typed_value, out);
    }
}

template <typename Ret>
bool InvokeTypedNativeFunction0(uint64_t target_address,
                                NativeFunctionValueType return_type,
                                NativeCallValue* result_out) {
    if constexpr (std::is_same_v<Ret, void>) {
        reinterpret_cast<void(*)()>(static_cast<uintptr_t>(target_address))();
        result_out->raw = 0u;
        result_out->number = 0.0;
    } else {
        const Ret value = reinterpret_cast<Ret(*)()>(static_cast<uintptr_t>(target_address))();
        StoreDispatchedNativeCallValue<Ret>(return_type, value, result_out);
    }
    return true;
}

template <typename Ret, typename Arg0>
bool InvokeTypedNativeFunction1(uint64_t target_address,
                                NativeFunctionValueType return_type,
                                const NativeCallValue* args,
                                NativeCallValue* result_out) {
    if constexpr (std::is_same_v<Ret, void>) {
        reinterpret_cast<void(*)(Arg0)>(static_cast<uintptr_t>(target_address))(
            NativeCallValueAs<Arg0>(args[0]));
        result_out->raw = 0u;
        result_out->number = 0.0;
    } else {
        const Ret value = reinterpret_cast<Ret(*)(Arg0)>(static_cast<uintptr_t>(target_address))(
            NativeCallValueAs<Arg0>(args[0]));
        StoreDispatchedNativeCallValue<Ret>(return_type, value, result_out);
    }
    return true;
}

template <typename Ret, typename Arg0, typename Arg1>
bool InvokeTypedNativeFunction2(uint64_t target_address,
                                NativeFunctionValueType return_type,
                                const NativeCallValue* args,
                                NativeCallValue* result_out) {
    if constexpr (std::is_same_v<Ret, void>) {
        reinterpret_cast<void(*)(Arg0, Arg1)>(static_cast<uintptr_t>(target_address))(
            NativeCallValueAs<Arg0>(args[0]),
            NativeCallValueAs<Arg1>(args[1]));
        result_out->raw = 0u;
        result_out->number = 0.0;
    } else {
        const Ret value = reinterpret_cast<Ret(*)(Arg0, Arg1)>(static_cast<uintptr_t>(target_address))(
            NativeCallValueAs<Arg0>(args[0]),
            NativeCallValueAs<Arg1>(args[1]));
        StoreDispatchedNativeCallValue<Ret>(return_type, value, result_out);
    }
    return true;
}

template <typename Ret>
bool DispatchTypedNativeFunction1(uint64_t target_address,
                                  NativeFunctionValueType return_type,
                                  NativeAbiArgKind arg0_kind,
                                  const NativeCallValue* args,
                                  NativeCallValue* result_out) {
    switch (arg0_kind) {
        case NativeAbiArgKind::kRaw:
            return InvokeTypedNativeFunction1<Ret, uint64_t>(
                target_address, return_type, args, result_out);
        case NativeAbiArgKind::kFloat:
            return InvokeTypedNativeFunction1<Ret, float>(
                target_address, return_type, args, result_out);
        case NativeAbiArgKind::kDouble:
            return InvokeTypedNativeFunction1<Ret, double>(
                target_address, return_type, args, result_out);
        default:
            return false;
    }
}

template <typename Ret, typename Arg0>
bool DispatchTypedNativeFunction2Arg1(uint64_t target_address,
                                      NativeFunctionValueType return_type,
                                      NativeAbiArgKind arg1_kind,
                                      const NativeCallValue* args,
                                      NativeCallValue* result_out) {
    switch (arg1_kind) {
        case NativeAbiArgKind::kRaw:
            return InvokeTypedNativeFunction2<Ret, Arg0, uint64_t>(
                target_address, return_type, args, result_out);
        case NativeAbiArgKind::kFloat:
            return InvokeTypedNativeFunction2<Ret, Arg0, float>(
                target_address, return_type, args, result_out);
        case NativeAbiArgKind::kDouble:
            return InvokeTypedNativeFunction2<Ret, Arg0, double>(
                target_address, return_type, args, result_out);
        default:
            return false;
    }
}

template <typename Ret>
bool DispatchTypedNativeFunction2(uint64_t target_address,
                                  NativeFunctionValueType return_type,
                                  NativeAbiArgKind arg0_kind,
                                  NativeAbiArgKind arg1_kind,
                                  const NativeCallValue* args,
                                  NativeCallValue* result_out) {
    switch (arg0_kind) {
        case NativeAbiArgKind::kRaw:
            return DispatchTypedNativeFunction2Arg1<Ret, uint64_t>(
                target_address, return_type, arg1_kind, args, result_out);
        case NativeAbiArgKind::kFloat:
            return DispatchTypedNativeFunction2Arg1<Ret, float>(
                target_address, return_type, arg1_kind, args, result_out);
        case NativeAbiArgKind::kDouble:
            return DispatchTypedNativeFunction2Arg1<Ret, double>(
                target_address, return_type, arg1_kind, args, result_out);
        default:
            return false;
    }
}

template <typename Ret>
bool DispatchTypedNativeFunctionWithAbi(uint64_t target_address,
                                        NativeFunctionValueType return_type,
                                        const std::vector<NativeFunctionValueType>& arg_types,
                                        const NativeCallValue* args,
                                        NativeCallValue* result_out) {
    switch (arg_types.size()) {
        case 0:
            return InvokeTypedNativeFunction0<Ret>(target_address, return_type, result_out);
        case 1:
            return DispatchTypedNativeFunction1<Ret>(
                target_address,
                return_type,
                GetNativeAbiArgKind(arg_types[0]),
                args,
                result_out);
        case 2:
            return DispatchTypedNativeFunction2<Ret>(
                target_address,
                return_type,
                GetNativeAbiArgKind(arg_types[0]),
                GetNativeAbiArgKind(arg_types[1]),
                args,
                result_out);
        default:
            return false;
    }
}

bool DispatchTypedNativeFunction(uint64_t target_address,
                                 NativeFunctionValueType return_type,
                                 const std::vector<NativeFunctionValueType>& arg_types,
                                 const NativeCallValue* args,
                                 NativeCallValue* result_out) {
    bool uses_fp_abi = NativeFunctionTypeUsesFpAbi(return_type);
    uint64_t raw_args[4] = {};
    for (size_t index = 0u; index < arg_types.size(); ++index) {
        uses_fp_abi = uses_fp_abi || NativeFunctionTypeUsesFpAbi(arg_types[index]);
        raw_args[index] = args[index].raw;
    }

    if (!uses_fp_abi) {
        if (return_type == NativeFunctionValueType::kVoid) {
            CallNativeFunctionRawVoid(target_address, raw_args, static_cast<uint32_t>(arg_types.size()));
            result_out->raw = 0u;
            result_out->number = 0.0;
            return true;
        }
        result_out->raw = CallNativeFunctionRawU64(
            target_address, raw_args, static_cast<uint32_t>(arg_types.size()));
        result_out->number = IsSignedIntegerType(return_type)
                                 ? static_cast<double>(static_cast<int64_t>(result_out->raw))
                                 : static_cast<double>(result_out->raw);
        return true;
    }

    if (return_type == NativeFunctionValueType::kVoid) {
        return DispatchTypedNativeFunctionWithAbi<void>(
            target_address, return_type, arg_types, args, result_out);
    }
    if (return_type == NativeFunctionValueType::kFloat) {
        return DispatchTypedNativeFunctionWithAbi<float>(
            target_address, return_type, arg_types, args, result_out);
    }
    if (return_type == NativeFunctionValueType::kDouble) {
        return DispatchTypedNativeFunctionWithAbi<double>(
            target_address, return_type, arg_types, args, result_out);
    }
    return DispatchTypedNativeFunctionWithAbi<uint64_t>(
        target_address, return_type, arg_types, args, result_out);
}

std::string TrimAsciiWhitespace(const std::string& value) {
    size_t start = 0u;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string GetPathBaseName(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1u);
}

bool CollectLoadedNativeModules(std::vector<NativeModuleRecord>* modules,
                                std::string* error_message) {
    if (modules == nullptr) {
        SetError(error_message, "modules is null");
        return false;
    }
    modules->clear();

#if defined(_WIN32)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        SetError(error_message, "CreateToolhelp32Snapshot failed");
        return false;
    }

    MODULEENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (!Module32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        SetError(error_message, "Module32First failed");
        return false;
    }

    do {
        NativeModuleRecord record = {};
        record.name = entry.szModule;
        record.base = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(entry.modBaseAddr));
        record.size = static_cast<uint64_t>(entry.modBaseSize);
        record.path = entry.szExePath;
        modules->push_back(std::move(record));
    } while (Module32Next(snapshot, &entry));

    CloseHandle(snapshot);
#else
#if defined(__ANDROID__)
    struct AndroidModuleCollectionContext {
        std::vector<NativeModuleRecord>* modules = nullptr;
        std::unordered_set<uint64_t> seen_bases;
    } context = {modules, {}};

    auto collect_from_phdr = [](struct dl_phdr_info* info, size_t, void* arg) -> int {
        if (info == nullptr || arg == nullptr || info->dlpi_phdr == nullptr || info->dlpi_phnum == 0u) {
            return 0;
        }

        auto* context = static_cast<AndroidModuleCollectionContext*>(arg);
        uint64_t min_vaddr = UINT64_MAX;
        uint64_t max_vaddr = 0u;
        for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
            const ElfW(Phdr)& phdr = info->dlpi_phdr[index];
            if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0u) {
                continue;
            }
            if (static_cast<uint64_t>(phdr.p_vaddr) < min_vaddr) {
                min_vaddr = static_cast<uint64_t>(phdr.p_vaddr);
            }
            const uint64_t end = static_cast<uint64_t>(phdr.p_vaddr) +
                                 static_cast<uint64_t>(phdr.p_memsz);
            if (end > max_vaddr) {
                max_vaddr = end;
            }
        }
        if (min_vaddr == UINT64_MAX || max_vaddr <= min_vaddr) {
            return 0;
        }

        const uint64_t base = static_cast<uint64_t>(info->dlpi_addr) + min_vaddr;
        if (!context->seen_bases.insert(base).second) {
            return 0;
        }

        std::string path = info->dlpi_name != nullptr ? info->dlpi_name : "";
        if (path.empty()) {
            return 0;
        }

        NativeModuleRecord record = {};
        record.name = GetPathBaseName(path);
        record.base = base;
        record.size = max_vaddr - min_vaddr;
        record.path = path;
        record.load_bias = static_cast<uint64_t>(info->dlpi_addr);
        record.phdr = info->dlpi_phdr;
        record.phnum = static_cast<size_t>(info->dlpi_phnum);
        context->modules->push_back(std::move(record));
        return 0;
    };

    (void)xdl_iterate_phdr(collect_from_phdr, &context, XDL_DEFAULT);
    if (!modules->empty()) {
        std::sort(modules->begin(),
                  modules->end(),
                  [](const NativeModuleRecord& left, const NativeModuleRecord& right) {
                      if (left.base != right.base) {
                          return left.base < right.base;
                      }
                      return left.path < right.path;
                  });
        return true;
    }
#endif

    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        SetError(error_message, "open /proc/self/maps failed");
        return false;
    }

    std::vector<NativeModuleMapping> mappings;
    char line[1024] = {};
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        unsigned long long start = 0u;
        unsigned long long end = 0u;
        char perms[5] = {};
        unsigned long long offset = 0u;
        char device[32] = {};
        unsigned long long inode = 0u;
        int path_offset = 0;
        if (std::sscanf(line,
                        "%llx-%llx %4s %llx %31s %llu %n",
                        &start,
                        &end,
                        perms,
                        &offset,
                        device,
                        &inode,
                        &path_offset) < 6) {
            continue;
        }

        std::string path;
        if (path_offset > 0 && static_cast<size_t>(path_offset) < std::strlen(line)) {
            path = TrimAsciiWhitespace(line + path_offset);
        }
        if (path.empty() || path[0] == '[') {
            continue;
        }
        NativeModuleMapping mapping = {};
        mapping.path = path;
        mapping.start = static_cast<uint64_t>(start);
        mapping.end = static_cast<uint64_t>(end);
        mappings.push_back(std::move(mapping));
    }

    std::fclose(maps);
    modules->reserve(mappings.size());
    if (!mappings.empty()) {
        std::sort(mappings.begin(),
                  mappings.end(),
                  [](const NativeModuleMapping& left, const NativeModuleMapping& right) {
                      if (left.path != right.path) {
                          return left.path < right.path;
                      }
                      if (left.start != right.start) {
                          return left.start < right.start;
                      }
                      return left.end < right.end;
                  });

        NativeModuleMapping current = mappings[0];
        for (size_t index = 1; index < mappings.size(); ++index) {
            const NativeModuleMapping& mapping = mappings[index];
            if (mapping.path == current.path && mapping.start <= current.end) {
                if (mapping.end > current.end) {
                    current.end = mapping.end;
                }
                continue;
            }

            if (current.end > current.start) {
                NativeModuleRecord record = {};
                record.name = GetPathBaseName(current.path);
                record.base = current.start;
                record.size = current.end - current.start;
                record.path = current.path;
                modules->push_back(std::move(record));
            }
            current = mapping;
        }

        if (current.end > current.start) {
            NativeModuleRecord record = {};
            record.name = GetPathBaseName(current.path);
            record.base = current.start;
            record.size = current.end - current.start;
            record.path = current.path;
            modules->push_back(std::move(record));
        }
    }
#endif

    std::sort(modules->begin(),
              modules->end(),
              [](const NativeModuleRecord& left, const NativeModuleRecord& right) {
                  if (left.base != right.base) {
                      return left.base < right.base;
                  }
                  return left.path < right.path;
              });
    return true;
}

bool GetArrayLength(JSContext* ctx, JSValueConst value, uint32_t* length_out) {
    if (length_out == nullptr) {
        return false;
    }
    *length_out = 0u;

    JSValue length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(length_value)) {
        JS_FreeValue(ctx, length_value);
        return false;
    }

    const int status = JS_ToUint32(ctx, length_out, length_value);
    JS_FreeValue(ctx, length_value);
    return status >= 0;
}

bool FindModuleIndexByAddressInSnapshot(JSContext* ctx,
                                        JSValueConst modules,
                                        uint64_t address,
                                        uint32_t* index_out,
                                        std::string* error_message) {
    if (index_out == nullptr) {
        SetError(error_message, "index_out is null");
        return false;
    }
    *index_out = 0u;

    uint32_t length = 0u;
    if (!GetArrayLength(ctx, modules, &length)) {
        SetError(error_message, "read module snapshot length failed");
        return false;
    }

    for (uint32_t index = 0u; index < length; ++index) {
        JSValue module = JS_GetPropertyUint32(ctx, modules, index);
        if (JS_IsException(module)) {
            JS_FreeValue(ctx, module);
            SetError(error_message, "read module snapshot entry failed");
            return false;
        }

        JSValue base_value = JS_GetPropertyStr(ctx, module, "base");
        JSValue size_value = JS_GetPropertyStr(ctx, module, "size");
        if (JS_IsException(base_value) || JS_IsException(size_value)) {
            JS_FreeValue(ctx, base_value);
            JS_FreeValue(ctx, size_value);
            JS_FreeValue(ctx, module);
            SetError(error_message, "read module snapshot fields failed");
            return false;
        }

        uint64_t base = 0u;
        uint32_t size = 0u;
        const bool base_ok = ParsePointerValue(ctx, base_value, &base);
        const bool size_ok = JS_ToUint32(ctx, &size, size_value) >= 0;
        JS_FreeValue(ctx, base_value);
        JS_FreeValue(ctx, size_value);
        JS_FreeValue(ctx, module);
        if (!base_ok || !size_ok) {
            SetError(error_message, "parse module snapshot fields failed");
            return false;
        }

        const uint64_t module_end = base + static_cast<uint64_t>(size);
        if (address >= base && address < module_end) {
            *index_out = index;
            return true;
        }
    }

    *index_out = UINT32_MAX;
    return true;
}

bool ParseModuleLookupAddressArgument(JSContext* ctx,
                                      const char* api_name,
                                      int argc,
                                      JSValueConst* argv,
                                      uint64_t* address_out) {
    if (address_out == nullptr) {
        return false;
    }
    *address_out = 0u;

    if (argc < 1) {
        JS_ThrowTypeError(ctx, "%s requires address", api_name);
        return false;
    }

    if (!ParsePointerValue(ctx, argv[0], address_out)) {
        JS_ThrowTypeError(ctx, "%s address must be a pointer value", api_name);
        return false;
    }
    if (*address_out == 0u) {
        JS_ThrowTypeError(ctx, "%s address must be a non-zero pointer value", api_name);
        return false;
    }

    *address_out = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(*address_out)));
    return true;
}

bool FindLoadedModuleBaseAddressByName(const char* module_name,
                                       uint64_t* module_base_out,
                                       std::string* error_message) {
    if (module_base_out == nullptr) {
        SetError(error_message, "module_base_out is null");
        return false;
    }
    *module_base_out = 0u;
    if (module_name == nullptr || module_name[0] == '\0') {
        SetError(error_message, "module_name is required");
        return false;
    }

#if defined(_WIN32)
    HMODULE module = GetModuleHandleA(module_name);
    if (module == nullptr) {
        return true;
    }
    *module_base_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(module));
    return true;
#elif defined(__ANDROID__) || defined(__linux__)
    void* module_base = nullptr;
    if (!ElfHooker::get_module_info(0, module_name, &module_base, nullptr) ||
        module_base == nullptr) {
        return true;
    }
    *module_base_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(module_base));
    return true;
#else
    SetError(error_message, "module base lookup not implemented");
    return false;
#endif
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
    address = NormalizeProcessAddressForRangeCheck(address);
    ReadableMappingCacheEntry& cache = GetReadableMappingCacheEntry();
    if (cache.valid &&
        address >= cache.start &&
        address < cache.end) {
        *mapping_end = cache.end;
        return true;
    }
    GetReadableMappingLookupCount().fetch_add(1u, std::memory_order_relaxed);

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
    cache.start = region_start;
    cache.end = region_end;
    cache.valid = true;
    *mapping_end = region_end;
    return true;
}

bool IsWritableProtection(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    const DWORD base_protect = protect & 0xffu;
    return base_protect == PAGE_READWRITE ||
           base_protect == PAGE_WRITECOPY ||
           base_protect == PAGE_EXECUTE_READWRITE ||
           base_protect == PAGE_EXECUTE_WRITECOPY;
}

bool FindWritableMappingEnd(uintptr_t address, uintptr_t* mapping_end) {
    if (mapping_end == nullptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect)) {
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
    GetReadableMemoryProbeCount().fetch_add(1u, std::memory_order_relaxed);
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

bool IsWritableMemoryRange(uintptr_t address, size_t length) {
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
        if (info.State != MEM_COMMIT || !IsWritableProtection(info.Protect)) {
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
bool FindReadableMappingEndInSnapshot(const std::vector<ReadableMappingRecord>& snapshot,
                                      uintptr_t address,
                                      uintptr_t* mapping_end) {
    if (mapping_end == nullptr) {
        return false;
    }

    for (const ReadableMappingRecord& record : snapshot) {
        if (address >= record.start && address < record.end) {
            *mapping_end = record.end;
            return true;
        }
    }
    return false;
}

bool RefreshReadableMappingSnapshot() {
    std::vector<ReadableMappingRecord> refreshed;

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
        if (permissions[0] != 'r' || end <= start) {
            continue;
        }

        ReadableMappingRecord record = {};
        record.start = static_cast<uintptr_t>(start);
        record.end = static_cast<uintptr_t>(end);
        refreshed.push_back(record);
    }

    std::fclose(maps);

    std::lock_guard<std::mutex> lock(GetReadableMappingSnapshotMutex());
    GetReadableMappingSnapshot() = std::move(refreshed);
    return true;
}

bool FindReadableMappingEnd(uintptr_t address, uintptr_t* mapping_end) {
    if (mapping_end == nullptr) {
        return false;
    }
    address = NormalizeProcessAddressForRangeCheck(address);
    ReadableMappingCacheEntry& cache = GetReadableMappingCacheEntry();
    if (cache.valid &&
        address >= cache.start &&
        address < cache.end) {
        *mapping_end = cache.end;
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(GetReadableMappingSnapshotMutex());
        if (FindReadableMappingEndInSnapshot(GetReadableMappingSnapshot(), address, mapping_end)) {
            cache.start = address;
            cache.end = *mapping_end;
            cache.valid = true;
            return true;
        }
    }

    GetReadableMappingLookupCount().fetch_add(1u, std::memory_order_relaxed);
    if (!RefreshReadableMappingSnapshot()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(GetReadableMappingSnapshotMutex());
    if (!FindReadableMappingEndInSnapshot(GetReadableMappingSnapshot(), address, mapping_end)) {
        return false;
    }

    cache.start = address;
    cache.end = *mapping_end;
    cache.valid = true;
    return true;
}

bool FindWritableMappingEnd(uintptr_t address, uintptr_t* mapping_end) {
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
        if (permissions[1] != 'w') {
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
    GetReadableMemoryProbeCount().fetch_add(1u, std::memory_order_relaxed);
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

bool IsWritableMemoryRange(uintptr_t address, size_t length) {
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
        if (!FindWritableMappingEnd(current, &mapping_end) || mapping_end <= current) {
            return false;
        }
        current = mapping_end < end ? mapping_end : end;
    }
    return true;
}
#endif

bool ReadUtf8StringFromReadableMemory(uintptr_t address,
                                      uint32_t max_length,
                                      std::string* text_out) {
    if (text_out == nullptr) {
        return false;
    }
    text_out->clear();

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

    text_out->reserve(max_length);
    for (uint32_t index = 0; index < max_length; ++index) {
        if (current >= mapping_end) {
            if (!FindReadableMappingEnd(current, &mapping_end) || mapping_end <= current) {
                return false;
            }
        }

        char ch = '\0';
        if (!TryReadMemoryBytesSafely(static_cast<uint64_t>(current), sizeof(ch), &ch)) {
            return false;
        }
        if (ch == '\0') {
            return true;
        }
        text_out->push_back(ch);

        uintptr_t next = 0;
        if (AddOverflows(current, 1u, &next)) {
            return false;
        }
        current = next;
    }

    return true;
}

size_t GetSystemPageSize() {
#if defined(_WIN32)
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);
    return info.dwPageSize > 0 ? static_cast<size_t>(info.dwPageSize) : 4096u;
#else
    const long page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0 ? static_cast<size_t>(page_size) : 4096u;
#endif
}

bool TryParseProtectionString(const std::string& protection,
#if defined(_WIN32)
                              DWORD* native_protection
#else
                              int* native_protection
#endif
) {
    if (native_protection == nullptr ||
        protection.size() != 3u ||
        (protection[0] != 'r' && protection[0] != '-') ||
        (protection[1] != 'w' && protection[1] != '-') ||
        (protection[2] != 'x' && protection[2] != '-')) {
        return false;
    }

    const bool readable = protection[0] == 'r';
    const bool writable = protection[1] == 'w';
    const bool executable = protection[2] == 'x';

#if defined(_WIN32)
    if (!readable && !writable && !executable) {
        *native_protection = PAGE_NOACCESS;
    } else if (executable) {
        if (writable) {
            *native_protection = PAGE_EXECUTE_READWRITE;
        } else if (readable) {
            *native_protection = PAGE_EXECUTE_READ;
        } else {
            *native_protection = PAGE_EXECUTE;
        }
    } else if (writable) {
        *native_protection = PAGE_READWRITE;
    } else if (readable) {
        *native_protection = PAGE_READONLY;
    } else {
        *native_protection = PAGE_NOACCESS;
    }
#else
    int result = 0;
    if (readable) {
        result |= PROT_READ;
    }
    if (writable) {
        result |= PROT_WRITE;
    }
    if (executable) {
        result |= PROT_EXEC;
    }
    *native_protection = result;
#endif

    return true;
}

bool ComputePageAlignedProtectionRange(uintptr_t address,
                                       size_t size,
                                       uintptr_t* aligned_start,
                                       size_t* aligned_size) {
    if (aligned_start == nullptr || aligned_size == nullptr || address == 0u || size == 0u) {
        return false;
    }

    const size_t page_size = GetSystemPageSize();
    if (page_size == 0u) {
        return false;
    }

    const uintptr_t page_mask = ~(static_cast<uintptr_t>(page_size) - 1u);
    uintptr_t last_address = 0;
    if (AddOverflows(address, size - 1u, &last_address)) {
        return false;
    }

    const uintptr_t start = address & page_mask;
    const uintptr_t end_page = last_address & page_mask;
    if (end_page < start) {
        return false;
    }

    const uintptr_t span = (end_page - start) + static_cast<uintptr_t>(page_size);
    *aligned_start = start;
    *aligned_size = static_cast<size_t>(span);
    return true;
}

struct NativeMemoryRangeRecord {
    uint64_t base = 0;
    uint64_t size = 0;
    std::string protection;
};

bool CollectAllNativeMemoryRanges(std::vector<NativeMemoryRangeRecord>* ranges_out);

bool TryMakeWritableProtectionString(const std::string& original_protection,
                                     std::string* writable_protection_out) {
    if (writable_protection_out == nullptr ||
        original_protection.size() != 3u ||
        (original_protection[0] != 'r' && original_protection[0] != '-') ||
        (original_protection[1] != 'w' && original_protection[1] != '-') ||
        (original_protection[2] != 'x' && original_protection[2] != '-')) {
        return false;
    }

    *writable_protection_out = original_protection;
    (*writable_protection_out)[1] = 'w';
    return true;
}

bool TryGetUniformProtectionForRange(uintptr_t address,
                                     size_t length,
                                     std::string* protection_out) {
    if (protection_out == nullptr || address == 0u || length == 0u) {
        return false;
    }

    address = NormalizeProcessAddressForRangeCheck(address);
    uintptr_t end = 0;
    if (AddOverflows(address, length, &end) || end <= address) {
        return false;
    }

    std::vector<NativeMemoryRangeRecord> ranges;
    if (!CollectAllNativeMemoryRanges(&ranges)) {
        return false;
    }

    bool found = false;
    uintptr_t covered_until = address;
    std::string protection;
    for (const NativeMemoryRangeRecord& range : ranges) {
        const uintptr_t range_base = static_cast<uintptr_t>(range.base);
        const uintptr_t range_end = range_base + static_cast<uintptr_t>(range.size);
        if (range_end <= covered_until || range_base > covered_until) {
            continue;
        }
        if (range_base > covered_until) {
            return false;
        }
        if (!found) {
            protection = range.protection;
            found = true;
        } else if (range.protection != protection) {
            return false;
        }
        covered_until = range_end < end ? range_end : end;
        if (covered_until >= end) {
            *protection_out = protection;
            return true;
        }
    }

    return false;
}

void FlushInstructionCacheForRange(uintptr_t address, size_t length) {
    if (address == 0u || length == 0u) {
        return;
    }
#if defined(_WIN32)
    (void)FlushInstructionCache(GetCurrentProcess(),
                                reinterpret_cast<const void*>(address),
                                static_cast<SIZE_T>(length));
#else
    char* start = reinterpret_cast<char*>(address);
    char* end = start + static_cast<ptrdiff_t>(length);
    __builtin___clear_cache(start, end);
#endif
}

struct MemoryScanPatternToken {
    bool wildcard = false;
    uint8_t value = 0;
};

int ParseHexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

bool TryParseMemoryScanPattern(const std::string& pattern,
                               std::vector<MemoryScanPatternToken>* tokens_out) {
    if (tokens_out == nullptr) {
        return false;
    }
    tokens_out->clear();

    std::istringstream stream(pattern);
    std::string token;
    while (stream >> token) {
        if (token == "??") {
            tokens_out->push_back({true, 0u});
            continue;
        }
        if (token.size() != 2u) {
            return false;
        }

        const int high = ParseHexNibble(token[0]);
        const int low = ParseHexNibble(token[1]);
        if (high < 0 || low < 0) {
            return false;
        }

        MemoryScanPatternToken parsed = {};
        parsed.wildcard = false;
        parsed.value = static_cast<uint8_t>((high << 4) | low);
        tokens_out->push_back(parsed);
    }

    return !tokens_out->empty();
}

JSValue MakeMemoryScanMatch(JSContext* ctx, uint64_t address, uint32_t size) {
    JSValue match = JS_NewObject(ctx);
    if (JS_IsException(match)) {
        return match;
    }

    if (JS_SetPropertyStr(ctx, match, "address", MakeNativePointer(ctx, address)) < 0 ||
        JS_SetPropertyStr(ctx, match, "size", JS_NewUint32(ctx, size)) < 0) {
        JS_FreeValue(ctx, match);
        return JS_EXCEPTION;
    }

    return match;
}

#if defined(_WIN32)
std::string FormatWindowsProtectionString(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0) {
        return "---";
    }

    const DWORD base_protect = protect & 0xffu;
    switch (base_protect) {
        case PAGE_NOACCESS:
            return "---";
        case PAGE_READONLY:
            return "r--";
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
            return "rw-";
        case PAGE_EXECUTE:
            return "--x";
        case PAGE_EXECUTE_READ:
            return "r-x";
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return "rwx";
        default:
            return "---";
    }
}
#endif

bool CollectAllNativeMemoryRanges(std::vector<NativeMemoryRangeRecord>* ranges_out) {
    if (ranges_out == nullptr) {
        return false;
    }
    ranges_out->clear();

#if defined(_WIN32)
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);

    uintptr_t current = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t max_address = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    while (current <= max_address) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t next = base + mbi.RegionSize;
        if (next <= current) {
            break;
        }

        if (mbi.State == MEM_COMMIT) {
            const std::string protection = FormatWindowsProtectionString(mbi.Protect);
            NativeMemoryRangeRecord record = {};
            record.base = static_cast<uint64_t>(base);
            record.size = static_cast<uint64_t>(mbi.RegionSize);
            record.protection = protection;
            ranges_out->push_back(record);
        }

        current = next;
    }
#else
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

        std::string protection = "---";
        protection[0] = permissions[0] == 'r' ? 'r' : '-';
        protection[1] = permissions[1] == 'w' ? 'w' : '-';
        protection[2] = permissions[2] == 'x' ? 'x' : '-';
        if (end < start) {
            continue;
        }

        NativeMemoryRangeRecord record = {};
        record.base = static_cast<uint64_t>(start);
        record.size = static_cast<uint64_t>(end - start);
        record.protection = protection;
        ranges_out->push_back(record);
    }

    std::fclose(maps);
#endif

    return true;
}

bool EnumerateNativeMemoryRanges(const std::string& protection_filter,
                                 std::vector<NativeMemoryRangeRecord>* ranges_out) {
    if (ranges_out == nullptr ||
        protection_filter.size() != 3u ||
        (protection_filter[0] != 'r' && protection_filter[0] != '-') ||
        (protection_filter[1] != 'w' && protection_filter[1] != '-') ||
        (protection_filter[2] != 'x' && protection_filter[2] != '-')) {
        return false;
    }

    std::vector<NativeMemoryRangeRecord> all_ranges;
    if (!CollectAllNativeMemoryRanges(&all_ranges)) {
        return false;
    }

    auto matches_protection_filter = [](const std::string& actual,
                                        const std::string& filter) -> bool {
        if (actual.size() != 3u || filter.size() != 3u) {
            return false;
        }
        for (size_t index = 0; index < 3u; ++index) {
            if (filter[index] == '-') {
                continue;
            }
            if (actual[index] != filter[index]) {
                return false;
            }
        }
        return true;
    };

    ranges_out->clear();
    for (const NativeMemoryRangeRecord& range : all_ranges) {
        if (matches_protection_filter(range.protection, protection_filter)) {
            ranges_out->push_back(range);
        }
    }

    return true;
}

std::string GetCurrentExecutablePath() {
#if defined(_WIN32)
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(sizeof(path)));
    if (length == 0 || length >= sizeof(path)) {
        return std::string();
    }
    return std::string(path, length);
#else
    char path[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) {
        return std::string();
    }
    path[length] = '\0';
    return std::string(path, static_cast<size_t>(length));
#endif
}

std::string GetPathBaseNameForModuleLookup(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1u);
}

const NativeModuleRecord* FindLoadedModuleByName(const std::vector<NativeModuleRecord>& modules,
                                                 const std::string& module_name) {
    auto equals_module_name = [](const std::string& left, const std::string& right) {
#if defined(_WIN32)
        if (left.size() != right.size()) {
            return false;
        }
        for (size_t index = 0; index < left.size(); ++index) {
            const unsigned char left_ch = static_cast<unsigned char>(left[index]);
            const unsigned char right_ch = static_cast<unsigned char>(right[index]);
            if (std::tolower(left_ch) != std::tolower(right_ch)) {
                return false;
            }
        }
        return true;
#else
        return left == right;
#endif
    };

    for (const NativeModuleRecord& module : modules) {
        if (equals_module_name(module.name, module_name)) {
            return &module;
        }
    }
    return nullptr;
}

const NativeModuleRecord* FindLoadedModuleByBase(const std::vector<NativeModuleRecord>& modules,
                                                 uint64_t module_base) {
    for (const NativeModuleRecord& module : modules) {
        if (module.base == module_base) {
            return &module;
        }
    }
    return nullptr;
}

const NativeModuleRecord* FindLoadedModuleContainingAddress(const std::vector<NativeModuleRecord>& modules,
                                                            uint64_t address) {
    for (const NativeModuleRecord& module : modules) {
        const uint64_t module_end = module.base + module.size;
        if (address >= module.base && address < module_end) {
            return &module;
        }
    }
    return nullptr;
}

void InvalidateDebugSymbolCacheLocked(RuntimeState& state) {
    state.debug_symbol_modules_valid = false;
    state.debug_symbol_modules.clear();
    state.debug_symbol_exports_by_base.clear();
}

bool EnsureDebugSymbolModulesCachedLocked(RuntimeState& state,
                                         bool refresh,
                                         std::string* error_message) {
    if (!refresh && state.debug_symbol_modules_valid) {
        return true;
    }

    std::vector<NativeModuleRecord> modules;
    if (!CollectLoadedNativeModules(&modules, error_message)) {
        return false;
    }

    state.debug_symbol_modules = std::move(modules);
    state.debug_symbol_modules_valid = true;
    state.debug_symbol_exports_by_base.clear();
    return true;
}

bool EnsureDebugSymbolExportsCachedLocked(RuntimeState& state,
                                          const NativeModuleRecord& module,
                                          const std::vector<NativeModuleExportRecord>** exports_out,
                                          std::string* error_message) {
    if (exports_out == nullptr) {
        return false;
    }

    auto it = state.debug_symbol_exports_by_base.find(module.base);
    if (it == state.debug_symbol_exports_by_base.end()) {
        std::vector<NativeModuleExportRecord> exports;
        if (!CollectModuleExports(module, &exports, error_message)) {
            return false;
        }
        it = state.debug_symbol_exports_by_base.emplace(module.base, std::move(exports)).first;
    }

    *exports_out = &it->second;
    return true;
}

bool TryReadPointerSizedValue(uint64_t address, uint64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    *value_out = 0u;

    const uintptr_t normalized_address =
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address));
    if (!IsReadableMemoryRange(normalized_address, sizeof(uint64_t))) {
        return false;
    }

    std::memcpy(value_out, reinterpret_cast<const void*>(normalized_address), sizeof(uint64_t));
    return true;
}

bool IsExecutableProtection(const std::string& protection) {
    return protection.size() == 3u && protection[2] == 'x';
}

const NativeMemoryRangeRecord* FindNativeMemoryRangeContainingAddress(
    const std::vector<NativeMemoryRangeRecord>& ranges,
    uint64_t address) {
    address = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));
    if (address == 0u) {
        return nullptr;
    }

    for (const NativeMemoryRangeRecord& range : ranges) {
        const uint64_t range_end = range.base + range.size;
        if (address >= range.base && address < range_end) {
            return &range;
        }
    }
    return nullptr;
}

void AppendNormalizedUniqueBacktraceFrame(uint64_t address,
                                          std::vector<uint64_t>* frames_out,
                                          std::unordered_map<uint64_t, bool>* seen_frames) {
    if (frames_out == nullptr || seen_frames == nullptr || address == 0u) {
        return;
    }

    address = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));
    if (address == 0u || seen_frames->find(address) != seen_frames->end()) {
        return;
    }

    seen_frames->emplace(address, true);
    frames_out->push_back(address);
}

bool CollectFuzzyBacktraceFromStackPointer(uint64_t pc,
                                          uint64_t lr,
                                          uint64_t sp,
                                          const std::vector<NativeMemoryRangeRecord>& ranges,
                                          std::vector<uint64_t>* frames_out) {
    if (frames_out == nullptr) {
        return false;
    }
    frames_out->clear();

    constexpr size_t kMaxBacktraceDepth = 64u;
    constexpr size_t kMaxFuzzyScanBytes = 16384u;
    std::unordered_map<uint64_t, bool> seen_frames;

    AppendNormalizedUniqueBacktraceFrame(pc, frames_out, &seen_frames);
    AppendNormalizedUniqueBacktraceFrame(lr, frames_out, &seen_frames);

    const uint64_t normalized_sp = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(sp)));
    if (normalized_sp == 0u) {
        return !frames_out->empty();
    }

    const NativeMemoryRangeRecord* stack_range =
        FindNativeMemoryRangeContainingAddress(ranges, normalized_sp);
    if (stack_range == nullptr || stack_range->protection.empty() ||
        stack_range->protection[0] != 'r') {
        return !frames_out->empty();
    }

    const uint64_t range_end = stack_range->base + stack_range->size;
    uint64_t scan_end = normalized_sp;
    if (range_end > normalized_sp) {
        const uint64_t max_scan_end = normalized_sp + kMaxFuzzyScanBytes;
        scan_end = range_end < max_scan_end ? range_end : max_scan_end;
    }

    for (uint64_t current = normalized_sp;
         current + sizeof(uint64_t) <= scan_end && frames_out->size() < kMaxBacktraceDepth;
         current += sizeof(uintptr_t)) {
        uint64_t candidate = 0u;
        if (!TryReadPointerSizedValue(current, &candidate)) {
            continue;
        }

        candidate = static_cast<uint64_t>(
            NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(candidate)));
        if (candidate == 0u) {
            continue;
        }

        const NativeMemoryRangeRecord* candidate_range =
            FindNativeMemoryRangeContainingAddress(ranges, candidate);
        if (candidate_range == nullptr || !IsExecutableProtection(candidate_range->protection)) {
            continue;
        }

        AppendNormalizedUniqueBacktraceFrame(candidate, frames_out, &seen_frames);
    }

    return !frames_out->empty();
}

bool CollectBacktraceFromContext(uint64_t pc,
                                 uint64_t lr,
                                 uint64_t fp,
                                 std::vector<uint64_t>* frames_out) {
    if (frames_out == nullptr) {
        return false;
    }
    frames_out->clear();

    auto append_frame = [&](uint64_t address) {
        if (address == 0u) {
            return;
        }
        address = static_cast<uint64_t>(
            NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));
        if (frames_out->empty() || frames_out->back() != address) {
            frames_out->push_back(address);
        }
    };

    append_frame(pc);
    append_frame(lr);

    constexpr size_t kMaxBacktraceDepth = 64u;
    std::unordered_map<uint64_t, bool> visited_frames;
    uint64_t current_fp = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(fp)));
    for (size_t depth = 0; depth < kMaxBacktraceDepth && current_fp != 0u; ++depth) {
        if (visited_frames.find(current_fp) != visited_frames.end()) {
            break;
        }
        visited_frames.emplace(current_fp, true);

        uint64_t previous_fp = 0u;
        uint64_t saved_lr = 0u;
        if (!TryReadPointerSizedValue(current_fp, &previous_fp) ||
            !TryReadPointerSizedValue(current_fp + sizeof(uint64_t), &saved_lr)) {
            break;
        }

        append_frame(saved_lr);
        previous_fp = static_cast<uint64_t>(
            NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(previous_fp)));
        if (previous_fp == 0u || previous_fp == current_fp) {
            break;
        }
        current_fp = previous_fp;
    }

    return true;
}

bool ParseBacktracerModeValue(JSContext* ctx,
                              JSValueConst value,
                              BacktracerMode* mode_out) {
    if (mode_out == nullptr) {
        return false;
    }
    *mode_out = BacktracerMode::kAccurate;

    const char* mode_cstr = JS_ToCString(ctx, value);
    if (mode_cstr == nullptr) {
        return false;
    }

    const std::string mode_text = mode_cstr;
    JS_FreeCString(ctx, mode_cstr);
    if (mode_text == "accurate") {
        *mode_out = BacktracerMode::kAccurate;
        return true;
    }
    if (mode_text == "fuzzy") {
        *mode_out = BacktracerMode::kFuzzy;
        return true;
    }
    return false;
}

bool CollectCurrentNativeBacktrace(std::vector<uint64_t>* frames_out) {
    if (frames_out == nullptr) {
        return false;
    }
    frames_out->clear();

    constexpr size_t kMaxBacktraceDepth = 64u;
#if defined(_WIN32)
    void* frames[kMaxBacktraceDepth] = {};
    const USHORT captured = CaptureStackBackTrace(0,
                                                  static_cast<DWORD>(kMaxBacktraceDepth),
                                                  frames,
                                                  nullptr);
    for (USHORT index = 1; index < captured; ++index) {
        frames_out->push_back(static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(frames[index])));
    }
    return true;
#else
    struct UnwindCaptureState {
        std::vector<uint64_t>* frames = nullptr;
        size_t skip = 1u;
        size_t max_depth = kMaxBacktraceDepth;
    } state = {frames_out, 1u, kMaxBacktraceDepth};

    auto callback = [](_Unwind_Context* context, void* user_data) -> _Unwind_Reason_Code {
        auto* state = reinterpret_cast<UnwindCaptureState*>(user_data);
        if (state == nullptr || state->frames == nullptr) {
            return _URC_END_OF_STACK;
        }

        uintptr_t ip = static_cast<uintptr_t>(_Unwind_GetIP(context));
        if (ip == 0u) {
            return _URC_NO_REASON;
        }

        if (state->skip > 0u) {
            --state->skip;
            return _URC_NO_REASON;
        }

        const uint64_t address = static_cast<uint64_t>(
            NormalizeProcessAddressForRangeCheck(ip));
        if (state->frames->empty() || state->frames->back() != address) {
            state->frames->push_back(address);
        }

        return state->frames->size() >= state->max_depth
                   ? _URC_END_OF_STACK
                   : _URC_NO_REASON;
    };

    _Unwind_Backtrace(callback, &state);
    return !frames_out->empty();
#endif
}

bool CollectCurrentFuzzyNativeBacktrace(std::vector<uint64_t>* frames_out) {
    if (frames_out == nullptr) {
        return false;
    }

    std::vector<NativeMemoryRangeRecord> ranges;
    if (!CollectAllNativeMemoryRanges(&ranges)) {
        return false;
    }

    uintptr_t stack_marker = 0u;
    return CollectFuzzyBacktraceFromStackPointer(0u,
                                                 0u,
                                                 static_cast<uint64_t>(
                                                     reinterpret_cast<uintptr_t>(&stack_marker)),
                                                 ranges,
                                                 frames_out);
}

bool ParseThreadBacktraceArguments(JSContext* ctx,
                                   int argc,
                                   JSValueConst* argv,
                                   JSValueConst* context_out,
                                   BacktracerMode* mode_out) {
    if (context_out == nullptr || mode_out == nullptr) {
        return false;
    }
    *context_out = JS_UNDEFINED;
    *mode_out = BacktracerMode::kAccurate;

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
            !ParseBacktracerModeValue(ctx, argv[1], mode_out)) {
            JS_ThrowTypeError(ctx,
                              "Thread.backtrace backtracer must be Backtracer.ACCURATE or Backtracer.FUZZY");
            return false;
        }
        return true;
    }

    if (JS_IsObject(argv[0])) {
        *context_out = argv[0];
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
            !ParseBacktracerModeValue(ctx, argv[1], mode_out)) {
            JS_ThrowTypeError(ctx,
                              "Thread.backtrace backtracer must be Backtracer.ACCURATE or Backtracer.FUZZY");
            return false;
        }
        return true;
    }

    if (JS_IsString(argv[0])) {
        if (!ParseBacktracerModeValue(ctx, argv[0], mode_out)) {
            JS_ThrowTypeError(ctx,
                              "Thread.backtrace backtracer must be Backtracer.ACCURATE or Backtracer.FUZZY");
            return false;
        }
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
            JS_ThrowTypeError(ctx, "Thread.backtrace does not accept a second argument when mode is passed first");
            return false;
        }
        return true;
    }

    JS_ThrowTypeError(ctx, "Thread.backtrace context must be an object");
    return false;
}

const NativeModuleRecord* FindMainLoadedModule(const std::vector<NativeModuleRecord>& modules) {
    const std::string executable_path = GetCurrentExecutablePath();
    const std::string executable_name = GetPathBaseNameForModuleLookup(executable_path);
    for (const NativeModuleRecord& module : modules) {
        if (!executable_path.empty() && module.path == executable_path) {
            return &module;
        }
    }
    if (!executable_name.empty()) {
        return FindLoadedModuleByName(modules, executable_name);
    }
    return modules.empty() ? nullptr : &modules.front();
}

JSValue MakeProcessRangeObject(JSContext* ctx, const NativeMemoryRangeRecord& range) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    if (JS_SetPropertyStr(ctx, object, "base", MakeNativePointer(ctx, range.base)) < 0 ||
        JS_SetPropertyStr(ctx, object, "size", JS_NewInt64(ctx, static_cast<int64_t>(range.size))) < 0 ||
        JS_SetPropertyStr(ctx, object, "protection", JS_NewString(ctx, range.protection.c_str())) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }

    return object;
}

JSValue MakeModuleExportObject(JSContext* ctx, const NativeModuleExportRecord& export_record) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    if (JS_SetPropertyStr(ctx,
                          object,
                          "type",
                          JS_NewString(ctx, export_record.type.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "name",
                          JS_NewString(ctx, export_record.name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "address",
                          MakeNativePointer(ctx, export_record.address)) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }

    return object;
}

JSValue MakeModuleImportObject(JSContext* ctx, const NativeModuleImportRecord& import_record) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    if (JS_SetPropertyStr(ctx,
                          object,
                          "type",
                          JS_NewString(ctx, import_record.type.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "name",
                          JS_NewString(ctx, import_record.name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "module",
                          JS_NewString(ctx, import_record.module.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "slot",
                          MakeNativePointer(ctx, import_record.slot)) < 0 ||
        JS_SetPropertyStr(ctx,
                          object,
                          "address",
                          MakeNativePointer(ctx, import_record.address)) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }

    return object;
}

bool CollectModuleExports(const NativeModuleRecord& module,
                          std::vector<NativeModuleExportRecord>* exports_out,
                          std::string* error_message) {
    if (exports_out == nullptr) {
        SetError(error_message, "exports_out is null");
        return false;
    }
    exports_out->clear();

#if defined(_WIN32)
    const HMODULE handle = GetModuleHandleA(module.name.c_str());
    if (handle == nullptr) {
        SetError(error_message, "GetModuleHandleA failed");
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(handle);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        SetError(error_message, "invalid DOS header");
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(handle) + dos->e_lfanew);
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE) {
        SetError(error_message, "invalid NT header");
        return false;
    }

    const IMAGE_DATA_DIRECTORY& export_dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (export_dir.VirtualAddress == 0u || export_dir.Size == 0u) {
        return true;
    }

    const auto* export_table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        reinterpret_cast<const uint8_t*>(handle) + export_dir.VirtualAddress);
    const auto* names = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(handle) + export_table->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const uint16_t*>(
        reinterpret_cast<const uint8_t*>(handle) + export_table->AddressOfNameOrdinals);
    const auto* functions = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(handle) + export_table->AddressOfFunctions);

    for (uint32_t index = 0; index < export_table->NumberOfNames; ++index) {
        const uint32_t name_rva = names[index];
        const uint16_t ordinal = ordinals[index];
        if (ordinal >= export_table->NumberOfFunctions) {
            continue;
        }

        const uint32_t function_rva = functions[ordinal];
        if (function_rva == 0u) {
            continue;
        }

        NativeModuleExportRecord record = {};
        record.type = "function";
        record.name = reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(handle) + name_rva);
        record.address = module.base + static_cast<uint64_t>(function_rva);
        exports_out->push_back(std::move(record));
    }
    return true;
#else
    if (module.path.empty()) {
        SetError(error_message, "module path is empty");
        return false;
    }

    ElfHooker::ElfioImageParser parser;
    if (!parser.LoadFromFile(module.path)) {
        SetError(error_message, "load module image failed");
        return false;
    }

    uintptr_t runtime_bias = 0u;
    if (!parser.ComputeRuntimeBias(static_cast<uintptr_t>(module.base), &runtime_bias)) {
        SetError(error_message, "compute runtime bias failed");
        return false;
    }

    std::vector<ElfHooker::ParsedDynamicSymbol> symbols;
    if (!parser.CollectDynamicSymbols(&symbols)) {
        SetError(error_message, "collect dynamic symbols failed");
        return false;
    }

    for (const ElfHooker::ParsedDynamicSymbol& symbol : symbols) {
        if (symbol.name.empty() || symbol.section_index == 0u || symbol.value == 0u) {
            continue;
        }

        NativeModuleExportRecord record = {};
        switch (symbol.type) {
            case kElfSymbolTypeFunction:
            case 10u:
                record.type = "function";
                break;
            case kElfSymbolTypeObject:
                record.type = "variable";
                break;
            default:
                record.type = "unknown";
                break;
        }
        record.name = symbol.name;
        record.address = static_cast<uint64_t>(runtime_bias + static_cast<uintptr_t>(symbol.value));
        exports_out->push_back(std::move(record));
    }
    return true;
#endif
}

bool CollectModuleImports(const NativeModuleRecord& module,
                          std::vector<NativeModuleImportRecord>* imports_out,
                          std::string* error_message) {
    if (imports_out == nullptr) {
        SetError(error_message, "imports_out is null");
        return false;
    }
    imports_out->clear();

#if defined(_WIN32)
    const HMODULE handle = GetModuleHandleA(module.name.c_str());
    if (handle == nullptr) {
        SetError(error_message, "GetModuleHandleA failed");
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(handle);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        SetError(error_message, "invalid DOS header");
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(handle) + dos->e_lfanew);
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE) {
        SetError(error_message, "invalid NT header");
        return false;
    }

    const IMAGE_DATA_DIRECTORY& import_dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir.VirtualAddress == 0u || import_dir.Size == 0u) {
        return true;
    }

    const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        reinterpret_cast<const uint8_t*>(handle) + import_dir.VirtualAddress);
    for (const IMAGE_IMPORT_DESCRIPTOR* descriptor = descriptors;
         descriptor->Name != 0u;
         ++descriptor) {
        const char* import_module = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(handle) + descriptor->Name);
#if defined(_WIN64)
        const auto* lookup_table = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
            reinterpret_cast<const uint8_t*>(handle) +
            (descriptor->OriginalFirstThunk != 0u ? descriptor->OriginalFirstThunk
                                                  : descriptor->FirstThunk));
        const auto* address_table = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
            reinterpret_cast<const uint8_t*>(handle) + descriptor->FirstThunk);
        for (size_t index = 0u; lookup_table[index].u1.AddressOfData != 0u; ++index) {
            NativeModuleImportRecord record = {};
            record.type = "function";
            record.module = import_module != nullptr ? import_module : "";
            record.slot = static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(&address_table[index].u1.Function));
            record.address = static_cast<uint64_t>(address_table[index].u1.Function);
            if (IMAGE_SNAP_BY_ORDINAL64(lookup_table[index].u1.Ordinal)) {
                record.name = "#" + std::to_string(
                    static_cast<uint32_t>(IMAGE_ORDINAL64(lookup_table[index].u1.Ordinal)));
            } else {
                const auto* import_by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                    reinterpret_cast<const uint8_t*>(handle) + lookup_table[index].u1.AddressOfData);
                if (import_by_name == nullptr || import_by_name->Name[0] == '\0') {
                    continue;
                }
                record.name = reinterpret_cast<const char*>(import_by_name->Name);
            }
            imports_out->push_back(std::move(record));
        }
#else
        const auto* lookup_table = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
            reinterpret_cast<const uint8_t*>(handle) +
            (descriptor->OriginalFirstThunk != 0u ? descriptor->OriginalFirstThunk
                                                  : descriptor->FirstThunk));
        const auto* address_table = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
            reinterpret_cast<const uint8_t*>(handle) + descriptor->FirstThunk);
        for (size_t index = 0u; lookup_table[index].u1.AddressOfData != 0u; ++index) {
            NativeModuleImportRecord record = {};
            record.type = "function";
            record.module = import_module != nullptr ? import_module : "";
            record.slot = static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(&address_table[index].u1.Function));
            record.address = static_cast<uint64_t>(address_table[index].u1.Function);
            if (IMAGE_SNAP_BY_ORDINAL32(lookup_table[index].u1.Ordinal)) {
                record.name = "#" + std::to_string(
                    static_cast<uint32_t>(IMAGE_ORDINAL32(lookup_table[index].u1.Ordinal)));
            } else {
                const auto* import_by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                    reinterpret_cast<const uint8_t*>(handle) + lookup_table[index].u1.AddressOfData);
                if (import_by_name == nullptr || import_by_name->Name[0] == '\0') {
                    continue;
                }
                record.name = reinterpret_cast<const char*>(import_by_name->Name);
            }
            imports_out->push_back(std::move(record));
        }
#endif
    }
    return true;
#else
    if (module.path.empty()) {
        SetError(error_message, "module path is empty");
        return false;
    }

    ElfHooker::ElfioImageParser parser;
    if (!parser.LoadFromFile(module.path)) {
        SetError(error_message, "load module image failed");
        return false;
    }

    uintptr_t runtime_bias = 0u;
    if (!parser.ComputeRuntimeBias(static_cast<uintptr_t>(module.base), &runtime_bias)) {
        SetError(error_message, "compute runtime bias failed");
        return false;
    }

    std::vector<ElfHooker::ParsedImportedSymbol> symbols;
    if (!parser.CollectImportedSymbols(&symbols)) {
        SetError(error_message, "collect imported symbols failed");
        return false;
    }

    for (const ElfHooker::ParsedImportedSymbol& symbol : symbols) {
        if (symbol.name.empty() || symbol.offset == 0u) {
            continue;
        }

        const uintptr_t slot_address = runtime_bias + static_cast<uintptr_t>(symbol.offset);
        NativeModuleImportRecord record = {};
        switch (symbol.symbol_type) {
            case kElfSymbolTypeFunction:
            case 10u:
                record.type = "function";
                break;
            case kElfSymbolTypeObject:
                record.type = "variable";
                break;
            default:
                record.type = "unknown";
                break;
        }
        record.name = symbol.name;
        record.module.clear();
        record.slot = static_cast<uint64_t>(slot_address);
        record.address = static_cast<uint64_t>(
            *reinterpret_cast<const uintptr_t*>(slot_address));
        imports_out->push_back(std::move(record));
    }
    return true;
#endif
}

bool TryReadMemoryBytesSafely(uint64_t address, size_t size, void* bytes_out) {
    if (bytes_out == nullptr) {
        return false;
    }
    if (size == 0u) {
        return true;
    }

#if defined(_WIN32)
    std::memcpy(bytes_out,
                reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
                size);
    return true;
#else
#if defined(SYS_process_vm_readv)
    {
        size_t total_read = 0u;
        const pid_t self_pid = getpid();
        uint8_t* output_bytes = static_cast<uint8_t*>(bytes_out);
        while (total_read < size) {
            struct iovec local_iov = {
                output_bytes + total_read,
                size - total_read,
            };
            struct iovec remote_iov = {
                reinterpret_cast<void*>(static_cast<uintptr_t>(address + total_read)),
                size - total_read,
            };
            const ssize_t read_count = static_cast<ssize_t>(
                syscall(SYS_process_vm_readv,
                        self_pid,
                        &local_iov,
                        1,
                        &remote_iov,
                        1,
                        0));
            if (read_count <= 0) {
                break;
            }
            total_read += static_cast<size_t>(read_count);
        }
        if (total_read == size) {
            return true;
        }
    }
#endif

    static std::mutex self_mem_mutex;
    static int self_mem_fd = -2;
    auto get_self_mem_fd = []() -> int {
        if (self_mem_fd != -2) {
            return self_mem_fd;
        }
        std::lock_guard<std::mutex> lock(self_mem_mutex);
        if (self_mem_fd == -2) {
            self_mem_fd = ::open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
        }
        return self_mem_fd;
    };

    int mem_fd = get_self_mem_fd();
    if (mem_fd < 0) {
#if defined(__ANDROID__)
        // Some Android mappings are advertised as readable in /proc/self/maps
        // but destabilize the target if we fall back to in-process pointer reads.
        // Fail closed here so JS-side callers can skip the mapping instead.
        return false;
#else
        return TryDirectReadWithSignalGuard(address, size, bytes_out);
#endif
    }
    size_t total_read = 0u;
    uint8_t* output_bytes = static_cast<uint8_t*>(bytes_out);
    while (total_read < size) {
        ssize_t read_count = ::pread(mem_fd,
                                     output_bytes + total_read,
                                     size - total_read,
                                     static_cast<off_t>(address + total_read));
        if (read_count <= 0) {
#if defined(__ANDROID__)
            return false;
#else
            return TryDirectReadWithSignalGuard(address, size, bytes_out);
#endif
        }
        total_read += static_cast<size_t>(read_count);
    }
    return true;
#endif
}

bool TryReadMemoryRangeSafely(uint64_t address, size_t size, std::vector<uint8_t>* bytes_out) {
    if (bytes_out == nullptr) {
        return false;
    }
    bytes_out->clear();
    if (size == 0u) {
        return true;
    }
    bytes_out->resize(size);
    if (!TryReadMemoryBytesSafely(address, size, bytes_out->data())) {
        bytes_out->clear();
        return false;
    }
    return true;
}

bool CollectMemoryScanMatchOffsets(uint64_t address,
                                   uint32_t size,
                                   const std::vector<MemoryScanPatternToken>& tokens,
                                   std::vector<uint32_t>* offsets_out) {
    if (offsets_out == nullptr) {
        return false;
    }
    offsets_out->clear();
    if (tokens.empty()) {
        return true;
    }
    if (static_cast<size_t>(size) < tokens.size()) {
        return true;
    }

    std::vector<uint8_t> owned_data;
    if (!TryReadMemoryRangeSafely(address, static_cast<size_t>(size), &owned_data)) {
        return false;
    }
    const uint8_t* data = owned_data.data();
    const size_t pattern_size = tokens.size();
    for (size_t offset = 0; offset + pattern_size <= static_cast<size_t>(size); ++offset) {
        bool matched = true;
        for (size_t pattern_index = 0; pattern_index < pattern_size; ++pattern_index) {
            const MemoryScanPatternToken& token = tokens[pattern_index];
            if (!token.wildcard && data[offset + pattern_index] != token.value) {
                matched = false;
                break;
            }
        }
        if (matched) {
            offsets_out->push_back(static_cast<uint32_t>(offset));
        }
    }
    return true;
}

bool ParseMemoryScanArguments(JSContext* ctx,
                              const char* api_name,
                              int argc,
                              JSValueConst* argv,
                              uint64_t* address_out,
                              uint32_t* size_out,
                              std::vector<MemoryScanPatternToken>* tokens_out) {
    if (address_out == nullptr || size_out == nullptr || tokens_out == nullptr) {
        return false;
    }
    if (argc < 3) {
        JS_ThrowTypeError(ctx, "%s requires address, size, and pattern", api_name);
        return false;
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        JS_ThrowTypeError(ctx, "%s address must be a pointer value", api_name);
        return false;
    }
    if (address == 0u) {
        JS_ThrowTypeError(ctx, "%s address must be a non-zero pointer value", api_name);
        return false;
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[1]) < 0) {
        JS_ThrowTypeError(ctx, "%s size must be a number", api_name);
        return false;
    }
    if (size == 0u) {
        JS_ThrowTypeError(ctx, "%s size must be a positive number", api_name);
        return false;
    }

    if (!JS_IsString(argv[2])) {
        JS_ThrowTypeError(ctx, "%s pattern must be a non-empty string", api_name);
        return false;
    }

    const char* pattern_cstr = JS_ToCString(ctx, argv[2]);
    if (pattern_cstr == nullptr) {
        JS_ThrowTypeError(ctx, "%s pattern must be a non-empty string", api_name);
        return false;
    }
    const std::string pattern = pattern_cstr;
    JS_FreeCString(ctx, pattern_cstr);
    if (pattern.empty()) {
        JS_ThrowTypeError(ctx, "%s pattern must be a non-empty string", api_name);
        return false;
    }

    std::vector<MemoryScanPatternToken> tokens;
    if (!TryParseMemoryScanPattern(pattern, &tokens)) {
        JS_ThrowTypeError(ctx, "%s pattern contains invalid token", api_name);
        return false;
    }

    *address_out = address;
    *size_out = size;
    *tokens_out = std::move(tokens);
    return true;
}

bool GetOptionalFunctionProperty(JSContext* ctx,
                                 JSValueConst object,
                                 const char* property_name,
                                 JSValue* function_out) {
    if (function_out == nullptr) {
        return false;
    }
    *function_out = JS_UNDEFINED;

    JSValue value = JS_GetPropertyStr(ctx, object, property_name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return false;
    }
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, value);
        return true;
    }
    if (!JS_IsFunction(ctx, value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "Memory.scan %s must be a function", property_name);
        return false;
    }

    *function_out = value;
    return true;
}

std::string GetExceptionString(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    std::string result = "quickjs exception";
    std::string message_text;
    std::string stack_text;

    JSValue message_value = JS_GetPropertyStr(ctx, exception, "message");
    if (!JS_IsUndefined(message_value) && !JS_IsNull(message_value)) {
        const char* message_cstr = JS_ToCString(ctx, message_value);
        if (message_cstr != nullptr) {
            message_text = message_cstr;
            JS_FreeCString(ctx, message_cstr);
        }
    }
    JS_FreeValue(ctx, message_value);

    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
        const char* stack_cstr = JS_ToCString(ctx, stack);
        if (stack_cstr != nullptr) {
            stack_text = stack_cstr;
            JS_FreeCString(ctx, stack_cstr);
        }
    }
    JS_FreeValue(ctx, stack);

    if (!message_text.empty()) {
        result = message_text;
        if (!stack_text.empty() && stack_text.find(message_text) == std::string::npos) {
            result += "\n";
            result += stack_text;
        }
        JS_FreeValue(ctx, exception);
        return result;
    }

    if (!stack_text.empty()) {
        result = stack_text;
        JS_FreeValue(ctx, exception);
        return result;
    }

    const char* message = JS_ToCString(ctx, exception);
    if (message != nullptr) {
        result = message;
        JS_FreeCString(ctx, message);
    }
    JS_FreeValue(ctx, exception);
    return result;
}

int GetEvalFlags(const std::string& source) {
    return JS_DetectModule(source.c_str(), source.size()) ? JS_EVAL_TYPE_MODULE
                                                          : JS_EVAL_TYPE_GLOBAL;
}

const char* GetConsoleLevelName(int magic) {
    switch (magic) {
        case 1:
            return "warn";
        case 2:
            return "error";
        case 0:
        default:
            return "info";
    }
}

bool GetRequiredStringProperty(JSContext* ctx,
                               JSValueConst object,
                               const char* property_name,
                               const char* missing_message,
                               std::string* value_out) {
    JSValue property = JS_GetPropertyStr(ctx, object, property_name);
    if (JS_IsException(property)) {
        return false;
    }
    if (JS_IsUndefined(property) || JS_IsNull(property)) {
        JS_FreeValue(ctx, property);
        JS_ThrowTypeError(ctx, "%s", missing_message);
        return false;
    }

    const char* property_cstr = JS_ToCString(ctx, property);
    if (property_cstr == nullptr) {
        JS_FreeValue(ctx, property);
        JS_ThrowTypeError(ctx, "%s", missing_message);
        return false;
    }

    *value_out = property_cstr;
    JS_FreeCString(ctx, property_cstr);
    JS_FreeValue(ctx, property);
    return true;
}

bool RequireFunctionProperty(JSContext* ctx,
                             JSValueConst object,
                             const char* property_name,
                             const char* error_message) {
    JSValue property = JS_GetPropertyStr(ctx, object, property_name);
    if (JS_IsException(property)) {
        return false;
    }
    const bool is_function = JS_IsFunction(ctx, property);
    JS_FreeValue(ctx, property);
    if (!is_function) {
        JS_ThrowTypeError(ctx, "%s", error_message);
        return false;
    }
    return true;
}

void FreeRecvCallbackLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.recv_callbacks.find(script_id);
    if (it == state.recv_callbacks.end()) {
        return;
    }
    JS_FreeValue(ctx, it->second);
    state.recv_callbacks.erase(it);
}

void FreeRpcExportsLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.rpc_exports.find(script_id);
    if (it == state.rpc_exports.end()) {
        return;
    }

    for (auto& entry : it->second) {
        JS_FreeValue(ctx, entry.second);
    }
    state.rpc_exports.erase(it);
}

void FreeNativeHookCallbacksLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.native_hook_callbacks.find(script_id);
    if (it == state.native_hook_callbacks.end()) {
        return;
    }
    for (auto& entry : it->second) {
        JS_FreeValue(ctx, entry.second.on_enter);
        JS_FreeValue(ctx, entry.second.on_leave);
        JS_FreeValue(ctx, entry.second.cached_invocation_receiver);
        JS_FreeValue(ctx, entry.second.cached_invocation_args);
        JS_FreeValue(ctx, entry.second.cached_invocation_retval);
        JS_FreeValue(ctx, entry.second.active_sync_invocation_receiver);
    }
    state.native_hook_callbacks.erase(it);
}

void FreeJavaHookCallbacksLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.java_hook_callbacks.find(script_id);
    if (it == state.java_hook_callbacks.end()) {
        return;
    }
    for (auto& entry : it->second) {
        std::string ignored_error;
        (void)UninstallJavaJsHook(entry.first, &ignored_error);
        if (ctx != nullptr) {
            JS_FreeValue(ctx, entry.second);
        }
    }
    state.java_hook_callbacks.erase(it);
}

std::vector<uint32_t> TakeJavaHookIdsForScriptLocked(JSContext* ctx,
                                                     RuntimeState& state,
                                                     uint32_t script_id) {
    std::vector<uint32_t> hook_ids;
    auto it = state.java_hook_callbacks.find(script_id);
    if (it == state.java_hook_callbacks.end()) {
        return hook_ids;
    }

    hook_ids.reserve(it->second.size());
    for (auto& entry : it->second) {
        hook_ids.push_back(entry.first);
        if (ctx != nullptr) {
            JS_FreeValue(ctx, entry.second);
        }
    }
    state.java_hook_callbacks.erase(it);
    return hook_ids;
}

void FreeJavaRegisteredClassCallbacksLocked(JSContext* ctx,
                                            RuntimeState& state,
                                            uint32_t script_id) {
    auto it = state.java_registered_class_callbacks.find(script_id);
    if (it == state.java_registered_class_callbacks.end()) {
        return;
    }
    if (ctx != nullptr) {
        for (auto& callback_entry : it->second) {
            for (auto& method_entry : callback_entry.second) {
                for (auto& signature_entry : method_entry.second) {
                    JS_FreeValue(ctx, signature_entry.second);
                }
            }
        }
    }
    state.java_registered_class_callbacks.erase(it);
}

void FreeActiveNativeInvocationsLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.active_native_invocations.find(script_id);
    if (it == state.active_native_invocations.end()) {
        return;
    }
    if (ctx != nullptr) {
        for (auto& entry : it->second) {
            JS_FreeValue(ctx, entry.second);
        }
    }
    state.active_native_invocations.erase(it);
}

void ReleaseNativeCallbackSlotLocked(RuntimeState& state, uint32_t slot) {
    if (slot >= kMaxNativeCallbackSlots) {
        return;
    }
    state.native_callback_slot_used[slot] = false;
}

void FreeNativeCallbacksLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.native_callback_records.find(script_id);
    if (it == state.native_callback_records.end()) {
        return;
    }
    for (auto& entry : it->second) {
        if (ctx != nullptr) {
            JS_FreeValue(ctx, entry.second.function);
        }
        ReleaseNativeCallbackSlotLocked(state, entry.second.slot);
    }
    state.native_callback_records.erase(it);
}

bool AllocateNativeCallbackSlotLocked(RuntimeState& state, uint32_t* slot_out) {
    if (slot_out == nullptr) {
        return false;
    }
    *slot_out = UINT32_MAX;
    for (uint32_t slot = 0u; slot < static_cast<uint32_t>(kMaxNativeCallbackSlots); ++slot) {
        if (!state.native_callback_slot_used[slot]) {
            state.native_callback_slot_used[slot] = true;
            *slot_out = slot;
            return true;
        }
    }
    return false;
}

bool StoreNativeCallbackRecordLocked(JSContext* ctx,
                                     RuntimeState& state,
                                     uint32_t script_id,
                                     uint32_t callback_id,
                                     JSValueConst function,
                                     NativeFunctionValueType return_type,
                                     const std::vector<NativeFunctionValueType>& arg_types,
                                     uint32_t slot) {
    RuntimeState::NativeCallbackRecord record = {};
    record.function = JS_DupValue(ctx, function);
    record.return_type = return_type;
    record.arg_types = arg_types;
    record.slot = slot;
    state.native_callback_records[script_id][callback_id] = std::move(record);
    return true;
}

bool RegisterNativeCallbackLocked(JSContext* ctx,
                                  RuntimeState& state,
                                  uint32_t script_id,
                                  JSValueConst function,
                                  NativeFunctionValueType return_type,
                                  const std::vector<NativeFunctionValueType>& arg_types,
                                  uint64_t* callback_address_out,
                                  std::string* error_message) {
    if (callback_address_out == nullptr) {
        SetError(error_message, "callback_address_out is null");
        return false;
    }
    *callback_address_out = 0u;

    uint32_t slot = UINT32_MAX;
    if (!AllocateNativeCallbackSlotLocked(state, &slot)) {
        SetError(error_message, "NativeCallback slot allocation failed");
        return false;
    }

    const uint32_t callback_id = state.next_native_callback_id++;
    if (!StoreNativeCallbackRecordLocked(ctx,
                                         state,
                                         script_id,
                                         callback_id,
                                         function,
                                         return_type,
                                         arg_types,
                                         slot)) {
        ReleaseNativeCallbackSlotLocked(state, slot);
        SetError(error_message, "NativeCallback registration failed");
        return false;
    }

    *callback_address_out = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(kNativeCallbackTrampolines[slot]));
    return true;
}

RuntimeState::NativeCallbackRecord* FindNativeCallbackRecordBySlotLocked(RuntimeState& state,
                                                                         uint32_t slot,
                                                                         uint32_t* script_id_out) {
    for (auto& script_entry : state.native_callback_records) {
        for (auto& callback_entry : script_entry.second) {
            if (callback_entry.second.slot == slot) {
                if (script_id_out != nullptr) {
                    *script_id_out = script_entry.first;
                }
                return &callback_entry.second;
            }
        }
    }
    return nullptr;
}

RuntimeState::NativeCallbackRecord* FindNativeCallbackRecordByAddressLocked(RuntimeState& state,
                                                                            uint64_t callback_address,
                                                                            uint32_t* script_id_out) {
    for (auto& script_entry : state.native_callback_records) {
        for (auto& callback_entry : script_entry.second) {
            const uint64_t address = static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(kNativeCallbackTrampolines[callback_entry.second.slot]));
            if (address != callback_address) {
                continue;
            }
            if (script_id_out != nullptr) {
                *script_id_out = script_entry.first;
            }
            return &callback_entry.second;
        }
    }
    return nullptr;
}

bool HasNativeCallbackAddressForScriptLocked(RuntimeState& state,
                                             uint32_t script_id,
                                             uint64_t callback_address) {
    const auto script_it = state.native_callback_records.find(script_id);
    if (script_it == state.native_callback_records.end()) {
        return false;
    }
    for (const auto& entry : script_it->second) {
        const uint64_t address = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(kNativeCallbackTrampolines[entry.second.slot]));
        if (address == callback_address) {
            return true;
        }
    }
    return false;
}

void EraseReplaceHookRecordByTargetLocked(RuntimeState& state, uint64_t target_address) {
    for (auto script_it = state.replace_hook_records.begin();
         script_it != state.replace_hook_records.end();
         ++script_it) {
        auto record_it = script_it->second.find(target_address);
        if (record_it == script_it->second.end()) {
            continue;
        }
        script_it->second.erase(record_it);
        if (script_it->second.empty()) {
            state.replace_hook_records.erase(script_it);
        }
        return;
    }
}

void EraseReplaceHookRecordByHookIdLocked(RuntimeState& state, uint32_t hook_id) {
    for (auto script_it = state.replace_hook_records.begin();
         script_it != state.replace_hook_records.end();
         ++script_it) {
        for (auto record_it = script_it->second.begin();
             record_it != script_it->second.end();
             ++record_it) {
            if (record_it->second.hook_id != hook_id) {
                continue;
            }
            script_it->second.erase(record_it);
            if (script_it->second.empty()) {
                state.replace_hook_records.erase(script_it);
            }
            return;
        }
    }
}

RuntimeState::ReplaceHookRecord* FindReplaceHookRecordByTargetLocked(RuntimeState& state,
                                                                     uint64_t target_address,
                                                                     uint32_t* script_id_out) {
    for (auto& script_entry : state.replace_hook_records) {
        auto record_it = script_entry.second.find(target_address);
        if (record_it == script_entry.second.end()) {
            continue;
        }
        if (script_id_out != nullptr) {
            *script_id_out = script_entry.first;
        }
        return &record_it->second;
    }
    return nullptr;
}

RuntimeState::ReplaceHookRecord* FindReplaceHookRecordByHookIdLocked(RuntimeState& state,
                                                                     uint32_t hook_id,
                                                                     uint32_t* script_id_out) {
    for (auto& script_entry : state.replace_hook_records) {
        for (auto& entry : script_entry.second) {
            if (entry.second.hook_id != hook_id) {
                continue;
            }
            if (script_id_out != nullptr) {
                *script_id_out = script_entry.first;
            }
            return &entry.second;
        }
    }
    return nullptr;
}

bool StoreReplaceHookRecordLocked(RuntimeState& state,
                                  uint32_t script_id,
                                  uint64_t target_address,
                                  uint64_t replacement_address,
                                  uint64_t original_address,
                                  uint32_t hook_id,
                                  void* hook_handle) {
    RuntimeState::ReplaceHookRecord record = {};
    record.target_address = target_address;
    record.replacement_address = replacement_address;
    record.original_address = original_address;
    record.hook_id = hook_id;
    record.hook_handle = hook_handle;
    state.replace_hook_records[script_id][target_address] = record;
    return true;
}

bool InvokeNativeCallbackRecordLocked(RuntimeState& state,
                                      uint32_t script_id,
                                      RuntimeState::NativeCallbackRecord* record,
                                      const NativeCallValue native_args[4],
                                      NativeCallValue* result_out) {
    if (state.context == nullptr || record == nullptr || result_out == nullptr) {
        return false;
    }
    result_out->raw = 0u;
    result_out->number = 0.0;

    JSValue global = JS_GetGlobalObject(state.context);
    if (JS_IsException(global)) {
        JS_FreeValue(state.context, global);
        return false;
    }

    JSValue callback = JS_DupValue(state.context, record->function);
    std::vector<JSValue> argv;
    argv.reserve(record->arg_types.size());
    for (size_t index = 0u; index < record->arg_types.size(); ++index) {
        JSValue value = JS_UNDEFINED;
        switch (record->arg_types[index]) {
            case NativeFunctionValueType::kBool:
                value = JS_NewBool(state.context, native_args[index].raw != 0u ? 1 : 0);
                break;
            case NativeFunctionValueType::kInt8:
                value = JS_NewInt32(state.context, static_cast<int8_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kUInt8:
                value = JS_NewUint32(state.context, static_cast<uint8_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kInt16:
                value = JS_NewInt32(state.context, static_cast<int16_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kUInt16:
                value = JS_NewUint32(state.context, static_cast<uint16_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kInt32:
                value = JS_NewInt32(state.context, static_cast<int32_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kUInt32:
                value = JS_NewUint32(state.context, static_cast<uint32_t>(native_args[index].raw));
                break;
            case NativeFunctionValueType::kInt64:
                value = MakeInteger64Object(state.context, native_args[index].raw, true);
                break;
            case NativeFunctionValueType::kUInt64:
                value = MakeInteger64Object(state.context, native_args[index].raw, false);
                break;
            case NativeFunctionValueType::kFloat:
            case NativeFunctionValueType::kDouble:
                value = JS_NewFloat64(state.context, native_args[index].number);
                break;
            case NativeFunctionValueType::kPointer:
                value = MakeNativePointer(state.context, native_args[index].raw);
                break;
            default:
                value = JS_EXCEPTION;
                break;
        }
        if (JS_IsException(value)) {
            for (JSValue arg : argv) {
                JS_FreeValue(state.context, arg);
            }
            JS_FreeValue(state.context, callback);
            JS_FreeValue(state.context, global);
            return false;
        }
        argv.push_back(value);
    }

    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context,
                             callback,
                             global,
                             static_cast<int>(argv.size()),
                             argv.empty() ? nullptr : argv.data());
    for (JSValue arg : argv) {
        JS_FreeValue(state.context, arg);
    }
    JS_FreeValue(state.context, callback);
    JS_FreeValue(state.context, global);
    if (JS_IsException(result)) {
        JS_FreeValue(state.context, result);
        return false;
    }

    switch (record->return_type) {
        case NativeFunctionValueType::kVoid:
            result_out->raw = 0u;
            result_out->number = 0.0;
            break;
        case NativeFunctionValueType::kBool:
            result_out->raw = JS_ToBool(state.context, result) != 0 ? 1u : 0u;
            result_out->number = result_out->raw != 0u ? 1.0 : 0.0;
            break;
        case NativeFunctionValueType::kInt8:
        case NativeFunctionValueType::kInt16:
        case NativeFunctionValueType::kInt32: {
            int32_t value = 0;
            if (JS_ToInt32(state.context, &value, result) < 0) {
                JS_FreeValue(state.context, result);
                return false;
            }
            if (record->return_type == NativeFunctionValueType::kInt8) {
                StoreTypedNativeCallValue<int8_t>(static_cast<int8_t>(value), result_out);
            } else if (record->return_type == NativeFunctionValueType::kInt16) {
                StoreTypedNativeCallValue<int16_t>(static_cast<int16_t>(value), result_out);
            } else {
                StoreTypedNativeCallValue<int32_t>(value, result_out);
            }
            break;
        }
        case NativeFunctionValueType::kUInt8:
        case NativeFunctionValueType::kUInt16:
        case NativeFunctionValueType::kUInt32: {
            uint32_t value = 0u;
            if (JS_ToUint32(state.context, &value, result) < 0) {
                JS_FreeValue(state.context, result);
                return false;
            }
            if (record->return_type == NativeFunctionValueType::kUInt8) {
                StoreTypedNativeCallValue<uint8_t>(static_cast<uint8_t>(value), result_out);
            } else if (record->return_type == NativeFunctionValueType::kUInt16) {
                StoreTypedNativeCallValue<uint16_t>(static_cast<uint16_t>(value), result_out);
            } else {
                StoreTypedNativeCallValue<uint32_t>(value, result_out);
            }
            break;
        }
        case NativeFunctionValueType::kInt64:
        case NativeFunctionValueType::kUInt64: {
            uint64_t value = 0u;
            bool is_signed = false;
            if (!ParseInteger64Value(state.context, result, &value, &is_signed)) {
                JS_FreeValue(state.context, result);
                return false;
            }
            result_out->raw = value;
            result_out->number = is_signed ? static_cast<double>(static_cast<int64_t>(value))
                                           : static_cast<double>(value);
            break;
        }
        case NativeFunctionValueType::kFloat:
        case NativeFunctionValueType::kDouble: {
            double value = 0.0;
            if (JS_ToFloat64(state.context, &value, result) < 0) {
                JS_FreeValue(state.context, result);
                return false;
            }
            if (record->return_type == NativeFunctionValueType::kFloat) {
                StoreTypedNativeCallValue<float>(static_cast<float>(value), result_out);
            } else {
                StoreTypedNativeCallValue<double>(value, result_out);
            }
            break;
        }
        case NativeFunctionValueType::kPointer: {
            uint64_t native_result = 0u;
            if (!ParsePointerValue(state.context, result, &native_result)) {
                JS_FreeValue(state.context, result);
                return false;
            }
            StoreTypedNativeCallValue<uint64_t>(native_result, result_out);
            break;
        }
        default:
            JS_FreeValue(state.context, result);
            return false;
    }

    JS_FreeValue(state.context, result);
    return true;
}

bool UninstallReplaceHooksForScriptLocked(RuntimeState& state,
                                          uint32_t script_id,
                                          std::string* error_message) {
    auto script_it = state.replace_hook_records.find(script_id);
    if (script_it == state.replace_hook_records.end()) {
        return true;
    }

    std::vector<void*> hook_handles;
    hook_handles.reserve(script_it->second.size());
    for (const auto& entry : script_it->second) {
        if (entry.second.hook_handle != nullptr) {
            hook_handles.push_back(entry.second.hook_handle);
        }
    }

    for (void* hook_handle : hook_handles) {
        if (!UninstallNativeJsReplacementHook(hook_handle, error_message)) {
            return false;
        }
    }

    state.replace_hook_records.erase(script_it);
    return true;
}

bool FreeNativeHookCallbackLocked(JSContext* ctx,
                                  RuntimeState& state,
                                  uint32_t script_id,
                                  uint32_t hook_id) {
    auto script_it = state.native_hook_callbacks.find(script_id);
    if (script_it == state.native_hook_callbacks.end()) {
        return false;
    }

    auto hook_it = script_it->second.find(hook_id);
    if (hook_it == script_it->second.end()) {
        return false;
    }

    JS_FreeValue(ctx, hook_it->second.on_enter);
    JS_FreeValue(ctx, hook_it->second.on_leave);
    script_it->second.erase(hook_it);
    if (script_it->second.empty()) {
        state.native_hook_callbacks.erase(script_it);
    }
    return true;
}

std::vector<uint32_t> CollectNativeHookIdsLocked(RuntimeState& state, uint32_t script_id) {
    std::vector<uint32_t> hook_ids;
    const auto it = state.native_hook_callbacks.find(script_id);
    if (it == state.native_hook_callbacks.end()) {
        return hook_ids;
    }
    hook_ids.reserve(it->second.size());
    for (const auto& entry : it->second) {
        hook_ids.push_back(entry.first);
    }
    return hook_ids;
}

bool UninstallNativeHooksForScriptLocked(RuntimeState& state,
                                         uint32_t script_id,
                                         std::string* error_message) {
    if (!UninstallReplaceHooksForScriptLocked(state, script_id, error_message)) {
        return false;
    }
    const std::vector<uint32_t> hook_ids = CollectNativeHookIdsLocked(state, script_id);
    for (uint32_t hook_id : hook_ids) {
        if (!UninstallNativeJsHook(hook_id, error_message)) {
            return false;
        }
    }
    return true;
}

bool StoreNativeHookCallbacksLocked(JSContext* ctx,
                                    RuntimeState& state,
                                    uint32_t script_id,
                                    uint32_t hook_id,
                                    bool blocking,
                                    JSValueConst on_enter,
                                    JSValueConst on_leave) {
    RuntimeState::NativeJsCallbackRecord record = {};
    record.on_enter = JS_DupValue(ctx, on_enter);
    record.on_leave = JS_DupValue(ctx, on_leave);
    record.blocking = blocking;
    state.native_hook_callbacks[script_id][hook_id] = record;
    return true;
}

RuntimeState::NativeJsCallbackRecord* FindNativeHookCallbacksLocked(RuntimeState& state,
                                                                    uint32_t hook_id,
                                                                    uint32_t* script_id_out) {
    for (auto& script_entry : state.native_hook_callbacks) {
        auto hook_it = script_entry.second.find(hook_id);
        if (hook_it != script_entry.second.end()) {
            if (script_id_out != nullptr) {
                *script_id_out = script_entry.first;
            }
            return &hook_it->second;
        }
    }
    return nullptr;
}

std::string FormatHookValue(uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

std::string FormatUInt64Decimal(uint64_t value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string FormatInt64Decimal(int64_t value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

bool ParsePointerString(const std::string& text, uint64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    *value_out = 0;
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *value_out = static_cast<uint64_t>(parsed);
    return *value_out != 0;
}

bool ParseNullablePointerString(const std::string& text, uint64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    *value_out = 0;
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *value_out = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseUInt64String(const std::string& text, uint64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    *value_out = 0;
    if (text.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        return false;
    }
    *value_out = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseInt64String(const std::string& text, int64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    *value_out = 0;
    if (text.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 0);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        return false;
    }
    *value_out = static_cast<int64_t>(parsed);
    return true;
}

bool ParsePointerValue(JSContext* ctx, JSValueConst value, uint64_t* value_out) {
    const char* cstr = JS_ToCString(ctx, value);
    if (cstr == nullptr) {
        return false;
    }
    const std::string text = cstr;
    JS_FreeCString(ctx, cstr);
    return ParseNullablePointerString(text, value_out);
}

bool ParseJavaEnvWrapperHandle(JSContext* ctx, JSValueConst value, uint64_t* env_ptr_out) {
    if (env_ptr_out == nullptr) {
        JS_ThrowInternalError(ctx, "env_ptr_out is null");
        return false;
    }
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "Java Env wrapper is invalid");
        return false;
    }

    JSValue env_handle_value = JS_GetPropertyStr(ctx, value, kJavaEnvHandleProperty);
    if (JS_IsException(env_handle_value)) {
        return false;
    }

    const bool ok = ParsePointerValue(ctx, env_handle_value, env_ptr_out);
    JS_FreeValue(ctx, env_handle_value);
    if (!ok || *env_ptr_out == 0u) {
        JS_ThrowTypeError(ctx, "Java Env wrapper is invalid");
        return false;
    }

    return true;
}

bool ParseJavaClassWrapperInfo(JSContext* ctx,
                               JSValueConst value,
                               std::string* class_name_out,
                               uint64_t* loader_handle_out) {
    if (class_name_out == nullptr || loader_handle_out == nullptr) {
        return false;
    }

    class_name_out->clear();
    *loader_handle_out = 0u;

    if (!JS_IsObject(value)) {
        return false;
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, value, "$className");
    JSValue receiver_handle_value = JS_GetPropertyStr(ctx, value, kJavaReceiverHandleProperty);
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, value, kJavaLoaderHandleProperty);
    if (JS_IsException(class_name_value) ||
        JS_IsException(receiver_handle_value) ||
        JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return false;
    }

    bool is_class_wrapper = false;
    const char* class_name = JS_ToCString(ctx, class_name_value);
    if (class_name != nullptr && class_name[0] != '\0' &&
        !JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value)) {
        uint64_t receiver_handle = UINT64_MAX;
        if (ParsePointerValue(ctx, receiver_handle_value, &receiver_handle) && receiver_handle == 0u) {
            is_class_wrapper = true;
        }
    }

    uint64_t loader_handle = 0u;
    if (is_class_wrapper &&
        !JS_IsUndefined(loader_handle_value) &&
        !JS_IsNull(loader_handle_value) &&
        !ParsePointerValue(ctx, loader_handle_value, &loader_handle)) {
        is_class_wrapper = false;
    }

    if (is_class_wrapper) {
        *class_name_out = class_name;
        *loader_handle_out = loader_handle;
    }

    if (class_name != nullptr) {
        JS_FreeCString(ctx, class_name);
    }
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    return is_class_wrapper;
}

bool TryGetInteger64ObjectValue(JSContext* ctx,
                                JSValueConst value,
                                bool* matched,
                                uint64_t* raw_value,
                                bool* is_signed) {
    if (matched == nullptr || raw_value == nullptr || is_signed == nullptr) {
        return false;
    }
    *matched = false;
    *raw_value = 0;
    *is_signed = false;

    if (!JS_IsObject(value)) {
        return true;
    }

    JSValue raw_prop = JS_GetPropertyStr(ctx, value, "__nook_int64_raw");
    if (JS_IsException(raw_prop)) {
        JS_FreeValue(ctx, raw_prop);
        return false;
    }
    if (JS_IsUndefined(raw_prop)) {
        JS_FreeValue(ctx, raw_prop);
        return true;
    }

    JSValue signed_prop = JS_GetPropertyStr(ctx, value, "__nook_int64_signed");
    if (JS_IsException(signed_prop)) {
        JS_FreeValue(ctx, raw_prop);
        JS_FreeValue(ctx, signed_prop);
        return false;
    }

    const char* raw_cstr = JS_ToCString(ctx, raw_prop);
    if (raw_cstr == nullptr) {
        JS_FreeValue(ctx, signed_prop);
        JS_FreeValue(ctx, raw_prop);
        return false;
    }

    const bool ok = ParseUInt64String(raw_cstr, raw_value);
    JS_FreeCString(ctx, raw_cstr);
    if (!ok) {
        JS_FreeValue(ctx, signed_prop);
        JS_FreeValue(ctx, raw_prop);
        return false;
    }

    *is_signed = JS_ToBool(ctx, signed_prop) != 0;
    *matched = true;
    JS_FreeValue(ctx, signed_prop);
    JS_FreeValue(ctx, raw_prop);
    return true;
}

bool ParseInteger64Value(JSContext* ctx,
                         JSValueConst value,
                         uint64_t* raw_value,
                         bool* is_signed_out) {
    if (raw_value == nullptr || is_signed_out == nullptr) {
        return false;
    }

    bool matched = false;
    bool is_signed = false;
    if (!TryGetInteger64ObjectValue(ctx, value, &matched, raw_value, &is_signed)) {
        return false;
    }
    if (matched) {
        *is_signed_out = is_signed;
        return true;
    }

    const char* cstr = JS_ToCString(ctx, value);
    if (cstr == nullptr) {
        return false;
    }
    const std::string text = cstr;
    JS_FreeCString(ctx, cstr);

    if (ParseUInt64String(text, raw_value)) {
        *is_signed_out = false;
        return true;
    }

    int64_t signed_value = 0;
    if (ParseInt64String(text, &signed_value)) {
        *raw_value = static_cast<uint64_t>(signed_value);
        *is_signed_out = true;
        return true;
    }

    return false;
}

bool ParseNativeFunctionValueType(const std::string& text,
                                  NativeFunctionValueType* type_out) {
    if (type_out == nullptr) {
        return false;
    }

    if (text == "void") {
        *type_out = NativeFunctionValueType::kVoid;
        return true;
    }
    if (text == "bool") {
        *type_out = NativeFunctionValueType::kBool;
        return true;
    }
    if (text == "int8") {
        *type_out = NativeFunctionValueType::kInt8;
        return true;
    }
    if (text == "uint8") {
        *type_out = NativeFunctionValueType::kUInt8;
        return true;
    }
    if (text == "int16") {
        *type_out = NativeFunctionValueType::kInt16;
        return true;
    }
    if (text == "uint16") {
        *type_out = NativeFunctionValueType::kUInt16;
        return true;
    }
    if (text == "int" || text == "int32") {
        *type_out = NativeFunctionValueType::kInt32;
        return true;
    }
    if (text == "uint32") {
        *type_out = NativeFunctionValueType::kUInt32;
        return true;
    }
    if (text == "int64") {
        *type_out = NativeFunctionValueType::kInt64;
        return true;
    }
    if (text == "uint64") {
        *type_out = NativeFunctionValueType::kUInt64;
        return true;
    }
    if (text == "float") {
        *type_out = NativeFunctionValueType::kFloat;
        return true;
    }
    if (text == "double") {
        *type_out = NativeFunctionValueType::kDouble;
        return true;
    }
    if (text == "pointer") {
        *type_out = NativeFunctionValueType::kPointer;
        return true;
    }

    return false;
}

bool ParseNativeFunctionValueTypeArray(JSContext* ctx,
                                       JSValueConst value,
                                       JSValue* array_out) {
    if (array_out == nullptr) {
        return false;
    }
    *array_out = JS_UNDEFINED;

    if (!JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "NativeFunction argTypes must be an array");
        return false;
    }

    uint32_t length = 0u;
    if (!GetArrayLength(ctx, value, &length)) {
        JS_ThrowInternalError(ctx, "NativeFunction read argTypes length failed");
        return false;
    }
    if (length > 4u) {
        JS_ThrowTypeError(ctx, "NativeFunction supports at most 4 arguments");
        return false;
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        *array_out = result;
        return false;
    }

    for (uint32_t index = 0u; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(ctx, value, index);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, item);
            JS_FreeValue(ctx, result);
            return false;
        }

        const char* item_cstr = JS_ToCString(ctx, item);
        JS_FreeValue(ctx, item);
        if (item_cstr == nullptr) {
            JS_FreeValue(ctx, result);
            JS_ThrowTypeError(ctx, "NativeFunction argument type must be a string");
            return false;
        }

        const std::string item_text = item_cstr;
        JS_FreeCString(ctx, item_cstr);
        NativeFunctionValueType item_type = NativeFunctionValueType::kVoid;
        if (!ParseNativeFunctionValueType(item_text, &item_type) ||
            item_type == NativeFunctionValueType::kVoid) {
            JS_FreeValue(ctx, result);
            JS_ThrowTypeError(ctx, "NativeFunction unsupported argument type");
            return false;
        }

        if (JS_SetPropertyUint32(ctx,
                                 result,
                                 index,
                                 JS_NewUint32(ctx, static_cast<uint32_t>(item_type))) < 0) {
            JS_FreeValue(ctx, result);
            return false;
        }
    }

    *array_out = result;
    return true;
}

bool ReadNativeFunctionValueTypeMetadataArray(JSContext* ctx,
                                              JSValueConst value,
                                              std::vector<NativeFunctionValueType>* types_out) {
    if (types_out == nullptr) {
        return false;
    }
    types_out->clear();

    if (!JS_IsArray(ctx, value)) {
        return false;
    }

    uint32_t length = 0u;
    if (!GetArrayLength(ctx, value, &length)) {
        return false;
    }
    if (length > 4u) {
        return false;
    }

    types_out->reserve(length);
    for (uint32_t index = 0u; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(ctx, value, index);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, item);
            return false;
        }

        uint32_t item_raw = 0u;
        if (JS_ToUint32(ctx, &item_raw, item) < 0) {
            JS_FreeValue(ctx, item);
            return false;
        }
        JS_FreeValue(ctx, item);

        const NativeFunctionValueType item_type =
            static_cast<NativeFunctionValueType>(item_raw);
        if (item_type == NativeFunctionValueType::kVoid) {
            return false;
        }
        types_out->push_back(item_type);
    }

    return true;
}

std::string FormatHexdumpBytes(const uint8_t* data, size_t length) {
    std::ostringstream stream;
    for (size_t index = 0; index < length; ++index) {
        if (index > 0) {
            if ((index % 16u) == 0u) {
                stream << '\n';
            } else {
                stream << ' ';
            }
        }
        char byte_text[3] = {};
        std::snprintf(byte_text, sizeof(byte_text), "%02x", data[index]);
        stream << byte_text;
    }
    return stream.str();
}

std::string FormatHexdumpStyled(const uint8_t* data,
                                size_t length,
                                uint64_t base_address,
                                bool header,
                                bool ansi) {
    const int address_width = base_address > 0xffffffffull ? 16 : 8;
    const char* address_prefix = ansi ? "\x1b[36m" : "";
    const char* ascii_prefix = ansi ? "\x1b[32m" : "";
    const char* color_suffix = ansi ? "\x1b[0m" : "";
    std::ostringstream stream;

    if (header) {
        stream << std::string(static_cast<size_t>(address_width), ' ') << "  ";
        for (int index = 0; index < 16; ++index) {
            char byte_text[3] = {};
            std::snprintf(byte_text, sizeof(byte_text), "%02x", index);
            if (index > 0) {
                stream << ' ';
            }
            stream << byte_text;
        }
        stream << '\n';
    }

    for (size_t line_start = 0; line_start < length; line_start += 16u) {
        const size_t line_length = std::min<size_t>(16u, length - line_start);
        char address_text[17] = {};
        if (address_width == 16) {
            std::snprintf(address_text,
                          sizeof(address_text),
                          "%016llx",
                          static_cast<unsigned long long>(base_address + line_start));
        } else {
            std::snprintf(address_text,
                          sizeof(address_text),
                          "%08llx",
                          static_cast<unsigned long long>(base_address + line_start));
        }
        stream << address_prefix << address_text << color_suffix << "  ";

        for (size_t index = 0; index < 16u; ++index) {
            if (index > 0) {
                stream << ' ';
            }
            if (index < line_length) {
                char byte_text[3] = {};
                std::snprintf(byte_text, sizeof(byte_text), "%02x", data[line_start + index]);
                stream << byte_text;
            } else {
                stream << "  ";
            }
        }

        stream << "  ";
        stream << ascii_prefix;
        for (size_t index = 0; index < line_length; ++index) {
            const uint8_t value = data[line_start + index];
            stream << ((value >= 0x20u && value <= 0x7eu) ? static_cast<char>(value) : '.');
        }
        stream << color_suffix;

        if ((line_start + line_length) < length) {
            stream << '\n';
        }
    }

    return stream.str();
}

bool TryGetInterceptorModuleSymbolTarget(JSContext* ctx,
                                         JSValueConst target,
                                         bool* matched,
                                         std::string* module_name,
                                         std::string* symbol_name) {
    if (matched == nullptr || module_name == nullptr || symbol_name == nullptr) {
        return false;
    }
    *matched = false;
    module_name->clear();
    symbol_name->clear();

    if (!JS_IsObject(target)) {
        return true;
    }

    JSValue module_value = JS_GetPropertyStr(ctx, target, "module");
    if (JS_IsException(module_value)) {
        return false;
    }
    JSValue symbol_value = JS_GetPropertyStr(ctx, target, "symbol");
    if (JS_IsException(symbol_value)) {
        JS_FreeValue(ctx, module_value);
        return false;
    }

    const bool has_module = !JS_IsUndefined(module_value) && !JS_IsNull(module_value);
    const bool has_symbol = !JS_IsUndefined(symbol_value) && !JS_IsNull(symbol_value);
    if (!has_module && !has_symbol) {
        JS_FreeValue(ctx, module_value);
        JS_FreeValue(ctx, symbol_value);
        return true;
    }
    if (!has_module) {
        JS_FreeValue(ctx, module_value);
        JS_FreeValue(ctx, symbol_value);
        JS_ThrowTypeError(ctx, "Interceptor.attach target.module is required");
        return false;
    }
    if (!has_symbol) {
        JS_FreeValue(ctx, module_value);
        JS_FreeValue(ctx, symbol_value);
        JS_ThrowTypeError(ctx, "Interceptor.attach target.symbol is required");
        return false;
    }

    const char* module_cstr = JS_ToCString(ctx, module_value);
    if (module_cstr == nullptr) {
        JS_FreeValue(ctx, module_value);
        JS_FreeValue(ctx, symbol_value);
        JS_ThrowTypeError(ctx, "Interceptor.attach target.module must be a string");
        return false;
    }
    const char* symbol_cstr = JS_ToCString(ctx, symbol_value);
    if (symbol_cstr == nullptr) {
        JS_FreeCString(ctx, module_cstr);
        JS_FreeValue(ctx, module_value);
        JS_FreeValue(ctx, symbol_value);
        JS_ThrowTypeError(ctx, "Interceptor.attach target.symbol must be a string");
        return false;
    }

    *module_name = module_cstr;
    *symbol_name = symbol_cstr;
    *matched = true;

    JS_FreeCString(ctx, module_cstr);
    JS_FreeCString(ctx, symbol_cstr);
    JS_FreeValue(ctx, module_value);
    JS_FreeValue(ctx, symbol_value);
    return true;
}

JSValue MakeNativePointer(JSContext* ctx, uint64_t value);
JSValue MakeInteger64Object(JSContext* ctx, uint64_t raw_value, bool is_signed);
JSValue JsNativePointerRead(JSContext* ctx,
                            JSValueConst this_val,
                            int argc,
                            JSValueConst* argv,
                            int magic,
                            JSValue* func_data);
JSValue JsNativePointerWrite(JSContext* ctx,
                             JSValueConst this_val,
                             int argc,
                             JSValueConst* argv,
                             int magic,
                             JSValue* func_data);
JSValue JsNativePointerToInteger(JSContext* ctx,
                                 JSValueConst this_val,
                                 int argc,
                                 JSValueConst* argv,
                                 int magic,
                                 JSValue* func_data);
JSValue JsNativePointerEquals(JSContext* ctx,
                              JSValueConst this_val,
                              int argc,
                              JSValueConst* argv,
                              int magic,
                              JSValue* func_data);
JSValue JsNativePointerCompare(JSContext* ctx,
                               JSValueConst this_val,
                               int argc,
                               JSValueConst* argv,
                               int magic,
                               JSValue* func_data);
JSValue JsNativePointerReadUtf16String(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv,
                                       int magic,
                                       JSValue* func_data);
JSValue JsNativePointerWriteUtf16String(JSContext* ctx,
                                        JSValueConst this_val,
                                        int argc,
                                        JSValueConst* argv,
                                        int magic,
                                        JSValue* func_data);
JSValue JsUInt64(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsInt64(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsHexdump(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue JsNativeHookArgumentReplace(JSContext* ctx,
                                    JSValueConst this_val,
                                    int argc,
                                    JSValueConst* argv,
                                    int magic,
                                    JSValue* func_data);
JSValue JsNativeHookReturnValueReplace(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv,
                                       int magic,
                                       JSValue* func_data);
JSValue JsMemoryAllocUtf16String(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
bool IsObserverModeHookMutation(JSContext* ctx, JSValueConst receiver);
void ReportIgnoredNativeHookMutationLocked(JSContext* ctx, const char* kind);

bool TryGetMutableNativePointerValue(JSContext* ctx,
                                     JSValueConst this_val,
                                     uint64_t* value_out) {
    if (value_out == nullptr || !JS_IsObject(this_val)) {
        return false;
    }
    JSValue value_prop = JS_GetPropertyStr(ctx, this_val, kNativePointerValueProperty);
    if (JS_IsException(value_prop)) {
        JS_FreeValue(ctx, value_prop);
        return false;
    }
    if (JS_IsUndefined(value_prop)) {
        JS_FreeValue(ctx, value_prop);
        return false;
    }
    bool ok = false;
    if (JS_IsNumber(value_prop) || JS_IsBigInt(ctx, value_prop)) {
        uint64_t raw_value = 0u;
        bool is_signed = false;
        ok = ParseInteger64Value(ctx, value_prop, &raw_value, &is_signed);
        if (ok) {
            *value_out = raw_value;
        }
    } else {
        ok = ParsePointerValue(ctx, value_prop, value_out);
    }
    JS_FreeValue(ctx, value_prop);
    return ok;
}

bool ResolveNativePointerValue(JSContext* ctx,
                               JSValueConst this_val,
                               JSValue* func_data,
                               uint64_t* value_out) {
    if (TryGetMutableNativePointerValue(ctx, this_val, value_out)) {
        return true;
    }
    if (func_data == nullptr) {
        return false;
    }
    return ParsePointerValue(ctx, func_data[0], value_out);
}

JSValue ResolveNativePointerDisplayValue(JSContext* ctx,
                                         JSValueConst this_val,
                                         JSValue* func_data) {
    if (JS_IsObject(this_val)) {
        JSValue value_prop = JS_GetPropertyStr(ctx, this_val, kNativePointerValueProperty);
        if (JS_IsException(value_prop)) {
            return value_prop;
        }
        if (!JS_IsUndefined(value_prop)) {
            if (JS_IsNumber(value_prop) || JS_IsBigInt(ctx, value_prop)) {
                uint64_t raw_value = 0u;
                bool is_signed = false;
                if (ParseInteger64Value(ctx, value_prop, &raw_value, &is_signed)) {
                    JS_FreeValue(ctx, value_prop);
                    return JS_NewString(ctx, FormatHookValue(raw_value).c_str());
                }
            }
            return value_prop;
        }
        JS_FreeValue(ctx, value_prop);
    }
    return JS_DupValue(ctx, func_data[0]);
}

bool UpdateNativePointerValue(JSContext* ctx,
                              JSValueConst pointer_object,
                              uint64_t value,
                              std::string* error_message) {
    if (!JS_IsObject(pointer_object)) {
        SetError(error_message, "native pointer object is invalid");
        return false;
    }
    if (JS_SetPropertyStr(ctx,
                          pointer_object,
                          kNativePointerValueProperty,
                          JS_NewBigUint64(ctx, value)) < 0) {
        SetError(error_message, "update native pointer value failed");
        return false;
    }
    return true;
}

bool SetOrUpdateNativePointerProperty(JSContext* ctx,
                                      JSValue object,
                                      const char* name,
                                      uint64_t value,
                                      std::string* error_message) {
    JSValue existing = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(existing)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    if (!JS_IsUndefined(existing)) {
        const bool ok = UpdateNativePointerValue(ctx, existing, value, error_message);
        JS_FreeValue(ctx, existing);
        return ok;
    }
    JS_FreeValue(ctx, existing);

    if (JS_SetPropertyStr(ctx, object, name, MakeNativePointer(ctx, value)) < 0) {
        SetError(error_message, "update native pointer property failed");
        return false;
    }
    return true;
}

bool SetUint32Property(JSContext* ctx,
                       JSValueConst object,
                       const char* name,
                       uint32_t value,
                       std::string* error_message) {
    if (JS_SetPropertyStr(ctx, object, name, JS_NewUint32(ctx, value)) < 0) {
        SetError(error_message, "update uint32 property failed");
        return false;
    }
    return true;
}

bool SetBoolProperty(JSContext* ctx,
                     JSValueConst object,
                     const char* name,
                     bool value,
                     std::string* error_message) {
    if (JS_SetPropertyStr(ctx, object, name, JS_NewBool(ctx, value ? 1 : 0)) < 0) {
        SetError(error_message, "update bool property failed");
        return false;
    }
    return true;
}

bool GetUint32Property(JSContext* ctx,
                       JSValueConst object,
                       const char* name,
                       uint32_t* value_out,
                       std::string* error_message) {
    if (value_out == nullptr) {
        SetError(error_message, "uint32 output is null");
        return false;
    }
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    uint32_t parsed = 0u;
    const int status = JS_ToUint32(ctx, &parsed, value);
    JS_FreeValue(ctx, value);
    if (status < 0) {
        SetError(error_message, "uint32 property is invalid");
        return false;
    }
    *value_out = parsed;
    return true;
}

bool GetBoolProperty(JSContext* ctx,
                     JSValueConst object,
                     const char* name,
                     bool* value_out,
                     std::string* error_message) {
    if (value_out == nullptr) {
        SetError(error_message, "bool output is null");
        return false;
    }
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    const int parsed = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
    if (parsed < 0) {
        SetError(error_message, "bool property is invalid");
        return false;
    }
    *value_out = parsed != 0;
    return true;
}

bool MarkNativeHookArgumentDirty(JSContext* ctx,
                                 JSValueConst receiver,
                                 uint32_t argument_index,
                                 std::string* error_message) {
    uint32_t mask = 0u;
    if (!GetUint32Property(ctx,
                           receiver,
                           kNativeHookMutationArgumentMaskProperty,
                           &mask,
                           error_message)) {
        return false;
    }
    return SetUint32Property(ctx,
                             receiver,
                             kNativeHookMutationArgumentMaskProperty,
                             mask | (1u << argument_index),
                             error_message);
}

bool MarkNativeHookContextDirty(JSContext* ctx,
                                JSValueConst receiver,
                                uint32_t context_index,
                                std::string* error_message) {
    uint32_t mask = 0u;
    if (!GetUint32Property(ctx,
                           receiver,
                           kNativeHookMutationContextMaskProperty,
                           &mask,
                           error_message)) {
        return false;
    }
    return SetUint32Property(ctx,
                             receiver,
                             kNativeHookMutationContextMaskProperty,
                             mask | (1u << context_index),
                             error_message);
}

bool MarkNativeHookReturnValueDirty(JSContext* ctx,
                                    JSValueConst receiver,
                                    std::string* error_message) {
    return SetBoolProperty(ctx,
                           receiver,
                           kNativeHookMutationReturnValueDirtyProperty,
                           true,
                           error_message);
}

bool MarkNativeHookReturnAddressDirty(JSContext* ctx,
                                      JSValueConst receiver,
                                      std::string* error_message) {
    return SetBoolProperty(ctx,
                           receiver,
                           kNativeHookMutationReturnAddressDirtyProperty,
                           true,
                           error_message);
}

bool ResetNativeHookMutationState(JSContext* ctx,
                                  JSValueConst receiver,
                                  std::string* error_message) {
    return SetUint32Property(ctx, receiver, kNativeHookMutationArgumentMaskProperty, 0u, error_message) &&
           SetUint32Property(ctx, receiver, kNativeHookMutationContextMaskProperty, 0u, error_message) &&
           SetBoolProperty(ctx, receiver, kNativeHookMutationReturnValueDirtyProperty, false, error_message) &&
           SetBoolProperty(ctx, receiver, kNativeHookMutationReturnAddressDirtyProperty, false, error_message);
}

bool SetBaselinePointerProperty(JSContext* ctx,
                                JSValue object,
                                const char* name,
                                uint64_t value,
                                std::string* error_message) {
    if (!JS_IsObject(object)) {
        SetError(error_message, "baseline pointer target is invalid");
        return false;
    }
    if (JS_SetPropertyStr(ctx, object, name, JS_NewString(ctx, FormatHookValue(value).c_str())) < 0) {
        SetError(error_message, "update baseline pointer value failed");
        return false;
    }
    return true;
}

bool ParseBaselinePointerProperty(JSContext* ctx,
                                  JSValue object,
                                  const char* name,
                                  uint64_t* value_out,
                                  std::string* error_message) {
    if (value_out == nullptr) {
        SetError(error_message, "baseline pointer output is null");
        return false;
    }
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    const bool ok = ParsePointerValue(ctx, value, value_out);
    JS_FreeValue(ctx, value);
    if (!ok) {
        SetError(error_message, "baseline pointer value is invalid");
        return false;
    }
    return true;
}

bool TryParseBaselinePointerProperty(JSContext* ctx,
                                     JSValue object,
                                     const char* name,
                                     uint64_t* value_out) {
    if (value_out == nullptr) {
        return false;
    }
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return false;
    }
    if (JS_IsUndefined(value)) {
        JS_FreeValue(ctx, value);
        return false;
    }
    const bool ok = ParsePointerValue(ctx, value, value_out);
    JS_FreeValue(ctx, value);
    return ok;
}

struct NativeHookContextPointerDescriptor {
    const char* public_name;
    const char* hidden_value_name;
    uint32_t bit_index;
};

const NativeHookContextPointerDescriptor kNativeHookContextPointerDescriptors[] = {
    {"x0", kNativeHookContextX0ValueProperty, 0u},
    {"x1", kNativeHookContextX1ValueProperty, 1u},
    {"x2", kNativeHookContextX2ValueProperty, 2u},
    {"x3", kNativeHookContextX3ValueProperty, 3u},
    {"x4", kNativeHookContextX4ValueProperty, 4u},
    {"x5", kNativeHookContextX5ValueProperty, 5u},
    {"x6", kNativeHookContextX6ValueProperty, 6u},
    {"x7", kNativeHookContextX7ValueProperty, 7u},
    {"sp", kNativeHookContextSpValueProperty, 8u},
    {"fp", kNativeHookContextFpValueProperty, 9u},
    {"lr", kNativeHookContextLrValueProperty, 10u},
    {"pc", kNativeHookContextPcValueProperty, 11u},
};

const NativeHookContextPointerDescriptor* FindNativeHookContextPointerDescriptorByName(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        if (std::strcmp(descriptor.public_name, name) == 0) {
            return &descriptor;
        }
    }
    return nullptr;
}

const NativeHookContextPointerDescriptor* FindNativeHookContextPointerDescriptorByIndex(uint32_t bit_index) {
    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        if (descriptor.bit_index == bit_index) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool DefineNativeHookPointerAccessor(JSContext* ctx,
                                     JSValue object,
                                     const char* property_name,
                                     JSCFunctionData* getter,
                                     JSCFunctionData* setter,
                                     int magic,
                                     JSValueConst receiver_ref,
                                     std::string* error_message) {
    JSAtom atom = JS_NewAtom(ctx, property_name);
    if (atom == JS_ATOM_NULL) {
        SetError(error_message, "create native hook accessor atom failed");
        return false;
    }
    JSValue func_data[1] = {JS_DupValue(ctx, receiver_ref)};
    JSValue getter_fn = JS_NewCFunctionData(ctx, getter, 0, magic, 1, func_data);
    JSValue setter_fn = JS_NewCFunctionData(ctx, setter, 1, magic, 1, func_data);
    JS_FreeValue(ctx, func_data[0]);
    if (JS_IsException(getter_fn) || JS_IsException(setter_fn) ||
        JS_DefinePropertyGetSet(ctx,
                                object,
                                atom,
                                getter_fn,
                                setter_fn,
                                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE) < 0) {
        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, getter_fn);
        JS_FreeValue(ctx, setter_fn);
        SetError(error_message, "define native hook accessor failed");
        return false;
    }
    JS_FreeAtom(ctx, atom);
    return true;
}

JSValue JsNativeHookReceiverPointerGetter(JSContext* ctx,
                                          JSValueConst this_val,
                                          int argc,
                                          JSValueConst* argv,
                                          int magic,
                                          JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (func_data == nullptr) {
        return JS_ThrowInternalError(ctx, "native hook receiver pointer getter missing state");
    }
    const char* hidden_value_name =
        magic == 0 ? kNativeHookReturnAddressValueProperty : kNativeHookReturnAddressValueProperty;
    JSValue value = JS_GetPropertyStr(ctx, func_data[0], hidden_value_name);
    if (JS_IsException(value)) {
        return value;
    }
    uint64_t pointer_value = 0u;
    const bool ok = ParsePointerValue(ctx, value, &pointer_value);
    JS_FreeValue(ctx, value);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "native hook receiver pointer is invalid");
    }
    return MakeNativePointer(ctx, pointer_value);
}

JSValue JsNativeHookReceiverPointerSetter(JSContext* ctx,
                                          JSValueConst this_val,
                                          int argc,
                                          JSValueConst* argv,
                                          int magic,
                                          JSValue* func_data) {
    (void)this_val;
    if (argc < 1 || func_data == nullptr) {
        return JS_ThrowTypeError(ctx, "native hook receiver pointer setter requires a pointer value");
    }
    uint64_t pointer_value = 0u;
    if (!ParsePointerValue(ctx, argv[0], &pointer_value)) {
        return JS_ThrowTypeError(ctx, "native hook receiver pointer setter requires a pointer value");
    }
    const char* hidden_value_name =
        magic == 0 ? kNativeHookReturnAddressValueProperty : kNativeHookReturnAddressValueProperty;
    std::string error_message;
    if (!SetOrUpdateNativePointerProperty(ctx,
                                          func_data[0],
                                          hidden_value_name,
                                          pointer_value,
                                          &error_message) ||
        !MarkNativeHookReturnAddressDirty(ctx, func_data[0], &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    return JS_UNDEFINED;
}

JSValue JsNativeHookContextPointerGetter(JSContext* ctx,
                                         JSValueConst this_val,
                                         int argc,
                                         JSValueConst* argv,
                                         int magic,
                                         JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    const auto* descriptor = FindNativeHookContextPointerDescriptorByIndex(static_cast<uint32_t>(magic));
    if (descriptor == nullptr || func_data == nullptr) {
        return JS_ThrowInternalError(ctx, "native hook context pointer getter missing state");
    }
    JSValue value = JS_GetPropertyStr(ctx, func_data[0], descriptor->hidden_value_name);
    if (JS_IsException(value)) {
        return value;
    }
    uint64_t pointer_value = 0u;
    const bool ok = ParsePointerValue(ctx, value, &pointer_value);
    JS_FreeValue(ctx, value);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "native hook context pointer is invalid");
    }
    return MakeNativePointer(ctx, pointer_value);
}

JSValue JsNativeHookContextPointerSetter(JSContext* ctx,
                                         JSValueConst this_val,
                                         int argc,
                                         JSValueConst* argv,
                                         int magic,
                                         JSValue* func_data) {
    (void)this_val;
    const auto* descriptor = FindNativeHookContextPointerDescriptorByIndex(static_cast<uint32_t>(magic));
    if (argc < 1 || descriptor == nullptr || func_data == nullptr) {
        return JS_ThrowTypeError(ctx, "native hook context pointer setter requires a pointer value");
    }
    uint64_t pointer_value = 0u;
    if (!ParsePointerValue(ctx, argv[0], &pointer_value)) {
        return JS_ThrowTypeError(ctx, "native hook context pointer setter requires a pointer value");
    }
    JSValue receiver = JS_GetPropertyStr(ctx, func_data[0], kNativeHookContextReceiverProperty);
    if (JS_IsException(receiver)) {
        return receiver;
    }
    std::string error_message;
    const bool ok = SetOrUpdateNativePointerProperty(ctx,
                                                     func_data[0],
                                                     descriptor->hidden_value_name,
                                                     pointer_value,
                                                     &error_message) &&
                    MarkNativeHookContextDirty(ctx, receiver, descriptor->bit_index, &error_message);
    JS_FreeValue(ctx, receiver);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    return JS_UNDEFINED;
}

JSValue JsNativePointerToString(JSContext* ctx,
                                JSValueConst this_val,
                                int argc,
                                JSValueConst* argv,
                                int magic,
                                JSValue* func_data) {
    (void)argc;
    (void)argv;
    (void)magic;
    return ResolveNativePointerDisplayValue(ctx, this_val, func_data);
}

JSValue JsNativeHookReturnValueReplace(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv,
                                       int magic,
                                       JSValue* func_data) {
    (void)magic;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "retval.replace requires a pointer value");
    }

    uint64_t value = 0u;
    if (!ParsePointerValue(ctx, argv[0], &value)) {
        return JS_ThrowTypeError(ctx, "retval.replace requires a pointer value");
    }

    if (func_data != nullptr && !JS_IsUndefined(func_data[0]) &&
        IsObserverModeHookMutation(ctx, func_data[0])) {
        ReportIgnoredNativeHookMutationLocked(ctx, "return");
        return JS_DupValue(ctx, this_val);
    }

    if (JS_SetPropertyStr(ctx,
                          this_val,
                          kNativeHookReturnValueReplacementProperty,
                          MakeNativePointer(ctx, value)) < 0) {
        return JS_ThrowInternalError(ctx, "retval.replace failed");
    }

    std::string error_message;
    if (!UpdateNativePointerValue(ctx, this_val, value, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (func_data != nullptr && !JS_IsUndefined(func_data[0])) {
        if (!MarkNativeHookReturnValueDirty(ctx, func_data[0], &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        JSValue context = JS_GetPropertyStr(ctx, func_data[0], "context");
        if (JS_IsException(context)) {
            JS_FreeValue(ctx, context);
            return JS_ThrowInternalError(ctx, "retval.replace failed");
        }
        if (JS_IsObject(context)) {
            JSValue x0 = JS_GetPropertyStr(ctx, context, "x0");
            if (JS_IsException(x0)) {
                JS_FreeValue(ctx, x0);
                JS_FreeValue(ctx, context);
                return JS_ThrowInternalError(ctx, "retval.replace failed");
            }
            if (JS_IsObject(x0)) {
                if (!SetNativeHookContextPointerProperty(ctx, context, "x0", value, &error_message)) {
                    JS_FreeValue(ctx, x0);
                    JS_FreeValue(ctx, context);
                    return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
                }
            } else if (JS_SetPropertyStr(ctx, context, "x0", MakeNativePointer(ctx, value)) < 0) {
                JS_FreeValue(ctx, x0);
                JS_FreeValue(ctx, context);
                return JS_ThrowInternalError(ctx, "retval.replace failed");
            }
            JS_FreeValue(ctx, x0);
        }
        JS_FreeValue(ctx, context);
    }

    return JS_DupValue(ctx, this_val);
}

JSValue JsNativeHookArgumentReplace(JSContext* ctx,
                                    JSValueConst this_val,
                                    int argc,
                                    JSValueConst* argv,
                                    int magic,
                                    JSValue* func_data) {
    (void)magic;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "arg.replace requires a pointer value");
    }

    uint64_t value = 0u;
    if (!ParsePointerValue(ctx, argv[0], &value)) {
        return JS_ThrowTypeError(ctx, "arg.replace requires a pointer value");
    }

    if (func_data != nullptr && !JS_IsUndefined(func_data[0]) &&
        IsObserverModeHookMutation(ctx, func_data[0])) {
        ReportIgnoredNativeHookMutationLocked(ctx, "argument");
        return JS_DupValue(ctx, this_val);
    }

    if (JS_SetPropertyStr(ctx,
                          this_val,
                          kNativeHookArgumentReplacementProperty,
                          MakeNativePointer(ctx, value)) < 0) {
        return JS_ThrowInternalError(ctx, "arg.replace failed");
    }

    std::string error_message;
    if (!UpdateNativePointerValue(ctx, this_val, value, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (func_data != nullptr && !JS_IsUndefined(func_data[0])) {
        uint32_t argument_index = 0u;
        JSValue index_value = JS_GetPropertyStr(ctx, this_val, "__nookArgumentIndex");
        if (JS_IsException(index_value)) {
            JS_FreeValue(ctx, index_value);
            return JS_ThrowInternalError(ctx, "arg.replace failed");
        }
        const int status = JS_ToUint32(ctx, &argument_index, index_value);
        JS_FreeValue(ctx, index_value);
        if (status < 0 || !MarkNativeHookArgumentDirty(ctx, func_data[0], argument_index, &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }

    return JS_DupValue(ctx, this_val);
}

JSValue JsNativePointerAddSub(JSContext* ctx,
                              JSValueConst this_val,
                              int argc,
                              JSValueConst* argv,
                              int magic,
                              JSValue* func_data) {
    (void)this_val;
    const char* method_name = nullptr;
    switch (magic) {
        case 0:
            method_name = "add";
            break;
        case 1:
            method_name = "sub";
            break;
        case 2:
            method_name = "and";
            break;
        case 3:
            method_name = "or";
            break;
        case 4:
            method_name = "xor";
            break;
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer binary operation");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "NativePointer.%s requires offset", method_name);
    }

    uint64_t base = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &base)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer base");
    }

    uint64_t offset = 0;
    if (!ParsePointerValue(ctx, argv[0], &offset)) {
        return JS_ThrowTypeError(ctx,
                                 "NativePointer.%s offset must be a pointer value",
                                 method_name);
    }

    switch (magic) {
        case 0:
            return MakeNativePointer(ctx, base + offset);
        case 1:
            return MakeNativePointer(ctx, base - offset);
        case 2:
            return MakeNativePointer(ctx, base & offset);
        case 3:
            return MakeNativePointer(ctx, base | offset);
        case 4:
            return MakeNativePointer(ctx, base ^ offset);
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer binary operation");
    }
}

JSValue JsNativePointerIsNull(JSContext* ctx,
                              JSValueConst this_val,
                              int argc,
                              JSValueConst* argv,
                              int magic,
                              JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    return JS_NewBool(ctx, value == 0 ? 1 : 0);
}

JSValue JsNativePointerToInteger(JSContext* ctx,
                                 JSValueConst this_val,
                                 int argc,
                                 JSValueConst* argv,
                                 int magic,
                                 JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }

    switch (magic) {
        case 0:
            return JS_NewInt32(ctx, static_cast<int32_t>(static_cast<uint32_t>(value)));
        case 1:
            return JS_NewUint32(ctx, static_cast<uint32_t>(value));
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer integer conversion");
    }
}

JSValue JsNativePointerEquals(JSContext* ctx,
                              JSValueConst this_val,
                              int argc,
                              JSValueConst* argv,
                              int magic,
                              JSValue* func_data) {
    (void)this_val;
    (void)magic;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "NativePointer.equals requires a pointer value");
    }

    uint64_t left = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &left)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }

    uint64_t right = 0;
    if (!ParsePointerValue(ctx, argv[0], &right)) {
        return JS_ThrowTypeError(ctx, "NativePointer.equals requires a pointer value");
    }

    return JS_NewBool(ctx, left == right ? 1 : 0);
}

JSValue JsNativePointerCompare(JSContext* ctx,
                               JSValueConst this_val,
                               int argc,
                               JSValueConst* argv,
                               int magic,
                               JSValue* func_data) {
    (void)this_val;
    (void)magic;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "NativePointer.compare requires a pointer value");
    }

    uint64_t left = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &left)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }

    uint64_t right = 0;
    if (!ParsePointerValue(ctx, argv[0], &right)) {
        return JS_ThrowTypeError(ctx, "NativePointer.compare requires a pointer value");
    }

    if (left < right) {
        return JS_NewInt32(ctx, -1);
    }
    if (left > right) {
        return JS_NewInt32(ctx, 1);
    }
    return JS_NewInt32(ctx, 0);
}

JSValue JsNativePointerReadUtf8String(JSContext* ctx,
                                      JSValueConst this_val,
                                      int argc,
                                      JSValueConst* argv,
                                      int magic,
                                      JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "readUtf8String on NULL pointer");
    }

    uint32_t max_length = 4096u;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_ToUint32(ctx, &max_length, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx, "readUtf8String maxLength must be a number");
        }
    }
    if (max_length == 0) {
        return JS_NewStringLen(ctx, "", 0);
    }

    JSValue cached_utf8 = JS_GetPropertyStr(ctx, this_val, "$utf8");
    if (!JS_IsException(cached_utf8) && !JS_IsUndefined(cached_utf8) && JS_IsString(cached_utf8)) {
        if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
            size_t cached_length = 0u;
            const char* cached_text = JS_ToCStringLen(ctx, &cached_length, cached_utf8);
            if (cached_text != nullptr) {
                const size_t output_length =
                    std::min(static_cast<size_t>(max_length), cached_length);
                JSValue result = JS_NewStringLen(ctx, cached_text, output_length);
                JS_FreeCString(ctx, cached_text);
                JS_FreeValue(ctx, cached_utf8);
                return result;
            }
        } else {
            return cached_utf8;
        }
    }
    JS_FreeValue(ctx, cached_utf8);

    std::string text;
    if (!ReadUtf8StringFromReadableMemory(static_cast<uintptr_t>(value), max_length, &text)) {
        return JS_ThrowTypeError(ctx, "readUtf8String unreadable pointer");
    }
    if (JS_IsObject(this_val)) {
        JS_DefinePropertyValueStr(ctx,
                                  JS_DupValue(ctx, this_val),
                                  "$utf8",
                                  JS_NewStringLen(ctx, text.data(), text.size()),
                                  JS_PROP_CONFIGURABLE);
    }
    return JS_NewStringLen(ctx, text.data(), text.size());
}

JSValue JsNativePointerReadUtf16String(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv,
                                       int magic,
                                       JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "readUtf16String on NULL pointer");
    }

    uint32_t max_length = 2048u;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_ToUint32(ctx, &max_length, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx, "readUtf16String maxLength must be a number");
        }
    }
    if (max_length == 0u) {
        return JS_NewStringLen(ctx, "", 0);
    }
    if (static_cast<size_t>(max_length) > static_cast<size_t>(-1) / sizeof(uint16_t)) {
        return JS_ThrowTypeError(ctx, "readUtf16String maxLength too large");
    }

    const size_t max_bytes = static_cast<size_t>(max_length) * sizeof(uint16_t);
    if (!IsReadableMemoryRange(static_cast<uintptr_t>(value), max_bytes)) {
        return JS_ThrowTypeError(ctx, "readUtf16String unreadable pointer");
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(value));
    std::vector<uint16_t> units;
    units.reserve(max_length);
    for (uint32_t index = 0; index < max_length; ++index) {
        uint16_t unit = 0u;
        std::memcpy(&unit, bytes + (static_cast<size_t>(index) * sizeof(uint16_t)), sizeof(unit));
        if (unit == 0u) {
            break;
        }
        units.push_back(unit);
    }

    std::string utf8;
    if (!Utf16ToUtf8(units.data(), units.size(), &utf8)) {
        return JS_ThrowTypeError(ctx, "readUtf16String invalid utf16");
    }
    return JS_NewStringLen(ctx, utf8.data(), utf8.size());
}

JSValue JsNativePointerWriteUtf8String(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv,
                                       int magic,
                                       JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "writeUtf8String on NULL pointer");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "writeUtf8String requires a string");
    }

    const char* text = JS_ToCString(ctx, argv[0]);
    if (text == nullptr) {
        return JS_ThrowTypeError(ctx, "writeUtf8String argument must be a string");
    }

    const size_t length = std::strlen(text);
    if (!IsWritableMemoryRange(static_cast<uintptr_t>(value), length + 1u)) {
        JS_FreeCString(ctx, text);
        return JS_ThrowTypeError(ctx, "writeUtf8String unwritable pointer");
    }

    std::memmove(reinterpret_cast<void*>(static_cast<uintptr_t>(value)), text, length + 1u);
    JS_FreeCString(ctx, text);
    return MakeNativePointer(ctx, value);
}

JSValue JsNativePointerWriteUtf16String(JSContext* ctx,
                                        JSValueConst this_val,
                                        int argc,
                                        JSValueConst* argv,
                                        int magic,
                                        JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "writeUtf16String on NULL pointer");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "writeUtf16String requires a string");
    }

    const char* text = JS_ToCString(ctx, argv[0]);
    if (text == nullptr) {
        return JS_ThrowTypeError(ctx, "writeUtf16String argument must be a string");
    }

    std::vector<uint16_t> units;
    if (!Utf8ToUtf16(text, std::strlen(text), &units)) {
        JS_FreeCString(ctx, text);
        return JS_ThrowTypeError(ctx, "writeUtf16String argument must be valid utf8");
    }
    JS_FreeCString(ctx, text);

    units.push_back(0u);
    const size_t byte_length = units.size() * sizeof(uint16_t);
    if (!IsWritableMemoryRange(static_cast<uintptr_t>(value), byte_length)) {
        return JS_ThrowTypeError(ctx, "writeUtf16String unwritable pointer");
    }

    std::memmove(reinterpret_cast<void*>(static_cast<uintptr_t>(value)), units.data(), byte_length);
    return MakeNativePointer(ctx, value);
}

bool TryExtractByteArrayData(JSContext* ctx,
                             JSValueConst value,
                             std::vector<uint8_t>* bytes_out,
                             bool* matched) {
    if (bytes_out == nullptr || matched == nullptr) {
        return false;
    }
    *matched = false;
    bytes_out->clear();

    size_t buffer_size = 0;
    uint8_t* buffer_data = JS_GetArrayBuffer(ctx, &buffer_size, value);
    if (buffer_data != nullptr) {
        bytes_out->assign(buffer_data, buffer_data + buffer_size);
        *matched = true;
        return true;
    }

    if (!JS_IsArray(ctx, value)) {
        return true;
    }

    JSValue length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(length_value)) {
        JS_FreeValue(ctx, length_value);
        return false;
    }

    uint32_t length = 0;
    if (JS_ToUint32(ctx, &length, length_value) < 0) {
        JS_FreeValue(ctx, length_value);
        return false;
    }
    JS_FreeValue(ctx, length_value);

    bytes_out->reserve(length);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(ctx, value, index);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, item);
            return false;
        }

        uint32_t byte_value = 0;
        if (JS_ToUint32(ctx, &byte_value, item) < 0 || byte_value > 0xffu) {
            JS_FreeValue(ctx, item);
            return false;
        }
        JS_FreeValue(ctx, item);
        bytes_out->push_back(static_cast<uint8_t>(byte_value));
    }

    *matched = true;
    return true;
}

bool ReadJStringUtf8(uint64_t env_ptr,
                     uint64_t jstring_ptr,
                     std::string* text_out,
                     std::string* error_message) {
    if (text_out == nullptr) {
        SetError(error_message, "text_out is null");
        return false;
    }
    text_out->clear();

    JsRuntimeReadJStringUtf8ForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.read_jstring_utf8;
    }
    if (callback != nullptr) {
        return callback(env_ptr, jstring_ptr, text_out, error_message);
    }

    (void)env_ptr;
    (void)jstring_ptr;
    SetError(error_message,
             "Nook.Jni.readJStringUtf8 is unsafe in the current async hook runtime; "
             "snapshot Java strings on the hook thread before JS dispatch");
    return false;
}

JSValue JsNativePointerReadByteArray(JSContext* ctx,
                                     JSValueConst this_val,
                                     int argc,
                                     JSValueConst* argv,
                                     int magic,
                                     JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "readByteArray on NULL pointer");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readByteArray requires length");
    }

    uint32_t length = 0;
    if (JS_ToUint32(ctx, &length, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "readByteArray length must be a number");
    }
    if (length == 0u) {
        return JS_NewArrayBufferCopy(ctx, nullptr, 0);
    }
    if (!IsReadableMemoryRange(static_cast<uintptr_t>(value), length)) {
        return JS_ThrowTypeError(ctx, "readByteArray unreadable pointer");
    }

    std::vector<uint8_t> bytes;
    if (!TryReadMemoryRangeSafely(value, static_cast<size_t>(length), &bytes)) {
        return JS_ThrowInternalError(ctx, "readByteArray failed to read range");
    }

    return JS_NewArrayBufferCopy(ctx, bytes.data(), bytes.size());
}

JSValue JsNativePointerWriteByteArray(JSContext* ctx,
                                      JSValueConst this_val,
                                      int argc,
                                      JSValueConst* argv,
                                      int magic,
                                      JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (value == 0) {
        return JS_ThrowTypeError(ctx, "writeByteArray on NULL pointer");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "writeByteArray requires a value");
    }

    std::vector<uint8_t> bytes;
    bool matched = false;
    if (!TryExtractByteArrayData(ctx, argv[0], &bytes, &matched)) {
        return JS_ThrowTypeError(ctx, "writeByteArray value must be an ArrayBuffer or number array");
    }
    if (!matched) {
        return JS_ThrowTypeError(ctx, "writeByteArray value must be an ArrayBuffer or number array");
    }
    if (bytes.empty()) {
        return MakeNativePointer(ctx, value);
    }
    if (!IsWritableMemoryRange(static_cast<uintptr_t>(value), bytes.size())) {
        return JS_ThrowTypeError(ctx, "writeByteArray unwritable pointer");
    }

    std::memmove(reinterpret_cast<void*>(static_cast<uintptr_t>(value)),
                 bytes.data(),
                 bytes.size());
    return MakeNativePointer(ctx, value);
}

JSValue JsInteger64ToString(JSContext* ctx,
                            JSValueConst this_val,
                            int argc,
                            JSValueConst* argv,
                            int magic,
                            JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;

    uint64_t raw_value = 0;
    if (!ParsePointerValue(ctx, func_data[0], &raw_value)) {
        return JS_ThrowInternalError(ctx, "invalid Int64/UInt64 raw value");
    }

    const bool is_signed = JS_ToBool(ctx, func_data[1]) != 0;
    if (is_signed) {
        return JS_NewString(ctx, FormatInt64Decimal(static_cast<int64_t>(raw_value)).c_str());
    }
    return JS_NewString(ctx, FormatUInt64Decimal(raw_value).c_str());
}

JSValue JsInteger64ValueOf(JSContext* ctx,
                           JSValueConst this_val,
                           int argc,
                           JSValueConst* argv,
                           int magic,
                           JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;

    uint64_t raw_value = 0;
    if (!ParsePointerValue(ctx, func_data[0], &raw_value)) {
        return JS_ThrowInternalError(ctx, "invalid Int64/UInt64 raw value");
    }

    const bool is_signed = JS_ToBool(ctx, func_data[1]) != 0;
    if (is_signed) {
        return JS_NewFloat64(ctx, static_cast<double>(static_cast<int64_t>(raw_value)));
    }
    return JS_NewFloat64(ctx, static_cast<double>(raw_value));
}

JSValue MakeInteger64Object(JSContext* ctx, uint64_t raw_value, bool is_signed) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    JSValue raw_string = JS_NewString(ctx, FormatUInt64Decimal(raw_value).c_str());
    if (JS_IsException(raw_string)) {
        JS_FreeValue(ctx, object);
        return raw_string;
    }

    JSValue signed_flag = JS_NewBool(ctx, is_signed ? 1 : 0);
    if (JS_IsException(signed_flag)) {
        JS_FreeValue(ctx, raw_string);
        JS_FreeValue(ctx, object);
        return signed_flag;
    }

    JSValue func_data[2] = {JS_DupValue(ctx, raw_string), JS_DupValue(ctx, signed_flag)};
    JSValue to_string = JS_NewCFunctionData(ctx, JsInteger64ToString, 0, 0, 2, func_data);
    JSValue value_of = JS_NewCFunctionData(ctx, JsInteger64ValueOf, 0, 0, 2, func_data);
    JS_FreeValue(ctx, func_data[0]);
    JS_FreeValue(ctx, func_data[1]);

    if (JS_IsException(to_string) || JS_IsException(value_of)) {
        JS_FreeValue(ctx, to_string);
        JS_FreeValue(ctx, value_of);
        JS_FreeValue(ctx, raw_string);
        JS_FreeValue(ctx, signed_flag);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build Int64/UInt64 failed");
    }

    if (JS_SetPropertyStr(ctx, object, "toString", to_string) < 0) {
        JS_FreeValue(ctx, to_string);
        JS_FreeValue(ctx, value_of);
        JS_FreeValue(ctx, raw_string);
        JS_FreeValue(ctx, signed_flag);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build Int64/UInt64 failed");
    }
    to_string = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "valueOf", value_of) < 0) {
        JS_FreeValue(ctx, value_of);
        JS_FreeValue(ctx, raw_string);
        JS_FreeValue(ctx, signed_flag);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build Int64/UInt64 failed");
    }
    value_of = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "__nook_int64_raw", raw_string) < 0) {
        JS_FreeValue(ctx, raw_string);
        JS_FreeValue(ctx, signed_flag);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build Int64/UInt64 failed");
    }
    raw_string = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "__nook_int64_signed", signed_flag) < 0) {
        JS_FreeValue(ctx, signed_flag);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build Int64/UInt64 failed");
    }

    return object;
}

JSValue JsUInt64(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "uint64 requires a value");
    }

    uint64_t raw_value = 0;
    bool is_signed = false;
    if (!ParseInteger64Value(ctx, argv[0], &raw_value, &is_signed)) {
        return JS_ThrowTypeError(ctx, "uint64 value must be a number, string, or Int64/UInt64");
    }
    return MakeInteger64Object(ctx, raw_value, false);
}

JSValue JsInt64(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "int64 requires a value");
    }

    uint64_t raw_value = 0;
    bool is_signed = false;
    if (!ParseInteger64Value(ctx, argv[0], &raw_value, &is_signed)) {
        return JS_ThrowTypeError(ctx, "int64 value must be a number, string, or Int64/UInt64");
    }
    return MakeInteger64Object(ctx, raw_value, true);
}

JSValue MakeNativePointer(JSContext* ctx, uint64_t value) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    JSValue value_string = JS_NewString(ctx, FormatHookValue(value).c_str());
    if (JS_IsException(value_string)) {
        JS_FreeValue(ctx, object);
        return value_string;
    }

    if (JS_SetPropertyStr(ctx,
                          object,
                          kNativePointerValueProperty,
                          JS_DupValue(ctx, value_string)) < 0) {
        JS_FreeValue(ctx, value_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue to_string = JS_NewCFunctionData(ctx,
                                            JsNativePointerToString,
                                            0,
                                            0,
                                            1,
                                            &value_string);
    JSValue add = JS_NewCFunctionData(ctx,
                                      JsNativePointerAddSub,
                                      1,
                                      0,
                                      1,
                                      &value_string);
    JSValue sub = JS_NewCFunctionData(ctx,
                                      JsNativePointerAddSub,
                                      1,
                                      1,
                                      1,
                                      &value_string);
    JSValue bit_and = JS_NewCFunctionData(ctx,
                                          JsNativePointerAddSub,
                                          1,
                                          2,
                                          1,
                                          &value_string);
    JSValue bit_or = JS_NewCFunctionData(ctx,
                                         JsNativePointerAddSub,
                                         1,
                                         3,
                                         1,
                                         &value_string);
    JSValue bit_xor = JS_NewCFunctionData(ctx,
                                          JsNativePointerAddSub,
                                          1,
                                          4,
                                          1,
                                          &value_string);
    JSValue is_null = JS_NewCFunctionData(ctx,
                                          JsNativePointerIsNull,
                                          0,
                                          0,
                                          1,
                                          &value_string);
    JSValue equals = JS_NewCFunctionData(ctx,
                                         JsNativePointerEquals,
                                         1,
                                         0,
                                         1,
                                         &value_string);
    JSValue compare = JS_NewCFunctionData(ctx,
                                          JsNativePointerCompare,
                                          1,
                                          0,
                                          1,
                                          &value_string);
    JSValue to_int32 = JS_NewCFunctionData(ctx,
                                           JsNativePointerToInteger,
                                           0,
                                           0,
                                           1,
                                           &value_string);
    JSValue to_uint32 = JS_NewCFunctionData(ctx,
                                            JsNativePointerToInteger,
                                            0,
                                            1,
                                            1,
                                            &value_string);
    JSValue read_pointer = JS_NewCFunctionData(ctx,
                                               JsNativePointerRead,
                                               0,
                                               0,
                                               1,
                                               &value_string);
    JSValue read_u8 = JS_NewCFunctionData(ctx,
                                          JsNativePointerRead,
                                          0,
                                          1,
                                          1,
                                          &value_string);
    JSValue read_u16 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           2,
                                           1,
                                           &value_string);
    JSValue read_u32 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           3,
                                           1,
                                           &value_string);
    JSValue read_u64 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           4,
                                           1,
                                           &value_string);
    JSValue read_s8 = JS_NewCFunctionData(ctx,
                                          JsNativePointerRead,
                                          0,
                                          5,
                                          1,
                                          &value_string);
    JSValue read_s16 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           6,
                                           1,
                                           &value_string);
    JSValue read_s32 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           7,
                                           1,
                                           &value_string);
    JSValue read_s64 = JS_NewCFunctionData(ctx,
                                           JsNativePointerRead,
                                           0,
                                           8,
                                           1,
                                           &value_string);
    JSValue read_byte_array = JS_NewCFunctionData(ctx,
                                                  JsNativePointerReadByteArray,
                                                  1,
                                                  0,
                                                  1,
                                                  &value_string);
    JSValue read_utf8_string = JS_NewCFunctionData(ctx,
                                                   JsNativePointerReadUtf8String,
                                                   1,
                                                   0,
                                                   1,
                                                   &value_string);
    JSValue write_pointer = JS_NewCFunctionData(ctx,
                                                JsNativePointerWrite,
                                                1,
                                                0,
                                                1,
                                                &value_string);
    JSValue write_u8 = JS_NewCFunctionData(ctx,
                                           JsNativePointerWrite,
                                           1,
                                           1,
                                           1,
                                           &value_string);
    JSValue write_u16 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            2,
                                            1,
                                            &value_string);
    JSValue write_u32 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            3,
                                            1,
                                            &value_string);
    JSValue write_u64 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            4,
                                            1,
                                            &value_string);
    JSValue write_s8 = JS_NewCFunctionData(ctx,
                                           JsNativePointerWrite,
                                           1,
                                           5,
                                           1,
                                           &value_string);
    JSValue write_s16 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            6,
                                            1,
                                            &value_string);
    JSValue write_s32 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            7,
                                            1,
                                            &value_string);
    JSValue write_s64 = JS_NewCFunctionData(ctx,
                                            JsNativePointerWrite,
                                            1,
                                            8,
                                            1,
                                            &value_string);
    JSValue write_byte_array = JS_NewCFunctionData(ctx,
                                                   JsNativePointerWriteByteArray,
                                                   1,
                                                   0,
                                                   1,
                                                   &value_string);
    JSValue write_utf8_string = JS_NewCFunctionData(ctx,
                                                    JsNativePointerWriteUtf8String,
                                                    1,
                                                    0,
                                                    1,
                                                    &value_string);
    JS_FreeValue(ctx, value_string);

    if (JS_IsException(to_string) || JS_IsException(add) || JS_IsException(sub) ||
        JS_IsException(bit_and) || JS_IsException(bit_or) || JS_IsException(bit_xor) ||
        JS_IsException(is_null) || JS_IsException(equals) || JS_IsException(compare) ||
        JS_IsException(to_int32) || JS_IsException(to_uint32) ||
        JS_IsException(read_pointer) || JS_IsException(read_u8) ||
        JS_IsException(read_u16) || JS_IsException(read_u32) || JS_IsException(read_u64) ||
        JS_IsException(read_s8) || JS_IsException(read_s16) || JS_IsException(read_s32) ||
        JS_IsException(read_s64) || JS_IsException(read_byte_array) ||
        JS_IsException(read_utf8_string) ||
        JS_IsException(write_pointer) || JS_IsException(write_u8) ||
        JS_IsException(write_u16) || JS_IsException(write_u32) || JS_IsException(write_u64) ||
        JS_IsException(write_s8) || JS_IsException(write_s16) || JS_IsException(write_s32) ||
        JS_IsException(write_s64) || JS_IsException(write_byte_array) ||
        JS_IsException(write_utf8_string)) {
        JS_FreeValue(ctx, to_string);
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, sub);
        JS_FreeValue(ctx, bit_and);
        JS_FreeValue(ctx, bit_or);
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    if (JS_SetPropertyStr(ctx, object, "toString", to_string) < 0) {
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, sub);
        JS_FreeValue(ctx, bit_and);
        JS_FreeValue(ctx, bit_or);
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    to_string = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "add", add) < 0) {
        JS_FreeValue(ctx, sub);
        JS_FreeValue(ctx, bit_and);
        JS_FreeValue(ctx, bit_or);
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    add = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "sub", sub) < 0) {
        JS_FreeValue(ctx, bit_and);
        JS_FreeValue(ctx, bit_or);
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    sub = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "and", bit_and) < 0) {
        JS_FreeValue(ctx, bit_or);
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    bit_and = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "or", bit_or) < 0) {
        JS_FreeValue(ctx, bit_xor);
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    bit_or = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "xor", bit_xor) < 0) {
        JS_FreeValue(ctx, is_null);
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    bit_xor = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "isNull", is_null) < 0) {
        JS_FreeValue(ctx, equals);
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    is_null = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "equals", equals) < 0) {
        JS_FreeValue(ctx, compare);
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    equals = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "compare", compare) < 0) {
        JS_FreeValue(ctx, to_int32);
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    compare = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "toInt32", to_int32) < 0) {
        JS_FreeValue(ctx, to_uint32);
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    to_int32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "toUInt32", to_uint32) < 0) {
        JS_FreeValue(ctx, read_pointer);
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    to_uint32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readPointer", read_pointer) < 0) {
        JS_FreeValue(ctx, read_u8);
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_pointer = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readU8", read_u8) < 0) {
        JS_FreeValue(ctx, read_u16);
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_u8 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readU16", read_u16) < 0) {
        JS_FreeValue(ctx, read_u32);
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_u16 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readU32", read_u32) < 0) {
        JS_FreeValue(ctx, read_u64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_u32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readU64", read_u64) < 0) {
        JS_FreeValue(ctx, read_s8);
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_u64 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readS8", read_s8) < 0) {
        JS_FreeValue(ctx, read_s16);
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_s8 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readS16", read_s16) < 0) {
        JS_FreeValue(ctx, read_s32);
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_s16 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readS32", read_s32) < 0) {
        JS_FreeValue(ctx, read_s64);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_s32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readS64", read_s64) < 0) {
        JS_FreeValue(ctx, read_byte_array);
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_s64 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readByteArray", read_byte_array) < 0) {
        JS_FreeValue(ctx, read_utf8_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_byte_array = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "readUtf8String", read_utf8_string) < 0) {
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    read_utf8_string = JS_UNDEFINED;

    JSValue read_c_string = JS_GetPropertyStr(ctx, object, "readUtf8String");
    if (JS_IsException(read_c_string) ||
        JS_SetPropertyStr(ctx, object, "readCString", read_c_string) < 0) {
        JS_FreeValue(ctx, read_c_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue read_ansi_string = JS_GetPropertyStr(ctx, object, "readUtf8String");
    if (JS_IsException(read_ansi_string) ||
        JS_SetPropertyStr(ctx, object, "readAnsiString", read_ansi_string) < 0) {
        JS_FreeValue(ctx, read_ansi_string);
        JS_FreeValue(ctx, write_pointer);
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    if (JS_SetPropertyStr(ctx, object, "writePointer", write_pointer) < 0) {
        JS_FreeValue(ctx, write_u8);
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_pointer = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeU8", write_u8) < 0) {
        JS_FreeValue(ctx, write_u16);
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_u8 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeU16", write_u16) < 0) {
        JS_FreeValue(ctx, write_u32);
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_u16 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeU32", write_u32) < 0) {
        JS_FreeValue(ctx, write_u64);
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_u32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeU64", write_u64) < 0) {
        JS_FreeValue(ctx, write_s8);
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_u64 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeS8", write_s8) < 0) {
        JS_FreeValue(ctx, write_s16);
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_s8 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeS16", write_s16) < 0) {
        JS_FreeValue(ctx, write_s32);
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_s16 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeS32", write_s32) < 0) {
        JS_FreeValue(ctx, write_s64);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_s32 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeS64", write_s64) < 0) {
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, write_utf8_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_s64 = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeUtf8String", write_utf8_string) < 0) {
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue write_ansi_string = JS_GetPropertyStr(ctx, object, "writeUtf8String");
    if (JS_IsException(write_ansi_string) ||
        JS_SetPropertyStr(ctx, object, "writeAnsiString", write_ansi_string) < 0) {
        JS_FreeValue(ctx, write_ansi_string);
        JS_FreeValue(ctx, write_byte_array);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_utf8_string = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, object, "writeByteArray", write_byte_array) < 0) {
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }
    write_byte_array = JS_UNDEFINED;

    JSValue read_utf16_string =
        JS_NewCFunctionData(ctx, JsNativePointerReadUtf16String, 1, 0, 0, nullptr);
    if (JS_IsException(read_utf16_string) ||
        JS_SetPropertyStr(ctx, object, "readUtf16String", read_utf16_string) < 0) {
        JS_FreeValue(ctx, read_utf16_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue write_utf16_string =
        JS_NewCFunctionData(ctx, JsNativePointerWriteUtf16String, 1, 0, 0, nullptr);
    if (JS_IsException(write_utf16_string) ||
        JS_SetPropertyStr(ctx, object, "writeUtf16String", write_utf16_string) < 0) {
        JS_FreeValue(ctx, write_utf16_string);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue read_float = JS_NewCFunctionData(ctx, JsNativePointerRead, 0, 9, 0, nullptr);
    if (JS_IsException(read_float) ||
        JS_SetPropertyStr(ctx, object, "readFloat", read_float) < 0) {
        JS_FreeValue(ctx, read_float);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue read_double = JS_NewCFunctionData(ctx, JsNativePointerRead, 0, 10, 0, nullptr);
    if (JS_IsException(read_double) ||
        JS_SetPropertyStr(ctx, object, "readDouble", read_double) < 0) {
        JS_FreeValue(ctx, read_double);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue write_float = JS_NewCFunctionData(ctx, JsNativePointerWrite, 1, 9, 0, nullptr);
    if (JS_IsException(write_float) ||
        JS_SetPropertyStr(ctx, object, "writeFloat", write_float) < 0) {
        JS_FreeValue(ctx, write_float);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    JSValue write_double = JS_NewCFunctionData(ctx, JsNativePointerWrite, 1, 10, 0, nullptr);
    if (JS_IsException(write_double) ||
        JS_SetPropertyStr(ctx, object, "writeDouble", write_double) < 0) {
        JS_FreeValue(ctx, write_double);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build NativePointer failed");
    }

    return object;
}

JSValue MakeJavaEnvWrapper(JSContext* ctx, uint64_t env_ptr) {
    JSValue wrapper = JS_NewObject(ctx);
    if (JS_IsException(wrapper)) {
        return wrapper;
    }

    JSValue handle = MakeNativePointer(ctx, env_ptr);
    JSValue hidden_handle = MakeNativePointer(ctx, env_ptr);
    JSValue to_string = JS_NewCFunction(ctx, JsJavaEnvToString, "toString", 0);
    JSValue exception_check = JS_NewCFunction(ctx, JsJavaEnvExceptionCheck, "exceptionCheck", 0);
    JSValue exception_occurred =
        JS_NewCFunction(ctx, JsJavaEnvExceptionOccurred, "exceptionOccurred", 0);
    JSValue exception_clear =
        JS_NewCFunction(ctx, JsJavaEnvExceptionClear, "exceptionClear", 0);
    JSValue find_class = JS_NewCFunction(ctx, JsJavaEnvFindClass, "findClass", 1);
    JSValue get_object_class =
        JS_NewCFunction(ctx, JsJavaEnvGetObjectClass, "getObjectClass", 1);
    JSValue is_same_object =
        JS_NewCFunction(ctx, JsJavaEnvIsSameObject, "isSameObject", 2);
    JSValue is_instance_of =
        JS_NewCFunction(ctx, JsJavaEnvIsInstanceOf, "isInstanceOf", 2);
    JSValue new_string_utf =
        JS_NewCFunction(ctx, JsJavaEnvNewStringUtf, "newStringUtf", 1);
    JSValue get_string_utf_chars =
        JS_NewCFunction(ctx, JsJavaEnvGetStringUtfChars, "getStringUtfChars", 1);
    JSValue release_string_utf_chars =
        JS_NewCFunction(ctx, JsJavaEnvReleaseStringUtfChars, "releaseStringUtfChars", 2);
    JSValue new_global_ref =
        JS_NewCFunction(ctx, JsJavaEnvNewGlobalRef, "newGlobalRef", 1);
    JSValue delete_global_ref =
        JS_NewCFunction(ctx, JsJavaEnvDeleteGlobalRef, "deleteGlobalRef", 1);
    JSValue new_weak_global_ref =
        JS_NewCFunction(ctx, JsJavaEnvNewWeakGlobalRef, "newWeakGlobalRef", 1);
    JSValue delete_weak_global_ref =
        JS_NewCFunction(ctx, JsJavaEnvDeleteWeakGlobalRef, "deleteWeakGlobalRef", 1);
    JSValue get_object_ref_type =
        JS_NewCFunction(ctx, JsJavaEnvGetObjectRefType, "getObjectRefType", 1);
    JSValue get_superclass =
        JS_NewCFunction(ctx, JsJavaEnvGetSuperclass, "getSuperclass", 1);
    JSValue is_assignable_from =
        JS_NewCFunction(ctx, JsJavaEnvIsAssignableFrom, "isAssignableFrom", 2);
    if (JS_IsException(handle) || JS_IsException(hidden_handle) || JS_IsException(to_string) ||
        JS_IsException(exception_check) || JS_IsException(exception_occurred) ||
        JS_IsException(exception_clear) || JS_IsException(find_class) ||
        JS_IsException(get_object_class) || JS_IsException(is_same_object) ||
        JS_IsException(is_instance_of) || JS_IsException(new_string_utf) ||
        JS_IsException(get_string_utf_chars) || JS_IsException(release_string_utf_chars) ||
        JS_IsException(new_global_ref) || JS_IsException(delete_global_ref) ||
        JS_IsException(new_weak_global_ref) || JS_IsException(delete_weak_global_ref) ||
        JS_IsException(get_object_ref_type) ||
        JS_IsException(get_superclass) || JS_IsException(is_assignable_from)) {
        JS_FreeValue(ctx, handle);
        JS_FreeValue(ctx, hidden_handle);
        JS_FreeValue(ctx, to_string);
        JS_FreeValue(ctx, exception_check);
        JS_FreeValue(ctx, exception_occurred);
        JS_FreeValue(ctx, exception_clear);
        JS_FreeValue(ctx, find_class);
        JS_FreeValue(ctx, get_object_class);
        JS_FreeValue(ctx, is_same_object);
        JS_FreeValue(ctx, is_instance_of);
        JS_FreeValue(ctx, new_string_utf);
        JS_FreeValue(ctx, get_string_utf_chars);
        JS_FreeValue(ctx, release_string_utf_chars);
        JS_FreeValue(ctx, new_global_ref);
        JS_FreeValue(ctx, delete_global_ref);
        JS_FreeValue(ctx, new_weak_global_ref);
        JS_FreeValue(ctx, delete_weak_global_ref);
        JS_FreeValue(ctx, get_object_ref_type);
        JS_FreeValue(ctx, get_superclass);
        JS_FreeValue(ctx, is_assignable_from);
        JS_FreeValue(ctx, wrapper);
        return JS_ThrowInternalError(ctx, "build Java Env wrapper failed");
    }

    const int public_flags = JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE;
    const int hidden_flags = JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE;
    if (JS_DefinePropertyValueStr(ctx, wrapper, "handle", handle, public_flags) < 0 ||
        JS_DefinePropertyValueStr(ctx, wrapper, kJavaEnvHandleProperty, hidden_handle, hidden_flags) < 0 ||
        JS_DefinePropertyValueStr(ctx, wrapper, "toString", to_string, public_flags) < 0 ||
        JS_DefinePropertyValueStr(ctx, wrapper, "exceptionCheck", exception_check, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "exceptionOccurred", exception_occurred, public_flags) < 0 ||
        JS_DefinePropertyValueStr(ctx, wrapper, "exceptionClear", exception_clear, public_flags) <
            0 ||
        JS_DefinePropertyValueStr(ctx, wrapper, "findClass", find_class, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "getObjectClass", get_object_class, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "isSameObject", is_same_object, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "isInstanceOf", is_instance_of, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "newStringUtf", new_string_utf, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "getStringUtfChars", get_string_utf_chars, public_flags) < 0 ||
        JS_DefinePropertyValueStr(ctx,
                                  wrapper,
                                  "releaseStringUtfChars",
                                  release_string_utf_chars,
                                  public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "newGlobalRef", new_global_ref, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "deleteGlobalRef", delete_global_ref, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "newWeakGlobalRef", new_weak_global_ref, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "deleteWeakGlobalRef", delete_weak_global_ref, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "getObjectRefType", get_object_ref_type, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "getSuperclass", get_superclass, public_flags) < 0 ||
        JS_DefinePropertyValueStr(
            ctx, wrapper, "isAssignableFrom", is_assignable_from, public_flags) < 0) {
        JS_FreeValue(ctx, wrapper);
        return JS_ThrowInternalError(ctx, "build Java Env wrapper failed");
    }

    return wrapper;
}

JSValue BuildNativeHookArgsArray(JSContext* ctx, const HookEvent& event, JSValueConst receiver) {
    JSValue args = JS_NewArray(ctx);
    if (JS_IsException(args)) {
        return args;
    }

    for (uint32_t index = 0; index < event.argument_count; ++index) {
        JSValue pointer = MakeNativePointer(ctx, event.argument_values[index]);
        if (JS_IsException(pointer)) {
            JS_FreeValue(ctx, pointer);
            JS_FreeValue(ctx, args);
            return JS_ThrowInternalError(ctx, "build native hook args failed");
        }

        JSValue func_data[1] = {JS_DupValue(ctx, receiver)};
        JSValue replace = JS_NewCFunctionData(ctx,
                                              JsNativeHookArgumentReplace,
                                              1,
                                              0,
                                              1,
                                              func_data);
        JS_FreeValue(ctx, func_data[0]);
        if (JS_IsException(replace) || JS_SetPropertyStr(ctx, pointer, "replace", replace) < 0) {
            JS_FreeValue(ctx, replace);
            JS_FreeValue(ctx, pointer);
            JS_FreeValue(ctx, args);
            return JS_ThrowInternalError(ctx, "build native hook args failed");
        }
        if (JS_SetPropertyStr(ctx, pointer, "__nookArgumentIndex", JS_NewUint32(ctx, index)) < 0) {
            JS_FreeValue(ctx, pointer);
            JS_FreeValue(ctx, args);
            return JS_ThrowInternalError(ctx, "build native hook args failed");
        }

        for (uint32_t snapshot_index = 0; snapshot_index < event.jni_utf8_snapshot_count; ++snapshot_index) {
            const HookEvent::JniUtf8ArgumentSnapshot& snapshot =
                event.jni_utf8_snapshots[snapshot_index];
            if (snapshot.argument_index != index) {
                continue;
            }

            JSValue utf8 = JS_NewStringLen(ctx, snapshot.utf8.c_str(), snapshot.utf8.size());
            const char* property_name =
                snapshot.property_name.empty() ? "$jniUtf8" : snapshot.property_name.c_str();
            if (JS_IsException(utf8) ||
                JS_DefinePropertyValueStr(ctx,
                                          pointer,
                                          property_name,
                                          utf8,
                                          JS_PROP_CONFIGURABLE) < 0) {
                JS_FreeValue(ctx, utf8);
                JS_FreeValue(ctx, pointer);
                JS_FreeValue(ctx, args);
                return JS_ThrowInternalError(ctx, "build native hook args failed");
            }
            break;
        }

        if (JS_SetPropertyUint32(ctx, args, index, pointer) < 0) {
            JS_FreeValue(ctx, pointer);
            JS_FreeValue(ctx, args);
            return JS_ThrowInternalError(ctx, "build native hook args failed");
        }
    }

    return args;
}

bool ResetNativeHookArgsArray(JSContext* ctx,
                              JSValue args,
                              const HookEvent& event,
                              JSValueConst receiver,
                              std::string* error_message) {
    if (!JS_IsObject(args)) {
        SetError(error_message, "native hook args wrapper is invalid");
        return false;
    }

    for (uint32_t index = 0; index < event.argument_count; ++index) {
        JSValue pointer = JS_GetPropertyUint32(ctx, args, index);
        if (JS_IsException(pointer)) {
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
        if (!JS_IsObject(pointer)) {
            JS_FreeValue(ctx, pointer);
            SetError(error_message, "native hook args wrapper is invalid");
            return false;
        }

        if (!UpdateNativePointerValue(ctx, pointer, event.argument_values[index], error_message)) {
            JS_FreeValue(ctx, pointer);
            return false;
        }

        JSValue func_data[1] = {JS_DupValue(ctx, receiver)};
        JSValue replace = JS_NewCFunctionData(ctx,
                                              JsNativeHookArgumentReplace,
                                              1,
                                              0,
                                              1,
                                              func_data);
        JS_FreeValue(ctx, func_data[0]);
        if (JS_IsException(replace) || JS_SetPropertyStr(ctx, pointer, "replace", replace) < 0) {
            JS_FreeValue(ctx, replace);
            JS_FreeValue(ctx, pointer);
            SetError(error_message, "build native hook args failed");
            return false;
        }
        if (JS_SetPropertyStr(ctx, pointer, "__nookArgumentIndex", JS_NewUint32(ctx, index)) < 0) {
            JS_FreeValue(ctx, pointer);
            SetError(error_message, "build native hook args failed");
            return false;
        }

        JSValue previous_utf8 = JS_GetPropertyStr(ctx, pointer, "$jniUtf8");
        if (!JS_IsException(previous_utf8) && !JS_IsUndefined(previous_utf8)) {
            JSAtom atom = JS_NewAtom(ctx, "$jniUtf8");
            JS_DeleteProperty(ctx, pointer, atom, 0);
            JS_FreeAtom(ctx, atom);
        }
        JS_FreeValue(ctx, previous_utf8);

        JSValue previous_utf8_alt = JS_GetPropertyStr(ctx, pointer, "$utf8");
        if (!JS_IsException(previous_utf8_alt) && !JS_IsUndefined(previous_utf8_alt)) {
            JSAtom atom = JS_NewAtom(ctx, "$utf8");
            JS_DeleteProperty(ctx, pointer, atom, 0);
            JS_FreeAtom(ctx, atom);
        }
        JS_FreeValue(ctx, previous_utf8_alt);

        for (uint32_t snapshot_index = 0; snapshot_index < event.jni_utf8_snapshot_count; ++snapshot_index) {
            const HookEvent::JniUtf8ArgumentSnapshot& snapshot =
                event.jni_utf8_snapshots[snapshot_index];
            if (snapshot.argument_index != index) {
                continue;
            }

            JSValue utf8 = JS_NewStringLen(ctx, snapshot.utf8.c_str(), snapshot.utf8.size());
            const char* property_name =
                snapshot.property_name.empty() ? "$jniUtf8" : snapshot.property_name.c_str();
            if (JS_IsException(utf8) ||
                JS_DefinePropertyValueStr(ctx,
                                          pointer,
                                          property_name,
                                          utf8,
                                          JS_PROP_CONFIGURABLE) < 0) {
                JS_FreeValue(ctx, utf8);
                JS_FreeValue(ctx, pointer);
                SetError(error_message, "build native hook args failed");
                return false;
            }
            break;
        }

        JS_FreeValue(ctx, pointer);
    }

    return true;
}

JSValue BuildNativeHookInvocationContextObject(JSContext* ctx, const HookEvent& event, bool blocking) {
    JSValue receiver = JS_NewObject(ctx);
    if (JS_IsException(receiver)) {
        return receiver;
    }

    JSValue context = JS_NewObject(ctx);
    if (JS_IsException(context)) {
        JS_FreeValue(ctx, receiver);
        return context;
    }

    std::string error_message;
    if (!SetUint32Property(ctx, receiver, "threadId", event.thread_id, &error_message) ||
        !SetOrUpdateNativePointerProperty(ctx,
                                         receiver,
                                         kNativeHookReturnAddressValueProperty,
                                         event.return_address,
                                         &error_message) ||
        !SetBoolProperty(ctx, receiver, kNativeHookBlockingProperty, blocking, &error_message) ||
        !ResetNativeHookMutationState(ctx, receiver, &error_message)) {
        JS_FreeValue(ctx, context);
        JS_FreeValue(ctx, receiver);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        uint64_t value = 0u;
        switch (descriptor.bit_index) {
            case 0u: value = event.argument_values[0]; break;
            case 1u: value = event.argument_values[1]; break;
            case 2u: value = event.argument_values[2]; break;
            case 3u: value = event.argument_values[3]; break;
            case 4u: value = event.argument_values[4]; break;
            case 5u: value = event.argument_values[5]; break;
            case 6u: value = event.argument_values[6]; break;
            case 7u: value = event.argument_values[7]; break;
            case 8u: value = event.stack_pointer; break;
            case 9u: value = event.frame_pointer; break;
            case 10u: value = event.link_register; break;
            case 11u: value = event.program_counter; break;
            default: value = 0u; break;
        }
        if (!SetOrUpdateNativePointerProperty(ctx,
                                              context,
                                              descriptor.hidden_value_name,
                                              value,
                                              &error_message)) {
            JS_FreeValue(ctx, context);
            JS_FreeValue(ctx, receiver);
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }
    if (JS_SetPropertyStr(ctx, context, kNativeHookContextReceiverProperty, JS_DupValue(ctx, receiver)) < 0) {
        JS_FreeValue(ctx, context);
        JS_FreeValue(ctx, receiver);
        return JS_ThrowInternalError(ctx, "build native hook invocation context failed");
    }
    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        if (!DefineNativeHookPointerAccessor(ctx,
                                             context,
                                             descriptor.public_name,
                                             JsNativeHookContextPointerGetter,
                                             JsNativeHookContextPointerSetter,
                                             static_cast<int>(descriptor.bit_index),
                                             context,
                                             &error_message)) {
            JS_FreeValue(ctx, context);
            JS_FreeValue(ctx, receiver);
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }
    if (!DefineNativeHookPointerAccessor(ctx,
                                         receiver,
                                         "returnAddress",
                                         JsNativeHookReceiverPointerGetter,
                                         JsNativeHookReceiverPointerSetter,
                                         0,
                                         receiver,
                                         &error_message) ||
        JS_SetPropertyStr(ctx, receiver, "context", context) < 0) {
        JS_FreeValue(ctx, context);
        JS_FreeValue(ctx, receiver);
        return JS_ThrowInternalError(ctx, "build native hook invocation context failed");
    }
    context = JS_UNDEFINED;

    return receiver;
}

bool ResetNativeHookInvocationContextObject(JSContext* ctx,
                                            JSValue receiver,
                                            const HookEvent& event,
                                            bool blocking,
                                            std::string* error_message) {
    if (!JS_IsObject(receiver)) {
        SetError(error_message, "native hook invocation receiver is invalid");
        return false;
    }

    JSValue context = JS_GetPropertyStr(ctx, receiver, "context");
    if (JS_IsException(context)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    if (!JS_IsObject(context)) {
        JS_FreeValue(ctx, context);
        SetError(error_message, "native hook receiver context is invalid");
        return false;
    }

    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        uint64_t value = 0u;
        switch (descriptor.bit_index) {
            case 0u: value = event.argument_values[0]; break;
            case 1u: value = event.argument_values[1]; break;
            case 2u: value = event.argument_values[2]; break;
            case 3u: value = event.argument_values[3]; break;
            case 4u: value = event.argument_values[4]; break;
            case 5u: value = event.argument_values[5]; break;
            case 6u: value = event.argument_values[6]; break;
            case 7u: value = event.argument_values[7]; break;
            case 8u: value = event.stack_pointer; break;
            case 9u: value = event.frame_pointer; break;
            case 10u: value = event.link_register; break;
            case 11u: value = event.program_counter; break;
            default: value = 0u; break;
        }
        if (!SetOrUpdateNativePointerProperty(ctx,
                                              context,
                                              descriptor.hidden_value_name,
                                              value,
                                              error_message)) {
            JS_FreeValue(ctx, context);
            return false;
        }
    }

    const bool ok =
        SetUint32Property(ctx, receiver, "threadId", event.thread_id, error_message) &&
        SetOrUpdateNativePointerProperty(ctx,
                                         receiver,
                                         kNativeHookReturnAddressValueProperty,
                                         event.return_address,
                                         error_message) &&
        SetBoolProperty(ctx, receiver, kNativeHookBlockingProperty, blocking, error_message) &&
        ResetNativeHookMutationState(ctx, receiver, error_message);
    JS_FreeValue(ctx, context);
    if (!ok) {
        SetError(error_message, "build native hook invocation context failed");
        return false;
    }
    return true;
}

JSValue BuildNativeHookReturnValue(JSContext* ctx, uint64_t value, JSValueConst receiver) {
    JSValue retval = MakeNativePointer(ctx, value);
    if (JS_IsException(retval)) {
        return retval;
    }

    JSValue func_data[1] = {JS_DupValue(ctx, receiver)};
    JSValue replace = JS_NewCFunctionData(ctx,
                                          JsNativeHookReturnValueReplace,
                                          1,
                                          0,
                                          1,
                                          func_data);
    JS_FreeValue(ctx, func_data[0]);
    if (JS_IsException(replace) || JS_SetPropertyStr(ctx, retval, "replace", replace) < 0) {
        JS_FreeValue(ctx, replace);
        JS_FreeValue(ctx, retval);
        return JS_ThrowInternalError(ctx, "build native hook return value failed");
    }
    return retval;
}

bool ResetNativeHookReturnValue(JSContext* ctx,
                                JSValue retval,
                                uint64_t value,
                                JSValueConst receiver,
                                std::string* error_message) {
    if (!JS_IsObject(retval)) {
        SetError(error_message, "native hook return value wrapper is invalid");
        return false;
    }
    if (!UpdateNativePointerValue(ctx, retval, value, error_message)) {
        return false;
    }

    JSValue func_data[1] = {JS_DupValue(ctx, receiver)};
    JSValue replace = JS_NewCFunctionData(ctx,
                                          JsNativeHookReturnValueReplace,
                                          1,
                                          0,
                                          1,
                                          func_data);
    JS_FreeValue(ctx, func_data[0]);
    if (JS_IsException(replace) || JS_SetPropertyStr(ctx, retval, "replace", replace) < 0) {
        JS_FreeValue(ctx, replace);
        SetError(error_message, "build native hook return value failed");
        return false;
    }
    return true;
}

bool NativeHookInvocationContextIsDirty(JSContext* ctx,
                                        RuntimeState::NativeJsCallbackRecord* callbacks) {
    if (callbacks == nullptr || !JS_IsObject(callbacks->cached_invocation_receiver)) {
        return false;
    }

    JSPropertyEnum* properties = nullptr;
    uint32_t property_count = 0u;
    if (JS_GetOwnPropertyNames(ctx,
                               &properties,
                               &property_count,
                               callbacks->cached_invocation_receiver,
                               JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0) {
        js_free(ctx, properties);
        return true;
    }
    js_free(ctx, properties);
    return property_count != callbacks->cached_invocation_receiver_property_count;
}

JSValue ObtainNativeHookInvocationReceiver(JSContext* ctx,
                                           RuntimeState::NativeJsCallbackRecord* callbacks,
                                           const HookEvent& event,
                                           std::string* error_message) {
    if (callbacks == nullptr) {
        SetError(error_message, "native hook callbacks missing");
        return JS_EXCEPTION;
    }

    JSValue receiver = JS_UNDEFINED;
    bool cached = false;
    if (!callbacks->cached_invocation_receiver_in_use &&
        JS_IsObject(callbacks->cached_invocation_receiver) &&
        !NativeHookInvocationContextIsDirty(ctx, callbacks)) {
        receiver = callbacks->cached_invocation_receiver;
        callbacks->cached_invocation_receiver_in_use = true;
        cached = true;
    } else if (!callbacks->cached_invocation_receiver_in_use &&
               JS_IsObject(callbacks->cached_invocation_receiver) &&
               NativeHookInvocationContextIsDirty(ctx, callbacks)) {
        JS_FreeValue(ctx, callbacks->cached_invocation_receiver);
        callbacks->cached_invocation_receiver = JS_UNDEFINED;
        callbacks->cached_invocation_receiver_property_count = 0u;
    }

    if (!cached) {
        receiver = BuildNativeHookInvocationContextObject(ctx, event, callbacks->blocking);
        if (JS_IsException(receiver)) {
            SetError(error_message, GetExceptionString(ctx));
            return receiver;
        }

        if (JS_IsUndefined(callbacks->cached_invocation_receiver)) {
            callbacks->cached_invocation_receiver = JS_DupValue(ctx, receiver);
            callbacks->cached_invocation_receiver_in_use = true;
            JSPropertyEnum* properties = nullptr;
            uint32_t property_count = 0u;
            if (JS_GetOwnPropertyNames(ctx,
                                       &properties,
                                       &property_count,
                                       callbacks->cached_invocation_receiver,
                                       JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0) {
                js_free(ctx, properties);
                callbacks->cached_invocation_receiver_property_count = 0u;
            } else {
                callbacks->cached_invocation_receiver_property_count = property_count;
                js_free(ctx, properties);
            }
        }
        return receiver;
    }

    if (!ResetNativeHookInvocationContextObject(ctx,
                                                receiver,
                                                event,
                                                callbacks->blocking,
                                                error_message)) {
        callbacks->cached_invocation_receiver_in_use = false;
        return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, receiver);
}

void ReleaseNativeHookInvocationReceiver(JSContext* ctx,
                                         RuntimeState::NativeJsCallbackRecord* callbacks,
                                         JSValue receiver) {
    if (callbacks != nullptr &&
        JS_IsObject(callbacks->cached_invocation_receiver) &&
        JS_VALUE_GET_TAG(receiver) == JS_VALUE_GET_TAG(callbacks->cached_invocation_receiver) &&
        JS_VALUE_GET_PTR(receiver) == JS_VALUE_GET_PTR(callbacks->cached_invocation_receiver)) {
        JS_FreeValue(ctx, receiver);
        callbacks->cached_invocation_receiver_in_use = false;
        return;
    }
    JS_FreeValue(ctx, receiver);
}

JSValue ObtainNativeHookArgsArray(JSContext* ctx,
                                  RuntimeState::NativeJsCallbackRecord* callbacks,
                                  const HookEvent& event,
                                  JSValueConst receiver,
                                  std::string* error_message) {
    if (callbacks == nullptr) {
        SetError(error_message, "native hook callbacks missing");
        return JS_EXCEPTION;
    }

    if (!callbacks->cached_invocation_args_in_use &&
        JS_IsObject(callbacks->cached_invocation_args) &&
        ResetNativeHookArgsArray(ctx,
                                 callbacks->cached_invocation_args,
                                 event,
                                 receiver,
                                 error_message)) {
        callbacks->cached_invocation_args_in_use = true;
        return JS_DupValue(ctx, callbacks->cached_invocation_args);
    }

    JSValue args = BuildNativeHookArgsArray(ctx, event, receiver);
    if (JS_IsException(args)) {
        SetError(error_message, GetExceptionString(ctx));
        return args;
    }

    if (JS_IsUndefined(callbacks->cached_invocation_args)) {
        callbacks->cached_invocation_args = JS_DupValue(ctx, args);
        callbacks->cached_invocation_args_in_use = true;
    }

    return args;
}

void ReleaseNativeHookArgsArray(JSContext* ctx,
                                RuntimeState::NativeJsCallbackRecord* callbacks,
                                JSValue args) {
    if (callbacks != nullptr &&
        JS_IsObject(callbacks->cached_invocation_args) &&
        JS_VALUE_GET_TAG(args) == JS_VALUE_GET_TAG(callbacks->cached_invocation_args) &&
        JS_VALUE_GET_PTR(args) == JS_VALUE_GET_PTR(callbacks->cached_invocation_args)) {
        JS_FreeValue(ctx, args);
        callbacks->cached_invocation_args_in_use = false;
        return;
    }
    JS_FreeValue(ctx, args);
}

JSValue ObtainNativeHookReturnValue(JSContext* ctx,
                                    RuntimeState::NativeJsCallbackRecord* callbacks,
                                    uint64_t value,
                                    JSValueConst receiver,
                                    std::string* error_message) {
    if (callbacks == nullptr) {
        SetError(error_message, "native hook callbacks missing");
        return JS_EXCEPTION;
    }

    if (!callbacks->cached_invocation_retval_in_use &&
        JS_IsObject(callbacks->cached_invocation_retval) &&
        ResetNativeHookReturnValue(ctx,
                                   callbacks->cached_invocation_retval,
                                   value,
                                   receiver,
                                   error_message)) {
        callbacks->cached_invocation_retval_in_use = true;
        return JS_DupValue(ctx, callbacks->cached_invocation_retval);
    }

    JSValue retval = BuildNativeHookReturnValue(ctx, value, receiver);
    if (JS_IsException(retval)) {
        SetError(error_message, GetExceptionString(ctx));
        return retval;
    }

    if (JS_IsUndefined(callbacks->cached_invocation_retval)) {
        callbacks->cached_invocation_retval = JS_DupValue(ctx, retval);
        callbacks->cached_invocation_retval_in_use = true;
    }

    return retval;
}

void ReleaseNativeHookReturnValue(JSContext* ctx,
                                  RuntimeState::NativeJsCallbackRecord* callbacks,
                                  JSValue retval) {
    if (callbacks != nullptr &&
        JS_IsObject(callbacks->cached_invocation_retval) &&
        JS_VALUE_GET_TAG(retval) == JS_VALUE_GET_TAG(callbacks->cached_invocation_retval) &&
        JS_VALUE_GET_PTR(retval) == JS_VALUE_GET_PTR(callbacks->cached_invocation_retval)) {
        JS_FreeValue(ctx, retval);
        callbacks->cached_invocation_retval_in_use = false;
        return;
    }
    JS_FreeValue(ctx, retval);
}

bool SetNativeHookContextPointerProperty(JSContext* ctx,
                                         JSValue context,
                                         const char* name,
                                         uint64_t value,
                                         std::string* error_message) {
    const auto* descriptor = FindNativeHookContextPointerDescriptorByName(name);
    if (descriptor == nullptr) {
        SetError(error_message, "native hook context property is unknown");
        return false;
    }
    return SetOrUpdateNativePointerProperty(ctx,
                                            context,
                                            descriptor->hidden_value_name,
                                            value,
                                            error_message);
}

bool RefreshNativeHookInvocationReceiverForLeave(JSContext* ctx,
                                                 JSValue receiver,
                                                 const HookEvent& event,
                                                 std::string* error_message) {
    JSValue context = JS_GetPropertyStr(ctx, receiver, "context");
    if (JS_IsException(context)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    if (!JS_IsObject(context)) {
        JS_FreeValue(ctx, context);
        SetError(error_message, "native hook receiver context is invalid");
        return false;
    }

    const bool ok =
        SetUint32Property(ctx, receiver, "threadId", event.thread_id, error_message) &&
        SetOrUpdateNativePointerProperty(ctx,
                                         receiver,
                                         kNativeHookReturnAddressValueProperty,
                                         event.return_address,
                                         error_message) &&
        SetNativeHookContextPointerProperty(ctx, context, "x0", event.return_value, error_message) &&
        SetNativeHookContextPointerProperty(ctx, context, "sp", event.stack_pointer, error_message) &&
        SetNativeHookContextPointerProperty(ctx, context, "fp", event.frame_pointer, error_message) &&
        SetNativeHookContextPointerProperty(ctx, context, "lr", event.link_register, error_message) &&
        SetNativeHookContextPointerProperty(ctx, context, "pc", event.program_counter, error_message) &&
        ResetNativeHookMutationState(ctx, receiver, error_message);
    JS_FreeValue(ctx, context);
    if (!ok) {
        if (error_message != nullptr && error_message->empty()) {
            SetError(error_message, "update native hook receiver failed");
        }
        return false;
    }
    return true;
}

bool CaptureNativeHookInvocationMutations(JSContext* ctx,
                                          const HookEvent& event,
                                          JSValueConst receiver,
                                          JSValueConst callback_argument,
                                          HookInvocationMutationResult* result_out,
                                          std::string* error_message) {
    if (result_out == nullptr) {
        SetError(error_message, "result_out is null");
        return false;
    }
    *result_out = HookInvocationMutationResult{};

    JSValue context = JS_GetPropertyStr(ctx, receiver, "context");
    if (JS_IsException(context)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    if (!JS_IsObject(context)) {
        JS_FreeValue(ctx, context);
        SetError(error_message, "native hook receiver context is invalid");
        return false;
    }
    uint32_t argument_mask = 0u;
    uint32_t context_mask = 0u;
    bool return_value_dirty = false;
    bool return_address_dirty = false;
    if (!GetUint32Property(ctx,
                           receiver,
                           kNativeHookMutationArgumentMaskProperty,
                           &argument_mask,
                           error_message) ||
        !GetUint32Property(ctx,
                           receiver,
                           kNativeHookMutationContextMaskProperty,
                           &context_mask,
                           error_message) ||
        !GetBoolProperty(ctx,
                         receiver,
                         kNativeHookMutationReturnValueDirtyProperty,
                         &return_value_dirty,
                         error_message) ||
        !GetBoolProperty(ctx,
                         receiver,
                         kNativeHookMutationReturnAddressDirtyProperty,
                         &return_address_dirty,
                         error_message)) {
        JS_FreeValue(ctx, context);
        return false;
    }
    if (argument_mask == 0u &&
        context_mask == 0u &&
        !return_value_dirty &&
        !return_address_dirty) {
        JS_FreeValue(ctx, context);
        return true;
    }

    for (uint32_t index = 0u; index < event.argument_count && index < 8u; ++index) {
        if ((argument_mask & (1u << index)) != 0u && JS_IsArray(ctx, callback_argument)) {
            JSValue arg_value = JS_GetPropertyUint32(ctx, callback_argument, index);
            if (JS_IsException(arg_value)) {
                JS_FreeValue(ctx, context);
                SetError(error_message, GetExceptionString(ctx));
                return false;
            }
            JSValue replacement = JS_GetPropertyStr(ctx, arg_value, kNativeHookArgumentReplacementProperty);
            if (JS_IsException(replacement)) {
                JS_FreeValue(ctx, arg_value);
                JS_FreeValue(ctx, context);
                SetError(error_message, GetExceptionString(ctx));
                return false;
            }
            uint64_t pointer_value = 0u;
            const bool parsed = !JS_IsUndefined(replacement) &&
                                ParsePointerValue(ctx, replacement, &pointer_value);
            JS_FreeValue(ctx, replacement);
            JS_FreeValue(ctx, arg_value);
            if (!parsed) {
                JS_FreeValue(ctx, context);
                SetError(error_message, "native hook argument replacement must be a pointer value");
                return false;
            }
            result_out->argument_overrides[index] = true;
            result_out->argument_values[index] = pointer_value;
        }
    }

    for (const auto& descriptor : kNativeHookContextPointerDescriptors) {
        if ((context_mask & (1u << descriptor.bit_index)) == 0u) {
            continue;
        }
        JSValue value = JS_GetPropertyStr(ctx, context, descriptor.hidden_value_name);
        if (JS_IsException(value)) {
            JS_FreeValue(ctx, context);
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
        uint64_t pointer_value = 0u;
        const bool parsed = ParsePointerValue(ctx, value, &pointer_value);
        JS_FreeValue(ctx, value);
        if (!parsed) {
            JS_FreeValue(ctx, context);
            SetError(error_message, "native hook context pointer must be a pointer value");
            return false;
        }
        if (event.phase == HookEventPhase::kEnter && descriptor.bit_index < 8u) {
            result_out->argument_overrides[descriptor.bit_index] = true;
            result_out->argument_values[descriptor.bit_index] = pointer_value;
        } else if (event.phase == HookEventPhase::kLeave && descriptor.bit_index == 0u) {
            result_out->has_return_value_override = true;
            result_out->return_value = pointer_value;
        }
    }
    JS_FreeValue(ctx, context);

    if (event.phase == HookEventPhase::kLeave && return_value_dirty) {
        JSValue replacement = JS_GetPropertyStr(ctx,
                                                callback_argument,
                                                kNativeHookReturnValueReplacementProperty);
        if (JS_IsException(replacement)) {
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
        if (!JS_IsUndefined(replacement)) {
            uint64_t pointer_value = 0u;
            const bool parsed = ParsePointerValue(ctx, replacement, &pointer_value);
            JS_FreeValue(ctx, replacement);
            if (!parsed) {
                SetError(error_message, "native hook return replacement must be a pointer value");
                return false;
            }
            result_out->has_return_value_override = true;
            result_out->return_value = pointer_value;
        } else {
            JS_FreeValue(ctx, replacement);
        }
    }

    return true;
}

bool SendJsonToHost(JSContext* ctx,
                    JSValueConst value,
                    const char* error_prefix,
                    const std::vector<uint8_t>& data = {});
JSValue JsNativeHookListenerDetach(JSContext* ctx,
                                   JSValueConst this_val,
                                   int argc,
                                   JSValueConst* argv,
                                   int magic,
                                   JSValue* func_data);

void RegisterOwnedAllocation(void* allocation) {
    if (allocation == nullptr) {
        return;
    }
    std::string error_message;
    RuntimeState& state = GetRuntimeState();
    state.owned_allocations[state.current_script_id].push_back(allocation);
}

void FreeOwnedAllocationsLocked(RuntimeState& state, uint32_t script_id) {
    auto it = state.owned_allocations.find(script_id);
    if (it == state.owned_allocations.end()) {
        return;
    }
    for (void* allocation : it->second) {
        std::free(allocation);
    }
    state.owned_allocations.erase(it);
}

void EnqueueWeakBindingLocked(RuntimeState& state, uint64_t binding_id) {
    auto it = state.weak_bindings.find(binding_id);
    if (it == state.weak_bindings.end() || it->second.fired || it->second.enqueued) {
        return;
    }
    it->second.enqueued = true;
    state.pending_weak_binding_ids.push_back(binding_id);
}

void RefreshQuickJsStackTopForCurrentThread(JSRuntime* runtime) {
    if (runtime == nullptr) {
        return;
    }
    JS_UpdateStackTop(runtime);
}

bool ExecutePendingJobsLocked(RuntimeState& state, std::string* error_message) {
    if (state.runtime == nullptr) {
        return true;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    for (;;) {
        JSContext* job_ctx = nullptr;
        const int status = JS_ExecutePendingJob(state.runtime, &job_ctx);
        if (status > 0) {
            continue;
        }
        if (status == 0) {
            return true;
        }
        SetError(error_message, GetExceptionString(job_ctx != nullptr ? job_ctx : state.context));
        return false;
    }
}

void FreeTimerRecordLocked(JSContext* ctx, RuntimeState::TimerRecord* record) {
    if (record == nullptr || ctx == nullptr) {
        return;
    }
    JS_FreeValue(ctx, record->callback);
    record->callback = JS_UNDEFINED;
    for (JSValue& arg : record->args) {
        JS_FreeValue(ctx, arg);
    }
    record->args.clear();
}

void FreeTimersForScriptLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    for (auto it = state.timers.begin(); it != state.timers.end();) {
        if (it->second.script_id == script_id) {
            FreeTimerRecordLocked(ctx, &it->second);
            it = state.timers.erase(it);
        } else {
            ++it;
        }
    }
    state.timer_cv.notify_all();
}

void FreeAllTimersLocked(JSContext* ctx, RuntimeState& state) {
    for (auto& entry : state.timers) {
        FreeTimerRecordLocked(ctx, &entry.second);
    }
    state.timers.clear();
    state.timer_cv.notify_all();
}

bool EmitLogMessageLocked(JSContext* ctx,
                          const char* level,
                          const std::string& text) {
    JSValue payload = JS_NewObject(ctx);
    if (JS_IsException(payload)) {
        JS_FreeValue(ctx, payload);
        return false;
    }
    if (JS_SetPropertyStr(ctx, payload, "type", JS_NewString(ctx, "log")) < 0 ||
        JS_SetPropertyStr(ctx, payload, "level", JS_NewString(ctx, level)) < 0 ||
        JS_SetPropertyStr(ctx, payload, "payload", JS_NewString(ctx, text.c_str())) < 0) {
        JS_FreeValue(ctx, payload);
        return false;
    }
    const bool ok = SendJsonToHost(ctx, payload, "log");
    JS_FreeValue(ctx, payload);
    return ok;
}

bool DispatchSingleTimerLocked(RuntimeState& state,
                               uint32_t timer_id,
                               std::string* error_message) {
    if (state.context == nullptr) {
        return true;
    }

    auto it = state.timers.find(timer_id);
    if (it == state.timers.end() || it->second.canceled) {
        return true;
    }

    RuntimeState::TimerRecord& record = it->second;
    const uint32_t script_id = record.script_id;
    JSValue callback = JS_DupValue(state.context, record.callback);
    std::vector<JSValue> argv;
    argv.reserve(record.args.size());
    for (JSValue arg : record.args) {
        argv.push_back(JS_DupValue(state.context, arg));
    }
    const bool repeat = record.repeat;
    const uint32_t delay_ms = record.delay_ms;

    if (repeat && !record.canceled) {
        record.due_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    } else {
        FreeTimerRecordLocked(state.context, &record);
        state.timers.erase(it);
    }

    JSValue global = JS_GetGlobalObject(state.context);
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context,
                             callback,
                             global,
                             static_cast<int>(argv.size()),
                             argv.empty() ? nullptr : argv.data());
    JS_FreeValue(state.context, global);
    JS_FreeValue(state.context, callback);
    for (JSValue& arg : argv) {
        JS_FreeValue(state.context, arg);
    }

    if (JS_IsException(result)) {
        const std::string exception_text = GetExceptionString(state.context);
        JS_FreeValue(state.context, result);
        if (repeat) {
            auto repeat_it = state.timers.find(timer_id);
            if (repeat_it != state.timers.end()) {
                FreeTimerRecordLocked(state.context, &repeat_it->second);
                state.timers.erase(repeat_it);
            }
        }
        (void)EmitLogMessageLocked(state.context,
                                   "error",
                                   std::string("timer callback exception: ") + exception_text);
        SetError(error_message, exception_text);
        return false;
    }

    JS_FreeValue(state.context, result);
    if (!ExecutePendingJobsLocked(state, error_message)) {
        return false;
    }
    return true;
}

bool DrainDueTimersLocked(RuntimeState& state, std::string* error_message) {
    for (;;) {
        uint32_t next_timer_id = 0u;
        auto now = std::chrono::steady_clock::now();
        for (const auto& entry : state.timers) {
            const RuntimeState::TimerRecord& record = entry.second;
            if (record.canceled) {
                next_timer_id = entry.first;
                break;
            }
            if (record.due_at <= now) {
                next_timer_id = entry.first;
                break;
            }
        }
        if (next_timer_id == 0u) {
            return true;
        }

        auto it = state.timers.find(next_timer_id);
        if (it != state.timers.end() && it->second.canceled) {
            FreeTimerRecordLocked(state.context, &it->second);
            state.timers.erase(it);
            continue;
        }

        if (!DispatchSingleTimerLocked(state, next_timer_id, error_message)) {
            return false;
        }
    }
}

bool DrainWeakBindingMaintenanceLocked(RuntimeState& state, std::string* error_message) {
    for (;;) {
        if (!ExecutePendingJobsLocked(state, error_message)) {
            return false;
        }
        if (!DrainDueTimersLocked(state, error_message)) {
            return false;
        }
        if (state.pending_weak_binding_ids.empty()) {
            return true;
        }
        DrainPendingWeakBindingsLocked(state);
    }
}

JSValue GetOrCreateWeakBindingRegistryLocked(JSContext* ctx, std::string* error_message) {
    JSValue global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global)) {
        return global;
    }

    JSValue registry = JS_GetPropertyStr(ctx, global, kWeakBindingRegistryProperty);
    if (JS_IsException(registry)) {
        JS_FreeValue(ctx, global);
        return registry;
    }
    if (!JS_IsUndefined(registry)) {
        JS_FreeValue(ctx, global);
        return registry;
    }
    JS_FreeValue(ctx, registry);

    JSValue constructor = JS_GetPropertyStr(ctx, global, "FinalizationRegistry");
    if (JS_IsException(constructor)) {
        JS_FreeValue(ctx, global);
        return constructor;
    }
    if (!JS_IsFunction(ctx, constructor)) {
        JS_FreeValue(ctx, constructor);
        JS_FreeValue(ctx, global);
        SetError(error_message, "FinalizationRegistry is unavailable");
        return JS_EXCEPTION;
    }

    JSValue callback = JS_NewCFunction(ctx,
                                       JsScriptOnWeakBindingCollected,
                                       "__nookOnWeakBindingCollected",
                                       1);
    if (JS_IsException(callback)) {
        JS_FreeValue(ctx, constructor);
        JS_FreeValue(ctx, global);
        return callback;
    }

    JSValue argv[1] = {callback};
    registry = JS_CallConstructor(ctx, constructor, 1, argv);
    JS_FreeValue(ctx, callback);
    JS_FreeValue(ctx, constructor);
    if (JS_IsException(registry)) {
        JS_FreeValue(ctx, global);
        return registry;
    }

    if (JS_SetPropertyStr(ctx,
                          global,
                          kWeakBindingRegistryProperty,
                          JS_DupValue(ctx, registry)) < 0) {
        JS_FreeValue(ctx, registry);
        JS_FreeValue(ctx, global);
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, global);
    return registry;
}

bool UnregisterWeakBindingLocked(JSContext* ctx,
                                 RuntimeState& state,
                                 const RuntimeState::WeakBindingRecord& record) {
    (void)state;
    if (ctx == nullptr || JS_IsUndefined(record.unregister_token)) {
        return true;
    }

    JSValue registry = GetOrCreateWeakBindingRegistryLocked(ctx, nullptr);
    if (JS_IsException(registry)) {
        JS_FreeValue(ctx, registry);
        return false;
    }

    JSValue method = JS_GetPropertyStr(ctx, registry, "unregister");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, registry);
        return false;
    }

    JSValue argv[1] = {JS_DupValue(ctx, record.unregister_token)};
    JSValue result = JS_Call(ctx, method, registry, 1, argv);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, registry);
    if (JS_IsException(result)) {
        return false;
    }
    JS_FreeValue(ctx, result);
    return true;
}

void FreeWeakBindingRecordLocked(JSContext* ctx, RuntimeState& state, uint64_t binding_id) {
    auto it = state.weak_bindings.find(binding_id);
    if (it == state.weak_bindings.end()) {
        return;
    }
    if (ctx != nullptr) {
        (void)UnregisterWeakBindingLocked(ctx, state, it->second);
        JS_FreeValue(ctx, it->second.callback);
        JS_FreeValue(ctx, it->second.unregister_token);
    }
    state.weak_bindings.erase(it);
}

void FreeWeakBindingsForScriptLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    for (auto it = state.weak_bindings.begin(); it != state.weak_bindings.end();) {
        if (it->second.script_id == script_id) {
            if (ctx != nullptr) {
                (void)UnregisterWeakBindingLocked(ctx, state, it->second);
                JS_FreeValue(ctx, it->second.callback);
                JS_FreeValue(ctx, it->second.unregister_token);
            }
            it = state.weak_bindings.erase(it);
        } else {
            ++it;
        }
    }
    state.pending_weak_binding_ids.erase(
        std::remove_if(state.pending_weak_binding_ids.begin(),
                       state.pending_weak_binding_ids.end(),
                       [&](uint64_t binding_id) {
                           auto it = state.weak_bindings.find(binding_id);
                           return it == state.weak_bindings.end() || it->second.script_id == script_id;
                       }),
        state.pending_weak_binding_ids.end());
}

void DispatchWeakBindingCallbackLocked(RuntimeState& state, uint64_t binding_id) {
    if (state.context == nullptr) {
        FreeWeakBindingRecordLocked(nullptr, state, binding_id);
        return;
    }

    auto it = state.weak_bindings.find(binding_id);
    if (it == state.weak_bindings.end()) {
        return;
    }
    if (it->second.fired) {
        return;
    }
    it->second.fired = true;

    const uint32_t script_id = it->second.script_id;
    JSValue callback = JS_DupValue(state.context, it->second.callback);
    JS_FreeValue(state.context, it->second.callback);
    JS_FreeValue(state.context, it->second.unregister_token);
    state.weak_bindings.erase(it);

    JSValue global = JS_GetGlobalObject(state.context);
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context, callback, global, 0, nullptr);
    JS_FreeValue(state.context, callback);
    JS_FreeValue(state.context, global);
    if (!JS_IsException(result)) {
        JS_FreeValue(state.context, result);
        return;
    }
    JS_FreeValue(state.context, result);
    (void)JS_GetException(state.context);
}

void DrainPendingWeakBindingsLocked(RuntimeState& state) {
    while (!state.pending_weak_binding_ids.empty()) {
        const uint64_t binding_id = state.pending_weak_binding_ids.back();
        state.pending_weak_binding_ids.pop_back();
        auto it = state.weak_bindings.find(binding_id);
        if (it == state.weak_bindings.end()) {
            continue;
        }
        it->second.enqueued = false;
        DispatchWeakBindingCallbackLocked(state, binding_id);
    }
}

void FireWeakBindingsForScriptLocked(RuntimeState& state, uint32_t script_id) {
    std::vector<uint64_t> binding_ids;
    for (const auto& entry : state.weak_bindings) {
        if (entry.second.script_id == script_id && !entry.second.fired) {
            binding_ids.push_back(entry.first);
        }
    }
    for (uint64_t binding_id : binding_ids) {
        auto it = state.weak_bindings.find(binding_id);
        if (it == state.weak_bindings.end()) {
            continue;
        }
        DispatchWeakBindingCallbackLocked(state, binding_id);
    }
}

JSValue JsScriptOnWeakBindingCollected(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }

    uint32_t binding_id32 = 0u;
    if (JS_ToUint32(ctx, &binding_id32, argv[0]) < 0 || binding_id32 == 0u) {
        return JS_UNDEFINED;
    }

    RuntimeState& state = GetRuntimeState();
    auto it = state.weak_bindings.find(binding_id32);
    if (it == state.weak_bindings.end() || it->second.fired) {
        return JS_UNDEFINED;
    }
    DispatchWeakBindingCallbackLocked(state, binding_id32);
    return JS_UNDEFINED;
}

void RegisterOwnedJavaHandleLocked(RuntimeState& state, uint64_t object_handle) {
    if (object_handle == 0u) {
        return;
    }
    state.owned_java_handles[state.current_script_id].insert(object_handle);
}

void UnregisterOwnedJavaHandleLocked(RuntimeState& state, uint64_t object_handle) {
    if (object_handle == 0u) {
        return;
    }
    for (auto it = state.owned_java_handles.begin(); it != state.owned_java_handles.end();) {
        it->second.erase(object_handle);
        if (it->second.empty()) {
            it = state.owned_java_handles.erase(it);
        } else {
            ++it;
        }
    }
}

bool FreeOwnedJavaHandlesLocked(RuntimeState& state,
                                uint32_t script_id,
                                std::string* error_message) {
    auto it = state.owned_java_handles.find(script_id);
    if (it == state.owned_java_handles.end()) {
        return true;
    }

    std::vector<uint64_t> handles(it->second.begin(), it->second.end());
    state.owned_java_handles.erase(it);
    NOOK_JS_RUNTIME_LOGI("java auto cleanup begin script_id=%u count=%u",
                         script_id,
                         static_cast<unsigned>(handles.size()));
    for (uint64_t object_handle : handles) {
        std::string release_error;
        if (!ReleaseJavaObject(object_handle,
                               state.java_hook_installer_dependencies,
                               &release_error)) {
            NOOK_JS_RUNTIME_LOGI("java auto cleanup failed script_id=%u handle=%s error=%s",
                                 script_id,
                                 FormatHookValue(object_handle).c_str(),
                                 release_error.c_str());
            SetError(error_message, release_error);
            return false;
        }
        NOOK_JS_RUNTIME_LOGI("java auto cleanup released script_id=%u handle=%s",
                             script_id,
                             FormatHookValue(object_handle).c_str());
    }
    return true;
}

JSValue JsNativePointerRead(JSContext* ctx,
                            JSValueConst this_val,
                            int argc,
                            JSValueConst* argv,
                            int magic,
                            JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;

    uint64_t pointer_value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &pointer_value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (pointer_value == 0) {
        return JS_ThrowTypeError(ctx, "read on NULL pointer");
    }

    size_t width = 0;
    switch (magic) {
        case 0:
            width = sizeof(uintptr_t);
            break;
        case 1:
        case 5:
            width = sizeof(uint8_t);
            break;
        case 2:
        case 6:
            width = sizeof(uint16_t);
            break;
        case 3:
        case 7:
            width = sizeof(uint32_t);
            break;
        case 4:
        case 8:
            width = sizeof(uint64_t);
            break;
        case 9:
            width = sizeof(float);
            break;
        case 10:
            width = sizeof(double);
            break;
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer read width");
    }

    if (!IsReadableMemoryRange(static_cast<uintptr_t>(pointer_value), width)) {
        return JS_ThrowTypeError(ctx, "read unreadable pointer");
    }

    const uint8_t* address =
        reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(pointer_value));
    switch (magic) {
        case 0: {
            uintptr_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return MakeNativePointer(ctx, static_cast<uint64_t>(value));
        }
        case 1: {
            uint8_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewUint32(ctx, static_cast<uint32_t>(value));
        }
        case 5: {
            int8_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewInt32(ctx, static_cast<int32_t>(value));
        }
        case 2: {
            uint16_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewUint32(ctx, static_cast<uint32_t>(value));
        }
        case 6: {
            int16_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewInt32(ctx, static_cast<int32_t>(value));
        }
        case 3: {
            uint32_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewUint32(ctx, value);
        }
        case 7: {
            int32_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewInt32(ctx, value);
        }
        case 4: {
            uint64_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return MakeInteger64Object(ctx, value, false);
        }
        case 8: {
            int64_t value = 0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return MakeInteger64Object(ctx, static_cast<uint64_t>(value), true);
        }
        case 9: {
            float value = 0.0f;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewFloat64(ctx, static_cast<double>(value));
        }
        case 10: {
            double value = 0.0;
            if (!TryReadMemoryBytesSafely(pointer_value, sizeof(value), &value)) {
                return JS_ThrowTypeError(ctx, "NativePointer read unreadable pointer");
            }
            return JS_NewFloat64(ctx, value);
        }
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer read width");
    }
}

JSValue JsNativePointerWrite(JSContext* ctx,
                             JSValueConst this_val,
                             int argc,
                             JSValueConst* argv,
                             int magic,
                             JSValue* func_data) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "NativePointer.write requires a value");
    }

    uint64_t pointer_value = 0;
    if (!ResolveNativePointerValue(ctx, this_val, func_data, &pointer_value)) {
        return JS_ThrowInternalError(ctx, "invalid NativePointer value");
    }
    if (pointer_value == 0) {
        return JS_ThrowTypeError(ctx, "write on NULL pointer");
    }

    size_t width = 0;
    switch (magic) {
        case 0:
            width = sizeof(uintptr_t);
            break;
        case 1:
        case 5:
            width = sizeof(uint8_t);
            break;
        case 2:
        case 6:
            width = sizeof(uint16_t);
            break;
        case 3:
        case 7:
            width = sizeof(uint32_t);
            break;
        case 4:
        case 8:
            width = sizeof(uint64_t);
            break;
        case 9:
            width = sizeof(float);
            break;
        case 10:
            width = sizeof(double);
            break;
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer write width");
    }

    if (!IsWritableMemoryRange(static_cast<uintptr_t>(pointer_value), width)) {
        return JS_ThrowTypeError(ctx, "write unwritable pointer");
    }

    uint64_t input_value = 0;
    if (magic == 4 || magic == 8) {
        bool input_is_signed = false;
        if (!ParseInteger64Value(ctx, argv[0], &input_value, &input_is_signed)) {
            return JS_ThrowTypeError(ctx,
                                     magic == 4
                                         ? "writeU64 value must be a number, string, or Int64/UInt64"
                                         : "writeS64 value must be a number, string, or Int64/UInt64");
        }
    } else if (magic == 9 || magic == 10) {
        double floating_value = 0.0;
        if (JS_ToFloat64(ctx, &floating_value, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     magic == 9 ? "writeFloat value must be a number"
                                                : "writeDouble value must be a number");
        }

        uint8_t* address = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(pointer_value));
        if (magic == 9) {
            const float value = static_cast<float>(floating_value);
            std::memcpy(address, &value, sizeof(value));
        } else {
            const double value = floating_value;
            std::memcpy(address, &value, sizeof(value));
        }
        return MakeNativePointer(ctx, pointer_value);
    } else {
        int32_t signed_value = 0;
        if (magic >= 5 && magic <= 7) {
            if (JS_ToInt32(ctx, &signed_value, argv[0]) < 0) {
                return JS_ThrowTypeError(ctx,
                                         magic == 5 ? "writeS8 value must be a number"
                                                    : (magic == 6 ? "writeS16 value must be a number"
                                                                  : "writeS32 value must be a number"));
            }
            input_value = static_cast<uint64_t>(static_cast<int64_t>(signed_value));
        } else if (!ParsePointerValue(ctx, argv[0], &input_value)) {
            return JS_ThrowTypeError(ctx, "write value must be a number or pointer value");
        }
    }

    uint8_t* address = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(pointer_value));
    switch (magic) {
        case 0: {
            const uintptr_t value = static_cast<uintptr_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 1: {
            const uint8_t value = static_cast<uint8_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 2: {
            const uint16_t value = static_cast<uint16_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 3: {
            const uint32_t value = static_cast<uint32_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 4: {
            const uint64_t value = input_value;
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 5: {
            const int8_t value = static_cast<int8_t>(static_cast<int32_t>(input_value));
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 6: {
            const int16_t value = static_cast<int16_t>(static_cast<int32_t>(input_value));
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 7: {
            const int32_t value = static_cast<int32_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        case 8: {
            const int64_t value = static_cast<int64_t>(input_value);
            std::memcpy(address, &value, sizeof(value));
            break;
        }
        default:
            return JS_ThrowInternalError(ctx, "unsupported NativePointer write width");
    }

    return MakeNativePointer(ctx, pointer_value);
}

JSValue JsMemoryAlloc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Memory.alloc requires size");
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "Memory.alloc size must be a number");
    }
    if (size == 0) {
        size = 1;
    }

    void* allocation = std::calloc(1u, static_cast<size_t>(size));
    if (allocation == nullptr) {
        return JS_ThrowOutOfMemory(ctx);
    }

    RegisterOwnedAllocation(allocation);
    return MakeNativePointer(ctx, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(allocation)));
}

JSValue JsMemoryCopy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Memory.copy requires dst, src, and size");
    }

    uint64_t dst = 0;
    if (!ParsePointerValue(ctx, argv[0], &dst)) {
        return JS_ThrowTypeError(ctx, "Memory.copy dst must be a pointer value");
    }

    uint64_t src = 0;
    if (!ParsePointerValue(ctx, argv[1], &src)) {
        return JS_ThrowTypeError(ctx, "Memory.copy src must be a pointer value");
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[2]) < 0) {
        return JS_ThrowTypeError(ctx, "Memory.copy size must be a number");
    }
    if (size == 0) {
        return JS_UNDEFINED;
    }

    if (!IsReadableMemoryRange(static_cast<uintptr_t>(src), size)) {
        return JS_ThrowTypeError(ctx, "Memory.copy source unreadable");
    }
    if (!IsWritableMemoryRange(static_cast<uintptr_t>(dst), size)) {
        return JS_ThrowTypeError(ctx, "Memory.copy destination unwritable");
    }

    std::memmove(reinterpret_cast<void*>(static_cast<uintptr_t>(dst)),
                 reinterpret_cast<const void*>(static_cast<uintptr_t>(src)),
                 static_cast<size_t>(size));
    return JS_UNDEFINED;
}

JSValue JsMemoryDup(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Memory.dup requires address and size");
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "Memory.dup address must be a pointer value");
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx, "Memory.dup size must be a number");
    }
    if (size == 0) {
        return JS_NewArrayBufferCopy(ctx, nullptr, 0);
    }

    if (!IsReadableMemoryRange(static_cast<uintptr_t>(address), size)) {
        return JS_ThrowTypeError(ctx, "Memory.dup unreadable source");
    }

    return JS_NewArrayBufferCopy(
        ctx, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address)), size);
}

JSValue JsMemoryProtect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Memory.protect requires address, size, and protection");
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "Memory.protect address must be a pointer value");
    }
    if (address == 0u) {
        return JS_ThrowTypeError(ctx, "Memory.protect address must be a non-zero pointer value");
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx, "Memory.protect size must be a number");
    }
    if (size == 0u) {
        return JS_ThrowTypeError(ctx, "Memory.protect size must be a positive number");
    }

    const char* protection_cstr = JS_ToCString(ctx, argv[2]);
    if (protection_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Memory.protect protection must be a string");
    }
    const std::string protection = protection_cstr;
    JS_FreeCString(ctx, protection_cstr);

#if defined(_WIN32)
    DWORD native_protection = 0;
#else
    int native_protection = 0;
#endif
    if (!TryParseProtectionString(protection, &native_protection)) {
        return JS_ThrowTypeError(
            ctx, "Memory.protect protection must be exactly 3 characters from r, w, x, and -");
    }

    uintptr_t aligned_start = 0;
    size_t aligned_size = 0;
    if (!ComputePageAlignedProtectionRange(static_cast<uintptr_t>(address),
                                           static_cast<size_t>(size),
                                           &aligned_start,
                                           &aligned_size)) {
        return JS_NewBool(ctx, 0);
    }

#if defined(_WIN32)
    DWORD previous_protection = 0;
    const bool ok = VirtualProtect(reinterpret_cast<void*>(aligned_start),
                                   aligned_size,
                                   native_protection,
                                   &previous_protection) != 0;
#else
    const bool ok = mprotect(reinterpret_cast<void*>(aligned_start), aligned_size, native_protection) == 0;
#endif
    return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsMemoryPatchCode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode requires address, size, and apply");
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode address must be a pointer value");
    }
    if (address == 0u) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode address must be a non-zero pointer value");
    }

    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode size must be a number");
    }
    if (size == 0u) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode size must be a positive number");
    }

    if (!JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode apply must be a function");
    }

    const uintptr_t normalized_address =
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address));
    if (!IsReadableMemoryRange(normalized_address, size)) {
        return JS_ThrowTypeError(ctx, "Memory.patchCode unreadable target");
    }

    void* scratch = std::malloc(static_cast<size_t>(size));
    if (scratch == nullptr) {
        return JS_ThrowOutOfMemory(ctx);
    }
    std::memcpy(scratch,
                reinterpret_cast<const void*>(normalized_address),
                static_cast<size_t>(size));

    JSValue scratch_pointer =
        MakeNativePointer(ctx, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(scratch)));
    JSValue size_value = JS_NewUint32(ctx, size);
    if (JS_IsException(scratch_pointer) || JS_IsException(size_value)) {
        JS_FreeValue(ctx, scratch_pointer);
        JS_FreeValue(ctx, size_value);
        std::free(scratch);
        return JS_EXCEPTION;
    }

    JSValue apply_argv[2] = {scratch_pointer, size_value};
    JSValue apply_result = JS_Call(ctx, argv[2], JS_UNDEFINED, 2, apply_argv);
    JS_FreeValue(ctx, scratch_pointer);
    JS_FreeValue(ctx, size_value);
    if (JS_IsException(apply_result)) {
        std::free(scratch);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, apply_result);

    std::string original_protection;
    if (!TryGetUniformProtectionForRange(normalized_address, size, &original_protection)) {
        std::free(scratch);
        return JS_ThrowInternalError(ctx, "Memory.patchCode failed to resolve target protection");
    }

    std::string writable_protection;
    if (!TryMakeWritableProtectionString(original_protection, &writable_protection)) {
        std::free(scratch);
        return JS_ThrowInternalError(ctx, "Memory.patchCode failed to compute writable protection");
    }

    uintptr_t aligned_start = 0;
    size_t aligned_size = 0;
    if (!ComputePageAlignedProtectionRange(normalized_address,
                                           static_cast<size_t>(size),
                                           &aligned_start,
                                           &aligned_size)) {
        std::free(scratch);
        return JS_ThrowInternalError(ctx, "Memory.patchCode failed to align target range");
    }

#if defined(_WIN32)
    DWORD writable_native_protection = 0;
    DWORD original_native_protection = 0;
#else
    int writable_native_protection = 0;
    int original_native_protection = 0;
#endif
    if (!TryParseProtectionString(writable_protection, &writable_native_protection) ||
        !TryParseProtectionString(original_protection, &original_native_protection)) {
        std::free(scratch);
        return JS_ThrowInternalError(ctx, "Memory.patchCode failed to translate protection");
    }

    bool changed_protection = false;
    if (writable_protection != original_protection) {
#if defined(_WIN32)
        DWORD previous_protection = 0;
        changed_protection = VirtualProtect(reinterpret_cast<void*>(aligned_start),
                                           aligned_size,
                                           writable_native_protection,
                                           &previous_protection) != 0;
#else
        changed_protection =
            mprotect(reinterpret_cast<void*>(aligned_start), aligned_size, writable_native_protection) == 0;
#endif
        if (!changed_protection) {
            std::free(scratch);
            return JS_ThrowInternalError(ctx, "Memory.patchCode failed to make target writable");
        }
    }

    std::memcpy(reinterpret_cast<void*>(normalized_address), scratch, static_cast<size_t>(size));
    FlushInstructionCacheForRange(normalized_address, static_cast<size_t>(size));

    bool restored_protection = true;
    if (writable_protection != original_protection) {
#if defined(_WIN32)
        DWORD ignored_previous_protection = 0;
        restored_protection = VirtualProtect(reinterpret_cast<void*>(aligned_start),
                                             aligned_size,
                                             original_native_protection,
                                             &ignored_previous_protection) != 0;
#else
        restored_protection =
            mprotect(reinterpret_cast<void*>(aligned_start), aligned_size, original_native_protection) == 0;
#endif
    }

    std::free(scratch);
    if (!restored_protection) {
        return JS_ThrowInternalError(ctx, "Memory.patchCode failed to restore target protection");
    }

    return JS_UNDEFINED;
}

JSValue JsMemoryScanSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    uint64_t address = 0;
    uint32_t size = 0;
    std::vector<MemoryScanPatternToken> tokens;
    if (!ParseMemoryScanArguments(ctx, "Memory.scanSync", argc, argv, &address, &size, &tokens)) {
        return JS_EXCEPTION;
    }

    if (!IsReadableMemoryRange(static_cast<uintptr_t>(address), size)) {
        return JS_ThrowTypeError(ctx, "Memory.scanSync unreadable range");
    }

    JSValue matches = JS_NewArray(ctx);
    if (JS_IsException(matches)) {
        return matches;
    }

    std::vector<uint32_t> offsets;
    if (!CollectMemoryScanMatchOffsets(address, size, tokens, &offsets)) {
        JS_FreeValue(ctx, matches);
        return JS_ThrowInternalError(ctx, "Memory.scanSync failed to read range");
    }

    const size_t pattern_size = tokens.size();
    uint32_t match_index = 0;
    for (uint32_t offset : offsets) {
        JSValue match = MakeMemoryScanMatch(ctx,
                                            address + static_cast<uint64_t>(offset),
                                            static_cast<uint32_t>(pattern_size));
        if (JS_IsException(match) || JS_SetPropertyUint32(ctx, matches, match_index++, match) < 0) {
            JS_FreeValue(ctx, match);
            JS_FreeValue(ctx, matches);
            return JS_EXCEPTION;
        }
    }

    return matches;
}

JSValue JsMemoryScan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    uint64_t address = 0;
    uint32_t size = 0;
    std::vector<MemoryScanPatternToken> tokens;
    if (!ParseMemoryScanArguments(ctx, "Memory.scan", argc, argv, &address, &size, &tokens)) {
        return JS_EXCEPTION;
    }
    if (argc < 4) {
        return JS_ThrowTypeError(ctx, "Memory.scan requires callbacks");
    }
    if (!JS_IsObject(argv[3])) {
        return JS_ThrowTypeError(ctx, "Memory.scan callbacks must be an object");
    }

    JSValue on_match = JS_UNDEFINED;
    JSValue on_error = JS_UNDEFINED;
    JSValue on_complete = JS_UNDEFINED;
    JSValue global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global)) {
        return global;
    }

    if (!GetOptionalFunctionProperty(ctx, argv[3], "onMatch", &on_match) ||
        !GetOptionalFunctionProperty(ctx, argv[3], "onError", &on_error) ||
        !GetOptionalFunctionProperty(ctx, argv[3], "onComplete", &on_complete)) {
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_error);
        JS_FreeValue(ctx, on_complete);
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(on_match)) {
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, on_error);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Memory.scan onMatch must be a function");
    }

    if (!IsReadableMemoryRange(static_cast<uintptr_t>(address), size)) {
        if (!JS_IsUndefined(on_error)) {
            JSValue reason = JS_NewString(ctx, "Memory.scan unreadable range");
            JSValue argv_error[1] = {reason};
            JSValue result = JS_Call(ctx, on_error, global, 1, argv_error);
            JS_FreeValue(ctx, reason);
            if (JS_IsException(result)) {
                JS_FreeValue(ctx, global);
                JS_FreeValue(ctx, on_match);
                JS_FreeValue(ctx, on_error);
                JS_FreeValue(ctx, on_complete);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, result);
        }
        if (!JS_IsUndefined(on_complete)) {
            JSValue result = JS_Call(ctx, on_complete, global, 0, nullptr);
            if (JS_IsException(result)) {
                JS_FreeValue(ctx, global);
                JS_FreeValue(ctx, on_match);
                JS_FreeValue(ctx, on_error);
                JS_FreeValue(ctx, on_complete);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, result);
        }
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_error);
        JS_FreeValue(ctx, on_complete);
        return JS_UNDEFINED;
    }

    std::vector<uint32_t> offsets;
    if (!CollectMemoryScanMatchOffsets(address, size, tokens, &offsets)) {
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_error);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "Memory.scan failed to read range");
    }

    const uint32_t pattern_size = static_cast<uint32_t>(tokens.size());
    for (uint32_t offset : offsets) {
        JSValue match_address = MakeNativePointer(ctx, address + static_cast<uint64_t>(offset));
        JSValue match_size = JS_NewUint32(ctx, pattern_size);
        JSValue argv_match[2] = {match_address, match_size};
        JSValue result = JS_Call(ctx, on_match, global, 2, argv_match);
        JS_FreeValue(ctx, match_address);
        JS_FreeValue(ctx, match_size);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_error);
            JS_FreeValue(ctx, on_complete);
            return JS_EXCEPTION;
        }

        bool stop = false;
        if (!JS_IsUndefined(result) && !JS_IsNull(result)) {
            const char* result_cstr = JS_ToCString(ctx, result);
            if (result_cstr != nullptr) {
                stop = std::strcmp(result_cstr, "stop") == 0;
                JS_FreeCString(ctx, result_cstr);
            }
        }
        JS_FreeValue(ctx, result);
        if (stop) {
            break;
        }
    }

    if (!JS_IsUndefined(on_complete)) {
        JSValue result = JS_Call(ctx, on_complete, global, 0, nullptr);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_error);
            JS_FreeValue(ctx, on_complete);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, result);
    }

    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, on_match);
    JS_FreeValue(ctx, on_error);
    JS_FreeValue(ctx, on_complete);
    return JS_UNDEFINED;
}

JSValue JsHexdump(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "hexdump requires a target");
    }

    uint32_t offset = 0;
    bool has_length = false;
    uint32_t length = 0;
    bool header = false;
    bool ansi = false;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue offset_value = JS_GetPropertyStr(ctx, argv[1], "offset");
        if (JS_IsException(offset_value)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(offset_value) && !JS_IsNull(offset_value)) {
            if (JS_ToUint32(ctx, &offset, offset_value) < 0) {
                JS_FreeValue(ctx, offset_value);
                return JS_ThrowTypeError(ctx, "hexdump offset must be a number");
            }
        }
        JS_FreeValue(ctx, offset_value);

        JSValue length_value = JS_GetPropertyStr(ctx, argv[1], "length");
        if (JS_IsException(length_value)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(length_value) && !JS_IsNull(length_value)) {
            if (JS_ToUint32(ctx, &length, length_value) < 0) {
                JS_FreeValue(ctx, length_value);
                return JS_ThrowTypeError(ctx, "hexdump length must be a number");
            }
            has_length = true;
        }
        JS_FreeValue(ctx, length_value);

        JSValue header_value = JS_GetPropertyStr(ctx, argv[1], "header");
        if (JS_IsException(header_value)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(header_value) && !JS_IsNull(header_value)) {
            header = JS_ToBool(ctx, header_value) != 0;
        }
        JS_FreeValue(ctx, header_value);

        JSValue ansi_value = JS_GetPropertyStr(ctx, argv[1], "ansi");
        if (JS_IsException(ansi_value)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(ansi_value) && !JS_IsNull(ansi_value)) {
            ansi = JS_ToBool(ctx, ansi_value) != 0;
        }
        JS_FreeValue(ctx, ansi_value);
    }
    size_t buffer_size = 0;
    uint8_t* buffer_data = JS_GetArrayBuffer(ctx, &buffer_size, argv[0]);
    if (buffer_data != nullptr) {
        if (static_cast<size_t>(offset) > buffer_size) {
            return JS_ThrowTypeError(ctx, "hexdump array buffer range out of bounds");
        }
        size_t actual_length = has_length ? static_cast<size_t>(length) : (buffer_size - offset);
        if (actual_length > (buffer_size - offset)) {
            return JS_ThrowTypeError(ctx, "hexdump array buffer range out of bounds");
        }
        return JS_NewString(
            ctx, FormatHexdumpStyled(buffer_data + offset, actual_length, offset, header, ansi)
                     .c_str());
    }

    uint64_t pointer_value = 0;
    if (ParsePointerValue(ctx, argv[0], &pointer_value)) {
        if (!has_length) {
            return JS_ThrowTypeError(ctx, "hexdump pointer target requires length");
        }

        const uint64_t start = pointer_value + offset;
        if (!IsReadableMemoryRange(static_cast<uintptr_t>(start), length)) {
            return JS_ThrowTypeError(ctx, "hexdump unreadable pointer");
        }

        return JS_NewString(ctx,
                            FormatHexdumpStyled(
                                reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(start)),
                                length,
                                start,
                                header,
                                ansi)
                                .c_str());
    }

    return JS_ThrowTypeError(ctx, "hexdump unsupported target");
}

JSValue JsMemoryAllocUtf8String(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Memory.allocUtf8String requires a string");
    }

    const char* text = JS_ToCString(ctx, argv[0]);
    if (text == nullptr) {
        return JS_ThrowTypeError(ctx, "Memory.allocUtf8String argument must be a string");
    }

    const size_t length = std::strlen(text);
    char* allocation = static_cast<char*>(std::malloc(length + 1u));
    if (allocation == nullptr) {
        JS_FreeCString(ctx, text);
        return JS_ThrowOutOfMemory(ctx);
    }

    std::memcpy(allocation, text, length + 1u);
    JS_FreeCString(ctx, text);
    RegisterOwnedAllocation(allocation);
    return MakeNativePointer(ctx, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(allocation)));
}

JSValue JsMemoryReadUtf8String(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Memory.readUtf8String requires a pointer");
    }

    uint64_t pointer_value = 0;
    if (!ParsePointerValue(ctx, argv[0], &pointer_value)) {
        return JS_ThrowTypeError(ctx, "Memory.readUtf8String requires a pointer");
    }

    JSValue pointer = JS_UNDEFINED;
    if (JS_IsObject(argv[0])) {
        pointer = JS_DupValue(ctx, argv[0]);
    } else {
        pointer = MakeNativePointer(ctx, pointer_value);
        if (JS_IsException(pointer)) {
            return pointer;
        }
    }

    JSValue func_data = JS_DupValue(ctx, pointer);
    JSValue reader = JS_NewCFunctionData(ctx,
                                         JsNativePointerReadUtf8String,
                                         1,
                                         0,
                                         1,
                                         &func_data);
    JS_FreeValue(ctx, func_data);
    if (JS_IsException(reader)) {
        JS_FreeValue(ctx, pointer);
        return reader;
    }

    const int forwarded_argc = argc > 0 ? argc - 1 : 0;
    JSValue result = JS_Call(ctx, reader, pointer, forwarded_argc, argv + 1);
    JS_FreeValue(ctx, reader);
    JS_FreeValue(ctx, pointer);
    return result;
}

JSValue JsMemoryAllocUtf16String(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Memory.allocUtf16String requires a string");
    }

    const char* text = JS_ToCString(ctx, argv[0]);
    if (text == nullptr) {
        return JS_ThrowTypeError(ctx, "Memory.allocUtf16String argument must be a string");
    }

    std::vector<uint16_t> units;
    if (!Utf8ToUtf16(text, std::strlen(text), &units)) {
        JS_FreeCString(ctx, text);
        return JS_ThrowTypeError(ctx, "Memory.allocUtf16String argument must be valid utf8");
    }
    JS_FreeCString(ctx, text);

    units.push_back(0u);
    const size_t byte_length = units.size() * sizeof(uint16_t);
    uint16_t* allocation = static_cast<uint16_t*>(std::malloc(byte_length));
    if (allocation == nullptr) {
        return JS_ThrowOutOfMemory(ctx);
    }

    std::memcpy(allocation, units.data(), byte_length);
    RegisterOwnedAllocation(allocation);
    return MakeNativePointer(ctx, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(allocation)));
}

JSValue JsNookJniReadJStringUtf8(JSContext* ctx,
                                 JSValueConst this_val,
                                 int argc,
                                 JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Nook.Jni.readJStringUtf8 requires env and jstring");
    }

    uint64_t env_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[0], &env_ptr) || env_ptr == 0u) {
        return JS_ThrowTypeError(ctx, "Nook.Jni.readJStringUtf8 env must be a non-zero pointer value");
    }

    uint64_t jstring_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[1], &jstring_ptr) || jstring_ptr == 0u) {
        return JS_ThrowTypeError(ctx, "Nook.Jni.readJStringUtf8 jstring must be a non-zero pointer value");
    }

    std::string text;
    std::string error_message;
    if (!ReadJStringUtf8(env_ptr, jstring_ptr, &text, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewStringLen(ctx, text.c_str(), text.size());
}

void ReportNativeHookCallbackExceptionLocked(JSContext* ctx,
                                             uint32_t hook_id,
                                             HookEventPhase phase,
                                             const std::string& exception_text) {
    JSValue message = JS_NewObject(ctx);
    if (JS_IsException(message)) {
        JS_FreeValue(ctx, message);
        return;
    }

    std::ostringstream payload;
    payload << "native hook "
            << (phase == HookEventPhase::kEnter ? "onEnter" : "onLeave")
            << " callback failed for hook "
            << hook_id
            << ": "
            << exception_text;

    if (JS_SetPropertyStr(ctx, message, "type", JS_NewString(ctx, "log")) < 0 ||
        JS_SetPropertyStr(ctx, message, "level", JS_NewString(ctx, "error")) < 0 ||
        JS_SetPropertyStr(ctx, message, "payload", JS_NewString(ctx, payload.str().c_str())) < 0) {
        JS_FreeValue(ctx, message);
        return;
    }

    const bool sent = SendJsonToHost(ctx, message, "native hook callback error");
    JS_FreeValue(ctx, message);
    if (!sent && JS_HasException(ctx)) {
        JSValue ignored = JS_GetException(ctx);
        JS_FreeValue(ctx, ignored);
    }
}

bool IsObserverModeHookMutation(JSContext* ctx, JSValueConst receiver) {
    if (!JS_IsObject(receiver)) {
        return false;
    }

    JSValue blocking = JS_GetPropertyStr(ctx, receiver, kNativeHookBlockingProperty);
    if (JS_IsException(blocking)) {
        JS_FreeValue(ctx, blocking);
        return false;
    }

    const bool is_observer_mode = JS_IsBool(blocking) && JS_ToBool(ctx, blocking) == 0;
    JS_FreeValue(ctx, blocking);
    return is_observer_mode;
}

void ReportIgnoredNativeHookMutationLocked(JSContext* ctx, const char* kind) {
    if (kind == nullptr) {
        return;
    }

    JSValue message = JS_NewObject(ctx);
    if (JS_IsException(message)) {
        JS_FreeValue(ctx, message);
        return;
    }

    std::ostringstream payload;
    payload << kind << " mutation ignored in observer mode (blocking: false)";
    if (JS_SetPropertyStr(ctx, message, "type", JS_NewString(ctx, "log")) < 0 ||
        JS_SetPropertyStr(ctx, message, "level", JS_NewString(ctx, "warn")) < 0 ||
        JS_SetPropertyStr(ctx, message, "payload", JS_NewString(ctx, payload.str().c_str())) < 0) {
        JS_FreeValue(ctx, message);
        return;
    }

    const bool sent = SendJsonToHost(ctx, message, "native hook mutation ignored");
    JS_FreeValue(ctx, message);
    if (!sent && JS_HasException(ctx)) {
        JSValue ignored = JS_GetException(ctx);
        JS_FreeValue(ctx, ignored);
    }
}

const char* NativeJsHookStatusStateToString(NativeJsHookStatusState state) {
    switch (state) {
        case NativeJsHookStatusState::kPending:
            return "pending";
        case NativeJsHookStatusState::kInstalled:
            return "installed";
        case NativeJsHookStatusState::kFailed:
            return "failed";
        default:
            return "unknown";
    }
}

bool ForwardNativeHookStatusEventToHost(JSContext* ctx,
                                        const NativeJsHookStatusEvent& event,
                                        std::string* error_message) {
    NOOK_JS_RUNTIME_LOGI("native hook status forwarding hook_id=%u state=%s module=%s symbol=%s error=%s",
                         event.hook_id,
                         NativeJsHookStatusStateToString(event.state),
                         event.module_name.c_str(),
                         event.symbol_name.c_str(),
                         event.error_message.empty() ? "" : event.error_message.c_str());
    JSValue message = JS_NewObject(ctx);
    if (JS_IsException(message)) {
        SetError(error_message, GetExceptionString(ctx));
        JS_FreeValue(ctx, message);
        return false;
    }

    const char* state_name = NativeJsHookStatusStateToString(event.state);
    const bool has_error = !event.error_message.empty();
    if (JS_SetPropertyStr(ctx, message, "type", JS_NewString(ctx, "hook-status")) < 0 ||
        JS_SetPropertyStr(ctx, message, "state", JS_NewString(ctx, state_name)) < 0 ||
        JS_SetPropertyStr(ctx, message, "hookId", JS_NewUint32(ctx, event.hook_id)) < 0 ||
        JS_SetPropertyStr(ctx, message, "moduleName", JS_NewString(ctx, event.module_name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, message, "symbolName", JS_NewString(ctx, event.symbol_name.c_str())) < 0 ||
        (has_error &&
         JS_SetPropertyStr(ctx, message, "error", JS_NewString(ctx, event.error_message.c_str())) < 0)) {
        SetError(error_message, GetExceptionString(ctx));
        JS_FreeValue(ctx, message);
        return false;
    }

    const bool sent = SendJsonToHost(ctx, message, "native hook status");
    JS_FreeValue(ctx, message);
    if (!sent) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    return true;
}

bool GetNativeHookCallbacks(JSContext* ctx,
                            JSValueConst options,
                            JSValue* on_enter,
                            JSValue* on_leave) {
    if (on_enter == nullptr || on_leave == nullptr) {
        return false;
    }
    *on_enter = JS_GetPropertyStr(ctx, options, "onEnter");
    if (JS_IsException(*on_enter)) {
        return false;
    }
    if (!JS_IsUndefined(*on_enter) && !JS_IsNull(*on_enter) && !JS_IsFunction(ctx, *on_enter)) {
        JS_FreeValue(ctx, *on_enter);
        *on_enter = JS_UNDEFINED;
        JS_ThrowTypeError(ctx, "attach onEnter must be a function");
        return false;
    }
    if (JS_IsNull(*on_enter)) {
        JS_FreeValue(ctx, *on_enter);
        *on_enter = JS_UNDEFINED;
    }

    *on_leave = JS_GetPropertyStr(ctx, options, "onLeave");
    if (JS_IsException(*on_leave)) {
        JS_FreeValue(ctx, *on_enter);
        *on_enter = JS_UNDEFINED;
        return false;
    }
    if (!JS_IsUndefined(*on_leave) && !JS_IsNull(*on_leave) && !JS_IsFunction(ctx, *on_leave)) {
        JS_FreeValue(ctx, *on_enter);
        JS_FreeValue(ctx, *on_leave);
        *on_enter = JS_UNDEFINED;
        *on_leave = JS_UNDEFINED;
        JS_ThrowTypeError(ctx, "attach onLeave must be a function");
        return false;
    }
    if (JS_IsNull(*on_leave)) {
        JS_FreeValue(ctx, *on_leave);
        *on_leave = JS_UNDEFINED;
    }
    if (!JS_IsFunction(ctx, *on_enter) && !JS_IsFunction(ctx, *on_leave)) {
        JS_FreeValue(ctx, *on_enter);
        JS_FreeValue(ctx, *on_leave);
        *on_enter = JS_UNDEFINED;
        *on_leave = JS_UNDEFINED;
        JS_ThrowTypeError(ctx, "attach requires onEnter or onLeave callback");
        return false;
    }

    return true;
}

bool GetNativeHookBlockingMode(JSContext* ctx, JSValueConst options, bool* blocking_out) {
    if (blocking_out == nullptr) {
        return false;
    }
    *blocking_out = true;

    JSValue blocking = JS_GetPropertyStr(ctx, options, "blocking");
    if (JS_IsException(blocking)) {
        return false;
    }
    if (JS_IsUndefined(blocking) || JS_IsNull(blocking)) {
        JS_FreeValue(ctx, blocking);
        return true;
    }
    if (!JS_IsBool(blocking)) {
        JS_FreeValue(ctx, blocking);
        JS_ThrowTypeError(ctx, "attach blocking must be a boolean");
        return false;
    }

    *blocking_out = JS_ToBool(ctx, blocking) == 1;
    JS_FreeValue(ctx, blocking);
    return true;
}

bool GetNativeHookSnapshots(JSContext* ctx,
                            JSValueConst options,
                            std::vector<NativeJsArgumentSnapshotRequest>* snapshots_out) {
    if (snapshots_out == nullptr) {
        return false;
    }
    snapshots_out->clear();

    JSValue snapshots = JS_GetPropertyStr(ctx, options, "snapshot");
    if (JS_IsException(snapshots)) {
        return false;
    }
    if (JS_IsUndefined(snapshots) || JS_IsNull(snapshots)) {
        JS_FreeValue(ctx, snapshots);
        return true;
    }
    if (!JS_IsArray(ctx, snapshots)) {
        JS_FreeValue(ctx, snapshots);
        JS_ThrowTypeError(ctx, "attach snapshot must be an array");
        return false;
    }

    uint32_t length = 0u;
    if (!GetArrayLength(ctx, snapshots, &length)) {
        JS_FreeValue(ctx, snapshots);
        JS_ThrowInternalError(ctx, "read attach snapshot length failed");
        return false;
    }

    snapshots_out->reserve(length);
    for (uint32_t index = 0u; index < length; ++index) {
        JSValue entry = JS_GetPropertyUint32(ctx, snapshots, index);
        if (JS_IsException(entry)) {
            JS_FreeValue(ctx, snapshots);
            return false;
        }
        if (!JS_IsObject(entry)) {
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            JS_ThrowTypeError(ctx, "attach snapshot entry must be an object");
            return false;
        }

        NativeJsArgumentSnapshotRequest snapshot = {};
        if (!GetRequiredStringProperty(ctx,
                                       entry,
                                       "type",
                                       "attach snapshot type is required",
                                       &snapshot.type)) {
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            return false;
        }
        if (snapshot.type != "jstringUtf8" &&
            snapshot.type != "cstringUtf8") {
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            JS_ThrowTypeError(ctx, "attach snapshot type must be 'jstringUtf8' or 'cstringUtf8'");
            return false;
        }

        JSValue argument_index = JS_GetPropertyStr(ctx, entry, "index");
        if (JS_IsException(argument_index)) {
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            return false;
        }
        if (JS_IsUndefined(argument_index) || JS_IsNull(argument_index) ||
            JS_ToUint32(ctx, &snapshot.argument_index, argument_index) < 0) {
            JS_FreeValue(ctx, argument_index);
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            JS_ThrowTypeError(ctx, "attach snapshot index must be a uint32");
            return false;
        }
        JS_FreeValue(ctx, argument_index);

        JSValue env_index = JS_GetPropertyStr(ctx, entry, "envIndex");
        if (JS_IsException(env_index)) {
            JS_FreeValue(ctx, entry);
            JS_FreeValue(ctx, snapshots);
            return false;
        }
        if (!JS_IsUndefined(env_index) && !JS_IsNull(env_index)) {
            if (JS_ToUint32(ctx, &snapshot.env_index, env_index) < 0) {
                JS_FreeValue(ctx, env_index);
                JS_FreeValue(ctx, entry);
                JS_FreeValue(ctx, snapshots);
                JS_ThrowTypeError(ctx, "attach snapshot envIndex must be a uint32");
                return false;
            }
        }
        JS_FreeValue(ctx, env_index);

        snapshots_out->push_back(std::move(snapshot));
        JS_FreeValue(ctx, entry);
    }

    JS_FreeValue(ctx, snapshots);
    return true;
}

JSValue InstallNativeHookForCurrentScript(JSContext* ctx,
                                          RuntimeState& state,
                                          const NativeJsHookRequest& request,
                                          JSValue on_enter,
                                          JSValue on_leave) {
    if (state.current_script_id == 0) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_ThrowInternalError(ctx, "attach must be called while loading a script");
    }

    NativeJsHookRecord record = {};
    std::string error_message;
    if (!InstallNativeJsHook(request,
                             state.native_hook_installer_dependencies,
                             &record,
                             &error_message)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    if (!StoreNativeHookCallbacksLocked(ctx,
                                        state,
                                        state.current_script_id,
                                        record.hook_id,
                                        record.blocking,
                                        on_enter,
                                        on_leave)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_ThrowInternalError(ctx, "store native hook callbacks failed");
    }

    JS_FreeValue(ctx, on_enter);
    JS_FreeValue(ctx, on_leave);

    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, 1)) < 0 ||
        JS_SetPropertyStr(ctx, result, "hookId", JS_NewInt32(ctx, static_cast<int32_t>(record.hook_id))) < 0 ||
        JS_SetPropertyStr(ctx, result, "deferred", JS_NewBool(ctx, record.deferred ? 1 : 0)) < 0) {
        JS_FreeValue(ctx, result);
        return JS_ThrowInternalError(ctx, "build attach result failed");
    }

    JSValue hook_id_value = JS_NewInt32(ctx, static_cast<int32_t>(record.hook_id));
    if (JS_IsException(hook_id_value)) {
        JS_FreeValue(ctx, result);
        return hook_id_value;
    }

    JSValue detach_func = JS_NewCFunctionData(ctx,
                                              JsNativeHookListenerDetach,
                                              0,
                                              0,
                                              1,
                                              &hook_id_value);
    JS_FreeValue(ctx, hook_id_value);
    if (JS_IsException(detach_func)) {
        JS_FreeValue(ctx, result);
        return detach_func;
    }

    if (JS_SetPropertyStr(ctx, result, "detach", detach_func) < 0) {
        JS_FreeValue(ctx, result);
        return JS_ThrowInternalError(ctx, "build attach listener failed");
    }

    return result;
}

JSValue DetachNativeHookForCurrentScript(JSContext* ctx,
                                         RuntimeState& state,
                                         uint32_t hook_id) {
    if (state.current_script_id == 0) {
        return JS_ThrowInternalError(ctx, "detach must be called while loading a script");
    }

    std::string error_message;
    const auto script_it = state.native_hook_callbacks.find(state.current_script_id);
    if (script_it != state.native_hook_callbacks.end() &&
        script_it->second.find(hook_id) != script_it->second.end()) {
        if (!UninstallNativeJsHook(hook_id, &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        FreeNativeHookCallbackLocked(ctx, state, state.current_script_id, hook_id);
        return JS_NewBool(ctx, 1);
    }

    uint32_t replace_script_id = 0u;
    RuntimeState::ReplaceHookRecord* replace_record =
        FindReplaceHookRecordByHookIdLocked(state, hook_id, &replace_script_id);
    if (replace_record == nullptr || replace_script_id != state.current_script_id) {
        return JS_NewBool(ctx, 0);
    }

    if (!UninstallNativeJsReplacementHook(replace_record->hook_handle, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    EraseReplaceHookRecordByHookIdLocked(state, hook_id);
    return JS_NewBool(ctx, 1);
}

JSValue DetachAllNativeHooksForCurrentScript(JSContext* ctx, RuntimeState& state) {
    if (state.current_script_id == 0) {
        return JS_ThrowInternalError(ctx, "detachAll must be called while loading a script");
    }

    const std::vector<uint32_t> hook_ids = CollectNativeHookIdsLocked(state, state.current_script_id);
    size_t replace_count = 0u;
    {
        const auto replace_it = state.replace_hook_records.find(state.current_script_id);
        if (replace_it != state.replace_hook_records.end()) {
            replace_count = replace_it->second.size();
        }
    }
    std::string error_message;
    if (!UninstallReplaceHooksForScriptLocked(state, state.current_script_id, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    for (uint32_t hook_id : hook_ids) {
        if (!UninstallNativeJsHook(hook_id, &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }
    FreeNativeHookCallbacksLocked(ctx, state, state.current_script_id);
    return JS_NewInt32(ctx, static_cast<int32_t>(hook_ids.size() + replace_count));
}

JSValue JsNativeHookListenerDetach(JSContext* ctx,
                                   JSValueConst this_val,
                                   int argc,
                                   JSValueConst* argv,
                                   int magic,
                                   JSValue* func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;

    uint32_t hook_id = 0;
    if (JS_ToUint32(ctx, &hook_id, func_data[0]) < 0 || hook_id == 0) {
        return JS_ThrowInternalError(ctx, "invalid hook listener");
    }

    RuntimeState& state = GetRuntimeState();
    return DetachNativeHookForCurrentScript(ctx, state, hook_id);
}

bool SendJsonToHost(JSContext* ctx,
                    JSValueConst value,
                    const char* error_prefix,
                    const std::vector<uint8_t>& data) {
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        return false;
    }

    const char* json_cstr = JS_ToCString(ctx, json);
    if (json_cstr == nullptr) {
        JS_FreeValue(ctx, json);
        JS_ThrowInternalError(ctx, "%s stringify failed", error_prefix);
        return false;
    }

    RuntimeState& state = GetRuntimeState();
    JsRuntime::SendCallback callback;
    {
        std::lock_guard<std::mutex> lock(state.callback_mutex);
        callback = state.send_callback;
    }

    const std::string json_string(json_cstr);
    JS_FreeCString(ctx, json_cstr);
    JS_FreeValue(ctx, json);

    if (!callback) {
        JS_ThrowInternalError(ctx, "send callback not set");
        return false;
    }

    JS_RT_LOGI("SendJsonToHost: script_id=%u prefix=%s payload=%s",
               state.current_script_id,
               error_prefix != nullptr ? error_prefix : "(null)",
               json_string.c_str());

    if (!callback(json_string, data)) {
        JS_ThrowInternalError(ctx, "send callback failed");
        return false;
    }

    return true;
}

bool TryGetOptionalStringProperty(JSContext* ctx,
                                  JSValueConst value,
                                  const char* property_name,
                                  std::string* out_text) {
    if (out_text == nullptr || property_name == nullptr) {
        return false;
    }

    JSValue property = JS_GetPropertyStr(ctx, value, property_name);
    if (JS_IsException(property)) {
        return false;
    }
    if (JS_IsUndefined(property) || JS_IsNull(property)) {
        JS_FreeValue(ctx, property);
        return false;
    }

    const char* text = JS_ToCString(ctx, property);
    if (text == nullptr) {
        JS_FreeValue(ctx, property);
        return false;
    }

    *out_text = text;
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, property);
    return true;
}

std::string FormatJavaWrapperFallback(JSContext* ctx, JSValueConst value) {
    JSValue array_type_value = JS_GetPropertyStr(ctx, value, kJavaArrayTypeProperty);
    if (!JS_IsException(array_type_value) &&
        !JS_IsUndefined(array_type_value) &&
        !JS_IsNull(array_type_value)) {
        const char* array_type = JS_ToCString(ctx, array_type_value);
        uint32_t length = 0u;
        const bool has_length = GetArrayLength(ctx, value, &length);
        if (array_type != nullptr) {
            std::ostringstream stream;
            stream << "<JavaArray " << array_type;
            if (has_length) {
                stream << " length=" << length;
            }
            stream << ">";
            JS_FreeCString(ctx, array_type);
            JS_FreeValue(ctx, array_type_value);
            return stream.str();
        }
    }
    JS_FreeValue(ctx, array_type_value);

    if (JS_IsArray(ctx, value)) {
        uint32_t length = 0u;
        const bool has_length = GetArrayLength(ctx, value, &length);
        std::string class_name;
        if (TryGetOptionalStringProperty(ctx, value, "$className", &class_name) &&
            !class_name.empty()) {
            std::ostringstream stream;
            stream << "<JavaArray " << class_name;
            if (has_length) {
                stream << " length=" << length;
            }
            stream << ">";
            return stream.str();
        }
    }

    std::string class_name;
    if (TryGetOptionalStringProperty(ctx, value, "$className", &class_name) &&
        !class_name.empty()) {
        std::string class_wrapper_name;
        uint64_t class_wrapper_loader_handle = 0u;
        const bool is_class_wrapper =
            ParseJavaClassWrapperInfo(ctx, value, &class_wrapper_name, &class_wrapper_loader_handle);
        JSValue method_name_value = JS_GetPropertyStr(ctx, value, "$methodName");
        if (!JS_IsException(method_name_value) &&
            !JS_IsUndefined(method_name_value) &&
            !JS_IsNull(method_name_value)) {
            const char* method_name = JS_ToCString(ctx, method_name_value);
            if (method_name != nullptr && method_name[0] != '\0') {
                std::string fallback = std::string("<JavaMethod ") + class_name + "." + method_name + ">";
                JS_FreeCString(ctx, method_name);
                JS_FreeValue(ctx, method_name_value);
                return fallback;
            }
            if (method_name != nullptr) {
                JS_FreeCString(ctx, method_name);
            }
        }
        JS_FreeValue(ctx, method_name_value);

        JSValue field_name_value = JS_GetPropertyStr(ctx, value, "$fieldName");
        if (!JS_IsException(field_name_value) &&
            !JS_IsUndefined(field_name_value) &&
            !JS_IsNull(field_name_value)) {
            const char* field_name = JS_ToCString(ctx, field_name_value);
            if (field_name != nullptr && field_name[0] != '\0') {
                std::string fallback = std::string("<JavaField ") + class_name + "." + field_name + ">";
                JS_FreeCString(ctx, field_name);
                JS_FreeValue(ctx, field_name_value);
                return fallback;
            }
            if (field_name != nullptr) {
                JS_FreeCString(ctx, field_name);
            }
        }
        JS_FreeValue(ctx, field_name_value);

        if (is_class_wrapper) {
            return std::string("<JavaClass ") + class_name + ">";
        }
        return std::string("<JavaObject ") + class_name + ">";
    }

    return std::string();
}

bool AppendConsoleValueString(JSContext* ctx,
                              JSValueConst value,
                              std::ostringstream* stream,
                              bool* used_fallback) {
    if (stream == nullptr) {
        return false;
    }

    const char* arg = JS_ToCString(ctx, value);
    if (arg != nullptr) {
        *stream << arg;
        JS_FreeCString(ctx, arg);
        return true;
    }

    std::string fallback;
    if (JS_IsObject(value)) {
        fallback = FormatJavaWrapperFallback(ctx, value);
    }
    if (!fallback.empty()) {
        *stream << fallback;
        if (used_fallback != nullptr) {
            *used_fallback = true;
        }
        return true;
    }

    return false;
}

JSValue MakeRecvWaitObject(JSContext* ctx) {
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }

    JSValue wait_func = JS_NewCFunction(
        ctx,
        [](JSContext* inner_ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
            (void)inner_ctx;
            (void)this_val;
            (void)argc;
            (void)argv;
            return JS_UNDEFINED;
        },
        "wait",
        0);
    if (JS_IsException(wait_func) || JS_SetPropertyStr(ctx, object, "wait", wait_func) < 0) {
        JS_FreeValue(ctx, wait_func);
        JS_FreeValue(ctx, object);
        return JS_ThrowInternalError(ctx, "build recv wait object failed");
    }

    return object;
}

JSValue JsSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "send requires a message argument");
    }
    std::vector<uint8_t> binary_payload;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        size_t payload_size = 0u;
        uint8_t* payload_data = JS_GetArrayBuffer(ctx, &payload_size, argv[1]);
        if (payload_data == nullptr) {
            return JS_ThrowTypeError(ctx, "send binary payload must be an ArrayBuffer");
        }
        binary_payload.assign(payload_data, payload_data + payload_size);
    }

    if (!SendJsonToHost(ctx, argv[0], "send", binary_payload)) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}

JSValue JsRecv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "recv requires a callback");
    }

    JSValueConst callback = argv[0];
    if (argc >= 2) {
        callback = argv[1];
    }
    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "recv callback must be a function");
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0) {
        return JS_ThrowInternalError(ctx, "recv must be called while loading a script");
    }

    FreeRecvCallbackLocked(ctx, state, state.current_script_id);
    state.recv_callbacks[state.current_script_id] = JS_DupValue(ctx, callback);
    return MakeRecvWaitObject(ctx);
}

JSValue JsGetCurrentScriptId(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    RuntimeState& state = GetRuntimeState();
    return JS_NewUint32(ctx, state.current_script_id);
}

JSValue JsRunInScript(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "__nookRunInScript requires scriptId and callback");
    }

    uint32_t script_id = 0u;
    if (JS_ToUint32(ctx, &script_id, argv[0]) < 0 || script_id == 0u) {
        return JS_ThrowTypeError(ctx, "__nookRunInScript scriptId must be a positive integer");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "__nookRunInScript callback must be a function");
    }

    RuntimeState& state = GetRuntimeState();
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(ctx, argv[1], JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(result)) {
        return result;
    }
    return result;
}

JSValue JsConsoleLog(JSContext* ctx,
                     JSValueConst this_val,
                     int argc,
                     JSValueConst* argv,
                     int magic) {
    (void)this_val;

    std::ostringstream stream;
    bool used_fallback = false;
    for (int index = 0; index < argc; ++index) {
        if (index > 0) {
            stream << ' ';
        }
        if (!AppendConsoleValueString(ctx, argv[index], &stream, &used_fallback)) {
            return JS_ThrowInternalError(ctx, "console stringify failed");
        }
    }

    JSValue message = JS_NewObject(ctx);
    if (JS_IsException(message)) {
        return message;
    }
    if (JS_SetPropertyStr(ctx, message, "type", JS_NewString(ctx, "log")) < 0 ||
        JS_SetPropertyStr(ctx, message, "level", JS_NewString(ctx, GetConsoleLevelName(magic))) < 0 ||
        JS_SetPropertyStr(ctx, message, "payload", JS_NewString(ctx, stream.str().c_str())) < 0) {
        JS_FreeValue(ctx, message);
        return JS_ThrowInternalError(ctx, "build console log message failed");
    }

    const bool sent = SendJsonToHost(ctx, message, "console");
    JS_FreeValue(ctx, message);
    return sent ? JS_UNDEFINED : JS_EXCEPTION;
}

JSValue JsNativeAttach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "attach options must be an object");
    }

    std::string type;
    if (!GetRequiredStringProperty(ctx, argv[0], "type", "attach type is required", &type)) {
        return JS_EXCEPTION;
    }
    if (type != "inline") {
        if (type == "plt") {
            return JS_ThrowInternalError(ctx, "not implemented yet");
        }
        return JS_ThrowTypeError(ctx, "attach type must be 'inline'");
    }

    std::string module_name;
    if (!GetRequiredStringProperty(ctx, argv[0], "module", "attach module is required", &module_name)) {
        return JS_EXCEPTION;
    }

    std::string symbol_name;
    if (!GetRequiredStringProperty(ctx, argv[0], "symbol", "attach symbol is required", &symbol_name)) {
        return JS_EXCEPTION;
    }

    JSValue on_enter = JS_UNDEFINED;
    JSValue on_leave = JS_UNDEFINED;
    if (!GetNativeHookCallbacks(ctx, argv[0], &on_enter, &on_leave)) {
        return JS_EXCEPTION;
    }
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
    if (!GetNativeHookSnapshots(ctx, argv[0], &snapshots)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }
    bool blocking = true;
    if (!GetNativeHookBlockingMode(ctx, argv[0], &blocking)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }

    RuntimeState& state = GetRuntimeState();
    NativeJsHookRequest request = {};
    request.type = type;
    request.module_name = module_name;
    request.symbol_name = symbol_name;
    request.blocking = blocking;
    request.snapshots = std::move(snapshots);
    return InstallNativeHookForCurrentScript(ctx, state, request, on_enter, on_leave);
}

JSValue InvokeJavaCallbackImmediately(JSContext* ctx,
                                      JSValueConst this_val,
                                      int argc,
                                      JSValueConst* argv,
                                      const char* error_prefix) {
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "%s requires a function", error_prefix);
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue callback = JS_DupValue(ctx, argv[0]);
    JSValue result = JS_Call(ctx, callback, global, 0, nullptr);
    JS_FreeValue(ctx, callback);
    JS_FreeValue(ctx, global);
    if (JS_IsException(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_UNDEFINED;
}

JSValue JsJavaPerform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return InvokeJavaCallbackImmediately(ctx, this_val, argc, argv, "Java.perform");
}

JSValue JsJavaVmPerform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return InvokeJavaVmPerformWithEnv(ctx, this_val, argc, argv);
}

JsRuntimeJavaEnvQueryStatus QueryCurrentJavaEnvPointer(bool allow_attach,
                                                       uint64_t* env_ptr_out,
                                                       std::string* error_message) {
    if (env_ptr_out == nullptr) {
        SetError(error_message, "env_ptr_out is null");
        return JsRuntimeJavaEnvQueryStatus::kError;
    }

    if (!allow_attach) {
        ScopedJavaEnvOverrideState& state = GetScopedJavaEnvOverrideState();
        if (state.depth > 0u) {
            *env_ptr_out = state.pointer;
            return JsRuntimeJavaEnvQueryStatus::kAvailable;
        }
    }

    JsRuntimeGetJavaEnvPointerForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.get_java_env_pointer;
    }
    if (callback != nullptr) {
        return callback(allow_attach, env_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    if (allow_attach) {
        std::string init_error;
        if (!EnsureJavaHookReadyForJs(&init_error)) {
            SetError(error_message, init_error);
            return JsRuntimeJavaEnvQueryStatus::kError;
        }

        JavaEnv jenv;
        if (jenv.isNull()) {
            SetError(error_message, "Java.vm.getEnv could not acquire JNIEnv");
            return JsRuntimeJavaEnvQueryStatus::kError;
        }

        *env_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(jenv.get()));
        return JsRuntimeJavaEnvQueryStatus::kAvailable;
    }

    JavaVM* java_vm = JavaEnv::GetJavaVM();
    if (java_vm == nullptr) {
        SetError(error_message, "Java.vm.tryGetEnv could not resolve JavaVM");
        return JsRuntimeJavaEnvQueryStatus::kError;
    }

    JNIEnv* env = nullptr;
    const jint result = java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_OK && env != nullptr) {
        *env_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(env));
        return JsRuntimeJavaEnvQueryStatus::kAvailable;
    }
    if (result == JNI_EDETACHED) {
        return JsRuntimeJavaEnvQueryStatus::kUnavailable;
    }

    SetError(error_message, "Java.vm.tryGetEnv failed to query JNIEnv");
    return JsRuntimeJavaEnvQueryStatus::kError;
#else
    SetError(error_message,
             allow_attach ? "Java.vm.getEnv is unavailable in this host runtime"
                          : "Java.vm.tryGetEnv is unavailable in this host runtime");
    return JsRuntimeJavaEnvQueryStatus::kError;
#endif
}

bool QueryJavaEnvExceptionCheck(uint64_t env_ptr,
                                bool* has_exception_out,
                                std::string* error_message) {
    if (has_exception_out == nullptr) {
        SetError(error_message, "has_exception_out is null");
        return false;
    }

    JsRuntimeJavaEnvExceptionCheckForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_exception_check;
    }
    if (callback != nullptr) {
        return callback(env_ptr, has_exception_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.exceptionCheck could not acquire JNIEnv");
        return false;
    }

    *has_exception_out = jenv->ExceptionCheck();
    return true;
#else
    SetError(error_message, "Java.Env.exceptionCheck is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvExceptionOccurred(uint64_t env_ptr,
                                   uint64_t* exception_ptr_out,
                                   std::string* error_message) {
    if (exception_ptr_out == nullptr) {
        SetError(error_message, "exception_ptr_out is null");
        return false;
    }

    JsRuntimeJavaEnvExceptionOccurredForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_exception_occurred;
    }
    if (callback != nullptr) {
        return callback(env_ptr, exception_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.exceptionOccurred could not acquire JNIEnv");
        return false;
    }

    jthrowable exception = jenv->ExceptionOccurred();
    *exception_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(exception));
    return true;
#else
    SetError(error_message, "Java.Env.exceptionOccurred is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvExceptionClear(uint64_t env_ptr, std::string* error_message) {
    JsRuntimeJavaEnvExceptionClearForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_exception_clear;
    }
    if (callback != nullptr) {
        return callback(env_ptr, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.exceptionClear could not acquire JNIEnv");
        return false;
    }

    jenv->ExceptionClear();
    return true;
#else
    SetError(error_message, "Java.Env.exceptionClear is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvFindClass(uint64_t env_ptr,
                           const char* class_name,
                           uint64_t* class_ptr_out,
                           std::string* error_message) {
    if (class_ptr_out == nullptr) {
        SetError(error_message, "class_ptr_out is null");
        return false;
    }
    if (class_name == nullptr) {
        SetError(error_message, "class_name is null");
        return false;
    }

    JsRuntimeJavaEnvFindClassForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_find_class;
    }
    if (callback != nullptr) {
        return callback(env_ptr, class_name, class_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.findClass could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = env->FindClass(class_name);
    if (clazz == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, std::string("Java.Env.findClass failed for ") + class_name);
        return false;
    }

    *class_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(clazz));
    return true;
#else
    SetError(error_message, "Java.Env.findClass is unavailable in this host runtime");
    return false;
#endif
}

bool ResolveJavaClassGlobalRef(uint64_t env_ptr,
                               const char* class_name,
                               uint64_t loader_handle,
                               uint64_t* class_global_ref_out,
                               std::string* error_message) {
    if (class_global_ref_out == nullptr) {
        SetError(error_message, "class_global_ref_out is null");
        return false;
    }
    *class_global_ref_out = 0u;
    if (class_name == nullptr || class_name[0] == '\0') {
        SetError(error_message, "class_name is invalid");
        return false;
    }

    std::string jni_class_name = class_name;
    std::replace(jni_class_name.begin(), jni_class_name.end(), '.', '/');

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java class resolution could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = nullptr;
    if (loader_handle != 0u) {
        jobject loader = reinterpret_cast<jobject>(static_cast<uintptr_t>(loader_handle));
        jobject local_loader = env->NewLocalRef(loader);
        if (local_loader != nullptr) {
            clazz = JavaHook::FindClassWithLoader(env, local_loader, jni_class_name.c_str());
            env->DeleteLocalRef(local_loader);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    if (clazz == nullptr) {
        clazz = JavaHook::FindClass(env, jni_class_name.c_str());
    }
    if (clazz == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message,
                 std::string("Java class resolution failed for ") + class_name);
        return false;
    }

    jobject global_ref = env->NewGlobalRef(clazz);
    env->DeleteLocalRef(clazz);
    if (global_ref == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message,
                 std::string("Java class global ref creation failed for ") + class_name);
        return false;
    }

    *class_global_ref_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(global_ref));
    return true;
#else
    uint64_t local_class_ref = 0u;
    if (!QueryJavaEnvFindClass(env_ptr, jni_class_name.c_str(), &local_class_ref, error_message)) {
        return false;
    }
    if (local_class_ref == 0u) {
        SetError(error_message, std::string("Java class resolution returned null for ") + class_name);
        return false;
    }
    if (!QueryJavaEnvNewGlobalRef(env_ptr, local_class_ref, class_global_ref_out, error_message)) {
        return false;
    }
    return true;
#endif
}

bool QueryJavaEnvGetObjectClass(uint64_t env_ptr,
                                uint64_t object_handle,
                                uint64_t* class_ptr_out,
                                std::string* error_message) {
    if (class_ptr_out == nullptr) {
        SetError(error_message, "class_ptr_out is null");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }

    JsRuntimeJavaEnvGetObjectClassForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_get_object_class;
    }
    if (callback != nullptr) {
        return callback(env_ptr, object_handle, class_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.getObjectClass could not acquire JNIEnv");
        return false;
    }

    jobject object = reinterpret_cast<jobject>(static_cast<uintptr_t>(object_handle));
    jclass clazz = jenv->GetObjectClass(object);
    if (clazz == nullptr || jenv->ExceptionCheck()) {
        if (jenv->ExceptionCheck()) {
            jenv->ExceptionClear();
        }
        SetError(error_message, "Java.Env.getObjectClass failed");
        return false;
    }

    *class_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(clazz));
    return true;
#else
    SetError(error_message, "Java.Env.getObjectClass is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvGetSuperclass(uint64_t env_ptr,
                               const char* class_name,
                               uint64_t loader_handle,
                               bool* has_superclass_out,
                               std::string* superclass_name_out,
                               std::string* error_message) {
    if (has_superclass_out == nullptr || superclass_name_out == nullptr) {
        SetError(error_message, "superclass outputs are null");
        return false;
    }
    if (class_name == nullptr || class_name[0] == '\0') {
        SetError(error_message, "class_name is invalid");
        return false;
    }

    JsRuntimeJavaEnvGetSuperclassForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_get_superclass;
    }
    if (callback != nullptr) {
        return callback(
            env_ptr, class_name, loader_handle, has_superclass_out, superclass_name_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.getSuperclass could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = nullptr;
    if (loader_handle != 0u) {
        jobject loader = reinterpret_cast<jobject>(static_cast<uintptr_t>(loader_handle));
        jobject local_loader = env->NewLocalRef(loader);
        if (local_loader != nullptr) {
            clazz = JavaHook::FindClassWithLoader(env, local_loader, class_name);
            env->DeleteLocalRef(local_loader);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    if (clazz == nullptr) {
        clazz = JavaHook::FindClass(env, class_name);
    }
    if (clazz == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, std::string("Java.Env.getSuperclass failed for ") + class_name);
        return false;
    }

    jclass superclass = env->GetSuperclass(clazz);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(clazz);
        SetError(error_message, "Java.Env.getSuperclass failed");
        return false;
    }
    if (superclass == nullptr) {
        env->DeleteLocalRef(clazz);
        *has_superclass_out = false;
        superclass_name_out->clear();
        return true;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jmethodID get_name = nullptr;
    if (class_class != nullptr) {
        get_name = env->GetMethodID(class_class, "getName", "()Ljava/lang/String;");
    }
    if (class_class == nullptr || get_name == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(superclass);
        env->DeleteLocalRef(clazz);
        if (class_class != nullptr) {
            env->DeleteLocalRef(class_class);
        }
        SetError(error_message, "Java.Env.getSuperclass failed");
        return false;
    }

    jstring superclass_text =
        static_cast<jstring>(env->CallObjectMethod(superclass, get_name));
    if (superclass_text == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(superclass);
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(class_class);
        SetError(error_message, "Java.Env.getSuperclass failed");
        return false;
    }

    const char* utf8_text = env->GetStringUTFChars(superclass_text, nullptr);
    if (utf8_text == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(superclass_text);
        env->DeleteLocalRef(superclass);
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(class_class);
        SetError(error_message, "Java.Env.getSuperclass failed");
        return false;
    }

    *has_superclass_out = true;
    *superclass_name_out = utf8_text;
    env->ReleaseStringUTFChars(superclass_text, utf8_text);
    env->DeleteLocalRef(superclass_text);
    env->DeleteLocalRef(superclass);
    env->DeleteLocalRef(clazz);
    env->DeleteLocalRef(class_class);
    return true;
#else
    SetError(error_message, "Java.Env.getSuperclass is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvIsAssignableFrom(uint64_t env_ptr,
                                  const char* target_class_name,
                                  uint64_t target_loader_handle,
                                  const char* source_class_name,
                                  uint64_t source_loader_handle,
                                  bool* result_out,
                                  std::string* error_message) {
    if (result_out == nullptr) {
        SetError(error_message, "result_out is null");
        return false;
    }
    if (target_class_name == nullptr || target_class_name[0] == '\0' ||
        source_class_name == nullptr || source_class_name[0] == '\0') {
        SetError(error_message, "class_name is invalid");
        return false;
    }

    JsRuntimeJavaEnvIsAssignableFromForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_is_assignable_from;
    }
    if (callback != nullptr) {
        return callback(env_ptr,
                        target_class_name,
                        target_loader_handle,
                        source_class_name,
                        source_loader_handle,
                        result_out,
                        error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.isAssignableFrom could not acquire JNIEnv");
        return false;
    }

    auto resolve_class = [&](const char* name, uint64_t loader) -> jclass {
        jclass resolved = nullptr;
        if (loader != 0u) {
            jobject loader_object = reinterpret_cast<jobject>(static_cast<uintptr_t>(loader));
            jobject local_loader = jenv->NewLocalRef(loader_object);
            if (local_loader != nullptr) {
                resolved = JavaHook::FindClassWithLoader(jenv.get(), local_loader, name);
                jenv->DeleteLocalRef(local_loader);
            }
            if (jenv->ExceptionCheck()) {
                jenv->ExceptionClear();
            }
        }
        if (resolved == nullptr) {
            resolved = JavaHook::FindClass(jenv.get(), name);
        }
        return resolved;
    };

    jclass target_class = resolve_class(target_class_name, target_loader_handle);
    if (target_class == nullptr || jenv->ExceptionCheck()) {
        if (jenv->ExceptionCheck()) {
            jenv->ExceptionClear();
        }
        SetError(error_message,
                 std::string("Java.Env.isAssignableFrom failed for ") + target_class_name);
        return false;
    }

    jclass source_class = resolve_class(source_class_name, source_loader_handle);
    if (source_class == nullptr || jenv->ExceptionCheck()) {
        if (jenv->ExceptionCheck()) {
            jenv->ExceptionClear();
        }
        jenv->DeleteLocalRef(target_class);
        SetError(error_message,
                 std::string("Java.Env.isAssignableFrom failed for ") + source_class_name);
        return false;
    }

    // JNI IsAssignableFrom takes (sub, super), while the public API follows
    // Java Class semantics: target.isAssignableFrom(source).
    *result_out = jenv->IsAssignableFrom(source_class, target_class) == JNI_TRUE;
    jenv->DeleteLocalRef(source_class);
    jenv->DeleteLocalRef(target_class);
    return true;
#else
    SetError(error_message, "Java.Env.isAssignableFrom is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvIsSameObject(uint64_t env_ptr,
                              uint64_t left_object_handle,
                              uint64_t right_object_handle,
                              bool* result_out,
                              std::string* error_message) {
    if (result_out == nullptr) {
        SetError(error_message, "result_out is null");
        return false;
    }
    if (left_object_handle == 0u || right_object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }

    JsRuntimeJavaEnvIsSameObjectForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_is_same_object;
    }
    if (callback != nullptr) {
        return callback(
            env_ptr, left_object_handle, right_object_handle, result_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.isSameObject could not acquire JNIEnv");
        return false;
    }

    jobject left = reinterpret_cast<jobject>(static_cast<uintptr_t>(left_object_handle));
    jobject right = reinterpret_cast<jobject>(static_cast<uintptr_t>(right_object_handle));
    *result_out = jenv->IsSameObject(left, right) == JNI_TRUE;
    return true;
#else
    SetError(error_message, "Java.Env.isSameObject is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvIsInstanceOf(uint64_t env_ptr,
                              uint64_t object_handle,
                              const char* class_name,
                              uint64_t loader_handle,
                              bool* result_out,
                              std::string* error_message) {
    if (result_out == nullptr) {
        SetError(error_message, "result_out is null");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }
    if (class_name == nullptr || class_name[0] == '\0') {
        SetError(error_message, "class_name is invalid");
        return false;
    }

    JsRuntimeJavaEnvIsInstanceOfForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_is_instance_of;
    }
    if (callback != nullptr) {
        return callback(env_ptr, object_handle, class_name, result_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.isInstanceOf could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = nullptr;
    if (loader_handle != 0u) {
        jobject loader = reinterpret_cast<jobject>(static_cast<uintptr_t>(loader_handle));
        jobject local_loader = env->NewLocalRef(loader);
        if (local_loader != nullptr) {
            clazz = JavaHook::FindClassWithLoader(env, local_loader, class_name);
            env->DeleteLocalRef(local_loader);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    if (clazz == nullptr) {
        clazz = JavaHook::FindClass(env, class_name);
    }
    if (clazz == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, std::string("Java.Env.isInstanceOf failed for ") + class_name);
        return false;
    }

    jobject object = reinterpret_cast<jobject>(static_cast<uintptr_t>(object_handle));
    *result_out = env->IsInstanceOf(object, clazz) == JNI_TRUE;
    env->DeleteLocalRef(clazz);
    return true;
#else
    SetError(error_message, "Java.Env.isInstanceOf is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvNewStringUtf(uint64_t env_ptr,
                              const char* utf8_text,
                              uint64_t* string_ptr_out,
                              std::string* error_message) {
    if (string_ptr_out == nullptr) {
        SetError(error_message, "string_ptr_out is null");
        return false;
    }
    if (utf8_text == nullptr) {
        SetError(error_message, "utf8_text is null");
        return false;
    }

    JsRuntimeJavaEnvNewStringUtfForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_new_string_utf;
    }
    if (callback != nullptr) {
        return callback(env_ptr, utf8_text, string_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.newStringUtf could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jstring text = env->NewStringUTF(utf8_text);
    if (text == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.Env.newStringUtf failed");
        return false;
    }

    jobject global_text = env->NewGlobalRef(text);
    env->DeleteLocalRef(text);
    if (global_text == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.Env.newStringUtf failed");
        return false;
    }

    *string_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(global_text));
    return true;
#else
    SetError(error_message, "Java.Env.newStringUtf is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvGetStringUtfChars(uint64_t env_ptr,
                                   uint64_t jstring_ptr,
                                   uint64_t* chars_ptr_out,
                                   std::string* error_message) {
    if (chars_ptr_out == nullptr) {
        SetError(error_message, "chars_ptr_out is null");
        return false;
    }
    if (jstring_ptr == 0u) {
        SetError(error_message, "jstring_ptr is null");
        return false;
    }

    JsRuntimeJavaEnvGetStringUtfCharsForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_get_string_utf_chars;
    }
    if (callback != nullptr) {
        return callback(env_ptr, jstring_ptr, chars_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.getStringUtfChars could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    const char* chars = env->GetStringUTFChars(
        reinterpret_cast<jstring>(static_cast<uintptr_t>(jstring_ptr)),
        nullptr);
    if (chars == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.Env.getStringUtfChars failed");
        return false;
    }

    *chars_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(chars));
    return true;
#else
    SetError(error_message, "Java.Env.getStringUtfChars is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvReleaseStringUtfChars(uint64_t env_ptr,
                                       uint64_t jstring_ptr,
                                       uint64_t chars_ptr,
                                       std::string* error_message) {
    if (jstring_ptr == 0u || chars_ptr == 0u) {
        SetError(error_message, "jstring_ptr or chars_ptr is null");
        return false;
    }

    JsRuntimeJavaEnvReleaseStringUtfCharsForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_release_string_utf_chars;
    }
    if (callback != nullptr) {
        return callback(env_ptr, jstring_ptr, chars_ptr, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.releaseStringUtfChars could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    env->ReleaseStringUTFChars(
        reinterpret_cast<jstring>(static_cast<uintptr_t>(jstring_ptr)),
        reinterpret_cast<const char*>(static_cast<uintptr_t>(chars_ptr)));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "Java.Env.releaseStringUtfChars failed");
        return false;
    }

    return true;
#else
    SetError(error_message, "Java.Env.releaseStringUtfChars is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvNewGlobalRef(uint64_t env_ptr,
                              uint64_t object_handle,
                              uint64_t* ref_ptr_out,
                              std::string* error_message) {
    if (ref_ptr_out == nullptr) {
        SetError(error_message, "ref_ptr_out is null");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }

    JsRuntimeJavaEnvNewGlobalRefForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_new_global_ref;
    }
    if (callback != nullptr) {
        return callback(env_ptr, object_handle, ref_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.newGlobalRef could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jobject ref = env->NewGlobalRef(reinterpret_cast<jobject>(static_cast<uintptr_t>(object_handle)));
    if (ref == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.Env.newGlobalRef failed");
        return false;
    }

    *ref_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ref));
    return true;
#else
    SetError(error_message, "Java.Env.newGlobalRef is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvDeleteGlobalRef(uint64_t env_ptr,
                                 uint64_t ref_ptr,
                                 std::string* error_message) {
    if (ref_ptr == 0u) {
        SetError(error_message, "ref_ptr is null");
        return false;
    }

    JsRuntimeJavaEnvDeleteGlobalRefForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_delete_global_ref;
    }
    if (callback != nullptr) {
        return callback(env_ptr, ref_ptr, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.deleteGlobalRef could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    env->DeleteGlobalRef(reinterpret_cast<jobject>(static_cast<uintptr_t>(ref_ptr)));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "Java.Env.deleteGlobalRef failed");
        return false;
    }

    return true;
#else
    SetError(error_message, "Java.Env.deleteGlobalRef is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvNewWeakGlobalRef(uint64_t env_ptr,
                                  uint64_t object_handle,
                                  uint64_t* ref_ptr_out,
                                  std::string* error_message) {
    if (ref_ptr_out == nullptr) {
        SetError(error_message, "ref_ptr_out is null");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }

    JsRuntimeJavaEnvNewWeakGlobalRefForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_new_weak_global_ref;
    }
    if (callback != nullptr) {
        return callback(env_ptr, object_handle, ref_ptr_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.newWeakGlobalRef could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jweak ref = env->NewWeakGlobalRef(reinterpret_cast<jobject>(static_cast<uintptr_t>(object_handle)));
    if (ref == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.Env.newWeakGlobalRef failed");
        return false;
    }

    *ref_ptr_out = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ref));
    return true;
#else
    SetError(error_message, "Java.Env.newWeakGlobalRef is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvDeleteWeakGlobalRef(uint64_t env_ptr,
                                     uint64_t ref_ptr,
                                     std::string* error_message) {
    if (ref_ptr == 0u) {
        SetError(error_message, "ref_ptr is null");
        return false;
    }

    JsRuntimeJavaEnvDeleteWeakGlobalRefForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_delete_weak_global_ref;
    }
    if (callback != nullptr) {
        return callback(env_ptr, ref_ptr, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.deleteWeakGlobalRef could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    env->DeleteWeakGlobalRef(reinterpret_cast<jweak>(static_cast<uintptr_t>(ref_ptr)));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "Java.Env.deleteWeakGlobalRef failed");
        return false;
    }

    return true;
#else
    SetError(error_message, "Java.Env.deleteWeakGlobalRef is unavailable in this host runtime");
    return false;
#endif
}

bool QueryJavaEnvGetObjectRefType(uint64_t env_ptr,
                                  uint64_t object_handle,
                                  uint32_t* ref_type_out,
                                  std::string* error_message) {
    if (ref_type_out == nullptr) {
        SetError(error_message, "ref_type_out is null");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "object_handle is null");
        return false;
    }

    JsRuntimeJavaEnvGetObjectRefTypeForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.java_env_get_object_ref_type;
    }
    if (callback != nullptr) {
        return callback(env_ptr, object_handle, ref_type_out, error_message);
    }

#if defined(__ANDROID__)
    std::string init_error;
    if (!EnsureJavaHookReadyForJs(&init_error)) {
        SetError(error_message, init_error);
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "Java.Env.getObjectRefType could not acquire JNIEnv");
        return false;
    }

    JNIEnv* env = jenv.get();
    jobject object = reinterpret_cast<jobject>(static_cast<uintptr_t>(object_handle));
    *ref_type_out = static_cast<uint32_t>(env->GetObjectRefType(object));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "Java.Env.getObjectRefType failed");
        return false;
    }
    return true;
#else
    SetError(error_message, "Java.Env.getObjectRefType is unavailable in this host runtime");
    return false;
#endif
}

class ScopedJavaEnvPointerOverrideForTesting {
public:
    explicit ScopedJavaEnvPointerOverrideForTesting(uint64_t env_ptr) {
        if (env_ptr == 0u) {
            return;
        }

        ScopedJavaEnvOverrideState& state = GetScopedJavaEnvOverrideState();
        previous_pointer_ = state.pointer;
        previous_depth_ = state.depth;
        state.pointer = env_ptr;
        state.depth = previous_depth_ + 1u;
        active_ = true;
    }

    ~ScopedJavaEnvPointerOverrideForTesting() {
        if (!active_) {
            return;
        }

        ScopedJavaEnvOverrideState& state = GetScopedJavaEnvOverrideState();
        state.pointer = previous_pointer_;
        state.depth = previous_depth_;
    }

private:
    uint64_t previous_pointer_ = 0u;
    size_t previous_depth_ = 0u;
    bool active_ = false;
};

JSValue InvokeJavaVmPerformWithEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
#if defined(__ANDROID__)
    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    ScopedJavaEnvPointerOverrideForTesting scoped_env(env_ptr);
    return InvokeJavaCallbackImmediately(ctx, this_val, argc, argv, "Java.vm.perform");
#else
    JsRuntimeGetJavaEnvPointerForTesting callback = nullptr;
    {
        JniBridgeState& state = GetJniBridgeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        callback = state.get_java_env_pointer;
    }
    if (callback == nullptr) {
        return InvokeJavaCallbackImmediately(ctx, this_val, argc, argv, "Java.vm.perform");
    }

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    ScopedJavaEnvPointerOverrideForTesting scoped_env(env_ptr);
    return InvokeJavaCallbackImmediately(ctx, this_val, argc, argv, "Java.vm.perform");
#endif
}

JSValue JsJavaVmGetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeJavaEnvWrapper(ctx, env_ptr);
}

JSValue JsJavaVmTryGetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    uint64_t env_ptr = 0u;
    std::string error_message;
    JsRuntimeJavaEnvQueryStatus status = QueryCurrentJavaEnvPointer(false, &env_ptr, &error_message);
    if (status == JsRuntimeJavaEnvQueryStatus::kUnavailable) {
        return JS_NULL;
    }
    if (status != JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeJavaEnvWrapper(ctx, env_ptr);
}

JSValue JsJavaEnvToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;

    uint64_t env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &env_ptr)) {
        return JS_EXCEPTION;
    }

    const std::string text = std::string("Env(") + FormatHookValue(env_ptr) + ")";
    return JS_NewString(ctx, text.c_str());
}

JSValue JsJavaEnvExceptionCheck(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;

    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }

    uint64_t env_ptr = 0u;
    bool has_exception = false;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvExceptionCheck(env_ptr, &has_exception, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, has_exception ? 1 : 0);
}

JSValue JsJavaEnvExceptionOccurred(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;

    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    uint64_t env_ptr = 0u;
    uint64_t exception_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvExceptionOccurred(env_ptr, &exception_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, exception_ptr);
}

JSValue JsJavaEnvExceptionClear(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;

    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvExceptionClear(env_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, 1);
}

JSValue JsJavaEnvFindClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java.Env.findClass requires a string name");
    }

    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_EXCEPTION;
    }

    uint64_t env_ptr = 0u;
    uint64_t class_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    const bool ok = QueryJavaEnvFindClass(env_ptr, class_name, &class_ptr, &error_message);
    JS_FreeCString(ctx, class_name);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, class_ptr);
}

JSValue JsJavaEnvGetObjectClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.Env.getObjectClass requires a Java object");
    }

    JavaJsValue object_value = {};
    std::string error_message;
    if (!ParseJavaJsValue(ctx, argv[0], &object_value, &error_message) ||
        object_value.kind != JavaJsValueKind::kObject ||
        object_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.Env.getObjectClass requires a Java object");
    }

    uint64_t env_ptr = 0u;
    uint64_t class_ptr = 0u;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvGetObjectClass(
            env_ptr, object_value.object_handle, &class_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, class_ptr);
}

JSValue JsJavaEnvIsSameObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java.Env.isSameObject requires two Java objects");
    }

    JavaJsValue left_value = {};
    JavaJsValue right_value = {};
    std::string error_message;
    const bool left_ok = ParseJavaJsValue(ctx, argv[0], &left_value, &error_message) &&
                         left_value.kind == JavaJsValueKind::kObject &&
                         left_value.object_handle != 0u;
    const bool right_ok = ParseJavaJsValue(ctx, argv[1], &right_value, &error_message) &&
                          right_value.kind == JavaJsValueKind::kObject &&
                          right_value.object_handle != 0u;
    if (!left_ok || !right_ok) {
        return JS_ThrowTypeError(ctx, "Java.Env.isSameObject requires two Java objects");
    }

    uint64_t env_ptr = 0u;
    bool result = false;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvIsSameObject(
            env_ptr, left_value.object_handle, right_value.object_handle, &result, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, result ? 1 : 0);
}

JSValue JsJavaEnvIsInstanceOf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 2) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.isInstanceOf requires a Java object and class wrapper");
    }

    std::string error_message;
    JavaJsValue object_value = {};
    if (!ParseJavaJsValue(ctx, argv[0], &object_value, &error_message) ||
        object_value.kind != JavaJsValueKind::kObject ||
        object_value.object_handle == 0u) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.isInstanceOf requires a Java object and class wrapper");
    }

    if (!JS_IsObject(argv[1])) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.isInstanceOf requires a Java object and class wrapper");
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, argv[1], "$className");
    JSValue receiver_handle_value = JS_GetPropertyStr(ctx, argv[1], kJavaReceiverHandleProperty);
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, argv[1], kJavaLoaderHandleProperty);
    if (JS_IsException(class_name_value) ||
        JS_IsException(receiver_handle_value) ||
        JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(
            ctx, "Java.Env.isInstanceOf requires a Java object and class wrapper");
    }

    const char* class_name = JS_ToCString(ctx, class_name_value);
    bool is_class_wrapper = false;
    if (class_name != nullptr && class_name[0] != '\0' &&
        !JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value)) {
        uint64_t receiver_handle = UINT64_MAX;
        if (ParsePointerValue(ctx, receiver_handle_value, &receiver_handle) && receiver_handle == 0u) {
            is_class_wrapper = true;
        }
    }

    uint64_t loader_handle = 0u;
    if (is_class_wrapper &&
        !JS_IsUndefined(loader_handle_value) &&
        !JS_IsNull(loader_handle_value) &&
        !ParsePointerValue(ctx, loader_handle_value, &loader_handle)) {
        is_class_wrapper = false;
    }

    if (!is_class_wrapper) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(
            ctx, "Java.Env.isInstanceOf requires a Java object and class wrapper");
    }

    uint64_t env_ptr = 0u;
    bool result = false;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    const bool ok = QueryJavaEnvIsInstanceOf(
        env_ptr, object_value.object_handle, class_name, loader_handle, &result, &error_message);
    JS_FreeCString(ctx, class_name);
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, result ? 1 : 0);
}

JSValue JsJavaEnvGetObjectRefType(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv) {
    constexpr uint32_t kJniInvalidRefType = 0u;
    constexpr uint32_t kJniLocalRefType = 1u;
    constexpr uint32_t kJniGlobalRefType = 2u;
    constexpr uint32_t kJniWeakGlobalRefType = 3u;

    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.Env.getObjectRefType requires a Java object");
    }

    std::string error_message;
    JavaJsValue object_value = {};
    if (!ParseJavaJsValue(ctx, argv[0], &object_value, &error_message) ||
        object_value.kind != JavaJsValueKind::kObject ||
        object_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.Env.getObjectRefType requires a Java object");
    }

    uint64_t env_ptr = 0u;
    uint32_t ref_type = kJniInvalidRefType;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvGetObjectRefType(
            env_ptr, object_value.object_handle, &ref_type, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const char* type_name = "invalid";
    switch (ref_type) {
        case kJniLocalRefType:
            type_name = "local";
            break;
        case kJniGlobalRefType:
            type_name = "global";
            break;
        case kJniWeakGlobalRefType:
            type_name = "weak-global";
            break;
        case kJniInvalidRefType:
        default:
            break;
    }

    return JS_NewString(ctx, type_name);
}

JSValue JsJavaEnvGetSuperclass(JSContext* ctx,
                               JSValueConst this_val,
                               int argc,
                               JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.Env.getSuperclass requires a Java class wrapper");
    }

    std::string class_name;
    uint64_t loader_handle = 0u;
    if (!ParseJavaClassWrapperInfo(ctx, argv[0], &class_name, &loader_handle)) {
        return JS_ThrowTypeError(ctx, "Java.Env.getSuperclass requires a Java class wrapper");
    }

    uint64_t env_ptr = 0u;
    bool has_superclass = false;
    std::string superclass_name;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvGetSuperclass(
            env_ptr, class_name.c_str(), loader_handle, &has_superclass, &superclass_name, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!has_superclass) {
        return JS_NULL;
    }

    return CreateJavaUseWrapper(ctx, superclass_name.c_str(), 0u, loader_handle, false);
}

JSValue JsJavaEnvIsAssignableFrom(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java.Env.isAssignableFrom requires two Java class wrappers");
    }

    std::string target_class_name;
    uint64_t target_loader_handle = 0u;
    std::string source_class_name;
    uint64_t source_loader_handle = 0u;
    if (!ParseJavaClassWrapperInfo(ctx, argv[0], &target_class_name, &target_loader_handle) ||
        !ParseJavaClassWrapperInfo(ctx, argv[1], &source_class_name, &source_loader_handle)) {
        return JS_ThrowTypeError(ctx, "Java.Env.isAssignableFrom requires two Java class wrappers");
    }

    uint64_t env_ptr = 0u;
    bool result = false;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvIsAssignableFrom(env_ptr,
                                      target_class_name.c_str(),
                                      target_loader_handle,
                                      source_class_name.c_str(),
                                      source_loader_handle,
                                      &result,
                                      &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, result ? 1 : 0);
}

JSValue JsJavaEnvNewStringUtf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java.Env.newStringUtf requires a string");
    }

    const char* utf8_text = JS_ToCString(ctx, argv[0]);
    if (utf8_text == nullptr) {
        return JS_EXCEPTION;
    }

    uint64_t env_ptr = 0u;
    uint64_t string_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        JS_FreeCString(ctx, utf8_text);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    const bool ok = QueryJavaEnvNewStringUtf(env_ptr, utf8_text, &string_ptr, &error_message);
    JS_FreeCString(ctx, utf8_text);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

#if defined(__ANDROID__)
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    RegisterOwnedJavaHandleLocked(state, string_ptr);
#endif

    return MakeNativePointer(ctx, string_ptr);
}

JSValue JsJavaEnvGetStringUtfChars(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.getStringUtfChars requires a non-null jstring pointer");
    }

    uint64_t jstring_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[0], &jstring_ptr) || jstring_ptr == 0u) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.getStringUtfChars requires a non-null jstring pointer");
    }

    uint64_t env_ptr = 0u;
    uint64_t chars_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvGetStringUtfChars(env_ptr, jstring_ptr, &chars_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, chars_ptr);
}

JSValue JsJavaEnvReleaseStringUtfChars(JSContext* ctx,
                                       JSValueConst this_val,
                                       int argc,
                                       JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 2) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.releaseStringUtfChars requires non-null jstring and cstring pointers");
    }

    uint64_t jstring_ptr = 0u;
    uint64_t chars_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[0], &jstring_ptr) || jstring_ptr == 0u ||
        !ParsePointerValue(ctx, argv[1], &chars_ptr) || chars_ptr == 0u) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.releaseStringUtfChars requires non-null jstring and cstring pointers");
    }

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvReleaseStringUtfChars(env_ptr, jstring_ptr, chars_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, 1);
}

JSValue JsJavaEnvNewGlobalRef(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.Env.newGlobalRef requires a Java object");
    }

    JavaJsValue object_value = {};
    std::string error_message;
    if (!ParseJavaJsValue(ctx, argv[0], &object_value, &error_message) ||
        object_value.kind != JavaJsValueKind::kObject ||
        object_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.Env.newGlobalRef requires a Java object");
    }

    uint64_t env_ptr = 0u;
    uint64_t ref_ptr = 0u;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvNewGlobalRef(env_ptr, object_value.object_handle, &ref_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, ref_ptr);
}

JSValue JsJavaEnvDeleteGlobalRef(JSContext* ctx,
                                 JSValueConst this_val,
                                 int argc,
                                 JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.deleteGlobalRef requires a non-null global reference pointer");
    }

    uint64_t ref_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[0], &ref_ptr) || ref_ptr == 0u) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.deleteGlobalRef requires a non-null global reference pointer");
    }

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvDeleteGlobalRef(env_ptr, ref_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, 1);
}

JSValue JsJavaEnvNewWeakGlobalRef(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.Env.newWeakGlobalRef requires a Java object");
    }

    JavaJsValue object_value = {};
    std::string error_message;
    if (!ParseJavaJsValue(ctx, argv[0], &object_value, &error_message) ||
        object_value.kind != JavaJsValueKind::kObject ||
        object_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.Env.newWeakGlobalRef requires a Java object");
    }

    uint64_t env_ptr = 0u;
    uint64_t ref_ptr = 0u;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvNewWeakGlobalRef(
            env_ptr, object_value.object_handle, &ref_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, ref_ptr);
}

JSValue JsJavaEnvDeleteWeakGlobalRef(JSContext* ctx,
                                     JSValueConst this_val,
                                     int argc,
                                     JSValueConst* argv) {
    uint64_t wrapper_env_ptr = 0u;
    if (!ParseJavaEnvWrapperHandle(ctx, this_val, &wrapper_env_ptr)) {
        return JS_EXCEPTION;
    }
    (void)wrapper_env_ptr;

    if (argc < 1) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.deleteWeakGlobalRef requires a non-null weak global reference pointer");
    }

    uint64_t ref_ptr = 0u;
    if (!ParsePointerValue(ctx, argv[0], &ref_ptr) || ref_ptr == 0u) {
        return JS_ThrowTypeError(
            ctx, "Java.Env.deleteWeakGlobalRef requires a non-null weak global reference pointer");
    }

    uint64_t env_ptr = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!QueryJavaEnvDeleteWeakGlobalRef(env_ptr, ref_ptr, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return JS_NewBool(ctx, 1);
}

JSValue JsJavaUpdateClassLoader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
#if defined(__ANDROID__)
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java._updateClassLoader requires a Java object");
    }

    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JavaJsValue loader_value = {};
    if (!ParseJavaJsValue(ctx, argv[0], &loader_value, &error_message)) {
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }
    if (loader_value.kind != JavaJsValueKind::kObject || loader_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java._updateClassLoader requires a Java object");
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        return JS_FALSE;
    }

    const bool updated = JavaHookLoaderResolver::UpdateApplicationClassLoader(
        jenv.get(), reinterpret_cast<jobject>(loader_value.object_handle));
    return JS_NewBool(ctx, updated ? 1 : 0);
#else
    (void)argc;
    (void)argv;
    return JS_FALSE;
#endif
}

JSValue JsJavaIsClassLoaderReady(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
#if defined(__ANDROID__)
    if (nook::gadget::ShouldDeferJavaReadyChecksForOnLoadWait()) {
        return JS_FALSE;
    }
    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        return JS_FALSE;
    }

    const bool ready = JavaHookLoaderResolver::IsApplicationClassLoaderReady(jenv.get());
    return JS_NewBool(ctx, ready ? 1 : 0);
#else
    return JS_FALSE;
#endif
}

JSValue JsJavaIsApplicationReady(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
#if defined(__ANDROID__)
    if (nook::gadget::ShouldDeferJavaReadyChecksForOnLoadWait()) {
        return JS_FALSE;
    }
    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        return JS_FALSE;
    }

    const bool ready = JavaHookLoaderResolver::IsCurrentApplicationReady(jenv.get());
    return JS_NewBool(ctx, ready ? 1 : 0);
#else
    return JS_FALSE;
#endif
}

JSValue JsJavaIsLifecycleReady(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
#if defined(__ANDROID__)
    if (nook::gadget::ShouldDeferJavaReadyChecksForOnLoadWait()) {
        return JS_FALSE;
    }
    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        return JS_FALSE;
    }

    const bool ready = JavaHookLoaderResolver::IsApplicationLifecycleReady(jenv.get());
    return JS_NewBool(ctx, ready ? 1 : 0);
#else
    return JS_FALSE;
#endif
}

#if defined(__ANDROID__)
bool EnsureJavaHookReadyForJs(std::string* error_message) {
    const NookStatus status = NookJavaHookInitialize();
    if (status == NOOK_STATUS_OK) {
        return true;
    }
    SetError(error_message, "JavaHook initialize failed");
    return false;
}

void ReleaseTemporaryJavaChooseMatches(const std::vector<JavaJsValue>& matches) {
    if (matches.empty()) {
        return;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        return;
    }

    JNIEnv* env = jenv.get();
    for (const JavaJsValue& match : matches) {
        if (match.kind != JavaJsValueKind::kObject || match.object_handle == 0u) {
            continue;
        }

        env->DeleteGlobalRef(reinterpret_cast<jobject>(match.object_handle));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
}
#endif

JSValue JsJavaDeopt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
#if defined(__ANDROID__)
    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    bool invalidated = false;
    const bool ok = ::JavaHook::DeoptimizeJit(&invalidated);
    DeoptDiagnostics diagnostics = {};
    ::JavaHook::GetLastDeoptDiagnostics(&diagnostics);
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, ok ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "invalidated", JS_NewBool(ctx, invalidated ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "symbolsAvailable", JS_NewBool(ctx, diagnostics.symbolsAvailable ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "runtimeAvailable", JS_NewBool(ctx, diagnostics.runtimeAvailable ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "reason", JS_NewString(ctx, diagnostics.reason.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "scanStart", JS_NewFloat64(ctx, static_cast<double>(diagnostics.scanStart))) < 0 ||
        JS_SetPropertyStr(ctx, result, "scanEnd", JS_NewFloat64(ctx, static_cast<double>(diagnostics.scanEnd))) < 0 ||
        JS_SetPropertyStr(ctx, result, "candidatesSeen", JS_NewFloat64(ctx, static_cast<double>(diagnostics.candidatesSeen))) < 0 ||
        JS_SetPropertyStr(ctx, result, "readableCandidates", JS_NewFloat64(ctx, static_cast<double>(diagnostics.readableCandidates))) < 0 ||
        JS_SetPropertyStr(ctx, result, "runtimeOffset", JS_NewFloat64(ctx, static_cast<double>(diagnostics.runtimeOffset))) < 0 ||
        JS_SetPropertyStr(ctx, result, "runtimeAddress", MakeNativePointer(ctx, diagnostics.runtimeAddress)) < 0 ||
        JS_SetPropertyStr(ctx, result, "codeCacheAddress", MakeNativePointer(ctx, diagnostics.codeCacheAddress)) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
#else
    return JS_FALSE;
#endif
}

JSValue JsJavaSetForcedInterpretOnly(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
#if defined(__ANDROID__)
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java._setForcedInterpretOnly requires a boolean");
    }

    const int enabled = JS_ToBool(ctx, argv[0]);
    if (enabled < 0) {
        return JS_ThrowTypeError(ctx, "Java._setForcedInterpretOnly enable must be a boolean");
    }

    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    bool changed = false;
    const bool ok = ::JavaHook::SetForcedInterpretOnly(enabled != 0, &changed);
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, ok ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "enabled", JS_NewBool(ctx, enabled != 0 ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "changed", JS_NewBool(ctx, changed ? 1 : 0)) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
#else
    (void)argc;
    (void)argv;
    return JS_FALSE;
#endif
}

JSValue JsJavaArtRouterDebug(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
#if defined(__ANDROID__)
    std::string error_message;
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    uint64_t last_x0 = 0u;
    uint64_t miss_count = 0u;
    ::JavaHook::GetArtRouterDebug(&last_x0, &miss_count);
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "lastX0", MakeNativePointer(ctx, last_x0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "missCount", JS_NewFloat64(ctx, static_cast<double>(miss_count))) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
#else
    return JS_FALSE;
#endif
}

JSValue MakeJavaJsValue(JSContext* ctx, const JavaJsValue& value) {
    switch (value.kind) {
        case JavaJsValueKind::kUndefined:
            return JS_UNDEFINED;
        case JavaJsValueKind::kString:
            return JS_NewString(ctx, value.string_value.c_str());
        case JavaJsValueKind::kBoolean:
            return JS_NewBool(ctx, value.bool_value ? 1 : 0);
        case JavaJsValueKind::kInt32:
            return JS_NewInt32(ctx, value.int_value);
        case JavaJsValueKind::kInt64:
            return JS_NewFloat64(ctx, static_cast<double>(value.int64_value));
        case JavaJsValueKind::kFloat:
            return JS_NewFloat64(ctx, static_cast<double>(value.float_value));
        case JavaJsValueKind::kDouble:
            return JS_NewFloat64(ctx, value.double_value);
        case JavaJsValueKind::kObject: {
            if (value.object_handle == 0u) {
                return JS_NULL;
            }
            const char* class_name =
                value.object_class_name.empty() ? "java.lang.Object" : value.object_class_name.c_str();
            return CreateJavaUseWrapper(
                ctx, class_name, value.object_handle, 0u, value.object_handle_is_global);
        }
        case JavaJsValueKind::kArray: {
            JSValue array = JS_NewArray(ctx);
            if (JS_IsException(array)) {
                return array;
            }
            for (uint32_t i = 0u; i < value.array_elements.size(); ++i) {
                JSValue element = MakeJavaJsValue(ctx, value.array_elements[i]);
                if (JS_IsException(element) ||
                    JS_SetPropertyUint32(ctx, array, i, element) < 0) {
                    if (!JS_IsException(element)) {
                        JS_FreeValue(ctx, element);
                    }
                    JS_FreeValue(ctx, array);
                    return JS_EXCEPTION;
                }
            }
            if (JS_SetPropertyStr(ctx,
                                  array,
                                  kJavaArrayTypeProperty,
                                  JS_NewString(ctx, value.array_type_name.c_str())) < 0 ||
                JS_SetPropertyStr(ctx,
                                  array,
                                  "$className",
                                  JS_NewString(ctx, value.array_type_name.c_str())) < 0) {
                JS_FreeValue(ctx, array);
                return JS_EXCEPTION;
            }
            return array;
        }
    }
    return JS_UNDEFINED;
}

std::string NormalizeJavaArrayTypeName(const std::string& type_name) {
    if (type_name.empty()) {
        return {};
    }
    if (type_name.front() == '[') {
        return type_name;
    }
    if (type_name.size() >= 2u && type_name.compare(type_name.size() - 2u, 2u, "[]") == 0) {
        return type_name;
    }
    return type_name + "[]";
}

bool ParseJavaJsValue(JSContext* ctx,
                      JSValueConst value,
                      JavaJsValue* out_value,
                      std::string* error_message) {
    if (out_value == nullptr) {
        SetError(error_message, "java value output is required");
        return false;
    }
    *out_value = {};
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        out_value->kind = JavaJsValueKind::kUndefined;
        return true;
    }
    if (JS_IsBool(value)) {
        out_value->kind = JavaJsValueKind::kBoolean;
        out_value->bool_value = JS_ToBool(ctx, value) != 0;
        return true;
    }
    if (JS_IsString(value)) {
        const char* text = JS_ToCString(ctx, value);
        if (text == nullptr) {
            SetError(error_message, "read java string result failed");
            return false;
        }
        out_value->kind = JavaJsValueKind::kString;
        out_value->string_value = text;
        JS_FreeCString(ctx, text);
        return true;
    }
    if (JS_IsObject(value)) {
        JSValue array_type_value = JS_GetPropertyStr(ctx, value, kJavaArrayTypeProperty);
        if (JS_IsException(array_type_value)) {
            SetError(error_message, "read Java array type failed");
            return false;
        }
        if (!JS_IsUndefined(array_type_value) && !JS_IsNull(array_type_value)) {
            const char* array_type = JS_ToCString(ctx, array_type_value);
            if (array_type == nullptr) {
                JS_FreeValue(ctx, array_type_value);
                SetError(error_message, "Java.array type name must be a string");
                return false;
            }

            out_value->kind = JavaJsValueKind::kArray;
            out_value->array_type_name = NormalizeJavaArrayTypeName(array_type);
            JSValue array_class_name_value = JS_GetPropertyStr(ctx, value, "$className");
            if (JS_IsException(array_class_name_value)) {
                JS_FreeCString(ctx, array_type);
                JS_FreeValue(ctx, array_type_value);
                SetError(error_message, "read Java.array class name failed");
                return false;
            }
            if (!JS_IsUndefined(array_class_name_value) && !JS_IsNull(array_class_name_value)) {
                const char* array_class_name = JS_ToCString(ctx, array_class_name_value);
                if (array_class_name == nullptr) {
                    JS_FreeValue(ctx, array_class_name_value);
                    JS_FreeCString(ctx, array_type);
                    JS_FreeValue(ctx, array_type_value);
                    SetError(error_message, "Java.array class name must be a string");
                    return false;
                }
                const std::string normalized_class_name =
                    NormalizeJavaArrayTypeName(array_class_name);
                if (!normalized_class_name.empty()) {
                    out_value->array_type_name = normalized_class_name;
                }
                JS_FreeCString(ctx, array_class_name);
            }
            JS_FreeValue(ctx, array_class_name_value);
            JS_FreeCString(ctx, array_type);
            JS_FreeValue(ctx, array_type_value);
            if (out_value->array_type_name.empty()) {
                SetError(error_message, "Java.array type name must be non-empty");
                return false;
            }

            uint32_t count = 0u;
            if (!GetArrayLength(ctx, value, &count)) {
                SetError(error_message, "Java.array elements must be an array");
                return false;
            }
            out_value->array_elements.reserve(count);
            for (uint32_t index = 0u; index < count; ++index) {
                JSValue element_value = JS_GetPropertyUint32(ctx, value, index);
                if (JS_IsException(element_value)) {
                    SetError(error_message, "read Java.array element failed");
                    return false;
                }
                JavaJsValue parsed_element = {};
                const bool parsed =
                    ParseJavaJsValue(ctx, element_value, &parsed_element, error_message);
                JS_FreeValue(ctx, element_value);
                if (!parsed) {
                    return false;
                }
                out_value->array_elements.push_back(std::move(parsed_element));
            }
            return true;
        }
        JS_FreeValue(ctx, array_type_value);

        JSValue receiver_handle_value = JS_GetPropertyStr(ctx, value, kJavaReceiverHandleProperty);
        if (JS_IsException(receiver_handle_value)) {
            SetError(error_message, "read Java object receiver handle failed");
            return false;
        }
        if (JS_IsUndefined(receiver_handle_value)) {
            JS_FreeValue(ctx, receiver_handle_value);
            receiver_handle_value = JS_GetPropertyStr(ctx, value, kJavaObjectPointerProperty);
            if (JS_IsException(receiver_handle_value)) {
                SetError(error_message, "read Java object pointer failed");
                return false;
            }
        }
        if (!JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value)) {
            uint64_t receiver_handle = 0u;
            if (!ParsePointerValue(ctx, receiver_handle_value, &receiver_handle)) {
                JS_FreeValue(ctx, receiver_handle_value);
                SetError(error_message, "Java object pointer must be a pointer");
                return false;
            }
            JSValue class_name_value = JS_GetPropertyStr(ctx, value, "$className");
            if (JS_IsException(class_name_value)) {
                JS_FreeValue(ctx, receiver_handle_value);
                SetError(error_message, "read Java object class name failed");
                return false;
            }

            out_value->kind = JavaJsValueKind::kObject;
            out_value->object_handle = receiver_handle;
            const char* class_name = JS_ToCString(ctx, class_name_value);
            if (class_name != nullptr) {
                out_value->object_class_name = class_name;
                JS_FreeCString(ctx, class_name);
            }
            JS_FreeValue(ctx, class_name_value);
            JS_FreeValue(ctx, receiver_handle_value);
            return true;
        }
        JS_FreeValue(ctx, receiver_handle_value);
    }
    double double_value = 0.0;
    if (JS_ToFloat64(ctx, &double_value, value) == 0) {
        out_value->kind = JavaJsValueKind::kDouble;
        out_value->double_value = double_value;
        return true;
    }
    int32_t int_value = 0;
    if (JS_ToInt32(ctx, &int_value, value) == 0) {
        out_value->kind = JavaJsValueKind::kInt32;
        out_value->int_value = int_value;
        return true;
    }

    SetError(error_message, "unsupported Java JS value type");
    return false;
}

bool ParseJavaMethodMetadata(JSContext* ctx,
                             JSValueConst value,
                             JavaJsMethodRecord* out_record,
                             uint64_t* receiver_handle_out,
                             std::string* error_message) {
    if (out_record == nullptr || receiver_handle_out == nullptr) {
        SetError(error_message, "java method metadata outputs are required");
        return false;
    }
    *out_record = {};
    *receiver_handle_out = 0u;

    if (!JS_IsObject(value)) {
        SetError(error_message, "java method target must be an object");
        return false;
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, value, "$className");
    JSValue method_name_value = JS_GetPropertyStr(ctx, value, "$methodName");
    JSValue signature_value = JS_GetPropertyStr(ctx, value, "$signature");
    JSValue is_static_value = JS_GetPropertyStr(ctx, value, "$isStatic");
    JSValue receiver_handle_value = JS_GetPropertyStr(ctx, value, kJavaReceiverHandleProperty);
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, value, kJavaLoaderHandleProperty);
    if (JS_IsException(class_name_value) ||
        JS_IsException(method_name_value) ||
        JS_IsException(signature_value) ||
        JS_IsException(is_static_value) ||
        JS_IsException(receiver_handle_value) ||
        JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "read Java method metadata failed");
        return false;
    }

    const char* class_name = JS_ToCString(ctx, class_name_value);
    const char* method_name = JS_ToCString(ctx, method_name_value);
    const char* signature = nullptr;
    if (!JS_IsUndefined(signature_value) && !JS_IsNull(signature_value)) {
        signature = JS_ToCString(ctx, signature_value);
    }
    if (class_name == nullptr || method_name == nullptr ||
        ((!JS_IsUndefined(signature_value) && !JS_IsNull(signature_value)) && signature == nullptr)) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        if (method_name != nullptr) {
            JS_FreeCString(ctx, method_name);
        }
        if (signature != nullptr) {
            JS_FreeCString(ctx, signature);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "Java method wrapper metadata is invalid");
        return false;
    }

    out_record->class_name = class_name;
    out_record->method_name = method_name;
    if (out_record->method_name == "$init") {
        out_record->method_name = "<init>";
    }
    out_record->signature = signature != nullptr ? signature : "";
    out_record->is_static = JS_ToBool(ctx, is_static_value) != 0;
    if (!JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value) &&
        !ParsePointerValue(ctx, receiver_handle_value, receiver_handle_out)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        if (signature != nullptr) {
            JS_FreeCString(ctx, signature);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "Java method receiver handle is invalid");
        return false;
    }
    if (!JS_IsUndefined(loader_handle_value) && !JS_IsNull(loader_handle_value) &&
        !ParsePointerValue(ctx, loader_handle_value, &out_record->loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        if (signature != nullptr) {
            JS_FreeCString(ctx, signature);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "Java method loader handle is invalid");
        return false;
    }

    JS_FreeCString(ctx, class_name);
    JS_FreeCString(ctx, method_name);
    if (signature != nullptr) {
        JS_FreeCString(ctx, signature);
    }
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, method_name_value);
    JS_FreeValue(ctx, signature_value);
    JS_FreeValue(ctx, is_static_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    return true;
}

bool ParseJavaFieldMetadata(JSContext* ctx,
                            JSValueConst value,
                            JavaJsFieldRecord* out_record,
                            uint64_t* receiver_handle_out,
                            std::string* error_message) {
    if (out_record == nullptr || receiver_handle_out == nullptr) {
        SetError(error_message, "java field metadata outputs are required");
        return false;
    }
    *out_record = {};
    *receiver_handle_out = 0u;

    if (!JS_IsObject(value)) {
        SetError(error_message, "java field target must be an object");
        return false;
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, value, "$className");
    JSValue field_name_value = JS_GetPropertyStr(ctx, value, "$fieldName");
    JSValue reflected_field_name_value = JS_GetPropertyStr(ctx, value, "$reflectedFieldName");
    JSValue signature_value = JS_GetPropertyStr(ctx, value, "$signature");
    JSValue is_static_value = JS_GetPropertyStr(ctx, value, "$isStatic");
    JSValue uses_declared_field_lookup_value =
        JS_GetPropertyStr(ctx, value, "$usesDeclaredFieldLookup");
    JSValue receiver_handle_value = JS_GetPropertyStr(ctx, value, kJavaReceiverHandleProperty);
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, value, kJavaLoaderHandleProperty);
    if (JS_IsException(class_name_value) ||
        JS_IsException(field_name_value) ||
        JS_IsException(reflected_field_name_value) ||
        JS_IsException(signature_value) ||
        JS_IsException(is_static_value) ||
        JS_IsException(uses_declared_field_lookup_value) ||
        JS_IsException(receiver_handle_value) ||
        JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, field_name_value);
        JS_FreeValue(ctx, reflected_field_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, uses_declared_field_lookup_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "read Java field metadata failed");
        return false;
    }

    const char* class_name = JS_ToCString(ctx, class_name_value);
    const char* field_name = JS_ToCString(ctx, field_name_value);
    const char* reflected_field_name =
        JS_IsUndefined(reflected_field_name_value) || JS_IsNull(reflected_field_name_value)
            ? nullptr
            : JS_ToCString(ctx, reflected_field_name_value);
    const char* signature = JS_ToCString(ctx, signature_value);
    if (class_name == nullptr ||
        field_name == nullptr ||
        signature == nullptr ||
        ((!JS_IsUndefined(reflected_field_name_value) && !JS_IsNull(reflected_field_name_value)) &&
         reflected_field_name == nullptr)) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        if (field_name != nullptr) {
            JS_FreeCString(ctx, field_name);
        }
        if (reflected_field_name != nullptr) {
            JS_FreeCString(ctx, reflected_field_name);
        }
        if (signature != nullptr) {
            JS_FreeCString(ctx, signature);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, field_name_value);
        JS_FreeValue(ctx, reflected_field_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, uses_declared_field_lookup_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "Java field metadata is invalid");
        return false;
    }

    out_record->class_name = class_name;
    out_record->field_name = field_name;
    if (reflected_field_name != nullptr) {
        out_record->reflected_field_name = reflected_field_name;
    }
    out_record->signature = signature;
    out_record->is_static = JS_ToBool(ctx, is_static_value) != 0;
    out_record->uses_declared_field_lookup =
        JS_ToBool(ctx, uses_declared_field_lookup_value) != 0;
    if (!JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value)) {
        if (!ParsePointerValue(ctx, receiver_handle_value, receiver_handle_out)) {
            JS_FreeCString(ctx, class_name);
            JS_FreeCString(ctx, field_name);
            if (reflected_field_name != nullptr) {
                JS_FreeCString(ctx, reflected_field_name);
            }
            JS_FreeCString(ctx, signature);
            JS_FreeValue(ctx, class_name_value);
            JS_FreeValue(ctx, field_name_value);
            JS_FreeValue(ctx, reflected_field_name_value);
            JS_FreeValue(ctx, signature_value);
            JS_FreeValue(ctx, is_static_value);
            JS_FreeValue(ctx, uses_declared_field_lookup_value);
            JS_FreeValue(ctx, receiver_handle_value);
            JS_FreeValue(ctx, loader_handle_value);
            SetError(error_message, "Java field receiver handle is invalid");
            return false;
        }
    }
    if (!JS_IsUndefined(loader_handle_value) && !JS_IsNull(loader_handle_value) &&
        !ParsePointerValue(ctx, loader_handle_value, &out_record->loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, field_name);
        if (reflected_field_name != nullptr) {
            JS_FreeCString(ctx, reflected_field_name);
        }
        JS_FreeCString(ctx, signature);
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, field_name_value);
        JS_FreeValue(ctx, reflected_field_name_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, uses_declared_field_lookup_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        SetError(error_message, "Java field loader handle is invalid");
        return false;
    }

    JS_FreeCString(ctx, class_name);
    JS_FreeCString(ctx, field_name);
    if (reflected_field_name != nullptr) {
        JS_FreeCString(ctx, reflected_field_name);
    }
    JS_FreeCString(ctx, signature);
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, field_name_value);
    JS_FreeValue(ctx, reflected_field_name_value);
    JS_FreeValue(ctx, signature_value);
    JS_FreeValue(ctx, is_static_value);
    JS_FreeValue(ctx, uses_declared_field_lookup_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    return true;
}

JSValue JsJavaResolveField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Java field resolution requires class, field, and static flag");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    const char* field_name = JS_ToCString(ctx, argv[1]);
    if (class_name == nullptr || field_name == nullptr) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        if (field_name != nullptr) {
            JS_FreeCString(ctx, field_name);
        }
        return JS_ThrowTypeError(ctx, "Java field resolution arguments must be strings");
    }

    const bool is_static = JS_ToBool(ctx, argv[2]) != 0;
    uint64_t loader_handle = 0u;
    if (argc >= 4 &&
        !JS_IsUndefined(argv[3]) &&
        !JS_IsNull(argv[3]) &&
        !ParsePointerValue(ctx, argv[3], &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, field_name);
        return JS_ThrowTypeError(ctx, "Java field loader handle is invalid");
    }
    RuntimeState& state = GetRuntimeState();
    JavaJsFieldRecord record = {};
    std::string error_message;
    const bool resolved = ResolveJavaField(class_name,
                                           field_name,
                                           loader_handle,
                                           is_static,
                                           state.java_hook_installer_dependencies,
                                           &record,
                                           &error_message);
    JS_FreeCString(ctx, class_name);
    JS_FreeCString(ctx, field_name);
    if (!resolved) {
        return JS_NULL;
    }

    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "className", JS_NewString(ctx, record.class_name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "fieldName", JS_NewString(ctx, record.field_name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "reflectedFieldName", JS_NewString(ctx, record.reflected_field_name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "signature", JS_NewString(ctx, record.signature.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "isStatic", JS_NewBool(ctx, record.is_static ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, result, "usesDeclaredFieldLookup", JS_NewBool(ctx, record.uses_declared_field_lookup ? 1 : 0)) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
}

JSValue JsJavaReadField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java field read requires a field wrapper");
    }

    JavaJsFieldRecord record = {};
    uint64_t receiver_handle = 0u;
    std::string error_message;
    if (!ParseJavaFieldMetadata(ctx, argv[0], &record, &receiver_handle, &error_message)) {
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    RuntimeState& state = GetRuntimeState();
    JavaJsValue result = {};
    if (!ReadJavaField(record,
                       receiver_handle,
                       state.java_hook_installer_dependencies,
                       &result,
                       &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    return MakeJavaJsValue(ctx, result);
}

JSValue JsJavaWriteField(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java field write requires field wrapper and value");
    }

    JavaJsFieldRecord record = {};
    uint64_t receiver_handle = 0u;
    std::string error_message;
    if (!ParseJavaFieldMetadata(ctx, argv[0], &record, &receiver_handle, &error_message)) {
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    JavaJsValue value = {};
    if (!ParseJavaJsValue(ctx, argv[1], &value, &error_message)) {
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    RuntimeState& state = GetRuntimeState();
    if (!WriteJavaField(record,
                        receiver_handle,
                        state.java_hook_installer_dependencies,
                        value,
                        &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    return JS_UNDEFINED;
}

JSValue JsJavaCallOriginal(JSContext* ctx,
                           JSValueConst this_val,
                           int argc,
                           JSValueConst* argv,
                           int magic,
                           JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint32_t hook_id = 0u;
    if (func_data == nullptr || JS_ToUint32(ctx, &hook_id, func_data[0]) < 0 || hook_id == 0u) {
        return JS_ThrowInternalError(ctx, "Java callOriginal metadata is invalid");
    }

    std::vector<JavaJsValue> args;
    args.reserve(static_cast<size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        JavaJsValue value = {};
        std::string error_message;
        if (!ParseJavaJsValue(ctx, argv[index], &value, &error_message)) {
            return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
        }
        args.push_back(std::move(value));
    }

    RuntimeState& state = GetRuntimeState();
    JavaJsValue result = {};
    std::string error_message;
    if (!CallOriginalJavaJsHook(hook_id,
                                args.data(),
                                args.size(),
                                state.java_hook_installer_dependencies,
                                &result,
                                &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    return MakeJavaJsValue(ctx, result);
}

void AppendJavaInvokeTypeCandidate(std::vector<std::string>* candidates, const char* type_name) {
    if (candidates == nullptr || type_name == nullptr) {
        return;
    }
    for (const std::string& existing : *candidates) {
        if (existing == type_name) {
            return;
        }
    }
    candidates->emplace_back(type_name);
}

bool CollectJavaInvokeArgumentTypeCandidates(JSContext* ctx,
                                             JSValueConst js_value,
                                             const JavaJsValue& value,
                                             std::vector<std::string>* candidates,
                                             std::string* error_message) {
    if (candidates == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Java invoke candidate output is null";
        }
        return false;
    }
    candidates->clear();

    if (value.kind == JavaJsValueKind::kUndefined) {
        AppendJavaInvokeTypeCandidate(candidates, kJavaInvokeNullTypeCandidate);
        return true;
    }
    if (JS_IsBool(js_value)) {
        AppendJavaInvokeTypeCandidate(candidates, "boolean");
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.Boolean");
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
        return true;
    }
    if (JS_IsString(js_value)) {
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.String");
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.CharSequence");
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
        return true;
    }
    if (JS_IsObject(js_value) && value.kind == JavaJsValueKind::kObject &&
        !value.object_class_name.empty()) {
        candidates->push_back(value.object_class_name);
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
        return true;
    }
    if (value.kind == JavaJsValueKind::kArray && !value.array_type_name.empty()) {
        candidates->push_back(value.array_type_name);
        AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
        return true;
    }

    switch (value.kind) {
        case JavaJsValueKind::kDouble: {
            double numeric = value.double_value;
            AppendJavaInvokeTypeCandidate(candidates, "double");
            if (std::isfinite(numeric) &&
                numeric >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
                numeric <= static_cast<double>(std::numeric_limits<int32_t>::max()) &&
                std::floor(numeric) == numeric) {
                AppendJavaInvokeTypeCandidate(candidates, "int");
                AppendJavaInvokeTypeCandidate(candidates, "long");
            }
            AppendJavaInvokeTypeCandidate(candidates, "float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Double");
            if (std::isfinite(numeric) &&
                numeric >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
                numeric <= static_cast<double>(std::numeric_limits<int32_t>::max()) &&
                std::floor(numeric) == numeric) {
                AppendJavaInvokeTypeCandidate(candidates, "java.lang.Integer");
                AppendJavaInvokeTypeCandidate(candidates, "java.lang.Long");
            }
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Number");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
            return true;
        }
        case JavaJsValueKind::kInt32:
            AppendJavaInvokeTypeCandidate(candidates, "int");
            AppendJavaInvokeTypeCandidate(candidates, "long");
            AppendJavaInvokeTypeCandidate(candidates, "float");
            AppendJavaInvokeTypeCandidate(candidates, "double");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Integer");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Long");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Double");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Number");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
            return true;
        case JavaJsValueKind::kInt64:
            AppendJavaInvokeTypeCandidate(candidates, "long");
            AppendJavaInvokeTypeCandidate(candidates, "double");
            AppendJavaInvokeTypeCandidate(candidates, "float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Long");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Double");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Number");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
            return true;
        case JavaJsValueKind::kFloat:
            AppendJavaInvokeTypeCandidate(candidates, "float");
            AppendJavaInvokeTypeCandidate(candidates, "double");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Float");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Double");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Number");
            AppendJavaInvokeTypeCandidate(candidates, "java.lang.Object");
            return true;
        default:
            if (error_message != nullptr) {
                *error_message = "Java invoke cannot infer overload for argument";
            }
            return false;
    }
}

bool ResolveJavaMethodSignatureFromCandidates(
    const JavaJsMethodRecord& record,
    const std::vector<std::vector<std::string>>& argument_type_candidates,
    const JavaJsHookInstallerDependencies& dependencies,
    std::string* signature,
    std::string* error_message) {
    if (signature == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Java invoke signature output is null";
        }
        return false;
    }

    std::vector<std::string> selected;
    selected.resize(argument_type_candidates.size());
    std::string last_error;

    const auto try_resolve = [&](const auto& self, size_t index) -> bool {
        if (index == argument_type_candidates.size()) {
            std::string resolved_signature;
            std::string resolve_error;
            if (ResolveJavaMethodSignature(record.class_name,
                                           record.method_name,
                                           selected,
                                           record.loader_handle,
                                           record.is_static,
                                           dependencies,
                                           &resolved_signature,
                                           &resolve_error)) {
                *signature = resolved_signature;
                return true;
            }
            if (!resolve_error.empty()) {
                last_error = resolve_error;
            }
            return false;
        }

        for (const std::string& candidate : argument_type_candidates[index]) {
            selected[index] = candidate;
            if (self(self, index + 1u)) {
                return true;
            }
        }
        return false;
    };

    if (try_resolve(try_resolve, 0u)) {
        return true;
    }

    if (error_message != nullptr) {
        std::ostringstream stream;
        stream << "Java invoke overload resolution failed"
               << " class=" << record.class_name
               << " method=" << record.method_name
               << " static=" << (record.is_static ? "true" : "false")
               << " candidates=[";
        for (size_t i = 0; i < argument_type_candidates.size(); ++i) {
            if (i > 0u) {
                stream << ";";
            }
            stream << "[";
            for (size_t j = 0; j < argument_type_candidates[i].size(); ++j) {
                if (j > 0u) {
                    stream << ",";
                }
                stream << argument_type_candidates[i][j];
            }
            stream << "]";
        }
        stream << "]";
        if (!last_error.empty()) {
            stream << " lastError=" << last_error;
        } else {
            stream << " lastError=Java invoke failed to resolve overload";
        }
        *error_message = stream.str();
    }
    return false;
}

JSValue JsJavaInvoke(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java invoke requires method wrapper");
    }

    JavaJsMethodRecord record = {};
    uint64_t receiver_handle = 0u;
    std::string error_message;
    if (!ParseJavaMethodMetadata(ctx, argv[0], &record, &receiver_handle, &error_message)) {
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    std::vector<JavaJsValue> args;
    args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    std::vector<std::vector<std::string>> argument_type_candidates;
    argument_type_candidates.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        JavaJsValue value = {};
        if (!ParseJavaJsValue(ctx, argv[index], &value, &error_message)) {
            return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
        }
        args.push_back(value);

        if (record.signature.empty()) {
            std::vector<std::string> candidates;
            if (!CollectJavaInvokeArgumentTypeCandidates(
                    ctx, argv[index], value, &candidates, &error_message)) {
                return JS_ThrowTypeError(ctx, "Java invoke cannot infer overload for argument");
            }
            argument_type_candidates.push_back(std::move(candidates));
        }
    }

    RuntimeState& state = GetRuntimeState();
    JSValue hook_id_value = JS_GetPropertyStr(ctx, argv[0], "__nookJavaHookId");
    if (JS_IsException(hook_id_value)) {
        return JS_ThrowInternalError(ctx, "read Java hook metadata failed");
    }
    uint32_t hook_id = 0u;
    const bool has_hook_id =
        !JS_IsUndefined(hook_id_value) &&
        !JS_IsNull(hook_id_value) &&
        JS_ToUint32(ctx, &hook_id, hook_id_value) >= 0 &&
        hook_id != 0u;
    JS_FreeValue(ctx, hook_id_value);

    if (has_hook_id) {
        JavaJsValue result = {};
        if (!CallOriginalJavaJsHook(hook_id,
                                    args.data(),
                                    args.size(),
                                    state.java_hook_installer_dependencies,
                                    &result,
                                    &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        return MakeJavaJsValue(ctx, result);
    }

    if (record.signature.empty()) {
        bool resolved = ResolveJavaMethodSignatureFromCandidates(record,
                                                                 argument_type_candidates,
                                                                 state.java_hook_installer_dependencies,
                                                                 &record.signature,
                                                                 &error_message);
        if (!resolved && receiver_handle == 0u && !record.is_static) {
            JavaJsMethodRecord static_record = record;
            static_record.is_static = true;
            resolved = ResolveJavaMethodSignatureFromCandidates(static_record,
                                                                argument_type_candidates,
                                                                state.java_hook_installer_dependencies,
                                                                &static_record.signature,
                                                                &error_message);
            if (resolved) {
                record.is_static = true;
                record.signature = static_record.signature;
            }
        }
        if (!resolved) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }
    JavaJsValue result = {};
    if (!InvokeJavaMethod(record,
                          receiver_handle,
                          state.java_hook_installer_dependencies,
                          args.data(),
                          args.size(),
                          &result,
                          &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (result.kind == JavaJsValueKind::kObject &&
        result.object_handle != 0u &&
        !result.object_handle_is_global) {
        uint64_t retained_handle = 0u;
        if (!RetainJavaObject(result.object_handle,
                              state.java_hook_installer_dependencies,
                              &retained_handle,
                              &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        result.object_handle = retained_handle;
        result.object_handle_is_global = true;
    }
    if (result.kind == JavaJsValueKind::kObject &&
        result.object_handle != 0u &&
        record.loader_handle != 0u) {
        const char* wrapper_class_name =
            result.object_class_name.empty() ? "java.lang.Object" : result.object_class_name.c_str();
        return CreateJavaUseWrapper(
            ctx, wrapper_class_name, result.object_handle, record.loader_handle, true);
    }
    return MakeJavaJsValue(ctx, result);
}

JSValue JsJavaInstallImplementation(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java implementation install requires method and function");
    }
    if (!JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java implementation target must be an object");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "Java implementation must be a function");
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, argv[0], "$className");
    if (JS_IsException(class_name_value)) {
        return class_name_value;
    }
    JSValue method_name_value = JS_GetPropertyStr(ctx, argv[0], "$methodName");
    if (JS_IsException(method_name_value)) {
        JS_FreeValue(ctx, class_name_value);
        return method_name_value;
    }
    JSValue is_static_value = JS_GetPropertyStr(ctx, argv[0], "$isStatic");
    if (JS_IsException(is_static_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        return is_static_value;
    }
    JSValue signature_value = JS_GetPropertyStr(ctx, argv[0], "$signature");
    if (JS_IsException(signature_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        return signature_value;
    }
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, argv[0], kJavaLoaderHandleProperty);
    if (JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, signature_value);
        return loader_handle_value;
    }

    const char* class_name = JS_ToCString(ctx, class_name_value);
    const char* method_name = JS_ToCString(ctx, method_name_value);
    const char* signature_cstr = nullptr;
    if (!JS_IsUndefined(signature_value) && !JS_IsNull(signature_value)) {
        signature_cstr = JS_ToCString(ctx, signature_value);
    }
    if (class_name == nullptr || method_name == nullptr ||
        ((!JS_IsUndefined(signature_value) && !JS_IsNull(signature_value)) && signature_cstr == nullptr)) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        if (method_name != nullptr) {
            JS_FreeCString(ctx, method_name);
        }
        if (signature_cstr != nullptr) {
            JS_FreeCString(ctx, signature_cstr);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(ctx, "Java method wrapper metadata is invalid");
    }

    const bool is_static = JS_ToBool(ctx, is_static_value) != 0;
    uint64_t loader_handle = 0u;
    if (!JS_IsUndefined(loader_handle_value) &&
        !JS_IsNull(loader_handle_value) &&
        !ParsePointerValue(ctx, loader_handle_value, &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        if (signature_cstr != nullptr) {
            JS_FreeCString(ctx, signature_cstr);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(ctx, "Java method loader handle is invalid");
    }
    std::string error_message;

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        if (signature_cstr != nullptr) {
            JS_FreeCString(ctx, signature_cstr);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowInternalError(ctx, "Java implementation install must run while loading a script");
    }

#if defined(__ANDROID__)
    if (!EnsureJavaHookReadyForJs(&error_message)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        if (signature_cstr != nullptr) {
            JS_FreeCString(ctx, signature_cstr);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, method_name_value);
        JS_FreeValue(ctx, is_static_value);
        JS_FreeValue(ctx, signature_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
#endif

    JavaJsHookRequest request = {};
    request.class_name = class_name;
    request.method_name = method_name;
    request.signature = signature_cstr != nullptr ? signature_cstr : "*";
    request.loader_handle = loader_handle;
    request.is_static = is_static;
    request.deferred = true;

    JS_FreeCString(ctx, class_name);
    JS_FreeCString(ctx, method_name);
    if (signature_cstr != nullptr) {
        JS_FreeCString(ctx, signature_cstr);
    }
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, method_name_value);
    JS_FreeValue(ctx, is_static_value);
    JS_FreeValue(ctx, signature_value);
    JS_FreeValue(ctx, loader_handle_value);

    JavaJsHookRecord record = {};
    error_message.clear();
    if (!InstallJavaJsHook(request, state.java_hook_installer_dependencies, &record, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    auto& script_callbacks = state.java_hook_callbacks[state.current_script_id];
    auto existing = script_callbacks.find(record.hook_id);
    if (existing != script_callbacks.end()) {
        JS_FreeValue(ctx, existing->second);
    }
    script_callbacks[record.hook_id] = JS_DupValue(ctx, argv[1]);
    return JS_NewUint32(ctx, record.hook_id);
}

JSValue JsJavaResolveOverloadSignature(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(
            ctx, "Java overload signature resolution requires class, method, and argument types");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    const char* method_name = JS_ToCString(ctx, argv[1]);
    if (class_name == nullptr || method_name == nullptr) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        if (method_name != nullptr) {
            JS_FreeCString(ctx, method_name);
        }
        return JS_ThrowTypeError(ctx, "Java overload metadata is invalid");
    }

    uint32_t arg_count = 0u;
    if (!GetArrayLength(ctx, argv[2], &arg_count)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        return JS_ThrowTypeError(ctx, "Java overload argument types must be an array");
    }

    std::vector<std::string> argument_type_names;
    argument_type_names.reserve(arg_count);
    for (uint32_t index = 0u; index < arg_count; ++index) {
        JSValue type_name_value = JS_GetPropertyUint32(ctx, argv[2], index);
        if (JS_IsException(type_name_value)) {
            JS_FreeCString(ctx, class_name);
            JS_FreeCString(ctx, method_name);
            return type_name_value;
        }
        const char* type_name = JS_ToCString(ctx, type_name_value);
        JS_FreeValue(ctx, type_name_value);
        if (type_name == nullptr) {
            JS_FreeCString(ctx, class_name);
            JS_FreeCString(ctx, method_name);
            return JS_ThrowTypeError(ctx, "Java overload type names must be strings");
        }
        argument_type_names.emplace_back(type_name);
        JS_FreeCString(ctx, type_name);
    }

    uint64_t loader_handle = 0u;
    if (argc >= 4 &&
        !JS_IsUndefined(argv[3]) &&
        !JS_IsNull(argv[3]) &&
        !ParsePointerValue(ctx, argv[3], &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeCString(ctx, method_name);
        return JS_ThrowTypeError(ctx, "Java overload loader handle is invalid");
    }

    RuntimeState& state = GetRuntimeState();
    std::string signature;
    std::string first_error_message;
    std::string error_message;
    bool resolved = ResolveJavaMethodSignature(class_name,
                                               method_name,
                                               argument_type_names,
                                               loader_handle,
                                               false,
                                               state.java_hook_installer_dependencies,
                                               &signature,
                                               &error_message);
    bool is_static = false;
    if (!resolved) {
        first_error_message = error_message;
        error_message.clear();
        resolved = ResolveJavaMethodSignature(class_name,
                                              method_name,
                                              argument_type_names,
                                              loader_handle,
                                              true,
                                              state.java_hook_installer_dependencies,
                                              &signature,
                                              &error_message);
        is_static = resolved;
    }
    JS_FreeCString(ctx, class_name);
    JS_FreeCString(ctx, method_name);
    if (!resolved) {
        return JS_ThrowInternalError(ctx,
                                     "%s",
                                     error_message.empty()
                                         ? first_error_message.c_str()
                                         : error_message.c_str());
    }
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }
    if (JS_SetPropertyStr(ctx, result, "signature", JS_NewString(ctx, signature.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "isStatic", JS_NewBool(ctx, is_static ? 1 : 0)) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
}

JSValue CreateJavaUseWrapper(JSContext* ctx,
                             const char* class_name,
                             uint64_t receiver_handle,
                             uint64_t loader_handle,
                             bool owns_handle) {
    static const char* kJavaUseFactorySource =
        "(function (className, receiverHandle, loaderHandle, ownsHandle) {"
        "  var currentReceiverHandle = receiverHandle;"
        "  var cachedModifierClass = undefined;"
        "  var cachedDeclaredMethodOverloads = undefined;"
        "  function invalidateCachedHandles(target) {"
        "    var name;"
        "    for (name in target.__nookMethodCache) {"
        "      if (Object.prototype.hasOwnProperty.call(target.__nookMethodCache, name)) {"
        "        target.__nookMethodCache[name].__nookJavaReceiverHandle = currentReceiverHandle;"
        "        target.__nookMethodCache[name].__jptr = currentReceiverHandle;"
        "      }"
        "    }"
        "    for (name in target.__nookFieldCache) {"
        "      if (Object.prototype.hasOwnProperty.call(target.__nookFieldCache, name)) {"
        "        target.__nookFieldCache[name].__nookJavaReceiverHandle = currentReceiverHandle;"
        "      }"
        "    }"
        "  }"
        "  function getModifierClass() {"
        "    if (cachedModifierClass === undefined) {"
        "      cachedModifierClass = __nookJavaUseWithLoader('java.lang.reflect.Modifier', loaderHandle);"
        "    }"
        "    return cachedModifierClass;"
        "  }"
        "  function typeNameToDescriptor(typeName) {"
        "    switch (typeName) {"
        "      case 'void': return 'V';"
        "      case 'boolean': return 'Z';"
        "      case 'byte': return 'B';"
        "      case 'char': return 'C';"
        "      case 'short': return 'S';"
        "      case 'int': return 'I';"
        "      case 'long': return 'J';"
        "      case 'float': return 'F';"
        "      case 'double': return 'D';"
        "    }"
        "    if (typeName.length > 0 && typeName.charAt(0) === '[') {"
        "      if (typeName.length >= 2 && typeName.charAt(1) === 'L' && typeName.charAt(typeName.length - 1) === ';') {"
        "        return '[L' + typeName.substring(2, typeName.length - 1).replace(/\\./g, '/') + ';';"
        "      }"
        "      return typeName;"
        "    }"
        "    return 'L' + typeName.replace(/\\./g, '/') + ';';"
        "  }"
        "  function getDeclaredMethodOverloads(target) {"
        "    if (cachedDeclaredMethodOverloads !== undefined) {"
        "      return cachedDeclaredMethodOverloads;"
        "    }"
        "    const classWrapper = target.__nookClassWrapper !== undefined"
        "      ? target.__nookClassWrapper"
        "      : (target.__nookClassWrapper = __nookJavaGetClassWrapper(className, loaderHandle));"
        "    const reflectedMethods = classWrapper.getDeclaredMethods();"
        "    const grouped = Object.create(null);"
        "    for (let i = 0; i < reflectedMethods.length; i++) {"
        "      const reflected = reflectedMethods[i];"
        "      const reflectedName = reflected.getName();"
        "      const parameterTypes = reflected.getParameterTypes();"
        "      const typeNames = [];"
        "      let signature = '(';"
        "      for (let j = 0; j < parameterTypes.length; j++) {"
        "        const parameterTypeName = parameterTypes[j].getName();"
        "        typeNames.push(parameterTypeName);"
        "        signature += typeNameToDescriptor(parameterTypeName);"
        "      }"
        "      signature += ')';"
        "      signature += typeNameToDescriptor(reflected.getReturnType().getName());"
        "      const cacheKey = JSON.stringify(typeNames);"
        "      let selected = target.__nookMethodCache[reflectedName];"
        "      if (selected === undefined) {"
        "        selected = makeMethod(reflectedName, undefined, undefined, false);"
        "        target.__nookMethodCache[reflectedName] = selected;"
        "      }"
        "      let overload = selected.__nookOverloadCache[cacheKey];"
        "      if (overload === undefined) {"
        "        const modifiers = reflected.getModifiers();"
        "        const isStatic = getModifierClass().isStatic(modifiers);"
        "        overload = makeMethod(reflectedName, signature, typeNames, isStatic);"
        "        selected.__nookOverloadCache[cacheKey] = overload;"
        "      }"
        "      if (grouped[reflectedName] === undefined) {"
        "        grouped[reflectedName] = [];"
        "      }"
        "      grouped[reflectedName].push(overload);"
        "    }"
        "    cachedDeclaredMethodOverloads = grouped;"
        "    return cachedDeclaredMethodOverloads;"
        "  }"
        "  function makeMethod(methodName, signature, overloadTypeNames, isStatic) {"
        "    function method() {"
        "      const activeMethodName = method.$methodName !== undefined ? method.$methodName : methodName;"
        "      const activeSignature = method.$signature !== undefined ? method.$signature : signature;"
        "      if (typeof method.callOriginal === 'function' &&"
        "          method.__nookJavaHookId !== undefined &&"
        "          target.__nookActiveHookMethodName === activeMethodName &&"
        "          target.__nookActiveHookSignature === activeSignature) {"
        "        return method.callOriginal.apply(method, Array.prototype.slice.call(arguments));"
        "      }"
        "      return __nookJavaInvoke.apply(null, [method].concat(Array.prototype.slice.call(arguments)));"
        "    }"
        "    method.$className = className;"
        "    method.$methodName = methodName;"
        "    method.$signature = signature;"
        "    method.$overloadTypeNames = overloadTypeNames;"
        "    method.$isStatic = isStatic === true;"
        "    method.__jptr = currentReceiverHandle;"
        "    method.__nookJavaReceiverHandle = currentReceiverHandle;"
        "    method.__nookJavaLoaderHandle = loaderHandle;"
        "    method.__nookImplementation = undefined;"
        "    method.__nookOverloadCache = Object.create(null);"
        "    method.__nookOverloadsList = undefined;"
        "    Object.defineProperty(method, 'implementation', {"
        "      configurable: true,"
        "      enumerable: true,"
        "      get() {"
        "        return this.__nookImplementation;"
        "      },"
        "      set(fn) {"
        "        if (typeof fn !== 'function') {"
        "          throw new TypeError('Java method implementation must be a function');"
        "        }"
        "        this.__nookJavaHookId = __nookJavaInstallImplementation(this, fn);"
        "        this.__nookImplementation = fn;"
        "      }"
        "    });"
        "    method.callOriginal = function () {"
        "      throw new Error('Java method callOriginal is not implemented yet');"
        "    };"
        "    method.toString = function () {"
        "      return '<JavaMethod ' + className + '.' + methodName + '>';"
        "    };"
        "    method.overload = function () {"
        "      const typeNames = Array.prototype.slice.call(arguments);"
        "      const cacheKey = JSON.stringify(typeNames);"
        "      let selected = method.__nookOverloadCache[cacheKey];"
        "      if (selected !== undefined) {"
        "        return selected;"
        "      }"
        "      const resolved = __nookJavaResolveOverloadSignature(className, methodName, typeNames, loaderHandle);"
        "      selected = makeMethod(methodName, resolved.signature, typeNames, resolved.isStatic);"
        "      method.__nookOverloadCache[cacheKey] = selected;"
        "      return selected;"
        "    };"
        "    Object.defineProperty(method, 'overloads', {"
        "      configurable: true,"
        "      enumerable: true,"
        "      get() {"
        "        if (method.__nookOverloadsList !== undefined) {"
        "          return method.__nookOverloadsList;"
        "        }"
        "        const grouped = getDeclaredMethodOverloads(target);"
        "        const overloads = grouped[methodName] !== undefined ? grouped[methodName].slice() : [];"
        "        if (overloads.length === 0) {"
        "          overloads.push(method);"
        "        }"
        "        method.__nookOverloadsList = overloads;"
        "        return method.__nookOverloadsList;"
        "      }"
        "    });"
        "    return method;"
        "  }"
        "  function makeField(fieldName, signature, isStatic) {"
        "    return {"
        "      $className: className,"
        "      $fieldName: fieldName,"
        "      $signature: signature,"
        "      $isStatic: isStatic,"
        "      __nookJavaReceiverHandle: currentReceiverHandle,"
        "      __nookJavaLoaderHandle: loaderHandle,"
        "      toString: function () {"
        "        return '<JavaField ' + className + '.' + fieldName + '>';"
        "      },"
        "      get value() {"
        "        return __nookJavaReadField(this);"
        "      },"
        "      set value(nextValue) {"
        "        __nookJavaWriteField(this, nextValue);"
        "      }"
        "    };"
        "  }"
        "  const target = {"
        "    $className: className,"
        "    __jptr: currentReceiverHandle,"
        "    __nookJavaReceiverHandle: currentReceiverHandle,"
        "    __nookJavaLoaderHandle: loaderHandle,"
        "    __nookJavaOwnedHandle: !!ownsHandle,"
        "    __nookActiveHookMethodName: undefined,"
        "    __nookActiveHookSignature: undefined,"
        "    __nookJavaWeakState: null,"
        "    __nookJavaWeakToken: 0,"
        "    __nookClassWrapper: undefined,"
        "    toString: function () {"
        "      return currentReceiverHandle === '0x0'"
        "        ? ('<JavaClass ' + className + '>')"
        "        : ('<JavaObject ' + className + '>');"
        "    },"
        "    $new: function () {"
        "      const args = Array.prototype.slice.call(arguments);"
        "      const constructorTarget = {"
        "        $className: className,"
        "        $methodName: '<init>',"
        "        $signature: (args.length > 0 && typeof args[0] === 'string' && args[0].charAt(0) === '(')"
        "          ? args.shift()"
        "          : undefined,"
        "        $isStatic: false,"
        "        __jptr: '0x0',"
        "        __nookJavaReceiverHandle: '0x0',"
        "        __nookJavaLoaderHandle: loaderHandle"
        "      };"
        "      return __nookJavaInvoke.apply(null, [constructorTarget].concat(args));"
        "    },"
        "    $dispose: function () {"
        "      if (!this.__nookJavaOwnedHandle || currentReceiverHandle === '0x0') {"
        "        return;"
        "      }"
        "      if (this.__nookJavaWeakToken && typeof Script === 'object' &&"
        "          Script !== null && typeof Script.unbindWeak === 'function') {"
        "        if (this.__nookJavaWeakState !== null && typeof this.__nookJavaWeakState === 'object') {"
        "          this.__nookJavaWeakState.disposed = true;"
        "        }"
        "        try {"
        "          Script.unbindWeak(this.__nookJavaWeakToken);"
        "        } catch (e) {"
        "        }"
        "      }"
        "      this.__nookJavaWeakToken = 0;"
        "      __nookJavaRelease(currentReceiverHandle);"
        "      currentReceiverHandle = '0x0';"
        "      this.__nookJavaReceiverHandle = currentReceiverHandle;"
        "      this.__jptr = currentReceiverHandle;"
        "      this.__nookJavaOwnedHandle = false;"
        "      if (this.__nookJavaWeakState !== null && typeof this.__nookJavaWeakState === 'object') {"
        "        this.__nookJavaWeakState.handle = currentReceiverHandle;"
        "      }"
        "      this.__nookJavaWeakState = null;"
        "      invalidateCachedHandles(this);"
        "    },"
        "    __nookMethodCache: Object.create(null),"
        "    __nookFieldCache: Object.create(null)"
        "  };"
        "  return new Proxy(target, {"
        "    get(target, prop) {"
        "      if (typeof prop !== 'string' || Object.prototype.hasOwnProperty.call(target, prop)) {"
        "        return target[prop];"
        "      }"
        "      if (prop === '__nookJavaArrayType') {"
        "        return undefined;"
        "      }"
        "      if (prop === 'class') {"
        "        if (target.__nookClassWrapper !== undefined) {"
        "          return target.__nookClassWrapper;"
        "        }"
        "        target.__nookClassWrapper = __nookJavaGetClassWrapper(className, loaderHandle);"
        "        return target.__nookClassWrapper;"
        "      }"
        "      let field = target.__nookFieldCache[prop];"
        "      if (field !== undefined) {"
        "        return field;"
        "      }"
        "      let resolvedFieldName = prop;"
        "      let resolvedField = __nookJavaResolveField(className, resolvedFieldName, currentReceiverHandle === '0x0', loaderHandle);"
        "      if (resolvedField === null && prop.length > 1 && prop.charAt(0) === '_') {"
        "        resolvedFieldName = prop.slice(1);"
        "        resolvedField = __nookJavaResolveField(className, resolvedFieldName, currentReceiverHandle === '0x0', loaderHandle);"
        "      }"
        "      if (resolvedField !== null) {"
        "        field = makeField(resolvedFieldName, resolvedField.signature, resolvedField.isStatic);"
        "        target.__nookFieldCache[prop] = field;"
        "        return field;"
        "      }"
        "      let method = target.__nookMethodCache[prop];"
        "      if (method !== undefined) {"
        "        return method;"
        "      }"
        "      const canonicalMethodName = prop === '$init' ? '<init>' : prop;"
        "      method = target.__nookMethodCache[canonicalMethodName];"
        "      if (method !== undefined) {"
        "        target.__nookMethodCache[prop] = method;"
        "        return method;"
        "      }"
        "      method = makeMethod(canonicalMethodName, undefined, undefined, false);"
        "      target.__nookMethodCache[canonicalMethodName] = method;"
        "      target.__nookMethodCache[prop] = method;"
        "      return method;"
        "    }"
        "  });"
        "})";

    JSValue factory = JS_Eval(ctx,
                              kJavaUseFactorySource,
                              std::strlen(kJavaUseFactorySource),
                              "<java_use_factory>",
                              JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory)) {
        return factory;
    }

    JSValue class_name_value = JS_NewString(ctx, class_name);
    if (JS_IsException(class_name_value)) {
        JS_FreeValue(ctx, factory);
        return class_name_value;
    }

    JSValue receiver_handle_value =
        JS_NewString(ctx, FormatHookValue(receiver_handle).c_str());
    if (JS_IsException(receiver_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, factory);
        return receiver_handle_value;
    }

    JSValue loader_handle_value =
        JS_NewString(ctx, FormatHookValue(loader_handle).c_str());
    if (JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, factory);
        return loader_handle_value;
    }

    JSValue owned_handle_value = JS_NewBool(ctx, owns_handle ? 1 : 0);
    if (JS_IsException(owned_handle_value)) {
        JS_FreeValue(ctx, loader_handle_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, factory);
        return owned_handle_value;
    }

    JSValue argv[4] = {
        class_name_value,
        receiver_handle_value,
        loader_handle_value,
        owned_handle_value,
    };
    JSValue wrapper = JS_Call(ctx, factory, JS_UNDEFINED, 4, argv);
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    JS_FreeValue(ctx, owned_handle_value);
    JS_FreeValue(ctx, factory);
    if (!JS_IsException(wrapper) && owns_handle && receiver_handle != 0u) {
        RuntimeState& state = GetRuntimeState();
        RegisterOwnedJavaHandleLocked(state, receiver_handle);
    }
    return wrapper;
}

JSValue JsJavaUse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.use requires a class name");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_ThrowTypeError(ctx, "Java.use class name must be a string");
    }

    JSValue wrapper = CreateJavaUseWrapper(ctx, class_name);
    if (JS_IsException(wrapper)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "build Java.use wrapper failed");
    }

    JS_FreeCString(ctx, class_name);
    return wrapper;
}

JSValue JsJavaRelease(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java internal release requires object handle");
    }

    uint64_t object_handle = 0u;
    if (!ParsePointerValue(ctx, argv[0], &object_handle) || object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java release object handle is invalid");
    }

    RuntimeState& state = GetRuntimeState();
#if !defined(__ANDROID__)
    if (state.java_hook_installer_dependencies.release_object == nullptr) {
        return JS_ThrowInternalError(ctx, "Java release is only available on Android");
    }
#endif

    std::string error_message;
    if (!ReleaseJavaObject(object_handle, state.java_hook_installer_dependencies, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    UnregisterOwnedJavaHandleLocked(state, object_handle);
    return JS_UNDEFINED;
}

JSValue JsJavaUseWithLoader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java internal loader-aware use requires class name and loader handle");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_ThrowTypeError(ctx, "Java class name must be a string");
    }

    uint64_t loader_handle = 0u;
    if (!ParsePointerValue(ctx, argv[1], &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "Java loader handle is invalid");
    }

    JSValue wrapper = CreateJavaUseWrapper(ctx, class_name, 0u, loader_handle);
    if (JS_IsException(wrapper)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "build Java.ClassFactory.use wrapper failed");
    }

    JS_FreeCString(ctx, class_name);
    return wrapper;
}

JSValue JsJavaGetClassWrapper(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(
            ctx,
            "Java internal getClassWrapper requires class name and loader handle");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_ThrowTypeError(ctx, "Java class name must be a string");
    }

    uint64_t loader_handle = 0u;
    if (!ParsePointerValue(ctx, argv[1], &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "Java loader handle is invalid");
    }

    uint64_t env_ptr = 0u;
    uint64_t class_handle = 0u;
    std::string error_message;
    if (QueryCurrentJavaEnvPointer(true, &env_ptr, &error_message) !=
        JsRuntimeJavaEnvQueryStatus::kAvailable) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (!ResolveJavaClassGlobalRef(
            env_ptr, class_name, loader_handle, &class_handle, &error_message)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue wrapper = CreateJavaUseWrapper(ctx, "java.lang.Class", class_handle, loader_handle, true);
    JS_FreeCString(ctx, class_name);
    if (JS_IsException(wrapper)) {
        return JS_ThrowInternalError(ctx, "build Java class wrapper failed");
    }
    return wrapper;
}

JSValue JsScriptBindWeak(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Script.bindWeak requires target and callback");
    }
    if (!JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "Script.bindWeak target must be an object");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "Script.bindWeak callback must be a function");
    }

    RuntimeState& state = GetRuntimeState();
    const uint64_t binding_id = state.next_weak_binding_id++;
    JSValue registry = GetOrCreateWeakBindingRegistryLocked(ctx, nullptr);
    if (JS_IsException(registry)) {
        return registry;
    }

    JSValue unregister_token = JS_NewObject(ctx);
    if (JS_IsException(unregister_token)) {
        JS_FreeValue(ctx, registry);
        return unregister_token;
    }

    JSValue method = JS_GetPropertyStr(ctx, registry, "register");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, unregister_token);
        JS_FreeValue(ctx, registry);
        return method;
    }

    JSValue argv_register[3] = {
        JS_DupValue(ctx, argv[0]),
        JS_NewUint32(ctx, static_cast<uint32_t>(binding_id)),
        JS_DupValue(ctx, unregister_token),
    };
    if (JS_IsException(argv_register[1])) {
        JS_FreeValue(ctx, argv_register[2]);
        JS_FreeValue(ctx, argv_register[1]);
        JS_FreeValue(ctx, argv_register[0]);
        JS_FreeValue(ctx, method);
        JS_FreeValue(ctx, unregister_token);
        JS_FreeValue(ctx, registry);
        return JS_EXCEPTION;
    }

    JSValue register_result = JS_Call(ctx, method, registry, 3, argv_register);
    JS_FreeValue(ctx, argv_register[2]);
    JS_FreeValue(ctx, argv_register[1]);
    JS_FreeValue(ctx, argv_register[0]);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, registry);
    if (JS_IsException(register_result)) {
        JS_FreeValue(ctx, unregister_token);
        return register_result;
    }
    JS_FreeValue(ctx, register_result);

    RuntimeState::WeakBindingRecord record = {};
    record.script_id = state.current_script_id;
    record.callback = JS_DupValue(ctx, argv[1]);
    record.unregister_token = unregister_token;
    state.weak_bindings.emplace(binding_id, std::move(record));
    return JS_NewUint32(ctx, static_cast<uint32_t>(binding_id));
}

JSValue JsScriptUnbindWeak(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Script.unbindWeak requires a binding token");
    }
    uint32_t binding_id32 = 0u;
    if (JS_ToUint32(ctx, &binding_id32, argv[0]) < 0 || binding_id32 == 0u) {
        return JS_ThrowTypeError(ctx, "Script.unbindWeak token must be a positive integer");
    }

    RuntimeState& state = GetRuntimeState();
    const uint64_t binding_id = binding_id32;
    auto it = state.weak_bindings.find(binding_id);
    if (it == state.weak_bindings.end() || it->second.script_id != state.current_script_id) {
        return JS_NewBool(ctx, 0);
    }

    RuntimeState::WeakBindingRecord& record = it->second;
    if (!UnregisterWeakBindingLocked(ctx, state, record)) {
        return JS_ThrowInternalError(ctx, "Script.unbindWeak failed to unregister binding");
    }
    DispatchWeakBindingCallbackLocked(state, binding_id);
    return JS_NewBool(ctx, 1);
}

JSValue JsScriptPin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "Script.pin is unavailable outside a script context");
    }
    ++state.script_pin_counts[state.current_script_id];
    return JS_UNDEFINED;
}

JSValue JsScriptUnpin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "Script.unpin is unavailable outside a script context");
    }

    auto it = state.script_pin_counts.find(state.current_script_id);
    if (it == state.script_pin_counts.end() || it->second == 0u) {
        return JS_ThrowInternalError(ctx, "Script.unpin called while pin count is zero");
    }

    --it->second;
    if (it->second == 0u) {
        state.script_pin_counts.erase(it);
    }
    return JS_UNDEFINED;
}

JSValue JsScriptRunGcForTesting(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;

    RuntimeState& state = GetRuntimeState();
    if (state.runtime == nullptr) {
        return JS_UNDEFINED;
    }

    std::string error_message;
    for (int i = 0; i < 4; ++i) {
        JS_RunGC(state.runtime);
        if (!DrainWeakBindingMaintenanceLocked(state, &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    }

    return JS_UNDEFINED;
}

JSValue CreateTimerForCurrentScript(JSContext* ctx,
                                    int argc,
                                    JSValueConst* argv,
                                    bool repeat,
                                    uint32_t default_delay_ms,
                                    const char* api_name) {
    (void)api_name;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "%s requires a function", api_name);
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "%s is unavailable outside a script context", api_name);
    }

    int32_t delay_ms_signed = static_cast<int32_t>(default_delay_ms);
    if (argc >= 2 && JS_ToInt32(ctx, &delay_ms_signed, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx, "%s delay must be a number", api_name);
    }
    if (delay_ms_signed < 0) {
        delay_ms_signed = 0;
    }
    const uint32_t delay_ms = static_cast<uint32_t>(delay_ms_signed);

    RuntimeState::TimerRecord record = {};
    record.script_id = state.current_script_id;
    record.timer_id = state.next_timer_id++;
    record.callback = JS_DupValue(ctx, argv[0]);
    record.due_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    record.delay_ms = delay_ms;
    record.repeat = repeat;
    for (int index = 2; index < argc; ++index) {
        record.args.push_back(JS_DupValue(ctx, argv[index]));
    }

    const uint32_t timer_id = record.timer_id;
    state.timers.emplace(timer_id, std::move(record));
    state.timer_cv.notify_all();
    return JS_NewUint32(ctx, timer_id);
}

JSValue CancelTimerById(JSContext* ctx, JSValueConst value) {
    RuntimeState& state = GetRuntimeState();

    uint32_t timer_id = 0u;
    if (JS_IsUndefined(value) || JS_IsNull(value) || JS_ToUint32(ctx, &timer_id, value) < 0) {
        return JS_UNDEFINED;
    }

    auto it = state.timers.find(timer_id);
    if (it != state.timers.end()) {
        FreeTimerRecordLocked(ctx, &it->second);
        state.timers.erase(it);
        state.timer_cv.notify_all();
    }
    return JS_UNDEFINED;
}

JSValue JsSetImmediate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setImmediate requires a function");
    }

    std::vector<JSValueConst> normalized;
    normalized.reserve(static_cast<size_t>(argc) + 1u);
    normalized.push_back(argv[0]);
    normalized.push_back(JS_UNDEFINED);
    for (int index = 1; index < argc; ++index) {
        normalized.push_back(argv[index]);
    }
    return CreateTimerForCurrentScript(
        ctx, static_cast<int>(normalized.size()), normalized.data(), false, 0u, "setImmediate");
}

JSValue JsSetTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    return CreateTimerForCurrentScript(ctx, argc, argv, false, 0u, "setTimeout");
}

JSValue JsSetInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    return CreateTimerForCurrentScript(ctx, argc, argv, true, 0u, "setInterval");
}

JSValue JsClearTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    return CancelTimerById(ctx, argv[0]);
}

JSValue JsClearInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    return CancelTimerById(ctx, argv[0]);
}

bool ReadJavaRegisterClassInterfaceNames(JSContext* ctx,
                                         JSValueConst value,
                                         std::vector<std::string>* interface_class_names,
                                         std::string* error_message) {
    if (interface_class_names == nullptr) {
        SetError(error_message, "Java.registerClass interface output is required");
        return false;
    }
    interface_class_names->clear();

    uint32_t count = 0u;
    if (!GetArrayLength(ctx, value, &count)) {
        SetError(error_message, "Java.registerClass interfaces must be an array");
        return false;
    }

    for (uint32_t index = 0u; index < count; ++index) {
        JSValue entry = JS_GetPropertyUint32(ctx, value, index);
        if (JS_IsException(entry)) {
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
        const char* interface_name = JS_ToCString(ctx, entry);
        JS_FreeValue(ctx, entry);
        if (interface_name == nullptr) {
            SetError(error_message, "Java.registerClass interface names must be strings");
            return false;
        }
        interface_class_names->emplace_back(interface_name);
        JS_FreeCString(ctx, interface_name);
    }

    if (interface_class_names->empty()) {
        SetError(error_message, "Java.registerClass requires at least one interface");
        return false;
    }
    return true;
}

bool ValidateJavaRegisterClassMethodDeclarationMetadata(JSContext* ctx,
                                                        JSValueConst declaration_value,
                                                        std::string* error_message) {
    JSValue return_type_value = JS_GetPropertyStr(ctx, declaration_value, "returnType");
    JSValue argument_types_value = JS_GetPropertyStr(ctx, declaration_value, "argumentTypes");
    if (JS_IsException(return_type_value) || JS_IsException(argument_types_value)) {
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, argument_types_value);
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    auto cleanup = [&]() {
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, argument_types_value);
    };

    if (!JS_IsUndefined(return_type_value) &&
        !JS_IsNull(return_type_value) &&
        !JS_IsString(return_type_value)) {
        cleanup();
        SetError(error_message, "Java.registerClass method declaration returnType must be a string");
        return false;
    }

    if (!JS_IsUndefined(argument_types_value) && !JS_IsNull(argument_types_value)) {
        uint32_t count = 0u;
        if (!JS_IsArray(ctx, argument_types_value) ||
            !GetArrayLength(ctx, argument_types_value, &count)) {
            cleanup();
            SetError(error_message,
                     "Java.registerClass method declaration argumentTypes must be an array of strings");
            return false;
        }
        for (uint32_t index = 0u; index < count; ++index) {
            JSValue entry = JS_GetPropertyUint32(ctx, argument_types_value, index);
            if (JS_IsException(entry)) {
                JS_FreeValue(ctx, entry);
                cleanup();
                SetError(error_message, GetExceptionString(ctx));
                return false;
            }
            const bool is_string = JS_IsString(entry);
            JS_FreeValue(ctx, entry);
            if (!is_string) {
                cleanup();
                SetError(error_message,
                         "Java.registerClass method declaration argumentTypes must be an array of strings");
                return false;
            }
        }
    }

    cleanup();
    return true;
}

std::string NormalizeJavaRegisterClassPrimitiveDescriptor(const std::string& type_name) {
    if (type_name == "void") return "V";
    if (type_name == "boolean") return "Z";
    if (type_name == "byte") return "B";
    if (type_name == "char") return "C";
    if (type_name == "short") return "S";
    if (type_name == "int") return "I";
    if (type_name == "long") return "J";
    if (type_name == "float") return "F";
    if (type_name == "double") return "D";
    return {};
}

bool NormalizeJavaRegisterClassTypeNameToDescriptor(const std::string& type_name,
                                                    std::string* descriptor_out,
                                                    std::string* error_message) {
    if (descriptor_out == nullptr) {
        SetError(error_message, "java descriptor output is required");
        return false;
    }
    descriptor_out->clear();
    if (type_name.empty()) {
        SetError(error_message, "java type name must be non-empty");
        return false;
    }

    const std::string primitive_descriptor =
        NormalizeJavaRegisterClassPrimitiveDescriptor(type_name);
    if (!primitive_descriptor.empty()) {
        *descriptor_out = primitive_descriptor;
        return true;
    }

    if (type_name.size() >= 2u && type_name.compare(type_name.size() - 2u, 2u, "[]") == 0) {
        std::string element_descriptor;
        if (!NormalizeJavaRegisterClassTypeNameToDescriptor(
                type_name.substr(0u, type_name.size() - 2u), &element_descriptor, error_message)) {
            return false;
        }
        *descriptor_out = "[" + element_descriptor;
        return true;
    }

    if (type_name[0] == '[') {
        *descriptor_out = type_name;
        std::replace(descriptor_out->begin(), descriptor_out->end(), '.', '/');
        return true;
    }

    if (type_name[0] == 'L' && type_name.back() == ';') {
        *descriptor_out = type_name;
        return true;
    }

    std::string normalized = type_name;
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    *descriptor_out = "L" + normalized + ";";
    return true;
}

bool ReadJavaRegisterClassMethodDeclarationSignature(JSContext* ctx,
                                                     JSValueConst declaration_value,
                                                     bool require_signature_metadata,
                                                     bool* has_signature_out,
                                                     std::string* signature_out,
                                                     std::string* error_message) {
    if (has_signature_out == nullptr || signature_out == nullptr) {
        SetError(error_message, "Java.registerClass signature outputs are required");
        return false;
    }
    *has_signature_out = false;
    signature_out->clear();

    JSValue return_type_value = JS_GetPropertyStr(ctx, declaration_value, "returnType");
    JSValue argument_types_value = JS_GetPropertyStr(ctx, declaration_value, "argumentTypes");
    if (JS_IsException(return_type_value) || JS_IsException(argument_types_value)) {
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, argument_types_value);
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    auto cleanup = [&]() {
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, argument_types_value);
    };

    const bool has_return_type =
        !JS_IsUndefined(return_type_value) && !JS_IsNull(return_type_value);
    const bool has_argument_types =
        !JS_IsUndefined(argument_types_value) && !JS_IsNull(argument_types_value);
    if (require_signature_metadata && !has_return_type) {
        cleanup();
        SetError(error_message,
                 "Java.registerClass method declaration arrays require returnType");
        return false;
    }
    if (require_signature_metadata && !has_argument_types) {
        cleanup();
        SetError(error_message,
                 "Java.registerClass method declaration arrays require argumentTypes");
        return false;
    }
    if (!has_return_type || !has_argument_types) {
        cleanup();
        return true;
    }

    const char* return_type = JS_ToCString(ctx, return_type_value);
    if (return_type == nullptr) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    uint32_t argument_count = 0u;
    if (!GetArrayLength(ctx, argument_types_value, &argument_count)) {
        JS_FreeCString(ctx, return_type);
        cleanup();
        SetError(error_message,
                 "Java.registerClass method declaration argumentTypes must be an array of strings");
        return false;
    }

    std::string return_descriptor;
    if (!NormalizeJavaRegisterClassTypeNameToDescriptor(
            return_type, &return_descriptor, error_message)) {
        JS_FreeCString(ctx, return_type);
        cleanup();
        return false;
    }
    JS_FreeCString(ctx, return_type);

    std::string signature = "(";
    for (uint32_t index = 0u; index < argument_count; ++index) {
        JSValue entry = JS_GetPropertyUint32(ctx, argument_types_value, index);
        if (JS_IsException(entry)) {
            JS_FreeValue(ctx, entry);
            cleanup();
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }

        const char* argument_type = JS_ToCString(ctx, entry);
        if (argument_type == nullptr) {
            JS_FreeValue(ctx, entry);
            cleanup();
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }

        std::string argument_descriptor;
        const bool normalized = NormalizeJavaRegisterClassTypeNameToDescriptor(
            argument_type, &argument_descriptor, error_message);
        JS_FreeCString(ctx, argument_type);
        JS_FreeValue(ctx, entry);
        if (!normalized) {
            cleanup();
            return false;
        }
        signature += argument_descriptor;
    }
    signature += ")";
    signature += return_descriptor;

    *has_signature_out = true;
    *signature_out = std::move(signature);
    cleanup();
    return true;
}

bool ExtractJavaRegisterClassMethodCallback(JSContext* ctx,
                                            JSValueConst declaration_value,
                                            JSValue* callback_out,
                                            bool* has_signature_out,
                                            std::string* signature_out,
                                            bool require_signature_metadata,
                                            std::string* error_message) {
    if (callback_out == nullptr || has_signature_out == nullptr || signature_out == nullptr) {
        SetError(error_message, "Java.registerClass callback outputs are required");
        return false;
    }

    if (!JS_IsObject(declaration_value)) {
        SetError(error_message,
                 "Java.registerClass method declarations must provide an implementation function");
        return false;
    }

    JSValue implementation_value = JS_GetPropertyStr(ctx, declaration_value, "implementation");
    if (JS_IsException(implementation_value)) {
        JS_FreeValue(ctx, implementation_value);
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    auto cleanup = [&]() {
        JS_FreeValue(ctx, implementation_value);
    };

    if (!JS_IsFunction(ctx, implementation_value)) {
        cleanup();
        SetError(error_message,
                 "Java.registerClass method declarations must provide an implementation function");
        return false;
    }

    if (!ValidateJavaRegisterClassMethodDeclarationMetadata(
            ctx, declaration_value, error_message)) {
        cleanup();
        return false;
    }

    if (!ReadJavaRegisterClassMethodDeclarationSignature(ctx,
                                                         declaration_value,
                                                         require_signature_metadata,
                                                         has_signature_out,
                                                         signature_out,
                                                         error_message)) {
        cleanup();
        return false;
    }

    *callback_out = JS_DupValue(ctx, implementation_value);
    cleanup();
    return true;
}

bool InsertJavaRegisterClassMethodCallback(
    JSContext* ctx,
    const std::string& method_name,
    const std::string& signature,
    JSValue callback_value,
    std::vector<JavaJsRegisteredClassMethodRecord>* methods_out,
    JavaRegisteredClassMethodCallbackMap* callbacks_out,
    bool record_method,
    std::string* error_message) {
    if (methods_out == nullptr || callbacks_out == nullptr) {
        SetError(error_message, "Java.registerClass method outputs are required");
        return false;
    }

    auto& method_callbacks = (*callbacks_out)[method_name];
    if (method_callbacks.find(signature) != method_callbacks.end()) {
        JS_FreeValue(ctx, callback_value);
        SetError(error_message,
                 "Java.registerClass method declaration signatures must be unique per method name");
        return false;
    }

    if (record_method) {
        JavaJsRegisteredClassMethodRecord method_record = {};
        method_record.name = method_name;
        method_record.signature = signature;
        methods_out->push_back(method_record);
    }
    method_callbacks.emplace(signature, callback_value);
    return true;
}

bool CollectJavaRegisterClassMethodDeclarations(
    JSContext* ctx,
    const std::string& method_name,
    JSValueConst method_value,
    std::vector<JavaJsRegisteredClassMethodRecord>* methods_out,
    JavaRegisteredClassMethodCallbackMap* callbacks_out,
    std::string* error_message) {
    if (JS_IsFunction(ctx, method_value)) {
        return InsertJavaRegisterClassMethodCallback(
            ctx,
            method_name,
            "",
            JS_DupValue(ctx, method_value),
            methods_out,
            callbacks_out,
            true,
            error_message);
    }

    if (!JS_IsObject(method_value)) {
        SetError(error_message,
                 "Java.registerClass methods must be functions or declaration objects");
        return false;
    }

    if (!JS_IsArray(ctx, method_value)) {
        JSValue callback_value = JS_UNDEFINED;
        bool has_signature = false;
        std::string signature;
        if (!ExtractJavaRegisterClassMethodCallback(ctx,
                                                    method_value,
                                                    &callback_value,
                                                    &has_signature,
                                                    &signature,
                                                    false,
                                                    error_message)) {
            return false;
        }
        if (!InsertJavaRegisterClassMethodCallback(ctx,
                                                   method_name,
                                                   has_signature ? signature : "",
                                                   callback_value,
                                                   methods_out,
                                                   callbacks_out,
                                                   true,
                                                   error_message)) {
            return false;
        }
        if (has_signature &&
            !InsertJavaRegisterClassMethodCallback(ctx,
                                                   method_name,
                                                   "",
                                                   JS_DupValue(ctx, callback_value),
                                                   methods_out,
                                                   callbacks_out,
                                                   false,
                                                   error_message)) {
            return false;
        }
        return true;
    }

    uint32_t declaration_count = 0u;
    if (!GetArrayLength(ctx, method_value, &declaration_count) || declaration_count == 0u) {
        SetError(error_message,
                 "Java.registerClass method declaration arrays must contain at least one entry");
        return false;
    }

    for (uint32_t index = 0u; index < declaration_count; ++index) {
        JSValue declaration_value = JS_GetPropertyUint32(ctx, method_value, index);
        if (JS_IsException(declaration_value)) {
            JS_FreeValue(ctx, declaration_value);
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }

        JSValue callback_value = JS_UNDEFINED;
        bool has_signature = false;
        std::string signature;
        const bool extracted = ExtractJavaRegisterClassMethodCallback(ctx,
                                                                      declaration_value,
                                                                      &callback_value,
                                                                      &has_signature,
                                                                      &signature,
                                                                      declaration_count > 1u,
                                                                      error_message);
        JS_FreeValue(ctx, declaration_value);
        if (!extracted) {
            return false;
        }

        if (!InsertJavaRegisterClassMethodCallback(ctx,
                                                   method_name,
                                                   has_signature ? signature : "",
                                                   callback_value,
                                                   methods_out,
                                                   callbacks_out,
                                                   true,
                                                   error_message)) {
            return false;
        }
        if (declaration_count == 1u && has_signature &&
            !InsertJavaRegisterClassMethodCallback(ctx,
                                                   method_name,
                                                   "",
                                                   JS_DupValue(ctx, callback_value),
                                                   methods_out,
                                                   callbacks_out,
                                                   false,
                                                   error_message)) {
            return false;
        }
    }

    return true;
}

bool CollectJavaRegisterClassMethods(JSContext* ctx,
                                     JSValueConst methods_value,
                                     std::vector<JavaJsRegisteredClassMethodRecord>* methods_out,
                                     JavaRegisteredClassMethodCallbackMap* callbacks_out,
                                     std::string* error_message) {
    if (methods_out == nullptr || callbacks_out == nullptr) {
        SetError(error_message, "Java.registerClass method outputs are required");
        return false;
    }
    methods_out->clear();
    callbacks_out->clear();

    JSPropertyEnum* props = nullptr;
    uint32_t prop_count = 0u;
    if (JS_GetOwnPropertyNames(ctx,
                               &props,
                               &prop_count,
                               methods_value,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    auto cleanup = [&]() {
        for (auto& method_entry : *callbacks_out) {
            for (auto& signature_entry : method_entry.second) {
                JS_FreeValue(ctx, signature_entry.second);
            }
        }
        callbacks_out->clear();
        methods_out->clear();
        if (props != nullptr) {
            for (uint32_t index = 0u; index < prop_count; ++index) {
                JS_FreeAtom(ctx, props[index].atom);
            }
            js_free(ctx, props);
        }
    };

    for (uint32_t index = 0u; index < prop_count; ++index) {
        const char* method_name = JS_AtomToCString(ctx, props[index].atom);
        JSValue method_value = JS_GetProperty(ctx, methods_value, props[index].atom);
        if (method_name == nullptr || JS_IsException(method_value)) {
            if (method_name != nullptr) {
                JS_FreeCString(ctx, method_name);
            }
            JS_FreeValue(ctx, method_value);
            SetError(error_message, GetExceptionString(ctx));
            cleanup();
            return false;
        }

        if (!CollectJavaRegisterClassMethodDeclarations(
                ctx, method_name, method_value, methods_out, callbacks_out, error_message)) {
            JS_FreeCString(ctx, method_name);
            JS_FreeValue(ctx, method_value);
            cleanup();
            return false;
        }

        JS_FreeCString(ctx, method_name);
        JS_FreeValue(ctx, method_value);
    }

    if (props != nullptr) {
        for (uint32_t index = 0u; index < prop_count; ++index) {
            JS_FreeAtom(ctx, props[index].atom);
        }
        js_free(ctx, props);
        props = nullptr;
        prop_count = 0u;
    }

    if (methods_out->empty()) {
        SetError(error_message, "Java.registerClass requires at least one method");
        cleanup();
        return false;
    }
    return true;
}

JSValue JsJavaRegisterClass(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx,
                                 "Java internal registerClass requires class name, interfaces, and methods");
    }

    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_ThrowTypeError(ctx, "Java.registerClass class name must be a string");
    }

    std::string error_message;
    std::vector<std::string> interface_class_names;
    if (!ReadJavaRegisterClassInterfaceNames(ctx, argv[1], &interface_class_names, &error_message)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    if (!JS_IsObject(argv[2])) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "Java.registerClass methods must be an object");
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowInternalError(ctx, "Java.registerClass must run while executing a script");
    }

    std::vector<JavaJsRegisteredClassMethodRecord> methods;
    JavaRegisteredClassMethodCallbackMap callbacks;
    if (!CollectJavaRegisterClassMethods(ctx, argv[2], &methods, &callbacks, &error_message)) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "%s", error_message.c_str());
    }

    uint64_t loader_handle = 0u;
    if (argc >= 4 &&
        !JS_IsUndefined(argv[3]) &&
        !JS_IsNull(argv[3]) &&
        !ParsePointerValue(ctx, argv[3], &loader_handle)) {
        for (auto& method_entry : callbacks) {
            for (auto& signature_entry : method_entry.second) {
                JS_FreeValue(ctx, signature_entry.second);
            }
        }
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "Java.registerClass loader handle is invalid");
    }

    JavaJsRegisterClassRequest request = {};
    request.class_name = class_name;
    request.loader_handle = loader_handle;
    request.interface_class_names = interface_class_names;
    request.methods = methods;
    request.callback_id = state.next_java_registered_class_callback_id++;

    JS_FreeCString(ctx, class_name);

    JavaJsValue result = {};
    if (!RegisterJavaClass(request,
                           state.java_hook_installer_dependencies,
                           &result,
                           &error_message)) {
        for (auto& method_entry : callbacks) {
            for (auto& signature_entry : method_entry.second) {
                JS_FreeValue(ctx, signature_entry.second);
            }
        }
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    state.java_registered_class_callbacks[state.current_script_id][request.callback_id] =
        std::move(callbacks);

    if (result.kind == JavaJsValueKind::kObject &&
        result.object_handle != 0u &&
        !result.object_handle_is_global) {
        uint64_t retained_handle = 0u;
        if (!RetainJavaObject(result.object_handle,
                              state.java_hook_installer_dependencies,
                              &retained_handle,
                              &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        result.object_handle = retained_handle;
        result.object_handle_is_global = true;
    }
    if (result.kind == JavaJsValueKind::kObject &&
        result.object_handle != 0u &&
        request.loader_handle != 0u) {
        const char* wrapper_class_name =
            result.object_class_name.empty() ? "java.lang.Object" : result.object_class_name.c_str();
        return CreateJavaUseWrapper(
            ctx, wrapper_class_name, result.object_handle, request.loader_handle, true);
    }
    return MakeJavaJsValue(ctx, result);
}

JSValue JsJavaChoose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java.choose requires class name and callbacks");
    }

    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java.choose class name must be a string");
    }
    const char* class_name = JS_ToCString(ctx, argv[0]);
    if (class_name == nullptr) {
        return JS_ThrowTypeError(ctx, "Java.choose class name must be a string");
    }
    if (!JS_IsObject(argv[1])) {
        JS_FreeCString(ctx, class_name);
        return JS_ThrowTypeError(ctx, "Java.choose callbacks must be an object");
    }

    JSValue on_match = JS_GetPropertyStr(ctx, argv[1], "onMatch");
    JSValue on_complete = JS_GetPropertyStr(ctx, argv[1], "onComplete");
    if (JS_IsException(on_match) || JS_IsException(on_complete)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_EXCEPTION;
    }
    if (!JS_IsFunction(ctx, on_match)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.choose onMatch must be a function");
    }
    if (!JS_IsFunction(ctx, on_complete)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.choose onComplete must be a function");
    }

    uint64_t loader_handle = 0u;
    if (argc >= 3 &&
        !JS_IsUndefined(argv[2]) &&
        !JS_IsNull(argv[2]) &&
        !ParsePointerValue(ctx, argv[2], &loader_handle)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.choose loader handle is invalid");
    }

    RuntimeState& state = GetRuntimeState();
#if !defined(__ANDROID__)
    if (state.java_hook_installer_dependencies.enumerate_objects == nullptr) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "Java.choose is only available on Android");
    }
#endif

    std::vector<JavaJsValue> matches;
    std::string error_message;
    if (!EnumerateJavaObjects(class_name,
                              loader_handle,
                              state.java_hook_installer_dependencies,
                              &matches,
                              &error_message)) {
        JS_FreeCString(ctx, class_name);
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    JS_FreeCString(ctx, class_name);

    auto cleanup_matches = [&matches]() {
#if defined(__ANDROID__)
        ReleaseTemporaryJavaChooseMatches(matches);
#endif
    };

    for (const JavaJsValue& match : matches) {
        if (match.kind != JavaJsValueKind::kObject) {
            continue;
        }
        JSValue instance = MakeJavaJsValue(ctx, match);
        if (match.object_handle != 0u && loader_handle != 0u) {
            JS_FreeValue(ctx, instance);
            const char* wrapper_class_name = match.object_class_name.empty()
                ? "java.lang.Object"
                : match.object_class_name.c_str();
            instance = CreateJavaUseWrapper(ctx, wrapper_class_name, match.object_handle, loader_handle);
        }
        if (JS_IsException(instance)) {
            cleanup_matches();
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return instance;
        }
        JSValue result = JS_Call(ctx, on_match, argv[1], 1, &instance);
        JS_FreeValue(ctx, instance);
        if (JS_IsException(result)) {
            cleanup_matches();
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return result;
        }
        JS_FreeValue(ctx, result);
    }

    JSValue complete_result = JS_Call(ctx, on_complete, argv[1], 0, nullptr);
    cleanup_matches();
    JS_FreeValue(ctx, on_match);
    JS_FreeValue(ctx, on_complete);
    if (JS_IsException(complete_result)) {
        return complete_result;
    }
    JS_FreeValue(ctx, complete_result);
    return JS_UNDEFINED;
}

JSValue JsJavaEnumerateLoadedClasses(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.enumerateLoadedClasses requires callbacks");
    }
    if (!JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java.enumerateLoadedClasses callbacks must be an object");
    }

    JSValue on_match = JS_GetPropertyStr(ctx, argv[0], "onMatch");
    JSValue on_complete = JS_GetPropertyStr(ctx, argv[0], "onComplete");
    if (JS_IsException(on_match) || JS_IsException(on_complete)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_EXCEPTION;
    }
    if (!JS_IsFunction(ctx, on_match)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.enumerateLoadedClasses onMatch must be a function");
    }
    if (!JS_IsFunction(ctx, on_complete)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.enumerateLoadedClasses onComplete must be a function");
    }

    RuntimeState& state = GetRuntimeState();
#if !defined(__ANDROID__)
    if (state.java_hook_installer_dependencies.enumerate_loaded_classes == nullptr) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "Java.enumerateLoadedClasses is only available on Android");
    }
#endif

    std::vector<std::string> class_names;
    std::string error_message;
    if (!EnumerateLoadedJavaClasses(state.java_hook_installer_dependencies,
                                    &class_names,
                                    &error_message)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    std::unordered_set<std::string> seen;
    seen.reserve(class_names.size());
    for (const std::string& class_name : class_names) {
        if (!seen.insert(class_name).second) {
            continue;
        }

        JSValue argv_match[1] = {
            JS_NewString(ctx, class_name.c_str())
        };
        if (JS_IsException(argv_match[0])) {
            JS_FreeValue(ctx, argv_match[0]);
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return JS_EXCEPTION;
        }
        JSValue result = JS_Call(ctx, on_match, argv[0], 1, argv_match);
        JS_FreeValue(ctx, argv_match[0]);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return result;
        }
        JS_FreeValue(ctx, result);
    }

    JSValue complete_result = JS_Call(ctx, on_complete, argv[0], 0, nullptr);
    JS_FreeValue(ctx, on_match);
    JS_FreeValue(ctx, on_complete);
    if (JS_IsException(complete_result)) {
        return complete_result;
    }
    JS_FreeValue(ctx, complete_result);
    return JS_UNDEFINED;
}

JSValue JsJavaEnumerateClassLoaders(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.enumerateClassLoaders requires callbacks");
    }
    if (!JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "Java.enumerateClassLoaders callbacks must be an object");
    }

    JSValue on_match = JS_GetPropertyStr(ctx, argv[0], "onMatch");
    JSValue on_complete = JS_GetPropertyStr(ctx, argv[0], "onComplete");
    if (JS_IsException(on_match) || JS_IsException(on_complete)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_EXCEPTION;
    }
    if (!JS_IsFunction(ctx, on_match)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.enumerateClassLoaders onMatch must be a function");
    }
    if (!JS_IsFunction(ctx, on_complete)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowTypeError(ctx, "Java.enumerateClassLoaders onComplete must be a function");
    }

    RuntimeState& state = GetRuntimeState();
#if !defined(__ANDROID__)
    if (state.java_hook_installer_dependencies.enumerate_class_loaders == nullptr) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "Java.enumerateClassLoaders is only available on Android");
    }
#endif

    std::vector<JavaJsValue> matches;
    std::string error_message;
    if (!EnumerateJavaClassLoaders(state.java_hook_installer_dependencies,
                                   &matches,
                                   &error_message)) {
        JS_FreeValue(ctx, on_match);
        JS_FreeValue(ctx, on_complete);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    std::unordered_set<uint64_t> seen;
    seen.reserve(matches.size());
    for (const JavaJsValue& match : matches) {
        if (match.kind != JavaJsValueKind::kObject) {
            continue;
        }
        if (!seen.insert(match.object_handle).second) {
            continue;
        }

        JSValue loader = MakeJavaJsValue(ctx, match);
        if (JS_IsException(loader)) {
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return loader;
        }
        JSValue result = JS_Call(ctx, on_match, argv[0], 1, &loader);
        JS_FreeValue(ctx, loader);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, on_match);
            JS_FreeValue(ctx, on_complete);
            return result;
        }
        JS_FreeValue(ctx, result);
    }

    JSValue complete_result = JS_Call(ctx, on_complete, argv[0], 0, nullptr);
    JS_FreeValue(ctx, on_match);
    JS_FreeValue(ctx, on_complete);
    if (JS_IsException(complete_result)) {
        return complete_result;
    }
    JS_FreeValue(ctx, complete_result);
    return JS_UNDEFINED;
}

JSValue JsJavaCast(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Java.cast requires object and class wrapper");
    }

    std::string error_message;
    JavaJsValue source_value = {};
    if (!ParseJavaJsValue(ctx, argv[0], &source_value, &error_message) ||
        source_value.kind != JavaJsValueKind::kObject) {
        return JS_ThrowTypeError(ctx, "Java.cast object must be a Java object wrapper");
    }
    if (source_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.cast object handle is invalid");
    }

    if (!JS_IsObject(argv[1])) {
        return JS_ThrowTypeError(ctx, "Java.cast target must be a Java class wrapper");
    }

    JSValue class_name_value = JS_GetPropertyStr(ctx, argv[1], "$className");
    JSValue receiver_handle_value = JS_GetPropertyStr(ctx, argv[1], kJavaReceiverHandleProperty);
    JSValue loader_handle_value = JS_GetPropertyStr(ctx, argv[1], kJavaLoaderHandleProperty);
    if (JS_IsException(class_name_value) ||
        JS_IsException(receiver_handle_value) ||
        JS_IsException(loader_handle_value)) {
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(ctx, "Java.cast target must be a Java class wrapper");
    }

    const char* class_name = JS_ToCString(ctx, class_name_value);
    bool is_class_wrapper = false;
    if (class_name != nullptr && class_name[0] != '\0' &&
        !JS_IsUndefined(receiver_handle_value) && !JS_IsNull(receiver_handle_value)) {
        uint64_t receiver_handle = UINT64_MAX;
        if (ParsePointerValue(ctx, receiver_handle_value, &receiver_handle) && receiver_handle == 0u) {
            is_class_wrapper = true;
        }
    }

    if (!is_class_wrapper) {
        if (class_name != nullptr) {
            JS_FreeCString(ctx, class_name);
        }
        JS_FreeValue(ctx, class_name_value);
        JS_FreeValue(ctx, receiver_handle_value);
        JS_FreeValue(ctx, loader_handle_value);
        return JS_ThrowTypeError(ctx, "Java.cast target must be a Java class wrapper");
    }

    uint64_t loader_handle = 0u;
    if (!JS_IsUndefined(loader_handle_value) && !JS_IsNull(loader_handle_value)) {
        if (!ParsePointerValue(ctx, loader_handle_value, &loader_handle)) {
            if (class_name != nullptr) {
                JS_FreeCString(ctx, class_name);
            }
            JS_FreeValue(ctx, class_name_value);
            JS_FreeValue(ctx, receiver_handle_value);
            JS_FreeValue(ctx, loader_handle_value);
            return JS_ThrowTypeError(ctx, "Java.cast target loader handle is invalid");
        }
    }

    JSValue wrapper = CreateJavaUseWrapper(
        ctx, class_name, source_value.object_handle, loader_handle);
    JS_FreeCString(ctx, class_name);
    JS_FreeValue(ctx, class_name_value);
    JS_FreeValue(ctx, receiver_handle_value);
    JS_FreeValue(ctx, loader_handle_value);
    if (JS_IsException(wrapper)) {
        return JS_ThrowInternalError(ctx, "build Java.cast wrapper failed");
    }
    return wrapper;
}

JSValue JsJavaRetain(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Java.retain requires a Java object wrapper");
    }

    std::string error_message;
    JavaJsValue source_value = {};
    if (!ParseJavaJsValue(ctx, argv[0], &source_value, &error_message) ||
        source_value.kind != JavaJsValueKind::kObject) {
        return JS_ThrowTypeError(ctx, "Java.retain requires a Java object wrapper");
    }
    if (source_value.object_handle == 0u) {
        return JS_ThrowTypeError(ctx, "Java.retain object handle is invalid");
    }

    RuntimeState& state = GetRuntimeState();
#if !defined(__ANDROID__)
    if (state.java_hook_installer_dependencies.retain_object == nullptr) {
        return JS_ThrowInternalError(ctx, "Java.retain is only available on Android");
    }
#endif

    uint64_t retained_handle = 0u;
    if (!RetainJavaObject(source_value.object_handle,
                          state.java_hook_installer_dependencies,
                          &retained_handle,
                          &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    if (retained_handle == 0u) {
        return JS_ThrowInternalError(ctx, "Java.retain returned an invalid handle");
    }

    const char* class_name = source_value.object_class_name.empty()
        ? "java.lang.Object"
        : source_value.object_class_name.c_str();
    JSValue wrapper = CreateJavaUseWrapper(ctx, class_name, retained_handle, 0u, true);
    if (JS_IsException(wrapper)) {
        return JS_ThrowInternalError(ctx, "build Java.retain wrapper failed");
    }
    return wrapper;
}

bool InstallJavaBootstrap(JSContext* ctx, std::string* error_message) {
    static const char* kJavaBootstrapSource =
        "(function () {"
        "  var readyCallbacks = [];"
        "  var readyFired = false;"
        "  var defaultLoaderHandle;"
        "  function isLoaderWrapper(loader) {"
        "    return loader !== null && typeof loader === 'object' &&"
        "      typeof loader.$className === 'string' &&"
        "      loader.$className.indexOf('ClassLoader') !== -1 &&"
        "      (loader.__nookJavaReceiverHandle !== undefined || loader.__jptr !== undefined);"
        "  }"
        "  function getLoaderHandle(loader) {"
        "    return loader.__nookJavaReceiverHandle !== undefined"
        "      ? loader.__nookJavaReceiverHandle"
        "      : loader.__jptr;"
        "  }"
        "  function drainReadyCallbacks() {"
        "    var pending = readyCallbacks.slice();"
        "    readyCallbacks = [];"
        "    for (var i = 0; i < pending.length; i++) {"
        "      var entry = pending[i];"
        "      try {"
        "        __nookRunInScript(entry.scriptId, entry.callback);"
        "      } catch (e) {"
        "        send('java-ready-callback-error:' + String(e));"
        "      }"
        "    }"
        "  }"
        "  Java.ready = function (fn) {"
        "    if (typeof fn !== 'function') {"
        "      throw new TypeError('Java.ready requires a function');"
        "    }"
        "    if (readyFired ||"
        "        Java._isClassLoaderReady() ||"
        "        (typeof Java._isLifecycleReady === 'function' && Java._isLifecycleReady())) {"
        "      readyFired = true;"
        "      fn();"
        "      return;"
        "    }"
        "    readyCallbacks.push({"
        "      scriptId: __nookGetCurrentScriptId(),"
        "      callback: fn"
        "    });"
        "  };"
        "  Java.__nookDispatchReady = function () {"
        "    if (readyFired) {"
        "      return;"
        "    }"
        "    if (!(Java._isClassLoaderReady() ||"
        "          (typeof Java._isLifecycleReady === 'function' && Java._isLifecycleReady()))) {"
        "      return;"
        "    }"
        "    readyFired = true;"
        "    drainReadyCallbacks();"
        "  };"
        "  Java.__nookDropReadyCallbacksForScript = function (scriptId) {"
        "    var kept = [];"
        "    for (var i = 0; i < readyCallbacks.length; i++) {"
        "      if (readyCallbacks[i].scriptId !== scriptId) {"
        "        kept.push(readyCallbacks[i]);"
        "      }"
        "    }"
        "    readyCallbacks = kept;"
        "  };"
        "  Java.performNow = function (fn) {"
        "    if (typeof fn !== 'function') {"
        "      throw new TypeError('Java.performNow requires a function');"
        "    }"
        "    return Java.vm.perform(fn);"
        "  };"
        "  Java.perform = function (fn) {"
        "    if (typeof fn !== 'function') {"
        "      throw new TypeError('Java.perform requires a function');"
        "    }"
        "    if (typeof Java._isClassLoaderReady === 'function' &&"
        "        Java._isClassLoaderReady()) {"
        "      return Java.vm.perform(fn);"
        "    }"
        "    return Java.ready(function () {"
        "      return Java.vm.perform(fn);"
        "    });"
        "  };"
        "  Java.array = function (typeName, elements) {"
        "    if (typeof typeName !== 'string' || typeName.length === 0) {"
        "      throw new TypeError('Java.array requires a non-empty type name');"
        "    }"
        "    if (!Array.isArray(elements)) {"
        "      throw new TypeError('Java.array elements must be an array');"
        "    }"
        "    var array = elements.slice();"
        "    array.__nookJavaArrayType = typeName;"
        "    array.$className = typeName + '[]';"
        "    return array;"
        "  };"
        "  var nativeUse = Java.use;"
        "  var nativeChoose = Java.choose;"
        "  var nativeCast = Java.cast;"
        "  var nativeRetain = Java.retain;"
        "  var mainThreadRunnableSerial = 0;"
        "  var cachedLooperClass = null;"
        "  var cachedHandlerClass = null;"
        "  var cachedRunnableInterface = null;"
        "  var cachedMainHandler = null;"
        "  function getLooperClass() {"
        "    if (cachedLooperClass === null) {"
        "      cachedLooperClass = Java.use('android.os.Looper');"
        "    }"
        "    return cachedLooperClass;"
        "  }"
        "  function getHandlerClass() {"
        "    if (cachedHandlerClass === null) {"
        "      cachedHandlerClass = Java.use('android.os.Handler');"
        "    }"
        "    return cachedHandlerClass;"
        "  }"
        "  function getRunnableInterface() {"
        "    if (cachedRunnableInterface === null) {"
        "      cachedRunnableInterface = Java.use('java.lang.Runnable');"
        "    }"
        "    return cachedRunnableInterface;"
        "  }"
        "  function getMainHandler() {"
        "    if (cachedMainHandler === null) {"
        "      cachedMainHandler = getHandlerClass().$new(getLooperClass().getMainLooper());"
        "    }"
        "    return cachedMainHandler;"
        "  }"
        "  function getCurrentApplicationIfAvailable() {"
        "    try {"
        "      var ActivityThread = nativeUse('android.app.ActivityThread');"
        "      var app = ActivityThread.currentApplication();"
        "      if (app === null || app === undefined) {"
        "        return null;"
        "      }"
        "      return app;"
        "    } catch (e) {"
        "      return null;"
        "    }"
        "  }"
        "  Java.isMainThread = function () {"
        "    var Looper = getLooperClass();"
        "    var current = Looper.myLooper();"
        "    var main = Looper.getMainLooper();"
        "    if (current === null || current === undefined ||"
        "        main === null || main === undefined) {"
        "      return false;"
        "    }"
        "    return current.equals.overload('java.lang.Object')(main);"
        "  };"
        "  Java.scheduleOnMainThread = function (fn) {"
        "    if (typeof fn !== 'function') {"
        "      throw new TypeError('Java.scheduleOnMainThread requires a function');"
        "    }"
        "    if (getCurrentApplicationIfAvailable() === null) {"
        "      Java.ready(function () {"
        "        Java.scheduleOnMainThread(fn);"
        "      });"
        "      return;"
        "    }"
        "    var Runnable = Java.registerClass({"
        "      name: 'com.nook.MainThreadRunnable' + (++mainThreadRunnableSerial),"
        "      implements: [getRunnableInterface()],"
        "      methods: {"
        "        run: function () {"
        "          return fn();"
        "        }"
        "      }"
        "    });"
        "    var runnable = Runnable.$new();"
        "    getMainHandler().post.overload('java.lang.Runnable')(runnable);"
        "  };"
        "  function attachOwnedHandleWeakCleanup(wrapper) {"
        "    if (wrapper === null || typeof wrapper !== 'object') {"
        "      return wrapper;"
        "    }"
        "    var handle = wrapper.__nookJavaReceiverHandle;"
        "    wrapper.__nookJavaOwnedHandle = true;"
        "    if (handle === undefined || handle === null || handle === '0x0') {"
        "      wrapper.__nookJavaWeakToken = 0;"
        "      return wrapper;"
        "    }"
        "    if (wrapper.__nookJavaWeakToken) {"
        "      return wrapper;"
        "    }"
        "    if (typeof Script !== 'object' || Script === null || typeof Script.bindWeak !== 'function') {"
        "      wrapper.__nookJavaWeakToken = 0;"
        "      return wrapper;"
        "    }"
        "    var weakState = {"
        "      handle: handle,"
        "      disposed: false"
        "    };"
        "    wrapper.__nookJavaWeakState = weakState;"
        "    wrapper.__nookJavaWeakToken = Script.bindWeak(wrapper, function () {"
        "      try {"
        "        var currentHandle = weakState.handle;"
        "        weakState.handle = '0x0';"
        "        if (weakState.disposed === true ||"
        "            currentHandle === undefined ||"
        "            currentHandle === null ||"
        "            currentHandle === '0x0') {"
        "          return;"
        "        }"
        "        __nookJavaRelease(currentHandle);"
        "      } catch (e) {"
        "        send('java-auto-cleanup-weak-error:' + String(e));"
        "      }"
        "    });"
        "    return wrapper;"
        "  }"
        "  Java.registerClass = function (spec) {"
        "    if (spec === null || typeof spec !== 'object') {"
        "      throw new TypeError('Java.registerClass requires a spec object');"
        "    }"
        "    if (typeof spec.name !== 'string' || spec.name.length === 0) {"
        "      throw new TypeError('Java.registerClass requires a non-empty spec.name');"
        "    }"
        "    if (!Array.isArray(spec.implements) || spec.implements.length === 0) {"
        "      throw new TypeError('Java.registerClass requires a non-empty spec.implements array');"
        "    }"
        "    if (spec.methods === null || typeof spec.methods !== 'object') {"
        "      throw new TypeError('Java.registerClass requires a spec.methods object');"
        "    }"
        "    if (spec.fields !== undefined && spec.fields !== null) {"
        "      throw new TypeError('Java.registerClass spec.fields is not supported by the current proxy implementation');"
        "    }"
        "    if (spec.superClass !== undefined && spec.superClass !== null) {"
        "      throw new TypeError('Java.registerClass spec.superClass is not supported by the current proxy implementation');"
        "    }"
        "    return {"
        "      $name: spec.name,"
        "      $spec: spec,"
        "      $new: function () {"
        "        var loaderHandle = defaultLoaderHandle;"
        "        if (loaderHandle === undefined &&"
        "            spec.implements.length > 0 &&"
        "            spec.implements[0] !== null &&"
        "            typeof spec.implements[0] === 'object') {"
        "          loaderHandle = spec.implements[0].__nookJavaLoaderHandle;"
        "        }"
        "        var interfaceClassNames = spec.implements.map(function (iface) {"
        "          if (iface === null || typeof iface !== 'object' || typeof iface.$className !== 'string') {"
        "            throw new TypeError('Java.registerClass implements entries must be Java class wrappers');"
        "          }"
        "          return iface.$className;"
        "        });"
        "        return __nookJavaRegisterClass(spec.name, interfaceClassNames, spec.methods, loaderHandle);"
        "      }"
        "    };"
        "  };"
        "  Java.setClassLoader = function (loader) {"
        "    if (!isLoaderWrapper(loader)) {"
        "      throw new TypeError('Java.setClassLoader requires a Java ClassLoader wrapper');"
        "    }"
        "    defaultLoaderHandle = getLoaderHandle(loader);"
        "    return loader;"
        "  };"
        "  Java.use = function (className) {"
        "    if (typeof className !== 'string') {"
        "      throw new TypeError('Java.use requires a class name');"
        "    }"
        "    if (defaultLoaderHandle !== undefined) {"
        "      return __nookJavaUseWithLoader(className, defaultLoaderHandle);"
        "    }"
        "    return nativeUse(className);"
        "  };"
        "  Java.choose = function (className, callbacks, loaderHandle) {"
        "    if (arguments.length >= 3) {"
        "      return nativeChoose(className, callbacks, loaderHandle);"
        "    }"
        "    if (defaultLoaderHandle !== undefined) {"
        "      return nativeChoose(className, callbacks, defaultLoaderHandle);"
        "    }"
        "    return nativeChoose(className, callbacks);"
        "  };"
        "  Java.cast = function (objectWrapper, classWrapper) {"
        "    if (defaultLoaderHandle !== undefined &&"
        "        classWrapper !== null && typeof classWrapper === 'object' &&"
        "        typeof classWrapper.$className === 'string') {"
        "      var targetLoaderHandle = classWrapper.__nookJavaLoaderHandle;"
        "      if (targetLoaderHandle === undefined || targetLoaderHandle === null || targetLoaderHandle === '0x0') {"
        "        classWrapper = __nookJavaUseWithLoader(classWrapper.$className, defaultLoaderHandle);"
        "      }"
        "    }"
        "    return nativeCast(objectWrapper, classWrapper);"
        "  };"
        "  Java.retain = function (objectWrapper) {"
        "    var kept = nativeRetain(objectWrapper);"
        "    var result = kept;"
        "    if (defaultLoaderHandle !== undefined &&"
        "        kept !== null && typeof kept === 'object' && typeof kept.$className === 'string') {"
        "      var loaderClassWrapper = __nookJavaUseWithLoader(kept.$className, defaultLoaderHandle);"
        "      result = nativeCast(kept, loaderClassWrapper);"
        "    }"
        "    return attachOwnedHandleWeakCleanup(result);"
        "  };"
        "  Java.openClassFile = function (filePath) {"
        "    if (typeof filePath !== 'string') {"
        "      throw new TypeError('Java.openClassFile requires a file path string');"
        "    }"
        "    return {"
        "      load: function () {"
        "        var ActivityThread = nativeUse('android.app.ActivityThread');"
        "        var app = ActivityThread.currentApplication();"
        "        if (app === null || app === undefined) {"
        "          throw new Error('Java.openClassFile.load could not get current Application');"
        "        }"
        "        var codeCachePath = app.getCodeCacheDir().getAbsolutePath();"
        "        var parentLoader = app.getClassLoader();"
        "        var DexClassLoader = nativeUse('dalvik.system.DexClassLoader');"
        "        var loader = DexClassLoader.$new("
        "          '(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V',"
        "          filePath,"
        "          codeCachePath,"
        "          '',"
        "          parentLoader"
        "        );"
        "        Java.setClassLoader(loader);"
        "        return loader;"
        "      }"
        "    };"
        "  };"
        "  Java.ClassFactory = {"
        "    get: function (loader) {"
        "      if (!isLoaderWrapper(loader)) {"
        "        throw new TypeError('Java.ClassFactory.get requires a Java ClassLoader wrapper');"
        "      }"
        "      var loaderHandle = getLoaderHandle(loader);"
        "      return {"
        "        use: function (className) {"
        "          if (typeof className !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.use requires a class name');"
        "          }"
        "          return __nookJavaUseWithLoader(className, loaderHandle);"
        "        },"
        "        choose: function (className, callbacks) {"
        "          if (typeof className !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.choose requires a class name');"
        "          }"
        "          return Java.choose(className, callbacks, loaderHandle);"
        "        },"
        "        cast: function (objectWrapper, classWrapper) {"
        "          if (classWrapper === null || typeof classWrapper !== 'object' ||"
        "              typeof classWrapper.$className !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.cast requires a Java class wrapper');"
        "          }"
        "          var loaderClassWrapper = __nookJavaUseWithLoader(classWrapper.$className, loaderHandle);"
        "          return Java.cast(objectWrapper, loaderClassWrapper);"
        "        },"
        "        retain: function (objectWrapper) {"
        "          var kept = nativeRetain(objectWrapper);"
        "          if (kept === null || typeof kept !== 'object' || typeof kept.$className !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.retain requires a Java object wrapper');"
        "          }"
        "          var loaderClassWrapper = __nookJavaUseWithLoader(kept.$className, loaderHandle);"
        "          return attachOwnedHandleWeakCleanup(Java.cast(kept, loaderClassWrapper));"
        "        },"
        "        $new: function (className) {"
        "          if (typeof className !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.$new requires a class name');"
        "          }"
        "          var classWrapper = __nookJavaUseWithLoader(className, loaderHandle);"
        "          return classWrapper.$new.apply(classWrapper, Array.prototype.slice.call(arguments, 1));"
        "        },"
        "        openClassFile: function (filePath) {"
        "          if (typeof filePath !== 'string') {"
        "            throw new TypeError('Java.ClassFactory.openClassFile requires a file path string');"
        "          }"
        "          return {"
        "            load: function () {"
        "              var ActivityThread = nativeUse('android.app.ActivityThread');"
        "              var app = ActivityThread.currentApplication();"
        "              if (app === null || app === undefined) {"
        "                throw new Error('Java.ClassFactory.openClassFile.load could not get current Application');"
        "              }"
        "              var codeCachePath = app.getCodeCacheDir().getAbsolutePath();"
        "              var DexClassLoader = nativeUse('dalvik.system.DexClassLoader');"
        "              return DexClassLoader.$new("
        "                '(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V',"
        "                filePath,"
        "                codeCachePath,"
        "                '',"
        "                loader"
        "              );"
        "            }"
        "          };"
        "        }"
        "      };"
        "    }"
        "  };"
        "})();";

    JSValue result = JS_Eval(ctx,
                             kJavaBootstrapSource,
                             std::strlen(kJavaBootstrapSource),
                             "<java_bootstrap>",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    JS_FreeValue(ctx, result);
    return true;
}

JSValue JsPtr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "ptr requires a value");
    }

    uint64_t value = 0;
    if (!ParsePointerValue(ctx, argv[0], &value)) {
        return JS_ThrowTypeError(ctx, "ptr value must be a pointer string or number");
    }
    return MakeNativePointer(ctx, value);
}

JSValue JsNativeFunctionInvoke(JSContext* ctx,
                               JSValueConst this_val,
                               int argc,
                               JSValueConst* argv,
                               int magic,
                               JSValue* func_data) {
    (void)this_val;
    (void)magic;

    uint64_t target_address = 0u;
    if (!ParsePointerValue(ctx, func_data[0], &target_address) || target_address == 0u) {
        return JS_ThrowInternalError(ctx, "NativeFunction target metadata is invalid");
    }

    uint32_t return_type_raw = 0u;
    if (JS_ToUint32(ctx, &return_type_raw, func_data[1]) < 0) {
        return JS_ThrowInternalError(ctx, "NativeFunction return type metadata is invalid");
    }
    const NativeFunctionValueType return_type =
        static_cast<NativeFunctionValueType>(return_type_raw);

    uint32_t expected_argc = 0u;
    if (!GetArrayLength(ctx, func_data[2], &expected_argc)) {
        return JS_ThrowInternalError(ctx, "NativeFunction argument metadata is invalid");
    }
    if (static_cast<uint32_t>(argc) != expected_argc) {
        return JS_ThrowTypeError(ctx, "NativeFunction wrong number of arguments");
    }

    std::vector<NativeFunctionValueType> arg_types;
    arg_types.reserve(expected_argc);
    NativeCallValue native_args[4] = {};
    for (uint32_t index = 0u; index < expected_argc; ++index) {
        JSValue arg_type_value = JS_GetPropertyUint32(ctx, func_data[2], index);
        if (JS_IsException(arg_type_value)) {
            JS_FreeValue(ctx, arg_type_value);
            return JS_ThrowInternalError(ctx, "NativeFunction argument metadata is invalid");
        }

        uint32_t arg_type_raw = 0u;
        if (JS_ToUint32(ctx, &arg_type_raw, arg_type_value) < 0) {
            JS_FreeValue(ctx, arg_type_value);
            return JS_ThrowInternalError(ctx, "NativeFunction argument metadata is invalid");
        }
        JS_FreeValue(ctx, arg_type_value);

        const NativeFunctionValueType arg_type =
            static_cast<NativeFunctionValueType>(arg_type_raw);
        const char* parse_error = "NativeFunction argument type metadata is invalid";
        if (!ParseJsNativeCallValue(ctx, argv[index], arg_type, &native_args[index], &parse_error)) {
            return JS_ThrowTypeError(ctx, "%s", parse_error);
        }
        arg_types.push_back(arg_type);
    }

    RuntimeState& state = GetRuntimeState();
    NativeCallValue result = {};

    uint32_t callback_script_id = 0u;
    RuntimeState::NativeCallbackRecord* direct_callback =
        FindNativeCallbackRecordByAddressLocked(state, target_address, &callback_script_id);
    if (direct_callback != nullptr) {
        if (!InvokeNativeCallbackRecordLocked(state,
                                              callback_script_id,
                                              direct_callback,
                                              native_args,
                                              &result)) {
            return JS_ThrowInternalError(ctx, "NativeCallback invocation failed");
        }
        return NativeCallValueToJs(ctx, direct_callback->return_type, result);
    }

    uint32_t replace_script_id = 0u;
    RuntimeState::ReplaceHookRecord* replace_record =
        FindReplaceHookRecordByTargetLocked(state, target_address, &replace_script_id);
    if (replace_record != nullptr) {
        uint32_t replacement_script_id = 0u;
        RuntimeState::NativeCallbackRecord* replacement_record =
            FindNativeCallbackRecordByAddressLocked(state,
                                                    replace_record->replacement_address,
                                                    &replacement_script_id);
        if (replacement_record != nullptr) {
            (void)replace_script_id;
            if (!InvokeNativeCallbackRecordLocked(state,
                                                  replacement_script_id,
                                                  replacement_record,
                                                  native_args,
                                                  &result)) {
                return JS_ThrowInternalError(ctx, "replacement NativeCallback invocation failed");
            }
            return NativeCallValueToJs(ctx, replacement_record->return_type, result);
        }
    }

    if (!DispatchTypedNativeFunction(target_address, return_type, arg_types, native_args, &result)) {
        return JS_ThrowInternalError(ctx, "NativeFunction typed dispatch failed");
    }
    return NativeCallValueToJs(ctx, return_type, result);
}

JSValue CreateNativeFunctionValue(JSContext* ctx,
                                  uint64_t target_address,
                                  NativeFunctionValueType return_type,
                                  JSValueConst arg_types_value) {
    if (target_address == 0u) {
        return JS_ThrowTypeError(ctx, "NativeFunction address must be a non-zero pointer value");
    }
    if (!JS_IsArray(ctx, arg_types_value)) {
        return JS_ThrowTypeError(ctx, "NativeFunction argTypes must be an array");
    }

    JSValue arg_types = JS_DupValue(ctx, arg_types_value);
    uint32_t length = 0u;
    if (!GetArrayLength(ctx, arg_types, &length)) {
        JS_FreeValue(ctx, arg_types);
        return JS_ThrowInternalError(ctx, "NativeFunction read argTypes length failed");
    }

    JSValue address_value = JS_NewString(ctx, FormatHookValue(target_address).c_str());
    JSValue return_type_value = JS_NewUint32(ctx, static_cast<uint32_t>(return_type));
    JSValue func_data[3] = {
        address_value,
        return_type_value,
        arg_types,
    };
    JSValue function = JS_NewCFunctionData(ctx,
                                           JsNativeFunctionInvoke,
                                           static_cast<int>(length),
                                           0,
                                           3,
                                           func_data);
    if (!JS_IsException(function)) {
        if (JS_DefinePropertyValueStr(ctx,
                                      function,
                                      kNativeFunctionTargetProperty,
                                      MakeNativePointer(ctx, target_address),
                                      JS_PROP_CONFIGURABLE) < 0 ||
            JS_DefinePropertyValueStr(ctx,
                                      function,
                                      kNativeFunctionReturnTypeProperty,
                                      JS_NewUint32(ctx, static_cast<uint32_t>(return_type)),
                                      JS_PROP_CONFIGURABLE) < 0 ||
            JS_DefinePropertyValueStr(ctx,
                                      function,
                                      kNativeFunctionArgTypesProperty,
                                      JS_DupValue(ctx, arg_types),
                                      JS_PROP_CONFIGURABLE) < 0) {
            JS_FreeValue(ctx, function);
            function = JS_EXCEPTION;
        }
    }
    JS_FreeValue(ctx, address_value);
    JS_FreeValue(ctx, return_type_value);
    JS_FreeValue(ctx, arg_types);

    return function;
}

JSValue JsNativeFunctionConstructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc != 3) {
        return JS_ThrowTypeError(ctx, "NativeFunction requires address, returnType, and argTypes");
    }

    uint64_t target_address = 0u;
    if (!ParsePointerValue(ctx, argv[0], &target_address) || target_address == 0u) {
        return JS_ThrowTypeError(ctx, "NativeFunction address must be a non-zero pointer value");
    }

    const char* return_type_cstr = JS_ToCString(ctx, argv[1]);
    if (return_type_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "NativeFunction returnType must be a string");
    }

    NativeFunctionValueType return_type = NativeFunctionValueType::kVoid;
    const std::string return_type_text = return_type_cstr;
    JS_FreeCString(ctx, return_type_cstr);
    if (!ParseNativeFunctionValueType(return_type_text, &return_type)) {
        return JS_ThrowTypeError(ctx, "NativeFunction unsupported return type");
    }

    JSValue arg_types = JS_UNDEFINED;
    if (!ParseNativeFunctionValueTypeArray(ctx, argv[2], &arg_types)) {
        JS_FreeValue(ctx, arg_types);
        return JS_EXCEPTION;
    }

    JSValue function = CreateNativeFunctionValue(ctx, target_address, return_type, arg_types);
    JS_FreeValue(ctx, arg_types);
    return function;
}

bool TryGetNativeFunctionMetadata(JSContext* ctx,
                                  JSValueConst value,
                                  uint64_t* target_address_out,
                                  NativeFunctionValueType* return_type_out,
                                  std::vector<NativeFunctionValueType>* arg_types_out) {
    if (target_address_out == nullptr || return_type_out == nullptr || arg_types_out == nullptr) {
        return false;
    }
    *target_address_out = 0u;
    *return_type_out = NativeFunctionValueType::kVoid;
    arg_types_out->clear();

    if (!JS_IsFunction(ctx, value)) {
        return false;
    }

    JSValue target_value = JS_GetPropertyStr(ctx, value, kNativeFunctionTargetProperty);
    JSValue return_type_value = JS_GetPropertyStr(ctx, value, kNativeFunctionReturnTypeProperty);
    JSValue arg_types_value = JS_GetPropertyStr(ctx, value, kNativeFunctionArgTypesProperty);
    const bool ok = !JS_IsException(target_value) &&
                    !JS_IsException(return_type_value) &&
                    !JS_IsException(arg_types_value) &&
                    ParsePointerValue(ctx, target_value, target_address_out) &&
                    *target_address_out != 0u &&
                    JS_IsArray(ctx, arg_types_value);
    if (!ok) {
        JS_FreeValue(ctx, target_value);
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, arg_types_value);
        return false;
    }

    uint32_t return_type_raw = 0u;
    if (JS_ToUint32(ctx, &return_type_raw, return_type_value) < 0) {
        JS_FreeValue(ctx, target_value);
        JS_FreeValue(ctx, return_type_value);
        JS_FreeValue(ctx, arg_types_value);
        return false;
    }
    *return_type_out = static_cast<NativeFunctionValueType>(return_type_raw);
    const bool arg_types_ok = ReadNativeFunctionValueTypeMetadataArray(ctx,
                                                                       arg_types_value,
                                                                       arg_types_out);
    JS_FreeValue(ctx, target_value);
    JS_FreeValue(ctx, return_type_value);
    JS_FreeValue(ctx, arg_types_value);
    return arg_types_ok;
}

bool ParseNativeCallbackTypeArray(JSContext* ctx,
                                  JSValueConst value,
                                  std::vector<NativeFunctionValueType>* types_out) {
    if (types_out == nullptr) {
        return false;
    }
    types_out->clear();

    if (!JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "NativeCallback argTypes must be an array");
        return false;
    }

    uint32_t length = 0u;
    if (!GetArrayLength(ctx, value, &length)) {
        JS_ThrowInternalError(ctx, "NativeCallback read argTypes length failed");
        return false;
    }
    if (length > 4u) {
        JS_ThrowTypeError(ctx, "NativeCallback supports at most 4 arguments");
        return false;
    }

    types_out->reserve(length);
    for (uint32_t index = 0u; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(ctx, value, index);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, item);
            return false;
        }
        const char* item_cstr = JS_ToCString(ctx, item);
        JS_FreeValue(ctx, item);
        if (item_cstr == nullptr) {
            JS_ThrowTypeError(ctx, "NativeCallback argument type must be a string");
            return false;
        }
        const std::string item_text = item_cstr;
        JS_FreeCString(ctx, item_cstr);

        NativeFunctionValueType item_type = NativeFunctionValueType::kVoid;
        if (!ParseNativeFunctionValueType(item_text, &item_type) ||
            item_type == NativeFunctionValueType::kVoid) {
            JS_ThrowTypeError(ctx, "NativeCallback unsupported argument type");
            return false;
        }
        types_out->push_back(item_type);
    }

    return true;
}

uint64_t InvokeNativeCallbackSlot(uint32_t slot,
                                  uint64_t arg0,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3) {
    RuntimeState& state = GetRuntimeState();
    if (state.context == nullptr) {
        return 0u;
    }

    uint32_t script_id = 0u;
    RuntimeState::NativeCallbackRecord* record =
        FindNativeCallbackRecordBySlotLocked(state, slot, &script_id);
    if (record == nullptr) {
        return 0u;
    }

    NativeCallValue native_args[4] = {};
    native_args[0].raw = arg0;
    native_args[1].raw = arg1;
    native_args[2].raw = arg2;
    native_args[3].raw = arg3;
    for (size_t index = 0u; index < 4u; ++index) {
        if (index >= record->arg_types.size()) {
            break;
        }
        if (record->arg_types[index] == NativeFunctionValueType::kFloat) {
            native_args[index].number =
                static_cast<double>(BitCastValue<float>(static_cast<uint32_t>(native_args[index].raw)));
        } else if (record->arg_types[index] == NativeFunctionValueType::kDouble) {
            native_args[index].number = BitCastValue<double>(native_args[index].raw);
        } else if (IsSignedIntegerType(record->arg_types[index])) {
            native_args[index].number = static_cast<double>(static_cast<int64_t>(native_args[index].raw));
        } else {
            native_args[index].number = static_cast<double>(native_args[index].raw);
        }
    }

    NativeCallValue result = {};
    if (!InvokeNativeCallbackRecordLocked(state, script_id, record, native_args, &result)) {
        return 0u;
    }
    return NativeCallValueToRawResult(record->return_type, result);
}

#define DEFINE_NATIVE_CALLBACK_TRAMPOLINE(index)                                                \
    extern "C" uint64_t NativeCallbackTrampoline##index(uint64_t arg0,                          \
                                                         uint64_t arg1,                          \
                                                         uint64_t arg2,                          \
                                                         uint64_t arg3) {                        \
        return InvokeNativeCallbackSlot(index, arg0, arg1, arg2, arg3);                         \
    }

DEFINE_NATIVE_CALLBACK_TRAMPOLINE(0)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(1)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(2)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(3)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(4)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(5)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(6)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(7)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(8)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(9)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(10)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(11)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(12)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(13)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(14)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(15)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(16)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(17)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(18)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(19)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(20)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(21)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(22)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(23)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(24)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(25)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(26)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(27)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(28)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(29)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(30)
DEFINE_NATIVE_CALLBACK_TRAMPOLINE(31)

#undef DEFINE_NATIVE_CALLBACK_TRAMPOLINE

const NativeCallbackTrampoline kNativeCallbackTrampolines[kMaxNativeCallbackSlots] = {
    &NativeCallbackTrampoline0,  &NativeCallbackTrampoline1,
    &NativeCallbackTrampoline2,  &NativeCallbackTrampoline3,
    &NativeCallbackTrampoline4,  &NativeCallbackTrampoline5,
    &NativeCallbackTrampoline6,  &NativeCallbackTrampoline7,
    &NativeCallbackTrampoline8,  &NativeCallbackTrampoline9,
    &NativeCallbackTrampoline10, &NativeCallbackTrampoline11,
    &NativeCallbackTrampoline12, &NativeCallbackTrampoline13,
    &NativeCallbackTrampoline14, &NativeCallbackTrampoline15,
    &NativeCallbackTrampoline16, &NativeCallbackTrampoline17,
    &NativeCallbackTrampoline18, &NativeCallbackTrampoline19,
    &NativeCallbackTrampoline20, &NativeCallbackTrampoline21,
    &NativeCallbackTrampoline22, &NativeCallbackTrampoline23,
    &NativeCallbackTrampoline24, &NativeCallbackTrampoline25,
    &NativeCallbackTrampoline26, &NativeCallbackTrampoline27,
    &NativeCallbackTrampoline28, &NativeCallbackTrampoline29,
    &NativeCallbackTrampoline30, &NativeCallbackTrampoline31,
};

JSValue JsNativeCallbackConstructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc != 3) {
        return JS_ThrowTypeError(ctx, "NativeCallback requires function, returnType, and argTypes");
    }
    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "NativeCallback first argument must be a function");
    }

    const char* return_type_cstr = JS_ToCString(ctx, argv[1]);
    if (return_type_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "NativeCallback returnType must be a string");
    }

    NativeFunctionValueType return_type = NativeFunctionValueType::kVoid;
    const std::string return_type_text = return_type_cstr;
    JS_FreeCString(ctx, return_type_cstr);
    if (!ParseNativeFunctionValueType(return_type_text, &return_type)) {
        return JS_ThrowTypeError(ctx, "NativeCallback unsupported return type");
    }

    std::vector<NativeFunctionValueType> arg_types;
    if (!ParseNativeCallbackTypeArray(ctx, argv[2], &arg_types)) {
        return JS_EXCEPTION;
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "NativeCallback requires an active script context");
    }

    uint64_t callback_address = 0u;
    std::string error_message;
    if (!RegisterNativeCallbackLocked(ctx,
                                      state,
                                      state.current_script_id,
                                      argv[0],
                                      return_type,
                                      arg_types,
                                      &callback_address,
                                      &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    return MakeNativePointer(ctx, callback_address);
}

uint32_t GetCurrentProcessIdForJs() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

uint32_t GetCurrentThreadIdForJs() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentThreadId());
#else
    return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
}

bool TryReadThreadStateForJs(uint32_t thread_id, std::string* state_out) {
    if (state_out == nullptr || thread_id == 0u) {
        return false;
    }
    *state_out = "unknown";

#if defined(_WIN32)
    (void)thread_id;
    *state_out = "running";
    return true;
#else
    char path[128] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%u/status", thread_id);
    FILE* status = std::fopen(path, "r");
    if (status == nullptr) {
        return false;
    }

    char line[256] = {};
    while (std::fgets(line, sizeof(line), status) != nullptr) {
        if (std::strncmp(line, "State:", 6) != 0) {
            continue;
        }

        char state_text[128] = {};
        if (std::sscanf(line, "State:%127[^\n]", state_text) == 1) {
            std::string parsed = TrimAsciiWhitespace(state_text);
            if (!parsed.empty()) {
                *state_out = parsed;
            }
        }
        std::fclose(status);
        return true;
    }

    std::fclose(status);
    return false;
#endif
}

bool TryReadThreadNameForJs(uint32_t thread_id, std::string* name_out) {
    if (name_out == nullptr || thread_id == 0u) {
        return false;
    }
    name_out->clear();

#if defined(_WIN32)
    std::ostringstream stream;
    stream << "thread-" << thread_id;
    *name_out = stream.str();
    return true;
#else
    char path[128] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%u/status", thread_id);
    FILE* status = std::fopen(path, "r");
    if (status == nullptr) {
        return false;
    }

    char line[256] = {};
    while (std::fgets(line, sizeof(line), status) != nullptr) {
        if (std::strncmp(line, "Name:", 5) != 0) {
            continue;
        }

        char name_text[128] = {};
        if (std::sscanf(line, "Name:%127[^\n]", name_text) == 1) {
            *name_out = TrimAsciiWhitespace(name_text);
        }
        std::fclose(status);
        return true;
    }

    std::fclose(status);
    return false;
#endif
}

bool CollectThreadsForJs(std::vector<NativeThreadRecord>* threads_out,
                         std::string* error_message) {
    if (threads_out == nullptr) {
        SetError(error_message, "threads_out is null");
        return false;
    }
    threads_out->clear();

#if defined(_WIN32)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        SetError(error_message, "CreateToolhelp32Snapshot failed");
        return false;
    }

    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    const DWORD process_id = GetCurrentProcessId();
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != process_id) {
                continue;
            }
            NativeThreadRecord record = {};
            record.id = static_cast<uint32_t>(entry.th32ThreadID);
            record.state = "running";
            TryReadThreadNameForJs(record.id, &record.name);
            threads_out->push_back(record);
        } while (Thread32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return true;
#else
    DIR* task_dir = opendir("/proc/self/task");
    if (task_dir == nullptr) {
        SetError(error_message, "open /proc/self/task failed");
        return false;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(task_dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char* end = nullptr;
        errno = 0;
        unsigned long tid = std::strtoul(entry->d_name, &end, 10);
        if (errno != 0 || end == entry->d_name || *end != '\0' || tid == 0u ||
            tid > static_cast<unsigned long>(UINT32_MAX)) {
            continue;
        }

        NativeThreadRecord record = {};
        record.id = static_cast<uint32_t>(tid);
        if (!TryReadThreadStateForJs(record.id, &record.state)) {
            record.state = "unknown";
        }
        if (!TryReadThreadNameForJs(record.id, &record.name)) {
            record.name.clear();
        }
        threads_out->push_back(record);
    }

    closedir(task_dir);
    std::sort(threads_out->begin(),
              threads_out->end(),
              [](const NativeThreadRecord& left, const NativeThreadRecord& right) {
                  return left.id < right.id;
              });
    return true;
#endif
}

bool IsDebuggerAttachedForJs() {
#if defined(_WIN32)
    return IsDebuggerPresent() != 0;
#else
    FILE* status = std::fopen("/proc/self/status", "r");
    if (status == nullptr) {
        return false;
    }

    char line[256] = {};
    bool attached = false;
    while (std::fgets(line, sizeof(line), status) != nullptr) {
        int tracer_pid = 0;
        if (std::sscanf(line, "TracerPid:%d", &tracer_pid) == 1 ||
            std::sscanf(line, "TracerPid:\t%d", &tracer_pid) == 1) {
            attached = tracer_pid != 0;
            break;
        }
    }
    std::fclose(status);
    return attached;
#endif
}

JSValue JsProcessIsDebuggerAttached(JSContext* ctx,
                                    JSValueConst this_val,
                                    int argc,
                                    JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewBool(ctx, IsDebuggerAttachedForJs() ? 1 : 0);
}

JSValue JsProcessEnumerateModules(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JsModuleEnumerateModules(ctx, JS_UNDEFINED, 0, nullptr);
}

bool FreeModuleObserverCallbacksLocked(JSContext* ctx, RuntimeState& state, uint32_t script_id) {
    auto it = state.module_observers.find(script_id);
    if (it == state.module_observers.end()) {
        return true;
    }
    if (ctx != nullptr) {
        JS_FreeValue(ctx, it->second.on_added);
        JS_FreeValue(ctx, it->second.on_removed);
    }
    state.module_observers.erase(it);
    return true;
}

JSValue JsProcessAttachModuleObserver(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "Process.attachModuleObserver requires observer object");
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "Process.attachModuleObserver requires active script");
    }

    JSValue on_added = JS_GetPropertyStr(ctx, argv[0], "onAdded");
    if (JS_IsException(on_added)) {
        return on_added;
    }
    if (!JS_IsUndefined(on_added) && !JS_IsNull(on_added) && !JS_IsFunction(ctx, on_added)) {
        JS_FreeValue(ctx, on_added);
        return JS_ThrowTypeError(ctx, "Process.attachModuleObserver onAdded must be a function");
    }
    if (JS_IsNull(on_added)) {
        JS_FreeValue(ctx, on_added);
        on_added = JS_UNDEFINED;
    }

    JSValue on_removed = JS_GetPropertyStr(ctx, argv[0], "onRemoved");
    if (JS_IsException(on_removed)) {
        JS_FreeValue(ctx, on_added);
        return on_removed;
    }
    if (!JS_IsUndefined(on_removed) && !JS_IsNull(on_removed) && !JS_IsFunction(ctx, on_removed)) {
        JS_FreeValue(ctx, on_added);
        JS_FreeValue(ctx, on_removed);
        return JS_ThrowTypeError(ctx, "Process.attachModuleObserver onRemoved must be a function");
    }
    if (JS_IsNull(on_removed)) {
        JS_FreeValue(ctx, on_removed);
        on_removed = JS_UNDEFINED;
    }

    if (!JS_IsFunction(ctx, on_added) && !JS_IsFunction(ctx, on_removed)) {
        JS_FreeValue(ctx, on_added);
        JS_FreeValue(ctx, on_removed);
        return JS_ThrowTypeError(ctx, "Process.attachModuleObserver requires onAdded or onRemoved");
    }

    FreeModuleObserverCallbacksLocked(ctx, state, state.current_script_id);

    RuntimeState::ModuleObserverRecord record = {};
    record.script_id = state.current_script_id;
    record.on_added = on_added;
    record.on_removed = on_removed;
    state.module_observers[state.current_script_id] = record;

    if (JS_IsFunction(ctx, record.on_added)) {
        std::vector<NativeModuleRecord> modules;
        std::string error_message;
        if (!CollectLoadedNativeModules(&modules, &error_message)) {
            FreeModuleObserverCallbacksLocked(ctx, state, state.current_script_id);
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }

        JSValue global = JS_GetGlobalObject(ctx);
        if (JS_IsException(global)) {
            FreeModuleObserverCallbacksLocked(ctx, state, state.current_script_id);
            return global;
        }

        ScopedCurrentScriptId script_scope(state, state.current_script_id);
        for (const NativeModuleRecord& module : modules) {
            JSValue module_value = MakeModuleObject(ctx, module);
            if (JS_IsException(module_value)) {
                JS_FreeValue(ctx, global);
                FreeModuleObserverCallbacksLocked(ctx, state, state.current_script_id);
                return module_value;
            }

            JSValue result = JS_Call(ctx, record.on_added, global, 1, &module_value);
            JS_FreeValue(ctx, module_value);
            if (JS_IsException(result)) {
                std::string exception_text = GetExceptionString(ctx);
                JS_FreeValue(ctx, global);
                FreeModuleObserverCallbacksLocked(ctx, state, state.current_script_id);
                return JS_ThrowInternalError(ctx,
                                             "Process.attachModuleObserver onAdded failed: %s",
                                             exception_text.c_str());
            }
            JS_FreeValue(ctx, result);
        }

        JS_FreeValue(ctx, global);
    }

    return JS_DupValue(ctx, argv[0]);
}

JSValue JsProcessGetCurrentThreadId(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, GetCurrentThreadIdForJs());
}

JSValue JsProcessEnumerateThreads(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    std::vector<NativeThreadRecord> threads;
    std::string error_message;
    if (!CollectThreadsForJs(&threads, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0u; index < threads.size(); ++index) {
        JSValue thread = MakeThreadObject(ctx, threads[index]);
        if (JS_IsException(thread) || JS_SetPropertyUint32(ctx, result, index, thread) < 0) {
            JS_FreeValue(ctx, thread);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsProcessFindModuleByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Process.findModuleByName requires name");
    }
    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Process.findModuleByName name must be a string");
    }

    const char* name_cstr = JS_ToCString(ctx, argv[0]);
    if (name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Process.findModuleByName name must be a string");
    }
    const std::string module_name = name_cstr;
    JS_FreeCString(ctx, name_cstr);

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    return module == nullptr ? JS_NULL : MakeModuleObject(ctx, *module);
}

JSValue JsProcessGetModuleByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsProcessFindModuleByName(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Process.getModuleByName module not found");
}

JSValue JsProcessEnumerateRanges(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Process.enumerateRanges requires protection");
    }

    const char* protection_cstr = JS_ToCString(ctx, argv[0]);
    if (protection_cstr == nullptr) {
        return JS_ThrowTypeError(
            ctx, "Process.enumerateRanges protection must be exactly 3 characters from r, w, x, and -");
    }

    const std::string protection = protection_cstr;
    JS_FreeCString(ctx, protection_cstr);
    if (protection.size() != 3u ||
        (protection[0] != 'r' && protection[0] != '-') ||
        (protection[1] != 'w' && protection[1] != '-') ||
        (protection[2] != 'x' && protection[2] != '-')) {
        return JS_ThrowTypeError(
            ctx, "Process.enumerateRanges protection must be exactly 3 characters from r, w, x, and -");
    }

    std::vector<NativeMemoryRangeRecord> ranges;
    if (!EnumerateNativeMemoryRanges(protection, &ranges)) {
        return JS_ThrowInternalError(ctx, "Process.enumerateRanges failed");
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0; index < ranges.size(); ++index) {
        JSValue range = MakeProcessRangeObject(ctx, ranges[index]);
        if (JS_IsException(range) || JS_SetPropertyUint32(ctx, result, index, range) < 0) {
            JS_FreeValue(ctx, range);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsProcessFindRangeByAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Process.findRangeByAddress requires address");
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "Process.findRangeByAddress address must be a pointer value");
    }
    if (address == 0u) {
        return JS_ThrowTypeError(
            ctx, "Process.findRangeByAddress address must be a non-zero pointer value");
    }
    address = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));

    std::vector<NativeMemoryRangeRecord> ranges;
    if (!CollectAllNativeMemoryRanges(&ranges)) {
        return JS_ThrowInternalError(ctx, "Process.findRangeByAddress failed");
    }

    for (const NativeMemoryRangeRecord& range : ranges) {
        const uint64_t range_end = range.base + range.size;
        if (address >= range.base && address < range_end) {
            return MakeProcessRangeObject(ctx, range);
        }
    }

    return JS_NULL;
}

JSValue JsProcessGetModuleByAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Process.getModuleByAddress requires address");
    }

    uint64_t address = 0;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "Process.getModuleByAddress address must be a pointer value");
    }
    if (address == 0u) {
        return JS_ThrowTypeError(
            ctx, "Process.getModuleByAddress address must be a non-zero pointer value");
    }
    address = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    for (const NativeModuleRecord& module : modules) {
        const uint64_t module_end = module.base + module.size;
        if (address >= module.base && address < module_end) {
            return MakeModuleObject(ctx, module);
        }
    }

    return JS_NULL;
}

bool NotifyModuleObserverModuleLoaded(const char* module_path, std::string* error_message) {
    if (module_path == nullptr || module_path[0] == '\0') {
        return true;
    }

    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.module_observers.empty()) {
        return true;
    }

    std::vector<NativeModuleRecord> modules;
    if (!CollectLoadedNativeModules(&modules, error_message)) {
        return false;
    }

    const std::string module_name = GetPathBaseName(module_path);
    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        for (const NativeModuleRecord& candidate : modules) {
            if (candidate.path == module_path) {
                module = &candidate;
                break;
            }
        }
    }
    if (module == nullptr) {
        NativeModuleRecord fallback = {};
        fallback.name = module_name;
        fallback.base = 1u;
        fallback.size = 0u;
        fallback.path = module_path;
        return EnqueueModuleEventLocked(state, fallback, true);
    }

    return EnqueueModuleEventLocked(state, *module, true);
}

JSValue MakeModuleObject(JSContext* ctx, const NativeModuleRecord& module) {
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    JSValue get_base_address = JS_NewCFunction(ctx, JsModuleGetBaseAddress, "getBaseAddress", 1);
    JSValue find_base_address = JS_NewCFunction(ctx, JsModuleFindBaseAddress, "findBaseAddress", 1);
    JSValue enumerate_exports = JS_NewCFunction(ctx, JsModuleEnumerateExports, "enumerateExports", 1);
    JSValue enumerate_imports = JS_NewCFunction(ctx, JsModuleEnumerateImports, "enumerateImports", 1);
    JSValue find_export_by_name = JS_NewCFunction(ctx, JsModuleFindExportByName, "findExportByName", 2);
    JSValue get_export_by_name = JS_NewCFunction(ctx, JsModuleGetExportByName, "getExportByName", 2);
    JSValue find_import_by_name = JS_NewCFunction(ctx, JsModuleFindImportByName, "findImportByName", 2);
    JSValue get_import_by_name = JS_NewCFunction(ctx, JsModuleGetImportByName, "getImportByName", 2);

    if (JS_SetPropertyStr(ctx, result, "name", JS_NewString(ctx, module.name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "base", MakeNativePointer(ctx, module.base)) < 0 ||
        JS_SetPropertyStr(ctx, result, "size", JS_NewUint32(ctx, static_cast<uint32_t>(module.size))) < 0 ||
        JS_SetPropertyStr(ctx, result, "path", JS_NewString(ctx, module.path.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, kModuleObjectNameProperty, JS_NewString(ctx, module.name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "getBaseAddress", get_base_address) < 0 ||
        JS_SetPropertyStr(ctx, result, "findBaseAddress", find_base_address) < 0 ||
        JS_SetPropertyStr(ctx, result, "enumerateExports", enumerate_exports) < 0 ||
        JS_SetPropertyStr(ctx, result, "enumerateImports", enumerate_imports) < 0 ||
        JS_SetPropertyStr(ctx, result, "findExportByName", find_export_by_name) < 0 ||
        JS_SetPropertyStr(ctx, result, "getExportByName", get_export_by_name) < 0 ||
        JS_SetPropertyStr(ctx, result, "findImportByName", find_import_by_name) < 0 ||
        JS_SetPropertyStr(ctx, result, "getImportByName", get_import_by_name) < 0) {
        JS_FreeValue(ctx, get_base_address);
        JS_FreeValue(ctx, find_base_address);
        JS_FreeValue(ctx, enumerate_exports);
        JS_FreeValue(ctx, enumerate_imports);
        JS_FreeValue(ctx, find_export_by_name);
        JS_FreeValue(ctx, get_export_by_name);
        JS_FreeValue(ctx, find_import_by_name);
        JS_FreeValue(ctx, get_import_by_name);
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }

    return result;
}

bool EnqueueModuleEventLocked(RuntimeState& state,
                              const NativeModuleRecord& module,
                              bool added) {
    RuntimeState::PendingModuleEventRecord event = {};
    event.added = added;
    event.module = module;
    state.pending_module_events.push_back(std::move(event));
    return true;
}

bool ForwardPendingModuleEventsLocked(RuntimeState& state, std::string* error_message) {
    if (state.context == nullptr || state.pending_module_events.empty()) {
        return true;
    }

    std::vector<RuntimeState::PendingModuleEventRecord> events;
    events.swap(state.pending_module_events);

    for (const RuntimeState::PendingModuleEventRecord& event : events) {
        for (const auto& pair : state.module_observers) {
            const RuntimeState::ModuleObserverRecord& observer = pair.second;
            JSValue callback = event.added ? observer.on_added : observer.on_removed;
            if (!JS_IsFunction(state.context, callback)) {
                continue;
            }

            JSValue global = JS_GetGlobalObject(state.context);
            if (JS_IsException(global)) {
                SetError(error_message, GetExceptionString(state.context));
                JS_FreeValue(state.context, global);
                return false;
            }

            JSValue module_value = MakeModuleObject(state.context, event.module);
            if (JS_IsException(module_value)) {
                SetError(error_message, GetExceptionString(state.context));
                JS_FreeValue(state.context, global);
                return false;
            }

            ScopedCurrentScriptId script_scope(state, observer.script_id);
            JSValue result = JS_Call(state.context, callback, global, 1, &module_value);
            JS_FreeValue(state.context, module_value);
            JS_FreeValue(state.context, global);
            if (JS_IsException(result)) {
                SetError(error_message, GetExceptionString(state.context));
                return false;
            }
            JS_FreeValue(state.context, result);
        }
    }

    return true;
}

JSValue MakeThreadObject(JSContext* ctx, const NativeThreadRecord& thread) {
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    if (JS_SetPropertyStr(ctx, result, "id", JS_NewUint32(ctx, thread.id)) < 0 ||
        JS_SetPropertyStr(ctx, result, "name", JS_NewString(ctx, thread.name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "state", JS_NewString(ctx, thread.state.c_str())) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }

    return result;
}

JSValue MakeDebugSymbolObject(JSContext* ctx,
                              uint64_t address,
                              const NativeModuleRecord* module,
                              const NativeModuleExportRecord* symbol) {
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    JSValue to_string = JS_NewCFunction(ctx, JsDebugSymbolToString, "toString", 0);
    if (JS_IsException(to_string) ||
        JS_SetPropertyStr(ctx, result, "address", MakeNativePointer(ctx, address)) < 0 ||
        JS_SetPropertyStr(ctx,
                          result,
                          "name",
                          symbol == nullptr ? JS_NULL : JS_NewString(ctx, symbol->name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx,
                          result,
                          "moduleName",
                          module == nullptr ? JS_NULL : JS_NewString(ctx, module->name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, result, "toString", to_string) < 0 ||
        JS_SetPropertyStr(ctx,
                          result,
                          kDebugSymbolModuleBaseProperty,
                          MakeNativePointer(ctx, module == nullptr ? 0u : module->base)) < 0 ||
        JS_SetPropertyStr(ctx,
                          result,
                          kDebugSymbolSymbolAddressProperty,
                          MakeNativePointer(ctx, symbol == nullptr ? 0u : symbol->address)) < 0) {
        JS_FreeValue(ctx, to_string);
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }

    return result;
}

JSValue CloneModuleObject(JSContext* ctx, JSValueConst module_value) {
    JSValue name = JS_GetPropertyStr(ctx, module_value, "name");
    JSValue base = JS_GetPropertyStr(ctx, module_value, "base");
    JSValue size = JS_GetPropertyStr(ctx, module_value, "size");
    JSValue path = JS_GetPropertyStr(ctx, module_value, "path");
    if (JS_IsException(name) || JS_IsException(base) ||
        JS_IsException(size) || JS_IsException(path)) {
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, base);
        JS_FreeValue(ctx, size);
        JS_FreeValue(ctx, path);
        return JS_EXCEPTION;
    }

    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, base);
        JS_FreeValue(ctx, size);
        JS_FreeValue(ctx, path);
        return result;
    }

    if (JS_SetPropertyStr(ctx, result, "name", name) < 0 ||
        JS_SetPropertyStr(ctx, result, "base", base) < 0 ||
        JS_SetPropertyStr(ctx, result, "size", size) < 0 ||
        JS_SetPropertyStr(ctx, result, "path", path) < 0) {
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, base);
        JS_FreeValue(ctx, size);
        JS_FreeValue(ctx, path);
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }

    return result;
}

JSValue CloneModuleArray(JSContext* ctx, JSValueConst modules) {
    uint32_t length = 0u;
    if (!GetArrayLength(ctx, modules, &length)) {
        return JS_ThrowInternalError(ctx, "read module snapshot length failed");
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0u; index < length; ++index) {
        JSValue module = JS_GetPropertyUint32(ctx, modules, index);
        if (JS_IsException(module)) {
            JS_FreeValue(ctx, module);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }

        JSValue clone = CloneModuleObject(ctx, module);
        JS_FreeValue(ctx, module);
        if (JS_IsException(clone) || JS_SetPropertyUint32(ctx, result, index, clone) < 0) {
            JS_FreeValue(ctx, clone);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue GetModuleMapSnapshot(JSContext* ctx, JSValueConst this_val, const char* method_name) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "%s receiver is not a ModuleMap", method_name);
    }

    JSValue snapshot = JS_GetPropertyStr(ctx, this_val, kModuleMapSnapshotProperty);
    if (JS_IsException(snapshot)) {
        return snapshot;
    }
    if (!JS_IsObject(snapshot)) {
        JS_FreeValue(ctx, snapshot);
        return JS_ThrowTypeError(ctx, "%s receiver is not a ModuleMap", method_name);
    }

    return snapshot;
}

bool ReplaceModuleMapSnapshot(JSContext* ctx, JSValueConst this_val, JSValue snapshot) {
    if (JS_DefinePropertyValueStr(ctx,
                                  this_val,
                                  kModuleMapSnapshotProperty,
                                  snapshot,
                                  JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0) {
        JS_FreeValue(ctx, snapshot);
        return false;
    }
    return true;
}

bool TryGetModuleNameFromReceiver(JSContext* ctx,
                                  JSValueConst this_val,
                                  std::string* module_name_out) {
    if (module_name_out == nullptr || !JS_IsObject(this_val)) {
        return false;
    }

    JSValue module_name_value = JS_GetPropertyStr(ctx, this_val, kModuleObjectNameProperty);
    if (JS_IsException(module_name_value)) {
        JS_FreeValue(ctx, module_name_value);
        return false;
    }
    if (!JS_IsString(module_name_value)) {
        JS_FreeValue(ctx, module_name_value);
        return false;
    }

    const char* module_name_cstr = JS_ToCString(ctx, module_name_value);
    if (module_name_cstr == nullptr) {
        JS_FreeValue(ctx, module_name_value);
        return false;
    }

    *module_name_out = module_name_cstr;
    JS_FreeCString(ctx, module_name_cstr);
    JS_FreeValue(ctx, module_name_value);
    return true;
}

JSValue JsModuleMapHas(JSContext* ctx,
                       JSValueConst this_val,
                       int argc,
                       JSValueConst* argv,
                       int magic,
                       JSValue* func_data) {
    (void)magic;
    (void)func_data;
    uint64_t address = 0u;
    if (!ParseModuleLookupAddressArgument(ctx, "ModuleMap.has", argc, argv, &address)) {
        return JS_EXCEPTION;
    }

    JSValue snapshot = GetModuleMapSnapshot(ctx, this_val, "ModuleMap.has");
    if (JS_IsException(snapshot)) {
        return snapshot;
    }

    uint32_t index = UINT32_MAX;
    std::string error_message;
    if (!FindModuleIndexByAddressInSnapshot(ctx, snapshot, address, &index, &error_message)) {
        JS_FreeValue(ctx, snapshot);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    JS_FreeValue(ctx, snapshot);
    return JS_NewBool(ctx, index != UINT32_MAX);
}

JSValue JsModuleMapFind(JSContext* ctx,
                        JSValueConst this_val,
                        int argc,
                        JSValueConst* argv,
                        int magic,
                        JSValue* func_data) {
    (void)magic;
    (void)func_data;
    uint64_t address = 0u;
    if (!ParseModuleLookupAddressArgument(ctx, "ModuleMap.find", argc, argv, &address)) {
        return JS_EXCEPTION;
    }

    JSValue snapshot = GetModuleMapSnapshot(ctx, this_val, "ModuleMap.find");
    if (JS_IsException(snapshot)) {
        return snapshot;
    }

    uint32_t index = UINT32_MAX;
    std::string error_message;
    if (!FindModuleIndexByAddressInSnapshot(ctx, snapshot, address, &index, &error_message)) {
        JS_FreeValue(ctx, snapshot);
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (index == UINT32_MAX) {
        JS_FreeValue(ctx, snapshot);
        return JS_NULL;
    }

    JSValue module = JS_GetPropertyUint32(ctx, snapshot, index);
    JS_FreeValue(ctx, snapshot);
    if (JS_IsException(module)) {
        JS_FreeValue(ctx, module);
        return module;
    }
    JSValue clone = CloneModuleObject(ctx, module);
    JS_FreeValue(ctx, module);
    return clone;
}

JSValue JsModuleMapGet(JSContext* ctx,
                       JSValueConst this_val,
                       int argc,
                       JSValueConst* argv,
                       int magic,
                       JSValue* func_data) {
    JSValue result = JsModuleMapFind(ctx, this_val, argc, argv, magic, func_data);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "ModuleMap.get module not found");
}

JSValue JsModuleMapValues(JSContext* ctx,
                          JSValueConst this_val,
                          int argc,
                          JSValueConst* argv,
                          int magic,
                          JSValue* func_data) {
    (void)argc;
    (void)argv;
    (void)magic;
    (void)func_data;
    JSValue snapshot = GetModuleMapSnapshot(ctx, this_val, "ModuleMap.values");
    if (JS_IsException(snapshot)) {
        return snapshot;
    }
    JSValue result = CloneModuleArray(ctx, snapshot);
    JS_FreeValue(ctx, snapshot);
    return result;
}

JSValue JsModuleMapUpdate(JSContext* ctx,
                          JSValueConst this_val,
                          int argc,
                          JSValueConst* argv,
                          int magic,
                          JSValue* func_data) {
    (void)argv;
    (void)magic;
    (void)func_data;
    if (argc > 0) {
        return JS_ThrowTypeError(ctx, "ModuleMap.update does not accept arguments");
    }

    JSValue snapshot = JsModuleEnumerateModules(ctx, JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(snapshot)) {
        return snapshot;
    }
    if (!ReplaceModuleMapSnapshot(ctx, this_val, snapshot)) {
        return JS_ThrowInternalError(ctx, "refresh ModuleMap failed");
    }
    return JS_DupValue(ctx, this_val);
}

JSValue JsModuleMapConstructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argv;
    if (argc > 0) {
        return JS_ThrowTypeError(ctx, "ModuleMap does not accept arguments yet");
    }

    JSValue snapshot = JsModuleEnumerateModules(ctx, JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(snapshot)) {
        return snapshot;
    }

    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, snapshot);
        return result;
    }

    if (!ReplaceModuleMapSnapshot(ctx, result, snapshot)) {
        JS_FreeValue(ctx, result);
        return JS_ThrowInternalError(ctx, "build ModuleMap failed");
    }

    JSValue has = JS_NewCFunctionData(ctx, JsModuleMapHas, 1, 0, 0, nullptr);
    JSValue find = JS_NewCFunctionData(ctx, JsModuleMapFind, 1, 0, 0, nullptr);
    JSValue get = JS_NewCFunctionData(ctx, JsModuleMapGet, 1, 0, 0, nullptr);
    JSValue values = JS_NewCFunctionData(ctx, JsModuleMapValues, 0, 0, 0, nullptr);
    JSValue update = JS_NewCFunctionData(ctx, JsModuleMapUpdate, 0, 0, 0, nullptr);

    if (JS_IsException(has) || JS_IsException(find) || JS_IsException(get) ||
        JS_IsException(values) || JS_IsException(update)) {
        JS_FreeValue(ctx, has);
        JS_FreeValue(ctx, find);
        JS_FreeValue(ctx, get);
        JS_FreeValue(ctx, values);
        JS_FreeValue(ctx, update);
        JS_FreeValue(ctx, result);
        return JS_ThrowInternalError(ctx, "build ModuleMap failed");
    }

    if (JS_DefinePropertyValueStr(ctx, result, "has", has, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0 ||
        JS_DefinePropertyValueStr(ctx, result, "find", find, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0 ||
        JS_DefinePropertyValueStr(ctx, result, "get", get, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0 ||
        JS_DefinePropertyValueStr(ctx, result, "values", values, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0 ||
        JS_DefinePropertyValueStr(ctx, result, "update", update, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0) {
        JS_FreeValue(ctx, has);
        JS_FreeValue(ctx, find);
        JS_FreeValue(ctx, get);
        JS_FreeValue(ctx, values);
        JS_FreeValue(ctx, update);
        JS_FreeValue(ctx, result);
        return JS_ThrowInternalError(ctx, "build ModuleMap failed");
    }

    return result;
}

JSValue JsModuleEnumerateModules(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0; index < modules.size(); ++index) {
        JSValue module = MakeModuleObject(ctx, modules[index]);
        if (JS_IsException(module) || JS_SetPropertyUint32(ctx, result, index, module) < 0) {
            JS_FreeValue(ctx, module);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsModuleFindRangeByAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JsProcessFindRangeByAddress(ctx, this_val, argc, argv);
}

JSValue JsModuleFindBaseAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    if (argc >= 1) {
        if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
            return JS_ThrowTypeError(ctx, "findBaseAddress module must be a string");
        }

        const char* module_cstr = JS_ToCString(ctx, argv[0]);
        if (module_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "findBaseAddress module must be a string");
        }
        module_name = module_cstr;
        JS_FreeCString(ctx, module_cstr);
    } else if (!TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        return JS_ThrowTypeError(ctx, "findBaseAddress requires module");
    }

    uint64_t module_base = 0u;
    std::string error_message;
    const bool ok = FindLoadedModuleBaseAddressByName(module_name.c_str(), &module_base, &error_message);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (module_base == 0u) {
        return JS_NULL;
    }
    return MakeNativePointer(ctx, module_base);
}

JSValue JsModuleGetBaseAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsModuleFindBaseAddress(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Module.getBaseAddress module not found");
}

JSValue JsModuleLoad(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Module.load requires module name");
    }
    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Module.load module name must be a string");
    }

    const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
    if (module_name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Module.load module name must be a string");
    }
    const std::string module_name = module_name_cstr;
    JS_FreeCString(ctx, module_name_cstr);

#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(module_name.c_str());
    if (handle == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.load LoadLibraryA failed");
    }
    const uint64_t expected_base = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
#else
    dlerror();
    void* handle = dlopen(module_name.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        return JS_ThrowInternalError(ctx,
                                     "Module.load dlopen failed%s%s",
                                     error == nullptr ? "" : ": ",
                                     error == nullptr ? "" : error);
    }
    (void)handle;
#endif

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

#if defined(_WIN32)
    const NativeModuleRecord* module = FindLoadedModuleByBase(modules, expected_base);
#else
    const NativeModuleRecord* module =
        FindLoadedModuleByName(modules, GetPathBaseNameForModuleLookup(module_name));
    if (module == nullptr) {
        module = FindLoadedModuleByName(modules, module_name);
    }
#endif
    if (module == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.load loaded module not found");
    }

    InvalidateDebugSymbolCacheLocked(GetRuntimeState());
    return MakeModuleObject(ctx, *module);
}

JSValue JsModuleEnsureInitialized(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Module.ensureInitialized requires module name");
    }
    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Module.ensureInitialized module name must be a string");
    }

    const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
    if (module_name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Module.ensureInitialized module name must be a string");
    }
    const std::string module_name = module_name_cstr;
    JS_FreeCString(ctx, module_name_cstr);

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.ensureInitialized module not found");
    }
    (void)module;

    return JS_UNDEFINED;
}

JSValue JsModuleEnumerateExports(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    if (argc >= 1) {
        if (!JS_IsString(argv[0])) {
            return JS_ThrowTypeError(ctx, "Module.enumerateExports module name must be a string");
        }

        const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
        if (module_name_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "Module.enumerateExports module name must be a string");
        }
        module_name = module_name_cstr;
        JS_FreeCString(ctx, module_name_cstr);
    } else if (!TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        return JS_ThrowTypeError(ctx, "Module.enumerateExports requires module name");
    }

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.enumerateExports module not found");
    }

    std::vector<NativeModuleExportRecord> exports;
    if (!CollectModuleExports(*module, &exports, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0; index < exports.size(); ++index) {
        JSValue export_value = MakeModuleExportObject(ctx, exports[index]);
        if (JS_IsException(export_value) || JS_SetPropertyUint32(ctx, result, index, export_value) < 0) {
            JS_FreeValue(ctx, export_value);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsModuleEnumerateImports(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    if (argc >= 1) {
        if (!JS_IsString(argv[0])) {
            return JS_ThrowTypeError(ctx, "Module.enumerateImports module name must be a string");
        }

        const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
        if (module_name_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "Module.enumerateImports module name must be a string");
        }
        module_name = module_name_cstr;
        JS_FreeCString(ctx, module_name_cstr);
    } else if (!TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        return JS_ThrowTypeError(ctx, "Module.enumerateImports requires module name");
    }

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.enumerateImports module not found");
    }

    std::vector<NativeModuleImportRecord> imports;
    if (!CollectModuleImports(*module, &imports, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0; index < imports.size(); ++index) {
        JSValue import_value = MakeModuleImportObject(ctx, imports[index]);
        if (JS_IsException(import_value) || JS_SetPropertyUint32(ctx, result, index, import_value) < 0) {
            JS_FreeValue(ctx, import_value);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsModuleFindImportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    int symbol_index = 1;
    if (argc >= 2) {
        if (!JS_IsString(argv[0]) || !JS_IsString(argv[1])) {
            return JS_ThrowTypeError(ctx, "Module.findImportByName arguments must be strings");
        }

        const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
        if (module_name_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "Module.findImportByName arguments must be strings");
        }
        module_name = module_name_cstr;
        JS_FreeCString(ctx, module_name_cstr);
    } else if (argc >= 1 && JS_IsString(argv[0]) &&
               TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        symbol_index = 0;
    } else {
        return JS_ThrowTypeError(ctx, "Module.findImportByName requires module name and symbol name");
    }

    const char* symbol_name_cstr = JS_ToCString(ctx, argv[symbol_index]);
    if (symbol_name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Module.findImportByName arguments must be strings");
    }

    const std::string symbol_name = symbol_name_cstr;
    JS_FreeCString(ctx, symbol_name_cstr);

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_NULL;
    }

    std::vector<NativeModuleImportRecord> imports;
    if (!CollectModuleImports(*module, &imports, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    for (const NativeModuleImportRecord& import_record : imports) {
        if (import_record.name == symbol_name) {
            return MakeNativePointer(ctx, import_record.address);
        }
    }

    return JS_NULL;
}

JSValue JsModuleGetImportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsModuleFindImportByName(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Module.getImportByName import not found");
}

JSValue JsModuleEnumerateSymbols(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    if (argc >= 1) {
        if (!JS_IsString(argv[0])) {
            return JS_ThrowTypeError(ctx, "Module.enumerateSymbols module name must be a string");
        }

        const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
        if (module_name_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "Module.enumerateSymbols module name must be a string");
        }
        module_name = module_name_cstr;
        JS_FreeCString(ctx, module_name_cstr);
    } else if (!TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        return JS_ThrowTypeError(ctx, "Module.enumerateSymbols requires module name");
    }

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_ThrowInternalError(ctx, "Module.enumerateSymbols module not found");
    }

    std::vector<NativeModuleExportRecord> symbols;
    if (!CollectModuleExports(*module, &symbols, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0; index < symbols.size(); ++index) {
        JSValue symbol_value = MakeModuleExportObject(ctx, symbols[index]);
        if (JS_IsException(symbol_value) || JS_SetPropertyUint32(ctx, result, index, symbol_value) < 0) {
            JS_FreeValue(ctx, symbol_value);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsModuleFindSymbolByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    int symbol_index = 1;
    if (argc >= 2) {
        if (!JS_IsString(argv[0]) || !JS_IsString(argv[1])) {
            return JS_ThrowTypeError(ctx, "Module.findSymbolByName arguments must be strings");
        }

        const char* module_name_cstr = JS_ToCString(ctx, argv[0]);
        if (module_name_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "Module.findSymbolByName arguments must be strings");
        }
        module_name = module_name_cstr;
        JS_FreeCString(ctx, module_name_cstr);
    } else if (argc >= 1 && JS_IsString(argv[0]) &&
               TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        symbol_index = 0;
    } else {
        return JS_ThrowTypeError(ctx, "Module.findSymbolByName requires module name and symbol name");
    }

    const char* symbol_name_cstr = JS_ToCString(ctx, argv[symbol_index]);
    if (symbol_name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Module.findSymbolByName arguments must be strings");
    }

    const std::string symbol_name = symbol_name_cstr;
    JS_FreeCString(ctx, symbol_name_cstr);

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module = FindLoadedModuleByName(modules, module_name);
    if (module == nullptr) {
        return JS_NULL;
    }

    std::vector<NativeModuleExportRecord> exports;
    if (!CollectModuleExports(*module, &exports, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    for (const NativeModuleExportRecord& export_record : exports) {
        if (export_record.name == symbol_name) {
            return MakeNativePointer(ctx, export_record.address);
        }
    }

    return JS_NULL;
}

JSValue JsModuleGetSymbolByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsModuleFindSymbolByName(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Module.getSymbolByName symbol not found");
}

JSValue JsModuleFindGlobalExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Module.findGlobalExportByName requires symbol name");
    }
    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "Module.findGlobalExportByName symbol name must be a string");
    }

    const char* symbol_name_cstr = JS_ToCString(ctx, argv[0]);
    if (symbol_name_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "Module.findGlobalExportByName symbol name must be a string");
    }
    const std::string symbol_name = symbol_name_cstr;
    JS_FreeCString(ctx, symbol_name_cstr);

    std::vector<NativeModuleRecord> modules;
    std::string error_message;
    if (!CollectLoadedNativeModules(&modules, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    for (const NativeModuleRecord& module : modules) {
        std::vector<NativeModuleExportRecord> exports;
        std::string export_error;
        if (!CollectModuleExports(module, &exports, &export_error)) {
            continue;
        }
        for (const NativeModuleExportRecord& export_record : exports) {
            if (export_record.name == symbol_name) {
                return MakeNativePointer(ctx, export_record.address);
            }
        }
    }

    return JS_NULL;
}

JSValue JsModuleGetGlobalExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsModuleFindGlobalExportByName(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Module.getGlobalExportByName export not found");
}

JSValue JsDebugSymbolToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;

    JSValue address_value = JS_GetPropertyStr(ctx, this_val, "address");
    JSValue name_value = JS_GetPropertyStr(ctx, this_val, "name");
    JSValue module_name_value = JS_GetPropertyStr(ctx, this_val, "moduleName");
    JSValue module_base_value = JS_GetPropertyStr(ctx, this_val, kDebugSymbolModuleBaseProperty);
    JSValue symbol_address_value = JS_GetPropertyStr(ctx, this_val, kDebugSymbolSymbolAddressProperty);

    uint64_t address = 0u;
    uint64_t module_base = 0u;
    uint64_t symbol_address = 0u;
    ParsePointerValue(ctx, address_value, &address);
    ParsePointerValue(ctx, module_base_value, &module_base);
    ParsePointerValue(ctx, symbol_address_value, &symbol_address);

    std::string name;
    std::string module_name;
    const bool has_name = !JS_IsNull(name_value) && !JS_IsUndefined(name_value);
    const bool has_module_name = !JS_IsNull(module_name_value) && !JS_IsUndefined(module_name_value);
    if (has_name) {
        const char* name_cstr = JS_ToCString(ctx, name_value);
        if (name_cstr != nullptr) {
            name = name_cstr;
            JS_FreeCString(ctx, name_cstr);
        }
    }
    if (has_module_name) {
        const char* module_name_cstr = JS_ToCString(ctx, module_name_value);
        if (module_name_cstr != nullptr) {
            module_name = module_name_cstr;
            JS_FreeCString(ctx, module_name_cstr);
        }
    }

    JS_FreeValue(ctx, address_value);
    JS_FreeValue(ctx, name_value);
    JS_FreeValue(ctx, module_name_value);
    JS_FreeValue(ctx, module_base_value);
    JS_FreeValue(ctx, symbol_address_value);

    std::ostringstream stream;
    stream << "0x" << std::hex << address;
    const std::string address_text = stream.str();
    stream.str("");
    stream.clear();

    if (has_module_name && has_name) {
        stream << module_name << "!" << name;
        if (symbol_address != 0u && address >= symbol_address) {
            const uint64_t offset = address - symbol_address;
            if (offset != 0u) {
                stream << "+0x" << std::hex << offset;
            }
        }
        return JS_NewString(ctx, stream.str().c_str());
    }

    if (has_module_name) {
        stream << module_name;
        if (module_base != 0u && address >= module_base) {
            stream << "+0x" << std::hex << (address - module_base);
        } else {
            stream << "!" << address_text;
        }
        return JS_NewString(ctx, stream.str().c_str());
    }

    return JS_NewString(ctx, address_text.c_str());
}

JSValue JsDebugSymbolFromAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "DebugSymbol.fromAddress requires address");
    }

    uint64_t address = 0u;
    if (!ParsePointerValue(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "DebugSymbol.fromAddress address must be a pointer value");
    }
    if (address == 0u) {
        return JS_ThrowTypeError(ctx, "DebugSymbol.fromAddress address must be a non-zero pointer value");
    }

    RuntimeState& state = GetRuntimeState();
    std::string error_message;
    if (!EnsureDebugSymbolModulesCachedLocked(state, false, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const NativeModuleRecord* module =
        FindLoadedModuleContainingAddress(state.debug_symbol_modules, address);
    if (module == nullptr) {
        if (!EnsureDebugSymbolModulesCachedLocked(state, true, &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
        module = FindLoadedModuleContainingAddress(state.debug_symbol_modules, address);
    }

    const NativeModuleExportRecord* best_symbol = nullptr;
    NativeModuleExportRecord matched_symbol = {};
    if (module != nullptr) {
        const std::vector<NativeModuleExportRecord>* exports = nullptr;
        if (EnsureDebugSymbolExportsCachedLocked(state, *module, &exports, &error_message)) {
            for (const NativeModuleExportRecord& export_record : *exports) {
                if (export_record.address > address) {
                    continue;
                }
                if (best_symbol == nullptr || export_record.address > matched_symbol.address) {
                    matched_symbol = export_record;
                    best_symbol = &matched_symbol;
                }
            }
        }
    }

    return MakeDebugSymbolObject(ctx, address, module, best_symbol);
}

JSValue JsThreadBacktrace(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;

    std::vector<uint64_t> frames;
    JSValueConst context_value = JS_UNDEFINED;
    BacktracerMode mode = BacktracerMode::kAccurate;
    if (!ParseThreadBacktraceArguments(ctx, argc, argv, &context_value, &mode)) {
        return JS_EXCEPTION;
    }

    if (!JS_IsUndefined(context_value) && !JS_IsNull(context_value)) {
        JSValue pc_value = JS_GetPropertyStr(ctx, context_value, "pc");
        JSValue lr_value = JS_GetPropertyStr(ctx, context_value, "lr");
        JSValue fp_value = JS_GetPropertyStr(ctx, context_value, "fp");
        JSValue sp_value = JS_GetPropertyStr(ctx, context_value, "sp");
        if (JS_IsException(pc_value) || JS_IsException(lr_value) ||
            JS_IsException(fp_value) || JS_IsException(sp_value)) {
            JS_FreeValue(ctx, pc_value);
            JS_FreeValue(ctx, lr_value);
            JS_FreeValue(ctx, fp_value);
            JS_FreeValue(ctx, sp_value);
            return JS_EXCEPTION;
        }

        uint64_t pc = 0u;
        uint64_t lr = 0u;
        uint64_t fp = 0u;
        uint64_t sp = 0u;
        const bool parsed =
            ParsePointerValue(ctx, pc_value, &pc) &&
            ParsePointerValue(ctx, lr_value, &lr) &&
            ParsePointerValue(ctx, fp_value, &fp);
        const bool parsed_sp =
            JS_IsUndefined(sp_value) || JS_IsNull(sp_value) || ParsePointerValue(ctx, sp_value, &sp);
        JS_FreeValue(ctx, pc_value);
        JS_FreeValue(ctx, lr_value);
        JS_FreeValue(ctx, fp_value);
        JS_FreeValue(ctx, sp_value);
        if (!parsed) {
            return JS_ThrowTypeError(ctx, "Thread.backtrace context must expose pc/lr/fp pointers");
        }
        if (!parsed_sp) {
            return JS_ThrowTypeError(ctx, "Thread.backtrace context.sp must be a pointer value");
        }

        bool ok = false;
        if (mode == BacktracerMode::kFuzzy) {
            std::vector<NativeMemoryRangeRecord> ranges;
            ok = CollectAllNativeMemoryRanges(&ranges) &&
                 CollectFuzzyBacktraceFromStackPointer(pc, lr, sp, ranges, &frames);
        } else {
            ok = CollectBacktraceFromContext(pc, lr, fp, &frames);
        }
        if (!ok) {
            return JS_ThrowInternalError(ctx, "Thread.backtrace failed");
        }
    } else if (!((mode == BacktracerMode::kFuzzy)
                     ? CollectCurrentFuzzyNativeBacktrace(&frames)
                     : CollectCurrentNativeBacktrace(&frames))) {
        return JS_ThrowInternalError(ctx, "Thread.backtrace unsupported");
    }

    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) {
        return result;
    }

    for (uint32_t index = 0u; index < frames.size(); ++index) {
        JSValue frame = MakeNativePointer(ctx, frames[index]);
        if (JS_IsException(frame) || JS_SetPropertyUint32(ctx, result, index, frame) < 0) {
            JS_FreeValue(ctx, frame);
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }

    return result;
}

JSValue JsThreadSleep(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Thread.sleep requires seconds");
    }

    double seconds = 0.0;
    if (JS_ToFloat64(ctx, &seconds, argv[0]) < 0 ||
        !std::isfinite(seconds) ||
        seconds < 0.0) {
        return JS_ThrowTypeError(ctx,
                                 "Thread.sleep seconds must be a non-negative finite number");
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return JS_UNDEFINED;
}

JSValue JsModuleFindExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::string module_name;
    int symbol_index = 1;
    if (argc >= 2) {
        if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
            return JsModuleFindGlobalExportByName(ctx, this_val, argc - 1, argv + 1);
        }

        const char* module_cstr = JS_ToCString(ctx, argv[0]);
        if (module_cstr == nullptr) {
            return JS_ThrowTypeError(ctx, "findExportByName module must be a string");
        }
        module_name = module_cstr;
        JS_FreeCString(ctx, module_cstr);
    } else if (argc >= 1 && TryGetModuleNameFromReceiver(ctx, this_val, &module_name)) {
        symbol_index = 0;
    } else {
        return JS_ThrowTypeError(ctx, "findExportByName requires module and symbol");
    }

    const char* symbol_cstr = JS_ToCString(ctx, argv[symbol_index]);
    if (symbol_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "findExportByName symbol must be a string");
    }

    uint64_t target_address = 0;
    std::string error_message;
    const bool ok = FindNativeJsExportByName(module_name.c_str(), symbol_cstr, &target_address, &error_message);
    JS_FreeCString(ctx, symbol_cstr);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    if (target_address == 0) {
        return JS_NULL;
    }
    return MakeNativePointer(ctx, target_address);
}

JSValue JsModuleGetExportByName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = JsModuleFindExportByName(ctx, this_val, argc, argv);
    if (JS_IsException(result) || !JS_IsNull(result)) {
        return result;
    }
    JS_FreeValue(ctx, result);
    return JS_ThrowInternalError(ctx, "Module.getExportByName export not found");
}

JSValue JsModuleAttachExport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx,
                                 "attachExport requires module, symbol, and callbacks");
    }

    const char* module_cstr = JS_ToCString(ctx, argv[0]);
    if (module_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "attachExport module must be a string");
    }
    const std::string module_name = module_cstr;
    JS_FreeCString(ctx, module_cstr);

    const char* symbol_cstr = JS_ToCString(ctx, argv[1]);
    if (symbol_cstr == nullptr) {
        return JS_ThrowTypeError(ctx, "attachExport symbol must be a string");
    }
    const std::string symbol_name = symbol_cstr;
    JS_FreeCString(ctx, symbol_cstr);

    if (!JS_IsObject(argv[2])) {
        return JS_ThrowTypeError(ctx, "attachExport callbacks must be an object");
    }

    JSValue on_enter = JS_UNDEFINED;
    JSValue on_leave = JS_UNDEFINED;
    if (!GetNativeHookCallbacks(ctx, argv[2], &on_enter, &on_leave)) {
        return JS_EXCEPTION;
    }
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
    if (!GetNativeHookSnapshots(ctx, argv[2], &snapshots)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }
    bool blocking = true;
    if (!GetNativeHookBlockingMode(ctx, argv[2], &blocking)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }

    RuntimeState& state = GetRuntimeState();
    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = module_name;
    request.symbol_name = symbol_name;
    request.blocking = blocking;
    request.snapshots = std::move(snapshots);
    return InstallNativeHookForCurrentScript(ctx, state, request, on_enter, on_leave);
}

JSValue JsInterceptorAttach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Interceptor.attach requires target and callbacks");
    }
    if (!JS_IsObject(argv[1])) {
        return JS_ThrowTypeError(ctx, "Interceptor.attach callbacks must be an object");
    }

    JSValue on_enter = JS_UNDEFINED;
    JSValue on_leave = JS_UNDEFINED;
    if (!GetNativeHookCallbacks(ctx, argv[1], &on_enter, &on_leave)) {
        return JS_EXCEPTION;
    }
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
    if (!GetNativeHookSnapshots(ctx, argv[1], &snapshots)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }
    bool blocking = true;
    if (!GetNativeHookBlockingMode(ctx, argv[1], &blocking)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }

    bool has_module_symbol_target = false;
    std::string module_name;
    std::string symbol_name;
    if (!TryGetInterceptorModuleSymbolTarget(ctx,
                                             argv[0],
                                             &has_module_symbol_target,
                                             &module_name,
                                             &symbol_name)) {
        JS_FreeValue(ctx, on_enter);
        JS_FreeValue(ctx, on_leave);
        return JS_EXCEPTION;
    }

    uint64_t target_address = 0;
    if (!has_module_symbol_target) {
        const char* target_cstr = JS_ToCString(ctx, argv[0]);
        if (target_cstr == nullptr) {
            JS_FreeValue(ctx, on_enter);
            JS_FreeValue(ctx, on_leave);
            return JS_ThrowTypeError(ctx, "Interceptor.attach target must be a pointer string");
        }

        const std::string target_text = target_cstr;
        JS_FreeCString(ctx, target_cstr);
        if (!ParsePointerString(target_text, &target_address)) {
            JS_FreeValue(ctx, on_enter);
            JS_FreeValue(ctx, on_leave);
            return JS_ThrowTypeError(ctx, "Interceptor.attach target must be a non-zero pointer string");
        }
    }

    RuntimeState& state = GetRuntimeState();
    NativeJsHookRequest request = {};
    request.type = "inline";
    if (has_module_symbol_target) {
        request.module_name = module_name;
        request.symbol_name = symbol_name;
    } else {
        request.has_target_address = true;
        request.target_address = target_address;
    }
    request.blocking = blocking;
    request.snapshots = std::move(snapshots);
    return InstallNativeHookForCurrentScript(ctx, state, request, on_enter, on_leave);
}

JSValue JsInterceptorReplace(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Interceptor.replace requires target and replacement");
    }

    RuntimeState& state = GetRuntimeState();
    if (state.current_script_id == 0u) {
        return JS_ThrowInternalError(ctx, "Interceptor.replace requires an active script context");
    }

    uint64_t target_address = 0u;
    NativeFunctionValueType target_return_type = NativeFunctionValueType::kVoid;
    std::vector<NativeFunctionValueType> target_arg_types;
    const bool has_native_function_metadata = TryGetNativeFunctionMetadata(ctx,
                                                                           argv[0],
                                                                           &target_address,
                                                                           &target_return_type,
                                                                           &target_arg_types);
    if ((!has_native_function_metadata &&
         (!ParsePointerValue(ctx, argv[0], &target_address) || target_address == 0u)) ||
        target_address == 0u) {
        return JS_ThrowTypeError(ctx, "Interceptor.replace target must be a non-zero pointer value");
    }

    if (FindReplaceHookRecordByTargetLocked(state, target_address, nullptr) != nullptr) {
        return JS_ThrowInternalError(ctx, "Interceptor.replace target already replaced");
    }

    uint64_t replacement_address = 0u;
    std::string error_message;
    if (JS_IsFunction(ctx, argv[1]) && !ParsePointerValue(ctx, argv[1], &replacement_address)) {
        if (!has_native_function_metadata) {
            return JS_ThrowTypeError(
                ctx,
                "Interceptor.replace plain function replacement requires a NativeFunction target");
        }
        if (!RegisterNativeCallbackLocked(ctx,
                                          state,
                                          state.current_script_id,
                                          argv[1],
                                          target_return_type,
                                          target_arg_types,
                                          &replacement_address,
                                          &error_message)) {
            return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
        }
    } else if (!ParsePointerValue(ctx, argv[1], &replacement_address) ||
               replacement_address == 0u ||
               !HasNativeCallbackAddressForScriptLocked(state,
                                                        state.current_script_id,
                                                        replacement_address)) {
        return JS_ThrowTypeError(ctx, "Interceptor.replace replacement must be a NativeCallback");
    }

    uint64_t original_address = 0u;
    void* hook_handle = nullptr;
    if (!InstallNativeJsReplacementHook(target_address,
                                        replacement_address,
                                        &original_address,
                                        &hook_handle,
                                        &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }

    const uint32_t hook_id = state.next_replace_hook_id++;
    if (!StoreReplaceHookRecordLocked(state,
                                      state.current_script_id,
                                      target_address,
                                      replacement_address,
                                      original_address,
                                      hook_id,
                                      hook_handle)) {
        if (!UninstallNativeJsReplacementHook(hook_handle, nullptr)) {
            return JS_ThrowInternalError(ctx, "Interceptor.replace registration failed");
        }
        return JS_ThrowInternalError(ctx, "Interceptor.replace registration failed");
    }

    if (has_native_function_metadata && original_address != 0u) {
        JSValue arg_types_value = JS_GetPropertyStr(ctx, argv[0], kNativeFunctionArgTypesProperty);
        if (JS_IsException(arg_types_value)) {
            if (!UninstallNativeJsReplacementHook(hook_handle, nullptr)) {
                return JS_ThrowInternalError(ctx, "Interceptor.replace original registration failed");
            }
            EraseReplaceHookRecordByTargetLocked(state, target_address);
            return JS_EXCEPTION;
        }

        JSValue original_function = CreateNativeFunctionValue(ctx,
                                                              original_address,
                                                              target_return_type,
                                                              arg_types_value);
        JS_FreeValue(ctx, arg_types_value);
        if (JS_IsException(original_function)) {
            if (!UninstallNativeJsReplacementHook(hook_handle, nullptr)) {
                return JS_ThrowInternalError(ctx, "Interceptor.replace original registration failed");
            }
            EraseReplaceHookRecordByTargetLocked(state, target_address);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueStr(ctx,
                                      argv[0],
                                      "original",
                                      original_function,
                                      JS_PROP_CONFIGURABLE) < 0) {
            JS_FreeValue(ctx, original_function);
            if (!UninstallNativeJsReplacementHook(hook_handle, nullptr)) {
                return JS_ThrowInternalError(ctx, "Interceptor.replace original registration failed");
            }
            EraseReplaceHookRecordByTargetLocked(state, target_address);
            return JS_ThrowInternalError(ctx, "Interceptor.replace original registration failed");
        }
    }

    return JS_UNDEFINED;
}

JSValue JsInterceptorRevert(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Interceptor.revert requires target");
    }

    uint64_t target_address = 0u;
    NativeFunctionValueType ignored_return_type = NativeFunctionValueType::kVoid;
    std::vector<NativeFunctionValueType> ignored_arg_types;
    const bool has_native_function_metadata = TryGetNativeFunctionMetadata(ctx,
                                                                           argv[0],
                                                                           &target_address,
                                                                           &ignored_return_type,
                                                                           &ignored_arg_types);
    if ((!has_native_function_metadata &&
         (!ParsePointerValue(ctx, argv[0], &target_address) || target_address == 0u)) ||
        target_address == 0u) {
        return JS_ThrowTypeError(ctx, "Interceptor.revert target must be a non-zero pointer value");
    }

    RuntimeState& state = GetRuntimeState();
    RuntimeState::ReplaceHookRecord* record =
        FindReplaceHookRecordByTargetLocked(state, target_address, nullptr);
    if (record == nullptr) {
        return JS_ThrowInternalError(ctx, "Interceptor.revert target is not replaced");
    }

    std::string error_message;
    if (!UninstallNativeJsReplacementHook(record->hook_handle, &error_message)) {
        return JS_ThrowInternalError(ctx, "%s", error_message.c_str());
    }
    EraseReplaceHookRecordByTargetLocked(state, target_address);
    return JS_UNDEFINED;
}

JSValue JsInterceptorDetach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Interceptor.detach requires hookId");
    }

    uint32_t hook_id = 0;
    if (JS_ToUint32(ctx, &hook_id, argv[0]) < 0 || hook_id == 0) {
        return JS_ThrowTypeError(ctx, "Interceptor.detach hookId must be a positive integer");
    }

    RuntimeState& state = GetRuntimeState();
    return DetachNativeHookForCurrentScript(ctx, state, hook_id);
}

JSValue JsInterceptorDetachAll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    RuntimeState& state = GetRuntimeState();
    return DetachAllNativeHooksForCurrentScript(ctx, state);
}

bool InstallGlobalBindingsLocked(JSContext* ctx, std::string* error_message) {
    JSValue global = JS_UNDEFINED;
    JSValue nook = JS_UNDEFINED;
    JSValue nook_jni = JS_UNDEFINED;
    JSValue nook_native = JS_UNDEFINED;
    JSValue process = JS_UNDEFINED;
    JSValue module = JS_UNDEFINED;
    JSValue interceptor = JS_UNDEFINED;
    JSValue memory = JS_UNDEFINED;
    JSValue console = JS_UNDEFINED;
    JSValue rpc = JS_UNDEFINED;
    JSValue rpc_exports = JS_UNDEFINED;
    JSValue get_current_script_id_func = JS_UNDEFINED;
    JSValue run_in_script_func = JS_UNDEFINED;
    JSValue script = JS_UNDEFINED;
    JSValue script_bind_weak = JS_UNDEFINED;
    JSValue script_unbind_weak = JS_UNDEFINED;
    JSValue script_pin = JS_UNDEFINED;
    JSValue script_unpin = JS_UNDEFINED;
    JSValue script_run_gc_for_testing = JS_UNDEFINED;
    JSValue debug_symbol = JS_UNDEFINED;
    JSValue java = JS_UNDEFINED;
    JSValue java_vm = JS_UNDEFINED;
    JSValue java_perform = JS_UNDEFINED;
    JSValue java_vm_perform = JS_UNDEFINED;
    JSValue java_vm_getenv = JS_UNDEFINED;
    JSValue java_vm_trygetenv = JS_UNDEFINED;
    JSValue java_use = JS_UNDEFINED;
    JSValue java_choose = JS_UNDEFINED;
    JSValue java_enumerate_loaded_classes = JS_UNDEFINED;
    JSValue java_enumerate_class_loaders = JS_UNDEFINED;
    JSValue java_cast = JS_UNDEFINED;
    JSValue java_retain = JS_UNDEFINED;
    JSValue java_deopt = JS_UNDEFINED;
    JSValue java_set_forced_interpret_only = JS_UNDEFINED;
    JSValue java_art_router_debug = JS_UNDEFINED;
    JSValue java_update_class_loader = JS_UNDEFINED;
    JSValue java_is_class_loader_ready = JS_UNDEFINED;
    JSValue java_is_app_ready = JS_UNDEFINED;
    JSValue java_is_lifecycle_ready = JS_UNDEFINED;
    JSValue java_install_implementation = JS_UNDEFINED;
    JSValue java_invoke = JS_UNDEFINED;
    JSValue java_release = JS_UNDEFINED;
    JSValue java_use_with_loader = JS_UNDEFINED;
    JSValue java_get_class_wrapper = JS_UNDEFINED;
    JSValue java_register_class = JS_UNDEFINED;
    JSValue java_resolve_overload_signature = JS_UNDEFINED;
    JSValue java_resolve_field = JS_UNDEFINED;
    JSValue java_read_field = JS_UNDEFINED;
    JSValue java_write_field = JS_UNDEFINED;
    JSValue debug_symbol_from_address = JS_UNDEFINED;
    JSValue jni_read_jstring_utf8 = JS_UNDEFINED;
    JSValue native_attach = JS_UNDEFINED;
    JSValue process_enumerate_ranges = JS_UNDEFINED;
    JSValue process_find_range_by_address = JS_UNDEFINED;
    JSValue process_get_module_by_address = JS_UNDEFINED;
    JSValue process_pointer_size = JS_UNDEFINED;
    JSValue process_page_size = JS_UNDEFINED;
    JSValue process_arch = JS_UNDEFINED;
    JSValue process_platform = JS_UNDEFINED;
    JSValue process_id = JS_UNDEFINED;
    JSValue process_is_debugger_attached = JS_UNDEFINED;
    JSValue process_get_current_thread_id = JS_UNDEFINED;
    JSValue process_enumerate_threads = JS_UNDEFINED;
    JSValue process_enumerate_modules = JS_UNDEFINED;
    JSValue process_attach_module_observer = JS_UNDEFINED;
    JSValue process_find_module_by_name = JS_UNDEFINED;
    JSValue process_get_module_by_name = JS_UNDEFINED;
    JSValue process_main_module = JS_UNDEFINED;
    JSValue module_load = JS_UNDEFINED;
    JSValue module_ensure_initialized = JS_UNDEFINED;
    JSValue module_find_base_address = JS_UNDEFINED;
    JSValue module_get_base_address = JS_UNDEFINED;
    JSValue module_enumerate_modules = JS_UNDEFINED;
    JSValue module_enumerate_imports = JS_UNDEFINED;
    JSValue module_find_import = JS_UNDEFINED;
    JSValue module_get_import = JS_UNDEFINED;
    JSValue module_enumerate_exports = JS_UNDEFINED;
    JSValue module_enumerate_symbols = JS_UNDEFINED;
    JSValue module_find_symbol = JS_UNDEFINED;
    JSValue module_get_symbol = JS_UNDEFINED;
    JSValue module_find_global_export = JS_UNDEFINED;
    JSValue module_get_global_export = JS_UNDEFINED;
    JSValue module_find_export = JS_UNDEFINED;
    JSValue module_get_export = JS_UNDEFINED;
    JSValue module_find_range_by_address = JS_UNDEFINED;
    JSValue module_attach_export = JS_UNDEFINED;
    JSValue interceptor_attach = JS_UNDEFINED;
    JSValue interceptor_replace = JS_UNDEFINED;
    JSValue interceptor_revert = JS_UNDEFINED;
    JSValue interceptor_detach = JS_UNDEFINED;
    JSValue interceptor_detach_all = JS_UNDEFINED;
    JSValue memory_alloc = JS_UNDEFINED;
    JSValue memory_copy = JS_UNDEFINED;
    JSValue memory_dup = JS_UNDEFINED;
    JSValue memory_protect = JS_UNDEFINED;
    JSValue memory_patch_code = JS_UNDEFINED;
    JSValue memory_scan = JS_UNDEFINED;
    JSValue memory_scan_sync = JS_UNDEFINED;
    JSValue memory_read_utf8_string = JS_UNDEFINED;
    JSValue memory_alloc_utf8_string = JS_UNDEFINED;
    JSValue memory_alloc_utf16_string = JS_UNDEFINED;
    JSValue ptr_func = JS_UNDEFINED;
    JSValue uint64_func = JS_UNDEFINED;
    JSValue int64_func = JS_UNDEFINED;
    JSValue hexdump_func = JS_UNDEFINED;
    JSValue module_map_ctor = JS_UNDEFINED;
    JSValue native_function_ctor = JS_UNDEFINED;
    JSValue native_callback_ctor = JS_UNDEFINED;
    JSValue native_pointer_func = JS_UNDEFINED;
    JSValue null_pointer = JS_UNDEFINED;
    JSValue thread = JS_UNDEFINED;
    JSValue thread_id = JS_UNDEFINED;
    JSValue thread_backtrace = JS_UNDEFINED;
    JSValue thread_sleep = JS_UNDEFINED;
    JSValue backtracer = JS_UNDEFINED;
    JSValue backtracer_accurate = JS_UNDEFINED;
    JSValue backtracer_fuzzy = JS_UNDEFINED;
    JSValue send_func = JS_UNDEFINED;
    JSValue recv_func = JS_UNDEFINED;
    JSValue set_immediate_func = JS_UNDEFINED;
    JSValue set_timeout_func = JS_UNDEFINED;
    JSValue clear_timeout_func = JS_UNDEFINED;
    JSValue set_interval_func = JS_UNDEFINED;
    JSValue clear_interval_func = JS_UNDEFINED;
    JSValue log_func = JS_UNDEFINED;
    JSValue info_func = JS_UNDEFINED;
    JSValue warn_func = JS_UNDEFINED;
    JSValue error_func = JS_UNDEFINED;

    auto cleanup = [&]() {
        JS_FreeValue(ctx, error_func);
        JS_FreeValue(ctx, warn_func);
        JS_FreeValue(ctx, info_func);
        JS_FreeValue(ctx, log_func);
        JS_FreeValue(ctx, clear_interval_func);
        JS_FreeValue(ctx, set_interval_func);
        JS_FreeValue(ctx, clear_timeout_func);
        JS_FreeValue(ctx, set_timeout_func);
        JS_FreeValue(ctx, set_immediate_func);
        JS_FreeValue(ctx, recv_func);
        JS_FreeValue(ctx, send_func);
        JS_FreeValue(ctx, run_in_script_func);
        JS_FreeValue(ctx, get_current_script_id_func);
        JS_FreeValue(ctx, script_unbind_weak);
        JS_FreeValue(ctx, script_bind_weak);
        JS_FreeValue(ctx, script);
        JS_FreeValue(ctx, backtracer_fuzzy);
        JS_FreeValue(ctx, backtracer_accurate);
        JS_FreeValue(ctx, backtracer);
        JS_FreeValue(ctx, thread_sleep);
        JS_FreeValue(ctx, thread_backtrace);
        JS_FreeValue(ctx, thread_id);
        JS_FreeValue(ctx, thread);
        JS_FreeValue(ctx, null_pointer);
        JS_FreeValue(ctx, native_pointer_func);
        JS_FreeValue(ctx, hexdump_func);
        JS_FreeValue(ctx, int64_func);
        JS_FreeValue(ctx, uint64_func);
        JS_FreeValue(ctx, ptr_func);
        JS_FreeValue(ctx, native_callback_ctor);
        JS_FreeValue(ctx, native_function_ctor);
        JS_FreeValue(ctx, module_map_ctor);
        JS_FreeValue(ctx, interceptor_detach_all);
        JS_FreeValue(ctx, interceptor_detach);
        JS_FreeValue(ctx, interceptor_revert);
        JS_FreeValue(ctx, interceptor_replace);
        JS_FreeValue(ctx, interceptor_attach);
        JS_FreeValue(ctx, memory_alloc_utf16_string);
        JS_FreeValue(ctx, memory_alloc_utf8_string);
        JS_FreeValue(ctx, memory_scan_sync);
        JS_FreeValue(ctx, memory_scan);
        JS_FreeValue(ctx, memory_patch_code);
        JS_FreeValue(ctx, memory_protect);
        JS_FreeValue(ctx, memory_dup);
        JS_FreeValue(ctx, memory_copy);
        JS_FreeValue(ctx, memory_alloc);
        JS_FreeValue(ctx, process_main_module);
        JS_FreeValue(ctx, process_get_module_by_name);
        JS_FreeValue(ctx, process_find_module_by_name);
        JS_FreeValue(ctx, process_enumerate_threads);
        JS_FreeValue(ctx, process_get_current_thread_id);
        JS_FreeValue(ctx, process_enumerate_modules);
        JS_FreeValue(ctx, process_attach_module_observer);
        JS_FreeValue(ctx, process_is_debugger_attached);
        JS_FreeValue(ctx, process_id);
        JS_FreeValue(ctx, process_platform);
        JS_FreeValue(ctx, process_arch);
        JS_FreeValue(ctx, process_page_size);
        JS_FreeValue(ctx, process_pointer_size);
        JS_FreeValue(ctx, process_find_range_by_address);
        JS_FreeValue(ctx, process_enumerate_ranges);
        JS_FreeValue(ctx, process_get_module_by_address);
        JS_FreeValue(ctx, module_load);
        JS_FreeValue(ctx, module_ensure_initialized);
        JS_FreeValue(ctx, module_get_base_address);
        JS_FreeValue(ctx, module_find_base_address);
        JS_FreeValue(ctx, module_enumerate_modules);
        JS_FreeValue(ctx, module_enumerate_imports);
        JS_FreeValue(ctx, module_get_import);
        JS_FreeValue(ctx, module_find_import);
        JS_FreeValue(ctx, module_enumerate_exports);
        JS_FreeValue(ctx, module_enumerate_symbols);
        JS_FreeValue(ctx, module_get_symbol);
        JS_FreeValue(ctx, module_find_symbol);
        JS_FreeValue(ctx, module_get_global_export);
        JS_FreeValue(ctx, module_find_global_export);
        JS_FreeValue(ctx, module_get_export);
        JS_FreeValue(ctx, module_attach_export);
        JS_FreeValue(ctx, module_find_range_by_address);
        JS_FreeValue(ctx, module_find_export);
        JS_FreeValue(ctx, java_art_router_debug);
        JS_FreeValue(ctx, java_is_class_loader_ready);
        JS_FreeValue(ctx, java_is_app_ready);
        JS_FreeValue(ctx, java_is_lifecycle_ready);
        JS_FreeValue(ctx, java_update_class_loader);
        JS_FreeValue(ctx, java_set_forced_interpret_only);
        JS_FreeValue(ctx, java_deopt);
        JS_FreeValue(ctx, java_use_with_loader);
        JS_FreeValue(ctx, java_register_class);
        JS_FreeValue(ctx, java_invoke);
        JS_FreeValue(ctx, java_write_field);
        JS_FreeValue(ctx, java_read_field);
        JS_FreeValue(ctx, java_resolve_field);
        JS_FreeValue(ctx, java_install_implementation);
        JS_FreeValue(ctx, java_resolve_overload_signature);
        JS_FreeValue(ctx, java_retain);
        JS_FreeValue(ctx, java_cast);
        JS_FreeValue(ctx, java_enumerate_class_loaders);
        JS_FreeValue(ctx, java_choose);
        JS_FreeValue(ctx, java_enumerate_loaded_classes);
        JS_FreeValue(ctx, java_use);
        JS_FreeValue(ctx, java_vm_trygetenv);
        JS_FreeValue(ctx, java_vm_getenv);
        JS_FreeValue(ctx, java_vm_perform);
        JS_FreeValue(ctx, java_perform);
        JS_FreeValue(ctx, java_vm);
        JS_FreeValue(ctx, java);
        JS_FreeValue(ctx, debug_symbol_from_address);
        JS_FreeValue(ctx, debug_symbol);
        JS_FreeValue(ctx, native_attach);
        JS_FreeValue(ctx, jni_read_jstring_utf8);
        JS_FreeValue(ctx, rpc_exports);
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, console);
        JS_FreeValue(ctx, memory);
        JS_FreeValue(ctx, interceptor);
        JS_FreeValue(ctx, module);
        JS_FreeValue(ctx, process);
        JS_FreeValue(ctx, nook_native);
        JS_FreeValue(ctx, nook_jni);
        JS_FreeValue(ctx, nook);
        JS_FreeValue(ctx, global);
    };

    global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    send_func = JS_NewCFunction(ctx, JsSend, "send", 1);
    if (JS_SetPropertyStr(ctx, global, "send", send_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    send_func = JS_UNDEFINED;

    recv_func = JS_NewCFunction(ctx, JsRecv, "recv", 2);
    if (JS_SetPropertyStr(ctx, global, "recv", recv_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    recv_func = JS_UNDEFINED;

    set_immediate_func = JS_NewCFunction(ctx, JsSetImmediate, "setImmediate", 1);
    if (JS_SetPropertyStr(ctx, global, "setImmediate", set_immediate_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    set_immediate_func = JS_UNDEFINED;

    set_timeout_func = JS_NewCFunction(ctx, JsSetTimeout, "setTimeout", 2);
    if (JS_SetPropertyStr(ctx, global, "setTimeout", set_timeout_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    set_timeout_func = JS_UNDEFINED;

    clear_timeout_func = JS_NewCFunction(ctx, JsClearTimeout, "clearTimeout", 1);
    if (JS_SetPropertyStr(ctx, global, "clearTimeout", clear_timeout_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    clear_timeout_func = JS_UNDEFINED;

    set_interval_func = JS_NewCFunction(ctx, JsSetInterval, "setInterval", 2);
    if (JS_SetPropertyStr(ctx, global, "setInterval", set_interval_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    set_interval_func = JS_UNDEFINED;

    clear_interval_func = JS_NewCFunction(ctx, JsClearInterval, "clearInterval", 1);
    if (JS_SetPropertyStr(ctx, global, "clearInterval", clear_interval_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    clear_interval_func = JS_UNDEFINED;

    ptr_func = JS_NewCFunction(ctx, JsPtr, "ptr", 1);
    if (JS_SetPropertyStr(ctx, global, "ptr", ptr_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    ptr_func = JS_UNDEFINED;

    uint64_func = JS_NewCFunction(ctx, JsUInt64, "uint64", 1);
    if (JS_SetPropertyStr(ctx, global, "uint64", uint64_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    uint64_func = JS_UNDEFINED;

    int64_func = JS_NewCFunction(ctx, JsInt64, "int64", 1);
    if (JS_SetPropertyStr(ctx, global, "int64", int64_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    int64_func = JS_UNDEFINED;

    hexdump_func = JS_NewCFunction(ctx, JsHexdump, "hexdump", 2);
    if (JS_SetPropertyStr(ctx, global, "hexdump", hexdump_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    hexdump_func = JS_UNDEFINED;

    native_callback_ctor =
        JS_NewCFunction2(ctx, JsNativeCallbackConstructor, "NativeCallback", 3, JS_CFUNC_constructor, 0);
    if (JS_IsException(native_callback_ctor) ||
        JS_SetConstructorBit(ctx, native_callback_ctor, true) < 0 ||
        JS_SetPropertyStr(ctx, global, "NativeCallback", native_callback_ctor) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    native_callback_ctor = JS_UNDEFINED;

    native_function_ctor =
        JS_NewCFunction2(ctx, JsNativeFunctionConstructor, "NativeFunction", 3, JS_CFUNC_constructor, 0);
    if (JS_IsException(native_function_ctor) ||
        JS_SetConstructorBit(ctx, native_function_ctor, true) < 0 ||
        JS_SetPropertyStr(ctx, global, "NativeFunction", native_function_ctor) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    native_function_ctor = JS_UNDEFINED;

    module_map_ctor =
        JS_NewCFunction2(ctx, JsModuleMapConstructor, "ModuleMap", 0, JS_CFUNC_constructor, 0);
    if (JS_IsException(module_map_ctor) ||
        JS_SetConstructorBit(ctx, module_map_ctor, true) < 0 ||
        JS_SetPropertyStr(ctx, global, "ModuleMap", module_map_ctor) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    module_map_ctor = JS_UNDEFINED;

    native_pointer_func = JS_NewCFunction(ctx, JsPtr, "NativePointer", 1);
    if (JS_SetPropertyStr(ctx, global, "NativePointer", native_pointer_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    native_pointer_func = JS_UNDEFINED;

    null_pointer = MakeNativePointer(ctx, 0);
    if (JS_IsException(null_pointer) || JS_SetPropertyStr(ctx, global, "NULL", null_pointer) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    null_pointer = JS_UNDEFINED;

    rpc = JS_NewObject(ctx);
    rpc_exports = JS_NewObject(ctx);
    if (JS_IsException(rpc) || JS_IsException(rpc_exports) ||
        JS_SetPropertyStr(ctx, rpc, "exports", rpc_exports) < 0 ||
        JS_SetPropertyStr(ctx, global, "rpc", rpc) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    rpc = JS_UNDEFINED;
    rpc_exports = JS_UNDEFINED;

    get_current_script_id_func =
        JS_NewCFunction(ctx, JsGetCurrentScriptId, kGetCurrentScriptIdFunctionName, 0);
    run_in_script_func = JS_NewCFunction(ctx, JsRunInScript, kRunInScriptFunctionName, 2);
    if (JS_IsException(get_current_script_id_func) ||
        JS_IsException(run_in_script_func) ||
        JS_SetPropertyStr(ctx, global, kGetCurrentScriptIdFunctionName, get_current_script_id_func) < 0 ||
        JS_SetPropertyStr(ctx, global, kRunInScriptFunctionName, run_in_script_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    get_current_script_id_func = JS_UNDEFINED;
    run_in_script_func = JS_UNDEFINED;

    script = JS_NewObject(ctx);
    script_bind_weak = JS_NewCFunction(ctx, JsScriptBindWeak, "bindWeak", 2);
    script_unbind_weak = JS_NewCFunction(ctx, JsScriptUnbindWeak, "unbindWeak", 1);
    script_pin = JS_NewCFunction(ctx, JsScriptPin, "pin", 0);
    script_unpin = JS_NewCFunction(ctx, JsScriptUnpin, "unpin", 0);
    script_run_gc_for_testing =
        JS_NewCFunction(ctx, JsScriptRunGcForTesting, "_runGcForTesting", 0);
    if (JS_IsException(script) || JS_IsException(script_bind_weak) ||
        JS_IsException(script_unbind_weak) ||
        JS_IsException(script_pin) ||
        JS_IsException(script_unpin) ||
        JS_IsException(script_run_gc_for_testing) ||
        JS_SetPropertyStr(ctx, script, "bindWeak", script_bind_weak) < 0 ||
        JS_SetPropertyStr(ctx, script, "unbindWeak", script_unbind_weak) < 0 ||
        JS_SetPropertyStr(ctx, script, "pin", script_pin) < 0 ||
        JS_SetPropertyStr(ctx, script, "unpin", script_unpin) < 0 ||
        JS_SetPropertyStr(ctx, script, "_runGcForTesting", script_run_gc_for_testing) < 0 ||
        JS_SetPropertyStr(ctx, global, "Script", script) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    script = JS_UNDEFINED;
    script_bind_weak = JS_UNDEFINED;
    script_unbind_weak = JS_UNDEFINED;
    script_pin = JS_UNDEFINED;
    script_unpin = JS_UNDEFINED;
    script_run_gc_for_testing = JS_UNDEFINED;
    java = JS_NewObject(ctx);
    java_vm = JS_NewObject(ctx);
    java_perform = JS_NewCFunction(ctx, JsJavaPerform, "perform", 1);
    java_vm_perform = JS_NewCFunction(ctx, JsJavaVmPerform, "perform", 1);
    java_vm_getenv = JS_NewCFunction(ctx, JsJavaVmGetEnv, "getEnv", 0);
    java_vm_trygetenv = JS_NewCFunction(ctx, JsJavaVmTryGetEnv, "tryGetEnv", 0);
    java_use = JS_NewCFunction(ctx, JsJavaUse, "use", 1);
    java_choose = JS_NewCFunction(ctx, JsJavaChoose, "choose", 2);
    java_enumerate_loaded_classes =
        JS_NewCFunction(ctx, JsJavaEnumerateLoadedClasses, "enumerateLoadedClasses", 1);
    java_enumerate_class_loaders =
        JS_NewCFunction(ctx, JsJavaEnumerateClassLoaders, "enumerateClassLoaders", 1);
    java_cast = JS_NewCFunction(ctx, JsJavaCast, "cast", 2);
    java_retain = JS_NewCFunction(ctx, JsJavaRetain, "retain", 1);
    java_deopt = JS_NewCFunction(ctx, JsJavaDeopt, "deopt", 0);
    java_set_forced_interpret_only =
        JS_NewCFunction(ctx, JsJavaSetForcedInterpretOnly, "_setForcedInterpretOnly", 1);
    java_art_router_debug =
        JS_NewCFunction(ctx, JsJavaArtRouterDebug, "_artRouterDebug", 0);
    java_update_class_loader =
        JS_NewCFunction(ctx, JsJavaUpdateClassLoader, "_updateClassLoader", 1);
    java_is_class_loader_ready =
        JS_NewCFunction(ctx, JsJavaIsClassLoaderReady, "_isClassLoaderReady", 0);
    java_is_app_ready =
        JS_NewCFunction(ctx, JsJavaIsApplicationReady, "_isAppReady", 0);
    java_is_lifecycle_ready =
        JS_NewCFunction(ctx, JsJavaIsLifecycleReady, "_isLifecycleReady", 0);
    java_install_implementation =
        JS_NewCFunction(ctx, JsJavaInstallImplementation, kJavaInstallImplementationFunctionName, 2);
    java_invoke =
        JS_NewCFunction(ctx, JsJavaInvoke, kJavaInvokeFunctionName, 1);
    java_release =
        JS_NewCFunction(ctx, JsJavaRelease, kJavaReleaseFunctionName, 1);
    java_use_with_loader =
        JS_NewCFunction(ctx, JsJavaUseWithLoader, kJavaUseWithLoaderFunctionName, 2);
    java_get_class_wrapper =
        JS_NewCFunction(ctx, JsJavaGetClassWrapper, kJavaGetClassWrapperFunctionName, 2);
    java_register_class =
        JS_NewCFunction(ctx, JsJavaRegisterClass, kJavaRegisterClassFunctionName, 4);
    java_resolve_overload_signature =
        JS_NewCFunction(ctx, JsJavaResolveOverloadSignature, kJavaResolveOverloadSignatureFunctionName, 4);
    java_resolve_field =
        JS_NewCFunction(ctx, JsJavaResolveField, kJavaResolveFieldFunctionName, 3);
    java_read_field =
        JS_NewCFunction(ctx, JsJavaReadField, kJavaReadFieldFunctionName, 1);
    java_write_field =
        JS_NewCFunction(ctx, JsJavaWriteField, kJavaWriteFieldFunctionName, 2);
    if (JS_IsException(java) || JS_IsException(java_vm) || JS_IsException(java_perform) ||
        JS_IsException(java_vm_perform) || JS_IsException(java_vm_getenv) ||
        JS_IsException(java_vm_trygetenv) || JS_IsException(java_use) ||
        JS_IsException(java_choose) ||
        JS_IsException(java_enumerate_loaded_classes) ||
        JS_IsException(java_enumerate_class_loaders) ||
        JS_IsException(java_cast) ||
        JS_IsException(java_retain) ||
        JS_IsException(java_deopt) ||
        JS_IsException(java_set_forced_interpret_only) ||
        JS_IsException(java_art_router_debug) ||
        JS_IsException(java_update_class_loader) ||
        JS_IsException(java_is_class_loader_ready) ||
        JS_IsException(java_is_app_ready) ||
        JS_IsException(java_is_lifecycle_ready) ||
        JS_IsException(java_install_implementation) ||
        JS_IsException(java_invoke) ||
        JS_IsException(java_release) ||
        JS_IsException(java_use_with_loader) ||
        JS_IsException(java_get_class_wrapper) ||
        JS_IsException(java_register_class) ||
        JS_IsException(java_resolve_overload_signature) ||
        JS_IsException(java_resolve_field) ||
        JS_IsException(java_read_field) ||
        JS_IsException(java_write_field) ||
        JS_SetPropertyStr(ctx, java_vm, "perform", java_vm_perform) < 0 ||
        JS_SetPropertyStr(ctx, java_vm, "getEnv", java_vm_getenv) < 0 ||
        JS_SetPropertyStr(ctx, java_vm, "tryGetEnv", java_vm_trygetenv) < 0 ||
        JS_SetPropertyStr(ctx, java, "vm", java_vm) < 0 ||
        JS_SetPropertyStr(ctx, java, "perform", java_perform) < 0 ||
        JS_SetPropertyStr(ctx, java, "use", java_use) < 0 ||
        JS_SetPropertyStr(ctx, java, "choose", java_choose) < 0 ||
        JS_SetPropertyStr(ctx, java, "enumerateLoadedClasses", java_enumerate_loaded_classes) < 0 ||
        JS_SetPropertyStr(ctx, java, "enumerateClassLoaders", java_enumerate_class_loaders) < 0 ||
        JS_SetPropertyStr(ctx, java, "cast", java_cast) < 0 ||
        JS_SetPropertyStr(ctx, java, "retain", java_retain) < 0 ||
        JS_SetPropertyStr(ctx, java, "deopt", java_deopt) < 0 ||
        JS_SetPropertyStr(ctx,
                          java,
                          "_invokeResolverVersion",
                          JS_NewString(ctx, kJavaInvokeResolverVersion)) < 0 ||
        JS_SetPropertyStr(ctx, java, "_setForcedInterpretOnly", java_set_forced_interpret_only) < 0 ||
        JS_SetPropertyStr(ctx, java, "_artRouterDebug", java_art_router_debug) < 0 ||
        JS_SetPropertyStr(ctx, java, "_updateClassLoader", java_update_class_loader) < 0 ||
        JS_SetPropertyStr(ctx, java, "_isClassLoaderReady", java_is_class_loader_ready) < 0 ||
        JS_SetPropertyStr(ctx, java, "_isAppReady", java_is_app_ready) < 0 ||
        JS_SetPropertyStr(ctx, java, "_isLifecycleReady", java_is_lifecycle_ready) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaInstallImplementationFunctionName, java_install_implementation) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaInvokeFunctionName, java_invoke) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaReleaseFunctionName, java_release) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaUseWithLoaderFunctionName, java_use_with_loader) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaGetClassWrapperFunctionName, java_get_class_wrapper) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaRegisterClassFunctionName, java_register_class) < 0 ||
        JS_SetPropertyStr(ctx,
                          global,
                          kJavaResolveOverloadSignatureFunctionName,
                          java_resolve_overload_signature) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaResolveFieldFunctionName, java_resolve_field) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaReadFieldFunctionName, java_read_field) < 0 ||
        JS_SetPropertyStr(ctx, global, kJavaWriteFieldFunctionName, java_write_field) < 0 ||
        JS_SetPropertyStr(ctx, global, "Java", java) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    if (!InstallJavaBootstrap(ctx, error_message)) {
        cleanup();
        return false;
    }
    java = JS_UNDEFINED;
    java_vm = JS_UNDEFINED;
    java_perform = JS_UNDEFINED;
    java_vm_perform = JS_UNDEFINED;
    java_vm_getenv = JS_UNDEFINED;
    java_vm_trygetenv = JS_UNDEFINED;
    java_use = JS_UNDEFINED;
    java_choose = JS_UNDEFINED;
    java_enumerate_loaded_classes = JS_UNDEFINED;
    java_enumerate_class_loaders = JS_UNDEFINED;
    java_cast = JS_UNDEFINED;
    java_retain = JS_UNDEFINED;
    java_deopt = JS_UNDEFINED;
    java_set_forced_interpret_only = JS_UNDEFINED;
    java_art_router_debug = JS_UNDEFINED;
    java_update_class_loader = JS_UNDEFINED;
    java_is_class_loader_ready = JS_UNDEFINED;
    java_is_app_ready = JS_UNDEFINED;
    java_is_lifecycle_ready = JS_UNDEFINED;
    java_install_implementation = JS_UNDEFINED;
    java_invoke = JS_UNDEFINED;
    java_release = JS_UNDEFINED;
    java_use_with_loader = JS_UNDEFINED;
    java_get_class_wrapper = JS_UNDEFINED;
    java_register_class = JS_UNDEFINED;
    java_resolve_overload_signature = JS_UNDEFINED;
    java_resolve_field = JS_UNDEFINED;
    java_read_field = JS_UNDEFINED;
    java_write_field = JS_UNDEFINED;

    process = JS_NewObject(ctx);
    process_enumerate_ranges =
        JS_NewCFunction(ctx, JsProcessEnumerateRanges, "enumerateRanges", 1);
    process_find_range_by_address =
        JS_NewCFunction(ctx, JsProcessFindRangeByAddress, "findRangeByAddress", 1);
    process_get_module_by_address =
        JS_NewCFunction(ctx, JsProcessGetModuleByAddress, "getModuleByAddress", 1);
    process_enumerate_modules =
        JS_NewCFunction(ctx, JsProcessEnumerateModules, "enumerateModules", 0);
    process_get_current_thread_id =
        JS_NewCFunction(ctx, JsProcessGetCurrentThreadId, "getCurrentThreadId", 0);
    process_enumerate_threads =
        JS_NewCFunction(ctx, JsProcessEnumerateThreads, "enumerateThreads", 0);
    process_attach_module_observer =
        JS_NewCFunction(ctx, JsProcessAttachModuleObserver, "attachModuleObserver", 1);
    process_find_module_by_name =
        JS_NewCFunction(ctx, JsProcessFindModuleByName, "findModuleByName", 1);
    process_get_module_by_name =
        JS_NewCFunction(ctx, JsProcessGetModuleByName, "getModuleByName", 1);
    process_pointer_size = JS_NewUint32(ctx, static_cast<uint32_t>(sizeof(void*)));
    process_id = JS_NewUint32(ctx, GetCurrentProcessIdForJs());
    process_is_debugger_attached =
        JS_NewCFunction(ctx, JsProcessIsDebuggerAttached, "isDebuggerAttached", 0);
#if defined(_WIN32)
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(system_info.dwPageSize));
    process_arch = JS_NewString(ctx, sizeof(void*) == 8 ? "x64" : "x86");
    process_platform = JS_NewString(ctx, "windows");
#elif defined(__ANDROID__) && defined(__aarch64__)
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(getpagesize()));
    process_arch = JS_NewString(ctx, "arm64");
    process_platform = JS_NewString(ctx, "linux");
#elif defined(__ANDROID__) && defined(__arm__)
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(getpagesize()));
    process_arch = JS_NewString(ctx, "arm");
    process_platform = JS_NewString(ctx, "linux");
#elif defined(__x86_64__)
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(getpagesize()));
    process_arch = JS_NewString(ctx, "x64");
    process_platform = JS_NewString(ctx, "linux");
#elif defined(__i386__)
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(getpagesize()));
    process_arch = JS_NewString(ctx, "x86");
    process_platform = JS_NewString(ctx, "linux");
#else
    process_page_size = JS_NewUint32(ctx, static_cast<uint32_t>(getpagesize()));
    process_arch = JS_NewString(ctx, sizeof(void*) == 8 ? "x64" : "x86");
    process_platform = JS_NewString(ctx, "linux");
#endif
    {
        std::vector<NativeModuleRecord> modules;
        std::string process_module_error;
        if (!CollectLoadedNativeModules(&modules, &process_module_error)) {
            process_main_module = JS_ThrowInternalError(ctx, "%s", process_module_error.c_str());
        } else {
            const NativeModuleRecord* main_module = FindMainLoadedModule(modules);
            process_main_module =
                main_module == nullptr ? JS_NULL : MakeModuleObject(ctx, *main_module);
        }
    }
    if (JS_IsException(process) || JS_IsException(process_enumerate_ranges) ||
        JS_IsException(process_find_range_by_address) ||
        JS_IsException(process_get_module_by_address) ||
        JS_IsException(process_enumerate_modules) ||
        JS_IsException(process_find_module_by_name) ||
        JS_IsException(process_get_module_by_name) ||
        JS_IsException(process_pointer_size) ||
        JS_IsException(process_page_size) ||
        JS_IsException(process_arch) ||
        JS_IsException(process_platform) ||
        JS_IsException(process_id) ||
        JS_IsException(process_is_debugger_attached) ||
        JS_IsException(process_get_current_thread_id) ||
        JS_IsException(process_enumerate_threads) ||
        JS_IsException(process_attach_module_observer) ||
        JS_IsException(process_main_module) ||
        JS_SetPropertyStr(ctx, process, "pointerSize", process_pointer_size) < 0 ||
        JS_SetPropertyStr(ctx, process, "pageSize", process_page_size) < 0 ||
        JS_SetPropertyStr(ctx, process, "arch", process_arch) < 0 ||
        JS_SetPropertyStr(ctx, process, "platform", process_platform) < 0 ||
        JS_SetPropertyStr(ctx, process, "id", process_id) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "isDebuggerAttached",
                          process_is_debugger_attached) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "getCurrentThreadId",
                          process_get_current_thread_id) < 0 ||
        JS_SetPropertyStr(ctx, process, "enumerateThreads", process_enumerate_threads) < 0 ||
        JS_SetPropertyStr(ctx, process, "enumerateModules", process_enumerate_modules) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "attachModuleObserver",
                          process_attach_module_observer) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "findModuleByName",
                          process_find_module_by_name) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "getModuleByName",
                          process_get_module_by_name) < 0 ||
        JS_SetPropertyStr(ctx, process, "mainModule", process_main_module) < 0 ||
        JS_SetPropertyStr(ctx, process, "enumerateRanges", process_enumerate_ranges) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "findRangeByAddress",
                          process_find_range_by_address) < 0 ||
        JS_SetPropertyStr(ctx,
                          process,
                          "getModuleByAddress",
                          process_get_module_by_address) < 0 ||
        JS_SetPropertyStr(ctx, global, "Process", process) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    process_pointer_size = JS_UNDEFINED;
    process_page_size = JS_UNDEFINED;
    process_arch = JS_UNDEFINED;
    process_platform = JS_UNDEFINED;
    process_id = JS_UNDEFINED;
    process_is_debugger_attached = JS_UNDEFINED;
    process_get_current_thread_id = JS_UNDEFINED;
    process_enumerate_threads = JS_UNDEFINED;
    process_enumerate_modules = JS_UNDEFINED;
    process_attach_module_observer = JS_UNDEFINED;
    process_find_module_by_name = JS_UNDEFINED;
    process_get_module_by_name = JS_UNDEFINED;
    process_main_module = JS_UNDEFINED;
    process_enumerate_ranges = JS_UNDEFINED;
    process_find_range_by_address = JS_UNDEFINED;
    process_get_module_by_address = JS_UNDEFINED;
    process = JS_UNDEFINED;

    thread = JS_NewObject(ctx);
    thread_id = JS_NewUint32(ctx, GetCurrentThreadIdForJs());
    thread_backtrace = JS_NewCFunction(ctx, JsThreadBacktrace, "backtrace", 2);
    thread_sleep = JS_NewCFunction(ctx, JsThreadSleep, "sleep", 1);
    backtracer = JS_NewObject(ctx);
    backtracer_accurate = JS_NewString(ctx, "accurate");
    backtracer_fuzzy = JS_NewString(ctx, "fuzzy");
    if (JS_IsException(thread) || JS_IsException(thread_id) || JS_IsException(thread_backtrace) ||
        JS_IsException(thread_sleep) ||
        JS_IsException(backtracer) || JS_IsException(backtracer_accurate) ||
        JS_IsException(backtracer_fuzzy) ||
        JS_SetPropertyStr(ctx, thread, "id", thread_id) < 0 ||
        JS_SetPropertyStr(ctx, thread, "sleep", thread_sleep) < 0 ||
        JS_SetPropertyStr(ctx, thread, "backtrace", thread_backtrace) < 0 ||
        JS_SetPropertyStr(ctx, backtracer, "ACCURATE", backtracer_accurate) < 0 ||
        JS_SetPropertyStr(ctx, backtracer, "FUZZY", backtracer_fuzzy) < 0 ||
        JS_SetPropertyStr(ctx, global, "Thread", thread) < 0 ||
        JS_SetPropertyStr(ctx, global, "Backtracer", backtracer) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    backtracer_fuzzy = JS_UNDEFINED;
    backtracer_accurate = JS_UNDEFINED;
    backtracer = JS_UNDEFINED;
    thread_sleep = JS_UNDEFINED;
    thread_backtrace = JS_UNDEFINED;
    thread_id = JS_UNDEFINED;
    thread = JS_UNDEFINED;

    nook = JS_NewObject(ctx);
    nook_jni = JS_NewObject(ctx);
    nook_native = JS_NewObject(ctx);
    jni_read_jstring_utf8 = JS_NewCFunction(ctx, JsNookJniReadJStringUtf8, "readJStringUtf8", 2);
    native_attach = JS_NewCFunction(ctx, JsNativeAttach, "attach", 1);
    if (JS_IsException(nook) || JS_IsException(nook_jni) || JS_IsException(nook_native) ||
        JS_IsException(jni_read_jstring_utf8) || JS_IsException(native_attach) ||
        JS_SetPropertyStr(ctx, nook_jni, "readJStringUtf8", jni_read_jstring_utf8) < 0 ||
        JS_SetPropertyStr(ctx, nook_native, "attach", native_attach) < 0 ||
        JS_SetPropertyStr(ctx, nook, "Jni", nook_jni) < 0 ||
        JS_SetPropertyStr(ctx, nook, "Native", nook_native) < 0 ||
        JS_SetPropertyStr(ctx, global, "Nook", nook) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    native_attach = JS_UNDEFINED;
    jni_read_jstring_utf8 = JS_UNDEFINED;
    nook_native = JS_UNDEFINED;
    nook_jni = JS_UNDEFINED;
    nook = JS_UNDEFINED;

    module = JS_NewObject(ctx);
    module_load = JS_NewCFunction(ctx, JsModuleLoad, "load", 1);
    module_ensure_initialized =
        JS_NewCFunction(ctx, JsModuleEnsureInitialized, "ensureInitialized", 1);
    module_find_base_address = JS_NewCFunction(ctx, JsModuleFindBaseAddress, "findBaseAddress", 1);
    module_get_base_address = JS_NewCFunction(ctx, JsModuleGetBaseAddress, "getBaseAddress", 1);
    module_enumerate_modules = JS_NewCFunction(ctx, JsModuleEnumerateModules, "enumerateModules", 0);
    module_enumerate_imports = JS_NewCFunction(ctx, JsModuleEnumerateImports, "enumerateImports", 1);
    module_find_import = JS_NewCFunction(ctx, JsModuleFindImportByName, "findImportByName", 2);
    module_get_import = JS_NewCFunction(ctx, JsModuleGetImportByName, "getImportByName", 2);
    module_enumerate_exports = JS_NewCFunction(ctx, JsModuleEnumerateExports, "enumerateExports", 1);
    module_enumerate_symbols = JS_NewCFunction(ctx, JsModuleEnumerateSymbols, "enumerateSymbols", 1);
    module_find_symbol = JS_NewCFunction(ctx, JsModuleFindSymbolByName, "findSymbolByName", 2);
    module_get_symbol = JS_NewCFunction(ctx, JsModuleGetSymbolByName, "getSymbolByName", 2);
    module_find_global_export =
        JS_NewCFunction(ctx, JsModuleFindGlobalExportByName, "findGlobalExportByName", 1);
    module_get_global_export =
        JS_NewCFunction(ctx, JsModuleGetGlobalExportByName, "getGlobalExportByName", 1);
    module_find_export = JS_NewCFunction(ctx, JsModuleFindExportByName, "findExportByName", 2);
    module_get_export = JS_NewCFunction(ctx, JsModuleGetExportByName, "getExportByName", 2);
    module_find_range_by_address =
        JS_NewCFunction(ctx, JsModuleFindRangeByAddress, "findRangeByAddress", 1);
    module_attach_export = JS_NewCFunction(ctx, JsModuleAttachExport, "attachExport", 3);
    if (JS_IsException(module) || JS_IsException(module_load) ||
        JS_IsException(module_ensure_initialized) ||
        JS_IsException(module_find_base_address) ||
        JS_IsException(module_get_base_address) || JS_IsException(module_enumerate_modules) ||
        JS_IsException(module_enumerate_imports) ||
        JS_IsException(module_find_import) ||
        JS_IsException(module_get_import) ||
        JS_IsException(module_enumerate_exports) ||
        JS_IsException(module_enumerate_symbols) ||
        JS_IsException(module_find_symbol) ||
        JS_IsException(module_get_symbol) ||
        JS_IsException(module_find_global_export) ||
        JS_IsException(module_get_global_export) ||
        JS_IsException(module_find_export) ||
        JS_IsException(module_get_export) ||
        JS_IsException(module_find_range_by_address) || JS_IsException(module_attach_export) ||
        JS_SetPropertyStr(ctx, module, "load", module_load) < 0 ||
        JS_SetPropertyStr(ctx, module, "ensureInitialized", module_ensure_initialized) < 0 ||
        JS_SetPropertyStr(ctx, module, "findBaseAddress", module_find_base_address) < 0 ||
        JS_SetPropertyStr(ctx, module, "getBaseAddress", module_get_base_address) < 0 ||
        JS_SetPropertyStr(ctx, module, "enumerateModules", module_enumerate_modules) < 0 ||
        JS_SetPropertyStr(ctx, module, "enumerateImports", module_enumerate_imports) < 0 ||
        JS_SetPropertyStr(ctx, module, "findImportByName", module_find_import) < 0 ||
        JS_SetPropertyStr(ctx, module, "getImportByName", module_get_import) < 0 ||
        JS_SetPropertyStr(ctx, module, "enumerateExports", module_enumerate_exports) < 0 ||
        JS_SetPropertyStr(ctx, module, "enumerateSymbols", module_enumerate_symbols) < 0 ||
        JS_SetPropertyStr(ctx, module, "findSymbolByName", module_find_symbol) < 0 ||
        JS_SetPropertyStr(ctx, module, "getSymbolByName", module_get_symbol) < 0 ||
        JS_SetPropertyStr(ctx, module, "findGlobalExportByName", module_find_global_export) < 0 ||
        JS_SetPropertyStr(ctx, module, "getGlobalExportByName", module_get_global_export) < 0 ||
        JS_SetPropertyStr(ctx, module, "findExportByName", module_find_export) < 0 ||
        JS_SetPropertyStr(ctx, module, "getExportByName", module_get_export) < 0 ||
        JS_SetPropertyStr(ctx, module, "findRangeByAddress", module_find_range_by_address) < 0 ||
        JS_SetPropertyStr(ctx, module, "attachExport", module_attach_export) < 0 ||
        JS_SetPropertyStr(ctx, global, "Module", module) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    module_load = JS_UNDEFINED;
    module_ensure_initialized = JS_UNDEFINED;
    module_find_base_address = JS_UNDEFINED;
    module_get_base_address = JS_UNDEFINED;
    module_enumerate_modules = JS_UNDEFINED;
    module_enumerate_imports = JS_UNDEFINED;
    module_find_import = JS_UNDEFINED;
    module_get_import = JS_UNDEFINED;
    module_enumerate_exports = JS_UNDEFINED;
    module_enumerate_symbols = JS_UNDEFINED;
    module_find_symbol = JS_UNDEFINED;
    module_get_symbol = JS_UNDEFINED;
    module_find_global_export = JS_UNDEFINED;
    module_get_global_export = JS_UNDEFINED;
    module_find_export = JS_UNDEFINED;
    module_get_export = JS_UNDEFINED;
    module_find_range_by_address = JS_UNDEFINED;
    module_attach_export = JS_UNDEFINED;
    module = JS_UNDEFINED;

    debug_symbol = JS_NewObject(ctx);
    debug_symbol_from_address =
        JS_NewCFunction(ctx, JsDebugSymbolFromAddress, "fromAddress", 1);
    if (JS_IsException(debug_symbol) || JS_IsException(debug_symbol_from_address) ||
        JS_SetPropertyStr(ctx, debug_symbol, "fromAddress", debug_symbol_from_address) < 0 ||
        JS_SetPropertyStr(ctx, global, "DebugSymbol", debug_symbol) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    debug_symbol_from_address = JS_UNDEFINED;
    debug_symbol = JS_UNDEFINED;

    interceptor = JS_NewObject(ctx);
    interceptor_attach = JS_NewCFunction(ctx, JsInterceptorAttach, "attach", 2);
    interceptor_replace = JS_NewCFunction(ctx, JsInterceptorReplace, "replace", 2);
    interceptor_revert = JS_NewCFunction(ctx, JsInterceptorRevert, "revert", 1);
    interceptor_detach = JS_NewCFunction(ctx, JsInterceptorDetach, "detach", 1);
    interceptor_detach_all = JS_NewCFunction(ctx, JsInterceptorDetachAll, "detachAll", 0);
    if (JS_IsException(interceptor) || JS_IsException(interceptor_attach) ||
        JS_IsException(interceptor_replace) || JS_IsException(interceptor_revert) ||
        JS_IsException(interceptor_detach) || JS_IsException(interceptor_detach_all) ||
        JS_SetPropertyStr(ctx, interceptor, "attach", interceptor_attach) < 0 ||
        JS_SetPropertyStr(ctx, interceptor, "replace", interceptor_replace) < 0 ||
        JS_SetPropertyStr(ctx, interceptor, "revert", interceptor_revert) < 0 ||
        JS_SetPropertyStr(ctx, interceptor, "detach", interceptor_detach) < 0 ||
        JS_SetPropertyStr(ctx, interceptor, "detachAll", interceptor_detach_all) < 0 ||
        JS_SetPropertyStr(ctx, global, "Interceptor", interceptor) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    interceptor_attach = JS_UNDEFINED;
    interceptor_replace = JS_UNDEFINED;
    interceptor_revert = JS_UNDEFINED;
    interceptor_detach = JS_UNDEFINED;
    interceptor_detach_all = JS_UNDEFINED;
    interceptor = JS_UNDEFINED;

    memory = JS_NewObject(ctx);
    memory_alloc = JS_NewCFunction(ctx, JsMemoryAlloc, "alloc", 1);
    memory_copy = JS_NewCFunction(ctx, JsMemoryCopy, "copy", 3);
    memory_dup = JS_NewCFunction(ctx, JsMemoryDup, "dup", 2);
    memory_protect = JS_NewCFunction(ctx, JsMemoryProtect, "protect", 3);
    memory_patch_code = JS_NewCFunction(ctx, JsMemoryPatchCode, "patchCode", 3);
    memory_scan = JS_NewCFunction(ctx, JsMemoryScan, "scan", 4);
    memory_scan_sync = JS_NewCFunction(ctx, JsMemoryScanSync, "scanSync", 3);
    memory_read_utf8_string = JS_NewCFunction(ctx, JsMemoryReadUtf8String, "readUtf8String", 2);
    memory_alloc_utf8_string = JS_NewCFunction(ctx, JsMemoryAllocUtf8String, "allocUtf8String", 1);
    memory_alloc_utf16_string = JS_NewCFunction(ctx, JsMemoryAllocUtf16String, "allocUtf16String", 1);
    if (JS_IsException(memory) || JS_IsException(memory_alloc) || JS_IsException(memory_copy) ||
        JS_IsException(memory_dup) || JS_IsException(memory_protect) ||
        JS_IsException(memory_patch_code) ||
        JS_IsException(memory_scan) ||
        JS_IsException(memory_scan_sync) ||
        JS_IsException(memory_read_utf8_string) ||
        JS_IsException(memory_alloc_utf8_string) ||
        JS_IsException(memory_alloc_utf16_string) ||
        JS_SetPropertyStr(ctx, memory, "alloc", memory_alloc) < 0 ||
        JS_SetPropertyStr(ctx, memory, "copy", memory_copy) < 0 ||
        JS_SetPropertyStr(ctx, memory, "dup", memory_dup) < 0 ||
        JS_SetPropertyStr(ctx, memory, "protect", memory_protect) < 0 ||
        JS_SetPropertyStr(ctx, memory, "patchCode", memory_patch_code) < 0 ||
        JS_SetPropertyStr(ctx, memory, "scan", memory_scan) < 0 ||
        JS_SetPropertyStr(ctx, memory, "scanSync", memory_scan_sync) < 0 ||
        JS_SetPropertyStr(ctx, memory, "readUtf8String", memory_read_utf8_string) < 0 ||
        JS_SetPropertyStr(ctx, memory, "allocUtf8String", memory_alloc_utf8_string) < 0 ||
        JS_SetPropertyStr(ctx, memory, "allocUtf16String", memory_alloc_utf16_string) < 0 ||
        JS_SetPropertyStr(ctx, global, "Memory", memory) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    memory_alloc = JS_UNDEFINED;
    memory_copy = JS_UNDEFINED;
    memory_dup = JS_UNDEFINED;
    memory_protect = JS_UNDEFINED;
    memory_patch_code = JS_UNDEFINED;
    memory_scan = JS_UNDEFINED;
    memory_scan_sync = JS_UNDEFINED;
    memory_read_utf8_string = JS_UNDEFINED;
    memory_alloc_utf8_string = JS_UNDEFINED;
    memory_alloc_utf16_string = JS_UNDEFINED;
    {
        JSValue memory_read_c_string = JS_GetPropertyStr(ctx, memory, "readUtf8String");
        if (JS_IsException(memory_read_c_string) ||
            JS_SetPropertyStr(ctx, memory, "readCString", memory_read_c_string) < 0) {
            cleanup();
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
    }
    {
        JSValue memory_read_ansi_string = JS_GetPropertyStr(ctx, memory, "readUtf8String");
        if (JS_IsException(memory_read_ansi_string) ||
            JS_SetPropertyStr(ctx, memory, "readAnsiString", memory_read_ansi_string) < 0) {
            cleanup();
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
    }
    {
        JSValue memory_alloc_ansi_string = JS_GetPropertyStr(ctx, memory, "allocUtf8String");
        if (JS_IsException(memory_alloc_ansi_string) ||
            JS_SetPropertyStr(ctx, memory, "allocAnsiString", memory_alloc_ansi_string) < 0) {
            cleanup();
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
    }
    memory = JS_UNDEFINED;

    console = JS_NewObject(ctx);
    if (JS_IsException(console)) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    log_func = JS_NewCFunctionMagic(ctx, JsConsoleLog, "log", 1, JS_CFUNC_generic_magic, 0);
    if (JS_SetPropertyStr(ctx, console, "log", log_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    log_func = JS_UNDEFINED;

    info_func = JS_NewCFunctionMagic(ctx, JsConsoleLog, "info", 1, JS_CFUNC_generic_magic, 0);
    if (JS_SetPropertyStr(ctx, console, "info", info_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    info_func = JS_UNDEFINED;

    warn_func = JS_NewCFunctionMagic(ctx, JsConsoleLog, "warn", 1, JS_CFUNC_generic_magic, 1);
    if (JS_SetPropertyStr(ctx, console, "warn", warn_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    warn_func = JS_UNDEFINED;

    error_func = JS_NewCFunctionMagic(ctx, JsConsoleLog, "error", 1, JS_CFUNC_generic_magic, 2);
    if (JS_SetPropertyStr(ctx, console, "error", error_func) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }
    error_func = JS_UNDEFINED;

    if (JS_SetPropertyStr(ctx, global, "console", console) < 0) {
        cleanup();
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    console = JS_UNDEFINED;
    cleanup();
    return true;
}

bool ResetRpcExportsObjectLocked(JSContext* ctx, std::string* error_message) {
    JSValue global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    JSValue rpc = JS_GetPropertyStr(ctx, global, "rpc");
    if (JS_IsException(rpc) || !JS_IsObject(rpc)) {
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, global);
        SetError(error_message, "rpc object missing");
        return false;
    }

    JSValue exports = JS_NewObject(ctx);
    if (JS_IsException(exports) || JS_SetPropertyStr(ctx, rpc, "exports", exports) < 0) {
        JS_FreeValue(ctx, exports);
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, global);
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    JS_FreeValue(ctx, rpc);
    JS_FreeValue(ctx, global);
    return true;
}

bool CaptureRpcExportsLocked(JSContext* ctx,
                             RuntimeState& state,
                             uint32_t script_id,
                             std::string* error_message) {
    if (script_id == 0) {
        return true;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    JSValue rpc = JS_GetPropertyStr(ctx, global, "rpc");
    if (JS_IsException(rpc) || !JS_IsObject(rpc)) {
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, global);
        SetError(error_message, "rpc object missing");
        return false;
    }

    JSValue exports = JS_GetPropertyStr(ctx, rpc, "exports");
    if (JS_IsException(exports) || !JS_IsObject(exports)) {
        JS_FreeValue(ctx, exports);
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, global);
        SetError(error_message, "rpc.exports must be an object");
        return false;
    }

    FreeRpcExportsLocked(ctx, state, script_id);
    std::unordered_map<std::string, JSValue> methods;

    JSPropertyEnum* props = nullptr;
    uint32_t prop_count = 0;
    if (JS_GetOwnPropertyNames(ctx,
                               &props,
                               &prop_count,
                               exports,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        JS_FreeValue(ctx, exports);
        JS_FreeValue(ctx, rpc);
        JS_FreeValue(ctx, global);
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    for (uint32_t index = 0; index < prop_count; ++index) {
        const char* name_cstr = JS_AtomToCString(ctx, props[index].atom);
        JSValue value = JS_GetProperty(ctx, exports, props[index].atom);
        if (name_cstr != nullptr && JS_IsFunction(ctx, value)) {
            methods.emplace(name_cstr, JS_DupValue(ctx, value));
        }
        JS_FreeValue(ctx, value);
        if (name_cstr != nullptr) {
            JS_FreeCString(ctx, name_cstr);
        }
        JS_FreeAtom(ctx, props[index].atom);
    }
    js_free(ctx, props);

    state.rpc_exports[script_id] = std::move(methods);
    JS_FreeValue(ctx, exports);
    JS_FreeValue(ctx, rpc);
    JS_FreeValue(ctx, global);
    return true;
}

bool DispatchJavaReadyCallbacksLocked(RuntimeState& state, std::string* error_message) {
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);

    JSValue global = JS_GetGlobalObject(state.context);
    if (JS_IsException(global)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    JSValue java = JS_GetPropertyStr(state.context, global, "Java");
    if (JS_IsException(java)) {
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    if (!JS_IsObject(java)) {
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        return true;
    }

    JSValue dispatch = JS_GetPropertyStr(state.context, java, "__nookDispatchReady");
    if (JS_IsException(dispatch)) {
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    if (!JS_IsFunction(state.context, dispatch)) {
        JS_FreeValue(state.context, dispatch);
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        return true;
    }

    JSValue result = JS_Call(state.context, dispatch, java, 0, nullptr);
    JS_FreeValue(state.context, dispatch);
    JS_FreeValue(state.context, java);
    JS_FreeValue(state.context, global);
    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    JS_FreeValue(state.context, result);
    return true;
}

bool DropJavaReadyCallbacksForScriptLocked(RuntimeState& state,
                                           uint32_t script_id,
                                           std::string* error_message) {
    if (state.context == nullptr) {
        return true;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);

    JSValue global = JS_GetGlobalObject(state.context);
    if (JS_IsException(global)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    JSValue java = JS_GetPropertyStr(state.context, global, "Java");
    if (JS_IsException(java)) {
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    if (!JS_IsObject(java)) {
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        return true;
    }

    JSValue drop = JS_GetPropertyStr(state.context, java, "__nookDropReadyCallbacksForScript");
    if (JS_IsException(drop)) {
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    if (!JS_IsFunction(state.context, drop)) {
        JS_FreeValue(state.context, drop);
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        return true;
    }

    JSValue script_id_value = JS_NewUint32(state.context, script_id);
    if (JS_IsException(script_id_value)) {
        JS_FreeValue(state.context, drop);
        JS_FreeValue(state.context, java);
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    JSValue argv[1] = {script_id_value};
    JSValue result = JS_Call(state.context, drop, java, 1, argv);
    JS_FreeValue(state.context, script_id_value);
    JS_FreeValue(state.context, drop);
    JS_FreeValue(state.context, java);
    JS_FreeValue(state.context, global);
    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    JS_FreeValue(state.context, result);
    return true;
}

bool EvaluateInternalLocked(JSContext* ctx,
                            const std::string& source,
                            const std::string& filename,
                            bool compile_only,
                            std::string* error_message) {
    RefreshQuickJsStackTopForCurrentThread(JS_GetRuntime(ctx));
    const int eval_flags = GetEvalFlags(source);
    JSValue result = JS_Eval(ctx,
                             source.c_str(),
                             source.size(),
                             filename.c_str(),
                             compile_only ? (eval_flags | JS_EVAL_FLAG_COMPILE_ONLY) : eval_flags);
    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(ctx));
        return false;
    }

    if (!compile_only && eval_flags == JS_EVAL_TYPE_MODULE) {
        JSValue evaluated = JS_EvalFunction(ctx, result);
        if (JS_IsException(evaluated)) {
            SetError(error_message, GetExceptionString(ctx));
            return false;
        }
        JS_FreeValue(ctx, evaluated);
        return true;
    }

    JS_FreeValue(ctx, result);
    return true;
}

}  // namespace

bool JsRuntime::Initialize(std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.runtime != nullptr && state.context != nullptr) {
        return true;
    }
    const char* stage = "begin";
    try {
        stage = "JS_NewRuntime";
        state.runtime = JS_NewRuntime();
        if (state.runtime == nullptr) {
            SetError(error_message, "JS_NewRuntime failed");
            return false;
        }

        stage = "JS_NewContext";
        state.context = JS_NewContext(state.runtime);
        if (state.context == nullptr) {
            JS_FreeRuntime(state.runtime);
            state.runtime = nullptr;
            SetError(error_message, "JS_NewContext failed");
            return false;
        }

        stage = "InstallGlobalBindingsLocked";
        InvalidateDebugSymbolCacheLocked(state);
        if (!InstallGlobalBindingsLocked(state.context, error_message)) {
            JS_FreeContext(state.context);
            JS_FreeRuntime(state.runtime);
            state.context = nullptr;
            state.runtime = nullptr;
            return false;
        }

        stage = "SetJavaJsHookInvocationDispatcher";
        SetJavaJsHookInvocationDispatcher(&DispatchJavaHookInvocationToRuntime);
        state.stop_timer_thread = false;
        state.timer_thread_running = true;

        stage = "timer_thread";
        state.timer_thread = std::thread([]() {
            RuntimeState& state = GetRuntimeState();
            std::unique_lock<std::recursive_mutex> lock(state.runtime_mutex);
            while (!state.stop_timer_thread) {
                if (state.context == nullptr) {
                    state.timer_cv.wait(lock, [&]() {
                        return state.stop_timer_thread || state.context != nullptr;
                    });
                    continue;
                }

                auto now = std::chrono::steady_clock::now();
                bool found_due = false;
                std::chrono::steady_clock::time_point next_due = {};
                for (const auto& entry : state.timers) {
                    const RuntimeState::TimerRecord& record = entry.second;
                    if (record.canceled || record.due_at <= now) {
                        found_due = true;
                        break;
                    }
                    if (next_due == std::chrono::steady_clock::time_point{} || record.due_at < next_due) {
                        next_due = record.due_at;
                    }
                }

                if (found_due) {
                    std::string ignored_error;
                    (void)DrainDueTimersLocked(state, &ignored_error);
                    (void)ExecutePendingJobsLocked(state, &ignored_error);
                    continue;
                }

                if (next_due == std::chrono::steady_clock::time_point{}) {
                    state.timer_cv.wait(lock, [&]() {
                        return state.stop_timer_thread || !state.timers.empty();
                    });
                } else {
                    state.timer_cv.wait_until(lock, next_due, [&]() {
                        return state.stop_timer_thread;
                    });
                }
            }
            state.timer_thread_running = false;
        });
        return true;
    } catch (const std::exception& exception) {
        NOOK_JS_RUNTIME_LOGE("js runtime initialize exception stage=%s error=%s",
                             stage,
                             exception.what());
        if (state.context != nullptr) {
            JS_FreeContext(state.context);
            state.context = nullptr;
        }
        if (state.runtime != nullptr) {
            JS_FreeRuntime(state.runtime);
            state.runtime = nullptr;
        }
        SetError(error_message,
                 std::string("js runtime initialize exception at stage=") + stage +
                     ": " + exception.what());
        return false;
    } catch (...) {
        NOOK_JS_RUNTIME_LOGE("js runtime initialize exception stage=%s error=unknown",
                             stage);
        if (state.context != nullptr) {
            JS_FreeContext(state.context);
            state.context = nullptr;
        }
        if (state.runtime != nullptr) {
            JS_FreeRuntime(state.runtime);
            state.runtime = nullptr;
        }
        SetError(error_message,
                 std::string("js runtime initialize exception at stage=") + stage +
                     ": unknown");
        return false;
    }
}

void JsRuntime::Shutdown() {
    RuntimeState& state = GetRuntimeState();
    {
        std::lock_guard<std::mutex> callback_lock(state.callback_mutex);
        state.send_callback = {};
    }
    if (state.timer_thread.joinable()) {
        {
            std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
            state.stop_timer_thread = true;
            state.timer_cv.notify_all();
        }
        state.timer_thread.join();
    }
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    InvalidateDebugSymbolCacheLocked(state);
    ResetJavaJsHookInvocationDispatcher();
    if (state.context != nullptr) {
        std::vector<uint32_t> script_ids;
        script_ids.reserve(state.native_hook_callbacks.size() + state.replace_hook_records.size());
        for (const auto& entry : state.native_hook_callbacks) {
            script_ids.push_back(entry.first);
        }
        for (const auto& entry : state.replace_hook_records) {
            if (std::find(script_ids.begin(), script_ids.end(), entry.first) == script_ids.end()) {
                script_ids.push_back(entry.first);
            }
        }
        for (uint32_t script_id : script_ids) {
            std::string ignored_error;
            (void)UninstallNativeHooksForScriptLocked(state, script_id, &ignored_error);
        }
        for (auto& entry : state.recv_callbacks) {
            JS_FreeValue(state.context, entry.second);
        }
        for (auto& entry : state.rpc_exports) {
            for (auto& rpc_entry : entry.second) {
                JS_FreeValue(state.context, rpc_entry.second);
            }
        }
        for (auto& entry : state.native_hook_callbacks) {
            for (auto& hook_entry : entry.second) {
                JS_FreeValue(state.context, hook_entry.second.on_enter);
                JS_FreeValue(state.context, hook_entry.second.on_leave);
                JS_FreeValue(state.context, hook_entry.second.cached_invocation_receiver);
                JS_FreeValue(state.context, hook_entry.second.cached_invocation_args);
                JS_FreeValue(state.context, hook_entry.second.cached_invocation_retval);
            }
        }
        for (auto& entry : state.java_hook_callbacks) {
            for (auto& hook_entry : entry.second) {
                std::string ignored_error;
                (void)UninstallJavaJsHook(hook_entry.first, &ignored_error);
                JS_FreeValue(state.context, hook_entry.second);
            }
        }
        for (auto& entry : state.java_registered_class_callbacks) {
            for (auto& callback_entry : entry.second) {
                for (auto& method_entry : callback_entry.second) {
                    for (auto& signature_entry : method_entry.second) {
                        JS_FreeValue(state.context, signature_entry.second);
                    }
                }
            }
        }
        for (auto& entry : state.active_native_invocations) {
            for (auto& invocation_entry : entry.second) {
                JS_FreeValue(state.context, invocation_entry.second);
            }
        }
        for (auto& entry : state.native_callback_records) {
            for (auto& callback_entry : entry.second) {
                JS_FreeValue(state.context, callback_entry.second.function);
                ReleaseNativeCallbackSlotLocked(state, callback_entry.second.slot);
            }
        }
        std::vector<uint32_t> allocation_script_ids;
        allocation_script_ids.reserve(state.owned_allocations.size());
        for (const auto& entry : state.owned_allocations) {
            allocation_script_ids.push_back(entry.first);
        }
        for (uint32_t script_id : allocation_script_ids) {
            FreeOwnedAllocationsLocked(state, script_id);
        }
        std::vector<uint32_t> java_handle_script_ids;
        java_handle_script_ids.reserve(state.owned_java_handles.size());
        for (const auto& entry : state.owned_java_handles) {
            java_handle_script_ids.push_back(entry.first);
        }
        for (uint32_t script_id : java_handle_script_ids) {
            std::string ignored_error;
            (void)FreeOwnedJavaHandlesLocked(state, script_id, &ignored_error);
        }
        std::vector<uint32_t> weak_binding_script_ids;
        weak_binding_script_ids.reserve(state.weak_bindings.size());
        for (const auto& entry : state.weak_bindings) {
            if (std::find(weak_binding_script_ids.begin(),
                          weak_binding_script_ids.end(),
                          entry.second.script_id) == weak_binding_script_ids.end()) {
                weak_binding_script_ids.push_back(entry.second.script_id);
            }
        }
        for (uint32_t script_id : weak_binding_script_ids) {
            FreeWeakBindingsForScriptLocked(state.context, state, script_id);
        }
        FreeAllTimersLocked(state.context, state);
    }
    state.recv_callbacks.clear();
    state.rpc_exports.clear();
    state.native_hook_callbacks.clear();
    state.java_hook_callbacks.clear();
    state.java_registered_class_callbacks.clear();
    state.active_native_invocations.clear();
    state.native_callback_records.clear();
    state.replace_hook_records.clear();
    state.module_observers.clear();
    state.pending_module_events.clear();
    state.native_callback_slot_used.fill(false);
    state.owned_allocations.clear();
    state.owned_java_handles.clear();
    state.script_pin_counts.clear();
    state.weak_bindings.clear();
    state.pending_weak_binding_ids.clear();
    state.timers.clear();
    state.next_timer_id = 1u;
    state.timer_thread_running = false;
    state.stop_timer_thread = false;
    state.native_hook_installer_dependencies = {};
    state.java_hook_installer_dependencies = {};
    state.current_script_id = 0;
    state.next_native_callback_id = 1u;
    state.next_replace_hook_id = 1u;
    state.next_weak_binding_id = 1u;
    JsRuntimeResetReadJStringUtf8ForTesting();
    ResetNativeJsHookEventQueueForTesting();
    if (state.context != nullptr) {
        JS_FreeContext(state.context);
        state.context = nullptr;
    }
    if (state.runtime != nullptr) {
        JS_FreeRuntime(state.runtime);
        state.runtime = nullptr;
    }
}

bool JsRuntime::IsInitialized() {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    return state.runtime != nullptr && state.context != nullptr;
}

void JsRuntime::SetSendCallback(SendCallback callback) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::mutex> lock(state.callback_mutex);
    state.send_callback = std::move(callback);
}

bool JsRuntime::DispatchMessage(uint32_t script_id,
                                const std::string& message_json,
                                const std::vector<uint8_t>& data,
                                std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }

    auto it = state.recv_callbacks.find(script_id);
    if (it == state.recv_callbacks.end()) {
        return true;
    }

    JSValue global = JS_GetGlobalObject(state.context);
    JSValue callback = JS_DupValue(state.context, it->second);
    JSValue message = JS_ParseJSON(state.context,
                                   message_json.c_str(),
                                   message_json.size(),
                                   "<script-post>");
    if (JS_IsException(message)) {
        JS_FreeValue(state.context, callback);
        JS_FreeValue(state.context, global);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    JSValue data_value = JS_UNDEFINED;
    if (!data.empty()) {
        data_value = JS_NewArrayBufferCopy(state.context, data.data(), data.size());
        if (JS_IsException(data_value)) {
            JS_FreeValue(state.context, message);
            JS_FreeValue(state.context, callback);
            JS_FreeValue(state.context, global);
            SetError(error_message, GetExceptionString(state.context));
            return false;
        }
    }

    JSValue argv[2] = {message, data_value};
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context, callback, global, 2, argv);
    JS_FreeValue(state.context, data_value);
    JS_FreeValue(state.context, message);
    JS_FreeValue(state.context, callback);
    JS_FreeValue(state.context, global);
    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    JS_FreeValue(state.context, result);
    return DrainWeakBindingMaintenanceLocked(state, error_message);
}

bool JsRuntime::DispatchPendingNativeHookEvents(std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }

    if (!ForwardPendingModuleEventsLocked(state, error_message)) {
        return false;
    }

    NativeJsHookStatusEvent status_event = {};
    while (TryDequeueNativeJsHookStatusEvent(&status_event)) {
        NOOK_JS_RUNTIME_LOGI("native hook status dequeued hook_id=%u state=%s",
                             status_event.hook_id,
                             NativeJsHookStatusStateToString(status_event.state));
        if (!ForwardNativeHookStatusEventToHost(state.context, status_event, error_message)) {
            return false;
        }
    }

    HookEvent event = {};
    while (TryDequeueNativeJsHookEvent(&event)) {
        HookInvocationMutationResult mutation_result = {};
        if (!InvokeNativeHookCallbackLocked(state, event, &mutation_result, error_message)) {
            return false;
        }
        (void)mutation_result;
    }

    return true;
}

bool JsRuntime::NotifyModuleLoaded(const char* module_path, std::string* error_message) {
    return NotifyModuleObserverModuleLoaded(module_path, error_message);
}

bool JsRuntime::InvokeNativeHookCallbackSync(const HookEvent& event,
                                             HookInvocationMutationResult* mutation_result,
                                             std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    return InvokeNativeHookCallbackLocked(state, event, mutation_result, error_message);
}

bool JsRuntime::RemoveMessageHandler(uint32_t script_id, std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::vector<uint32_t> deferred_java_hook_ids;
    std::unique_lock<std::recursive_mutex> lock(state.runtime_mutex);
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    if (state.context == nullptr) {
        std::string ignored_error;
        (void)UninstallReplaceHooksForScriptLocked(state, script_id, &ignored_error);
        deferred_java_hook_ids = TakeJavaHookIdsForScriptLocked(nullptr, state, script_id);
        FreeJavaRegisteredClassCallbacksLocked(nullptr, state, script_id);
        state.module_observers.erase(script_id);
        state.recv_callbacks.erase(script_id);
        state.rpc_exports.erase(script_id);
        state.native_hook_callbacks.erase(script_id);
        state.active_native_invocations.erase(script_id);
        state.replace_hook_records.erase(script_id);
        FreeNativeCallbacksLocked(nullptr, state, script_id);
        FreeWeakBindingsForScriptLocked(nullptr, state, script_id);
        FreeTimersForScriptLocked(nullptr, state, script_id);
        if (!FreeOwnedJavaHandlesLocked(state, script_id, error_message)) {
            return false;
        }
        FreeOwnedAllocationsLocked(state, script_id);
        state.script_pin_counts.erase(script_id);
        lock.unlock();
        for (uint32_t hook_id : deferred_java_hook_ids) {
            std::string ignored_unhook_error;
            (void)UninstallJavaJsHook(hook_id, &ignored_unhook_error);
        }
        return true;
    }
    auto pin_it = state.script_pin_counts.find(script_id);
    if (pin_it != state.script_pin_counts.end() && pin_it->second > 0u) {
        SetError(error_message, "script is pinned");
        return false;
    }
    if (!UninstallNativeHooksForScriptLocked(state, script_id, error_message)) {
        return false;
    }
    if (!DropJavaReadyCallbacksForScriptLocked(state, script_id, error_message)) {
        return false;
    }
    FreeModuleObserverCallbacksLocked(state.context, state, script_id);
    FreeRecvCallbackLocked(state.context, state, script_id);
    FreeRpcExportsLocked(state.context, state, script_id);
    FreeNativeHookCallbacksLocked(state.context, state, script_id);
    deferred_java_hook_ids = TakeJavaHookIdsForScriptLocked(state.context, state, script_id);
    FreeJavaRegisteredClassCallbacksLocked(state.context, state, script_id);
    FreeActiveNativeInvocationsLocked(state.context, state, script_id);
    FreeNativeCallbacksLocked(state.context, state, script_id);
    FireWeakBindingsForScriptLocked(state, script_id);
    FreeWeakBindingsForScriptLocked(state.context, state, script_id);
    FreeTimersForScriptLocked(state.context, state, script_id);
    if (!FreeOwnedJavaHandlesLocked(state, script_id, error_message)) {
        return false;
    }
    FreeOwnedAllocationsLocked(state, script_id);
    state.script_pin_counts.erase(script_id);
    lock.unlock();
    for (uint32_t hook_id : deferred_java_hook_ids) {
        std::string ignored_unhook_error;
        (void)UninstallJavaJsHook(hook_id, &ignored_unhook_error);
    }
    return true;
}

bool JsRuntime::ValidateScript(const std::string& source,
                               const std::string& filename,
                               std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    return EvaluateInternalLocked(state.context, source, filename, true, error_message);
}

bool JsRuntime::Evaluate(const std::string& source,
                         const std::string& filename,
                         uint32_t script_id,
                         std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    if (!ResetRpcExportsObjectLocked(state.context, error_message)) {
        return false;
    }
    const uint32_t previous_script_id = state.current_script_id;
    state.current_script_id = script_id;
    const bool ok = EvaluateInternalLocked(state.context, source, filename, false, error_message);
    state.current_script_id = previous_script_id;
    if (!ok) {
        return false;
    }
    if (!CaptureRpcExportsLocked(state.context, state, script_id, error_message)) {
        return false;
    }
    return ExecutePendingJobsLocked(state, error_message);
}

bool JsRuntime::DispatchJavaReadyCallbacks(std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    if (!DispatchJavaReadyCallbacksLocked(state, error_message)) {
        return false;
    }
    return DrainWeakBindingMaintenanceLocked(state, error_message);
}

bool JsRuntime::PumpPendingTasks(std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);
    return DrainWeakBindingMaintenanceLocked(state, error_message);
}

bool JsRuntime::CallRpc(uint32_t script_id,
                        const std::string& method,
                        const std::string& args_json,
                        std::string* result_json,
                        std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);

    auto script_it = state.rpc_exports.find(script_id);
    if (script_it == state.rpc_exports.end()) {
        SetError(error_message, "rpc exports not found");
        return false;
    }

    auto method_it = script_it->second.find(method);
    if (method_it == script_it->second.end()) {
        SetError(error_message, "rpc method not found");
        return false;
    }

    const std::string args_source = args_json.empty() ? "[]" : args_json;
    JSValue args_value = JS_ParseJSON(state.context,
                                      args_source.c_str(),
                                      args_source.size(),
                                      "<rpc-args>");
    if (JS_IsException(args_value)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    if (!JS_IsArray(state.context, args_value)) {
        JS_FreeValue(state.context, args_value);
        SetError(error_message, "rpc args must be a JSON array");
        return false;
    }

    uint32_t argc = 0;
    JSValue length_value = JS_GetPropertyStr(state.context, args_value, "length");
    if (JS_IsException(length_value) ||
        JS_ToUint32(state.context, &argc, length_value) < 0) {
        JS_FreeValue(state.context, length_value);
        JS_FreeValue(state.context, args_value);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }
    JS_FreeValue(state.context, length_value);
    std::vector<JSValue> argv;
    argv.reserve(argc);
    for (uint32_t index = 0; index < argc; ++index) {
        argv.push_back(JS_GetPropertyUint32(state.context, args_value, index));
    }

    JSValue global = JS_GetGlobalObject(state.context);
    JSValue function = JS_DupValue(state.context, method_it->second);
    ScopedCurrentScriptId script_scope(state, script_id);
    JSValue result = JS_Call(state.context,
                             function,
                             global,
                             static_cast<int>(argv.size()),
                             argv.empty() ? nullptr : argv.data());

    for (JSValue& value : argv) {
        JS_FreeValue(state.context, value);
    }
    JS_FreeValue(state.context, function);
    JS_FreeValue(state.context, global);
    JS_FreeValue(state.context, args_value);

    if (JS_IsException(result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    if (result_json != nullptr) {
        if (JS_IsUndefined(result)) {
            *result_json = "null";
        } else {
            JSValue json = JS_JSONStringify(state.context, result, JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(json)) {
                JS_FreeValue(state.context, result);
                SetError(error_message, GetExceptionString(state.context));
                return false;
            }

            if (JS_IsUndefined(json)) {
                *result_json = "null";
            } else {
                const char* json_cstr = JS_ToCString(state.context, json);
                if (json_cstr == nullptr) {
                    JS_FreeValue(state.context, json);
                    JS_FreeValue(state.context, result);
                    SetError(error_message, "rpc result stringify failed");
                    return false;
                }
                *result_json = json_cstr;
                JS_FreeCString(state.context, json_cstr);
                JS_FreeValue(state.context, json);
            }
        }
    }

    JS_FreeValue(state.context, result);
    return DrainWeakBindingMaintenanceLocked(state, error_message);
}

JSValue CreateJavaHookCallbackReceiver(JSContext* ctx,
                                       uint32_t hook_id,
                                       uint64_t receiver_handle,
                                       const JavaJsHookRecord& record) {
    JSValue receiver = CreateJavaUseWrapper(ctx, record.class_name.c_str(), receiver_handle);
    const char* callback_method_name =
        record.method_name == "<init>" ? "$init" : record.method_name.c_str();
    JSValue method = JS_GetPropertyStr(ctx, receiver, callback_method_name);
    JSValue hook_id_value = JS_NewUint32(ctx, hook_id);
    JSValue call_original =
        JS_NewCFunctionData(ctx, JsJavaCallOriginal, 0, 0, 1, &hook_id_value);
    if (JS_IsException(receiver) ||
        JS_IsException(method) ||
        JS_IsException(hook_id_value) ||
        JS_IsException(call_original)) {
        JS_FreeValue(ctx, receiver);
        JS_FreeValue(ctx, method);
        JS_FreeValue(ctx, hook_id_value);
        JS_FreeValue(ctx, call_original);
        return JS_EXCEPTION;
    }

    if (JS_SetPropertyStr(ctx, receiver,
                          "__nookActiveHookMethodName",
                          JS_NewString(ctx, callback_method_name)) < 0 ||
        JS_SetPropertyStr(ctx, receiver,
                          "__nookActiveHookSignature",
                          JS_NewString(ctx, record.signature.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, method, "$className", JS_NewString(ctx, record.class_name.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, method, "$methodName", JS_NewString(ctx, callback_method_name)) < 0 ||
        JS_SetPropertyStr(ctx, method, "$signature", JS_NewString(ctx, record.signature.c_str())) < 0 ||
        JS_SetPropertyStr(ctx, method, "$isStatic", JS_NewBool(ctx, record.is_static ? 1 : 0)) < 0 ||
        JS_SetPropertyStr(ctx, method, "__nookJavaHookId", JS_NewUint32(ctx, hook_id)) < 0 ||
        JS_SetPropertyStr(ctx, method, "callOriginal", call_original) < 0) {
        JS_FreeValue(ctx, receiver);
        JS_FreeValue(ctx, method);
        JS_FreeValue(ctx, hook_id_value);
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, hook_id_value);
    JS_FreeValue(ctx, method);
    return receiver;
}

bool InvokeJavaHookCallbackLocked(RuntimeState& state,
                                  uint32_t script_id,
                                  uint32_t hook_id,
                                  uint64_t receiver_handle,
                                  const JavaJsValue* args,
                                  size_t arg_count,
                                  JavaJsValue* result,
                                  std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java callback result output is required");
        return false;
    }
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }

    const auto script_it = state.java_hook_callbacks.find(script_id);
    if (script_it == state.java_hook_callbacks.end()) {
        SetError(error_message, "java hook script callbacks not found");
        return false;
    }
    const auto callback_it = script_it->second.find(hook_id);
    if (callback_it == script_it->second.end()) {
        SetError(error_message, "java hook callback not found");
        return false;
    }

    JavaJsHookRecord record = {};
    if (!GetJavaJsHookRecordForTesting(hook_id, &record)) {
        SetError(error_message, "java hook record not found");
        return false;
    }

    JS_RT_LOGI("InvokeJavaHookCallbackLocked: script_id=%u hook_id=%u class=%s method=%s sig=%s argc=%zu receiver=0x%llx",
               script_id,
               hook_id,
               record.class_name.c_str(),
               record.method_name.c_str(),
               record.signature.c_str(),
               arg_count,
               static_cast<unsigned long long>(receiver_handle));

    JSValue callback = JS_DupValue(state.context, callback_it->second);
    JSValue receiver = CreateJavaHookCallbackReceiver(
        state.context, hook_id, receiver_handle, record);
    if (JS_IsException(receiver)) {
        JS_FreeValue(state.context, callback);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    std::vector<JSValue> argv;
    argv.reserve(arg_count);
    for (size_t index = 0; index < arg_count; ++index) {
        JSValue value = MakeJavaJsValue(state.context, args[index]);
        if (JS_IsException(value)) {
            JS_FreeValue(state.context, receiver);
            JS_FreeValue(state.context, callback);
            SetError(error_message, GetExceptionString(state.context));
            return false;
        }
        argv.push_back(value);
    }

    ScopedCurrentScriptId current_script_scope(state, script_id);
    JSValue js_result =
        JS_Call(state.context,
                callback,
                receiver,
                static_cast<int>(argv.size()),
                argv.empty() ? nullptr : argv.data());

    for (JSValue& value : argv) {
        JS_FreeValue(state.context, value);
    }
    JS_FreeValue(state.context, receiver);
    JS_FreeValue(state.context, callback);

    if (JS_IsException(js_result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    const bool parsed = ParseJavaJsValue(state.context, js_result, result, error_message);
    JS_FreeValue(state.context, js_result);
    return parsed;
}

bool InvokeJavaRegisteredClassCallbackLocked(RuntimeState& state,
                                             uint32_t script_id,
                                             uint32_t callback_id,
                                             uint64_t receiver_handle,
                                             const std::string& receiver_class_name,
                                             const std::string& method_name,
                                             const std::string& method_signature,
                                             const JavaJsValue* args,
                                             size_t arg_count,
                                             JavaJsValue* result,
                                             std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java registerClass callback result output is required");
        return false;
    }
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }

    const auto script_it = state.java_registered_class_callbacks.find(script_id);
    if (script_it == state.java_registered_class_callbacks.end()) {
        SetError(error_message, "java registerClass script callbacks not found");
        return false;
    }
    const auto callback_it = script_it->second.find(callback_id);
    if (callback_it == script_it->second.end()) {
        SetError(error_message, "java registerClass callback id not found");
        return false;
    }
    const auto method_it = callback_it->second.find(method_name);
    if (method_it == callback_it->second.end()) {
        SetError(error_message, "java registerClass method callback not found");
        return false;
    }

    const auto signature_it =
        !method_signature.empty() ? method_it->second.find(method_signature) : method_it->second.end();
    const auto fallback_it = method_it->second.find("");
    if (signature_it == method_it->second.end() && fallback_it == method_it->second.end()) {
        SetError(error_message, "java registerClass method callback not found");
        return false;
    }

    JSValue callback =
        JS_DupValue(state.context,
                    signature_it != method_it->second.end() ? signature_it->second
                                                            : fallback_it->second);
    const char* wrapper_class_name =
        receiver_class_name.empty() ? "java.lang.Object" : receiver_class_name.c_str();
    JSValue receiver = CreateJavaUseWrapper(
        state.context, wrapper_class_name, receiver_handle, 0u);
    if (JS_IsException(receiver)) {
        JS_FreeValue(state.context, callback);
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    std::vector<JSValue> argv;
    argv.reserve(arg_count);
    for (size_t index = 0; index < arg_count; ++index) {
        JSValue value = MakeJavaJsValue(state.context, args[index]);
        if (JS_IsException(value)) {
            JS_FreeValue(state.context, receiver);
            JS_FreeValue(state.context, callback);
            SetError(error_message, GetExceptionString(state.context));
            return false;
        }
        argv.push_back(value);
    }

    ScopedCurrentScriptId current_script_scope(state, script_id);
    JSValue js_result =
        JS_Call(state.context,
                callback,
                receiver,
                static_cast<int>(argv.size()),
                argv.empty() ? nullptr : argv.data());

    for (JSValue& value : argv) {
        JS_FreeValue(state.context, value);
    }
    JS_FreeValue(state.context, receiver);
    JS_FreeValue(state.context, callback);

    if (JS_IsException(js_result)) {
        SetError(error_message, GetExceptionString(state.context));
        return false;
    }

    const bool parsed = ParseJavaJsValue(state.context, js_result, result, error_message);
    JS_FreeValue(state.context, js_result);
    return parsed;
}

bool DispatchJavaHookInvocationToRuntime(uint32_t hook_id,
                                         uint64_t receiver_handle,
                                         const JavaJsValue* args,
                                         size_t arg_count,
                                         JavaJsValue* result,
                                         std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);

    uint32_t script_id = 0u;
    for (const auto& script_entry : state.java_hook_callbacks) {
        if (script_entry.second.find(hook_id) == script_entry.second.end()) {
            continue;
        }
        script_id = script_entry.first;
        break;
    }

    if (script_id == 0u) {
        SetError(error_message, "java hook callback not found");
        return false;
    }

    return InvokeJavaHookCallbackLocked(
        state, script_id, hook_id, receiver_handle, args, arg_count, result, error_message);
}

bool DispatchJavaRegisteredClassInvocationToRuntime(uint32_t callback_id,
                                                    uint64_t receiver_handle,
                                                    const std::string& receiver_class_name,
                                                    const std::string& method_name,
                                                    const std::string& method_signature,
                                                    const JavaJsValue* args,
                                                    size_t arg_count,
                                                    JavaJsValue* result,
                                                    std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.context == nullptr) {
        SetError(error_message, "runtime not initialized");
        return false;
    }
    RefreshQuickJsStackTopForCurrentThread(state.runtime);

    uint32_t script_id = 0u;
    for (const auto& script_entry : state.java_registered_class_callbacks) {
        if (script_entry.second.find(callback_id) == script_entry.second.end()) {
            continue;
        }
        script_id = script_entry.first;
        break;
    }
    if (script_id == 0u) {
        SetError(error_message, "java registerClass callback not found");
        return false;
    }

    return InvokeJavaRegisteredClassCallbackLocked(state,
                                                   script_id,
                                                   callback_id,
                                                   receiver_handle,
                                                   receiver_class_name,
                                                   method_name,
                                                   method_signature,
                                                   args,
                                                   arg_count,
                                                   result,
                                                   error_message);
}

void JsRuntimeSetNativeHookInstallerDependenciesForTesting(
    const NativeJsHookInstallerDependencies& dependencies) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    state.native_hook_installer_dependencies = dependencies;
}

void JsRuntimeResetNativeHookInstallerDependenciesForTesting() {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    state.native_hook_installer_dependencies = {};
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
}

void JsRuntimeSetJavaHookInstallerDependenciesForTesting(
    const JavaJsHookInstallerDependencies& dependencies) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    state.java_hook_installer_dependencies = dependencies;
}

void JsRuntimeResetJavaHookInstallerDependenciesForTesting() {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    state.java_hook_installer_dependencies = {};
    ResetJavaJsHookRegistryForTesting();
}

void JsRuntimeSetReadJStringUtf8ForTesting(JsRuntimeReadJStringUtf8ForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.read_jstring_utf8 = callback;
}

void JsRuntimeResetReadJStringUtf8ForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.read_jstring_utf8 = nullptr;
}

void JsRuntimeSetGetJavaEnvPointerForTesting(JsRuntimeGetJavaEnvPointerForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.get_java_env_pointer = callback;
}

void JsRuntimeResetGetJavaEnvPointerForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.get_java_env_pointer = nullptr;
}

void JsRuntimeSetJavaEnvExceptionCheckForTesting(JsRuntimeJavaEnvExceptionCheckForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_check = callback;
}

void JsRuntimeResetJavaEnvExceptionCheckForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_check = nullptr;
}

void JsRuntimeSetJavaEnvExceptionOccurredForTesting(
    JsRuntimeJavaEnvExceptionOccurredForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_occurred = callback;
}

void JsRuntimeResetJavaEnvExceptionOccurredForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_occurred = nullptr;
}

void JsRuntimeSetJavaEnvExceptionClearForTesting(JsRuntimeJavaEnvExceptionClearForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_clear = callback;
}

void JsRuntimeResetJavaEnvExceptionClearForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_exception_clear = nullptr;
}

void JsRuntimeSetJavaEnvFindClassForTesting(JsRuntimeJavaEnvFindClassForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_find_class = callback;
}

void JsRuntimeResetJavaEnvFindClassForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_find_class = nullptr;
}

void JsRuntimeSetJavaEnvGetObjectClassForTesting(
    JsRuntimeJavaEnvGetObjectClassForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_object_class = callback;
}

void JsRuntimeResetJavaEnvGetObjectClassForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_object_class = nullptr;
}

void JsRuntimeSetJavaEnvIsSameObjectForTesting(
    JsRuntimeJavaEnvIsSameObjectForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_same_object = callback;
}

void JsRuntimeResetJavaEnvIsSameObjectForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_same_object = nullptr;
}

void JsRuntimeSetJavaEnvIsInstanceOfForTesting(
    JsRuntimeJavaEnvIsInstanceOfForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_instance_of = callback;
}

void JsRuntimeResetJavaEnvIsInstanceOfForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_instance_of = nullptr;
}

void JsRuntimeSetJavaEnvNewStringUtfForTesting(
    JsRuntimeJavaEnvNewStringUtfForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_string_utf = callback;
}

void JsRuntimeResetJavaEnvNewStringUtfForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_string_utf = nullptr;
}

void JsRuntimeSetJavaEnvGetStringUtfCharsForTesting(
    JsRuntimeJavaEnvGetStringUtfCharsForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_string_utf_chars = callback;
}

void JsRuntimeResetJavaEnvGetStringUtfCharsForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_string_utf_chars = nullptr;
}

void JsRuntimeSetJavaEnvReleaseStringUtfCharsForTesting(
    JsRuntimeJavaEnvReleaseStringUtfCharsForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_release_string_utf_chars = callback;
}

void JsRuntimeResetJavaEnvReleaseStringUtfCharsForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_release_string_utf_chars = nullptr;
}

void JsRuntimeSetJavaEnvNewGlobalRefForTesting(JsRuntimeJavaEnvNewGlobalRefForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_global_ref = callback;
}

void JsRuntimeResetJavaEnvNewGlobalRefForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_global_ref = nullptr;
}

void JsRuntimeSetJavaEnvDeleteGlobalRefForTesting(
    JsRuntimeJavaEnvDeleteGlobalRefForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_delete_global_ref = callback;
}

void JsRuntimeResetJavaEnvDeleteGlobalRefForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_delete_global_ref = nullptr;
}

void JsRuntimeSetJavaEnvNewWeakGlobalRefForTesting(
    JsRuntimeJavaEnvNewWeakGlobalRefForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_weak_global_ref = callback;
}

void JsRuntimeResetJavaEnvNewWeakGlobalRefForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_new_weak_global_ref = nullptr;
}

void JsRuntimeSetJavaEnvDeleteWeakGlobalRefForTesting(
    JsRuntimeJavaEnvDeleteWeakGlobalRefForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_delete_weak_global_ref = callback;
}

void JsRuntimeResetJavaEnvDeleteWeakGlobalRefForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_delete_weak_global_ref = nullptr;
}

void JsRuntimeSetJavaEnvGetObjectRefTypeForTesting(
    JsRuntimeJavaEnvGetObjectRefTypeForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_object_ref_type = callback;
}

void JsRuntimeResetJavaEnvGetObjectRefTypeForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_object_ref_type = nullptr;
}

void JsRuntimeSetJavaEnvGetSuperclassForTesting(
    JsRuntimeJavaEnvGetSuperclassForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_superclass = callback;
}

void JsRuntimeResetJavaEnvGetSuperclassForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_get_superclass = nullptr;
}

void JsRuntimeSetJavaEnvIsAssignableFromForTesting(
    JsRuntimeJavaEnvIsAssignableFromForTesting callback) {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_assignable_from = callback;
}

void JsRuntimeResetJavaEnvIsAssignableFromForTesting() {
    JniBridgeState& state = GetJniBridgeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.java_env_is_assignable_from = nullptr;
}

bool JsRuntimeHasNativeHookCallbacksForTesting(uint32_t script_id, uint32_t hook_id) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    const auto script_it = state.native_hook_callbacks.find(script_id);
    if (script_it == state.native_hook_callbacks.end()) {
        return false;
    }
    return script_it->second.find(hook_id) != script_it->second.end();
}

bool JsRuntimeHasJavaHookCallbackForTesting(uint32_t script_id, uint32_t hook_id) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    const auto script_it = state.java_hook_callbacks.find(script_id);
    if (script_it == state.java_hook_callbacks.end()) {
        return false;
    }
    return script_it->second.find(hook_id) != script_it->second.end();
}

bool JsRuntimeInvokeJavaHookCallbackForTesting(uint32_t script_id,
                                               uint32_t hook_id,
                                               const JavaJsValue* args,
                                               size_t arg_count,
                                               JavaJsValue* result,
                                               std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    JavaJsHookRecord record = {};
    if (!GetJavaJsHookRecordForTesting(hook_id, &record)) {
        SetError(error_message, "java hook record not found");
        return false;
    }
    const uint64_t receiver_handle = record.is_static ? 0u : static_cast<uint64_t>(hook_id);
    return InvokeJavaHookCallbackLocked(
        state, script_id, hook_id, receiver_handle, args, arg_count, result, error_message);
}

bool JsRuntimeInvokeJavaRegisteredClassCallbackForTesting(uint32_t script_id,
                                                          uint32_t callback_id,
                                                          uint64_t receiver_handle,
                                                          const char* receiver_class_name,
                                                          const char* method_name,
                                                          const char* method_signature,
                                                          const JavaJsValue* args,
                                                          size_t arg_count,
                                                          JavaJsValue* result,
                                                          std::string* error_message) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    return InvokeJavaRegisteredClassCallbackLocked(state,
                                                   script_id,
                                                   callback_id,
                                                   receiver_handle,
                                                   receiver_class_name != nullptr
                                                       ? receiver_class_name
                                                       : "java.lang.Object",
                                                   method_name != nullptr ? method_name : "",
                                                   method_signature != nullptr ? method_signature : "",
                                                   args,
                                                   arg_count,
                                                   result,
                                                   error_message);
}

size_t JsRuntimeGetNativeCallbackCountForTesting(uint32_t script_id) {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    const auto it = state.native_callback_records.find(script_id);
    if (it == state.native_callback_records.end()) {
        return 0u;
    }
    return it->second.size();
}

void JsRuntimeRunGcForTesting() {
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);
    if (state.runtime == nullptr) {
        return;
    }
    std::string ignored_error;
    for (int i = 0; i < 4; ++i) {
        JS_RunGC(state.runtime);
        (void)DrainWeakBindingMaintenanceLocked(state, &ignored_error);
    }
}

void JsRuntimeResetReadableMemoryProbeCountForTesting() {
    GetReadableMemoryProbeCount().store(0u, std::memory_order_relaxed);
}

uint64_t JsRuntimeGetReadableMemoryProbeCountForTesting() {
    return GetReadableMemoryProbeCount().load(std::memory_order_relaxed);
}

void JsRuntimeResetReadableMappingLookupCountForTesting() {
    GetReadableMappingLookupCount().store(0u, std::memory_order_relaxed);
    GetReadableMappingCacheEntry() = {};
}

uint64_t JsRuntimeGetReadableMappingLookupCountForTesting() {
    return GetReadableMappingLookupCount().load(std::memory_order_relaxed);
}

uint64_t JsRuntimeNormalizeProcessAddressForTesting(uint64_t value) {
    return static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(value)));
}

bool JsRuntimeGetModuleByAddressForTesting(const JsRuntimeTestModuleMapping* mappings,
                                           size_t mapping_count,
                                           uint64_t address,
                                           JsRuntimeTestModuleRecord* out_record) {
    if (out_record == nullptr) {
        return false;
    }

    out_record->name.clear();
    out_record->path.clear();
    out_record->base = 0u;
    out_record->size = 0u;

    if (mappings == nullptr || mapping_count == 0u) {
        return true;
    }

    std::vector<NativeModuleMapping> native_mappings;
    native_mappings.reserve(mapping_count);
    for (size_t index = 0; index < mapping_count; ++index) {
        if (mappings[index].path == nullptr ||
            mappings[index].path[0] == '\0' ||
            mappings[index].end <= mappings[index].start) {
            continue;
        }
        NativeModuleMapping mapping = {};
        mapping.path = mappings[index].path;
        mapping.start = mappings[index].start;
        mapping.end = mappings[index].end;
        native_mappings.push_back(std::move(mapping));
    }
    if (native_mappings.empty()) {
        return true;
    }

    std::sort(native_mappings.begin(),
              native_mappings.end(),
              [](const NativeModuleMapping& left, const NativeModuleMapping& right) {
                  if (left.path != right.path) {
                      return left.path < right.path;
                  }
                  if (left.start != right.start) {
                      return left.start < right.start;
                  }
                  return left.end < right.end;
              });

    std::vector<NativeModuleRecord> modules;
    NativeModuleMapping current = native_mappings[0];
    for (size_t index = 1; index < native_mappings.size(); ++index) {
        const NativeModuleMapping& mapping = native_mappings[index];
        if (mapping.path == current.path && mapping.start <= current.end) {
            if (mapping.end > current.end) {
                current.end = mapping.end;
            }
            continue;
        }

        NativeModuleRecord record = {};
        record.name = GetPathBaseName(current.path);
        record.base = current.start;
        record.size = current.end - current.start;
        record.path = current.path;
        modules.push_back(std::move(record));
        current = mapping;
    }

    NativeModuleRecord record = {};
    record.name = GetPathBaseName(current.path);
    record.base = current.start;
    record.size = current.end - current.start;
    record.path = current.path;
    modules.push_back(std::move(record));

    address = static_cast<uint64_t>(
        NormalizeProcessAddressForRangeCheck(static_cast<uintptr_t>(address)));
    for (const NativeModuleRecord& module : modules) {
        const uint64_t end = module.base + module.size;
        if (address >= module.base && address < end) {
            out_record->name = module.name;
            out_record->path = module.path;
            out_record->base = module.base;
            out_record->size = module.size;
            return true;
        }
    }

    return true;
}

}  // namespace agent_runtime
}  // namespace nook
