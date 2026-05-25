#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nook/Nook.h"

namespace nook {
namespace agent_runtime {

struct NativeJsArgumentSnapshotRequest {
    std::string type;
    uint32_t argument_index = 0;
    uint32_t env_index = 0;
};

struct NativeJsHookRequest {
    uint32_t hook_id = 0;
    std::string type;
    std::string module_name;
    std::string symbol_name;
    bool has_target_address = false;
    uint64_t target_address = 0;
    bool blocking = true;
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
};

struct NativeJsHookRecord {
    uint32_t hook_id = 0;
    std::string type;
    std::string module_name;
    std::string symbol_name;
    bool has_target_address = false;
    uint64_t target_address = 0;
    void* hook_handle = nullptr;
    bool deferred = false;
    bool blocking = true;
    std::vector<NativeJsArgumentSnapshotRequest> snapshots;
};

struct NativeJsPendingHookRecord {
    uint32_t hook_id = 0;
    std::string module_name;
    std::string symbol_name;
    std::size_t slot_index = 0;
    void* native_hook_handle = nullptr;
    bool installed = false;
};

struct NativeJsHookInstallerDependencies {
    bool (*install_inline_hook)(const NativeJsHookRequest& request,
                                void** hook_handle,
                                std::string* error_message) = nullptr;
};

using NativeJsHookEventNotifier = void (*)();

enum class NativeJsHookStatusState {
    kPending = 0,
    kInstalled = 1,
    kFailed = 2,
};

struct NativeJsHookStatusEvent {
    uint32_t hook_id = 0;
    NativeJsHookStatusState state = NativeJsHookStatusState::kPending;
    std::string module_name;
    std::string symbol_name;
    std::string error_message;
};

enum class HookEventPhase {
    kEnter = 0,
    kLeave = 1,
};

struct HookEvent {
    struct JniUtf8ArgumentSnapshot {
        uint32_t argument_index = 0;
        std::string property_name;
        std::string utf8;
    };

    uint32_t hook_id = 0;
    uint64_t invocation_id = 0;
    HookEventPhase phase = HookEventPhase::kEnter;
    uint32_t argument_count = 0;
    std::array<uint64_t, 8> argument_values = {};
    uint32_t thread_id = 0;
    uint64_t return_address = 0;
    uint64_t stack_pointer = 0;
    uint64_t frame_pointer = 0;
    uint64_t link_register = 0;
    uint64_t program_counter = 0;
    uint32_t jni_utf8_snapshot_count = 0;
    std::array<JniUtf8ArgumentSnapshot, 4> jni_utf8_snapshots = {};
    uint64_t return_value = 0;
};

struct HookInvocationMutationResult {
    std::array<bool, 8> argument_overrides = {};
    std::array<uint64_t, 8> argument_values = {};
    bool has_return_value_override = false;
    uint64_t return_value = 0;
};

bool InstallNativeJsHook(const NativeJsHookRequest& request,
                         const NativeJsHookInstallerDependencies& dependencies,
                         NativeJsHookRecord* out_record,
                         std::string* error_message);
bool UninstallNativeJsHook(uint32_t hook_id, std::string* error_message);
bool InstallNativeJsReplacementHook(uint64_t target_address,
                                    uint64_t replacement_address,
                                    uint64_t* original_address,
                                    void** hook_handle,
                                    std::string* error_message);
bool UninstallNativeJsReplacementHook(void* hook_handle, std::string* error_message);
bool FindNativeJsExportByName(const char* module_name,
                              const char* symbol_name,
                              uint64_t* target_address,
                              std::string* error_message);

bool EnqueueNativeJsHookEvent(const HookEvent& event, std::string* error_message);
bool TryDequeueNativeJsHookEvent(HookEvent* out_event);
bool TryDequeueNativeJsHookStatusEvent(NativeJsHookStatusEvent* out_event);
bool CompleteNativeJsHookInvocation(uint64_t invocation_id,
                                    HookEventPhase phase,
                                    const HookInvocationMutationResult& result,
                                    std::string* error_message);
void SetNativeJsHookEventNotifier(NativeJsHookEventNotifier notifier);
void ResetNativeJsHookEventNotifier();
size_t NotifyNativeJsHookModuleLoaded(const char* module_path, std::string* error_message);

std::size_t GetInstalledNativeJsHookCountForTesting();
bool GetNativeJsHookRecordForTesting(uint32_t hook_id, NativeJsHookRecord* out_record);
std::size_t GetPendingNativeJsHookCountForTesting();
bool GetPendingNativeJsHookRecordForTesting(uint32_t hook_id, NativeJsPendingHookRecord* out_record);
bool InvokeInstalledNativeJsHookForTesting(uint32_t hook_id,
                                           const std::array<uint64_t, 8>& arguments,
                                           uint64_t* return_value_out);
void PushNativeJsInlineHookIgnoreForTesting();
void PopNativeJsInlineHookIgnoreForTesting();
void RunWithInlineHookBridgeMutexHeldForTesting(const std::function<void()>& callback);
void ResetNativeJsHookRegistryForTesting();
void ResetNativeJsHookEventQueueForTesting();
void ResetNativeJsHookStatusEventQueueForTesting();

using NativeJsResolveLoadedSymbolAddressForTesting =
        bool (*)(const char* module_name, const char* symbol_name, void** symbol_address);
using NativeJsResolveSymbolAddressForTesting =
        bool (*)(const char* module_name, const char* symbol_name, void** symbol_address);
using NativeJsInlineHookSymbolSafetyCheckerForTesting =
        bool (*)(const char* module_name, const char* symbol_name, void* symbol_address);
using NativeJsInlineHookAddressInvokerForTesting =
        NookStatus (*)(void* target_address, void* replacement, void** original, void** hook_handle);
using NativeJsInlineHookUnhookInvokerForTesting = NookStatus (*)(void* hook_handle);
using NativeJsEnsureInlineHookModuleObserverAsyncForTesting = NookStatus (*)();
void SetNativeJsResolveLoadedSymbolAddressForTesting(
        NativeJsResolveLoadedSymbolAddressForTesting resolver);
void ResetNativeJsResolveLoadedSymbolAddressForTesting();
void SetNativeJsResolveSymbolAddressForTesting(
        NativeJsResolveSymbolAddressForTesting resolver);
void ResetNativeJsResolveSymbolAddressForTesting();
void SetNativeJsInlineHookSymbolSafetyCheckerForTesting(
        NativeJsInlineHookSymbolSafetyCheckerForTesting checker);
void ResetNativeJsInlineHookSymbolSafetyCheckerForTesting();
void SetNativeJsInlineHookAddressInvokerForTesting(
        NativeJsInlineHookAddressInvokerForTesting invoker);
void ResetNativeJsInlineHookAddressInvokerForTesting();
void SetNativeJsInlineHookUnhookInvokerForTesting(
        NativeJsInlineHookUnhookInvokerForTesting invoker);
void ResetNativeJsInlineHookUnhookInvokerForTesting();
void SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
        NativeJsEnsureInlineHookModuleObserverAsyncForTesting invoker);
void ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();

}  // namespace agent_runtime
}  // namespace nook
