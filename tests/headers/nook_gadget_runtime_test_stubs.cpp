#include "agent_runtime/script_registry.h"
#include "framework/NookCommInternal.h"
#include "gadget/nook_gadget_config.h"

#include <atomic>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace nook {
namespace test_support {

framework::RpcRequestHandler g_registered_rpc_handler;
std::string g_registered_rpc_method;
int g_register_rpc_call_count = 0;
int g_unregister_rpc_call_count = 0;
int g_refresh_rpc_call_count = 0;
int g_ensure_control_channel_call_count = 0;
int g_bridge_initialize_call_count = 0;
int g_notify_runtime_ready_call_count = 0;
framework::ExternalResumeHandler g_external_resume_handler;

void ResetGadgetRuntimeTestStubs() {
    g_registered_rpc_handler = framework::RpcRequestHandler();
    g_registered_rpc_method.clear();
    g_register_rpc_call_count = 0;
    g_unregister_rpc_call_count = 0;
    g_refresh_rpc_call_count = 0;
    g_ensure_control_channel_call_count = 0;
    g_bridge_initialize_call_count = 0;
    g_notify_runtime_ready_call_count = 0;
}

bool HasRegisteredRpcHandler() {
    return static_cast<bool>(g_registered_rpc_handler);
}

}  // namespace test_support
}  // namespace nook

namespace nook {
namespace framework {

void SetPendingScriptCallbackError(std::string) {}

std::string TakePendingScriptCallbackError() {
    return {};
}

std::string MakeScriptCallbackErrorMessage(const char* fallback_message) {
    return fallback_message != nullptr ? fallback_message : "";
}

void RegisterInternalRpcRequestHandler(const std::string& method, RpcRequestHandler handler) {
    test_support::g_registered_rpc_method = method;
    test_support::g_registered_rpc_handler = std::move(handler);
    ++test_support::g_register_rpc_call_count;
}

void UnregisterInternalRpcRequestHandler(const std::string& method) {
    if (test_support::g_registered_rpc_method == method) {
        test_support::g_registered_rpc_method.clear();
        test_support::g_registered_rpc_handler = RpcRequestHandler();
    }
    ++test_support::g_unregister_rpc_call_count;
}

bool HasInternalRpcRequestHandlers() {
    return test_support::HasRegisteredRpcHandler();
}

comm::RpcResponse DispatchInternalRpcRequest(const comm::RpcRequest& request) {
    if (!test_support::g_registered_rpc_handler) {
        return {};
    }
    return test_support::g_registered_rpc_handler(request);
}

void SetInternalRpcRequestHandler(RpcRequestHandler handler) {
    test_support::g_registered_rpc_handler = std::move(handler);
}

void RefreshAgentCallbacksForInternalRpc() {
    ++test_support::g_refresh_rpc_call_count;
}

void SetExternalResumeHandler(ExternalResumeHandler handler) {
    test_support::g_external_resume_handler = std::move(handler);
}

void ResetExternalResumeHandler() {
    test_support::g_external_resume_handler = ExternalResumeHandler();
}

NookStatus EnsureControlChannelReadyForCurrentProcess() {
    ++test_support::g_ensure_control_channel_call_count;
    return NOOK_STATUS_OK;
}

bool HasActiveControlChannelConnection() {
    return false;
}

NookStatus EnsureOutboundControlChannelReadyForCurrentProcess(const char*, int) {
    ++test_support::g_ensure_control_channel_call_count;
    return NOOK_STATUS_OK;
}

NookStatus AdoptInboundControlChannelTransportForCurrentProcess(
    std::unique_ptr<nook::comm::Transport>) {
    ++test_support::g_ensure_control_channel_call_count;
    return NOOK_STATUS_OK;
}

NookStatus NotifyRuntimeReadyToServer() {
    ++test_support::g_notify_runtime_ready_call_count;
    return NOOK_STATUS_OK;
}

NookStatus EnsureFullAgentReadyForCurrentProcess() {
    return NOOK_STATUS_OK;
}

NookStatus NotifyZygoteControlReadyToServer() {
    return NOOK_STATUS_OK;
}

NookStatus ResetZygoteControlConnectionStateForReinit() {
    return NOOK_STATUS_OK;
}

void RequestControlChannelDisconnectAfterCurrentReply(const char*) {}

void ScheduleControlChannelHardCloseAfterDelay(const char*, uint32_t) {}

void PrepareInheritedChildAgentActivation(const std::string&,
                                          const std::string&,
                                          bool) {}

void ResetInheritedInternalSynchronizationForChild() {}

void ResetInheritedInternalRpcStateForChild() {}

void ResetInheritedForkChildConnectionState() {}

void ResetInheritedConnectionStateForChild(const std::string&,
                                           const std::string&,
                                           bool) {}

bool SuspendAgentConnectionForFork() {
    return true;
}

bool ResumeAgentConnectionAfterFork() {
    return true;
}

bool IsCurrentProcessSpawnGateHeld() {
    return false;
}

bool IsCurrentProcessStrictLifecycleSpawnGateHeld() {
    return false;
}

}  // namespace framework
}  // namespace nook

namespace nook {
namespace gadget {

NookStatus EnsureDirectAttachListenerForCurrentProcess(const GadgetConfig&) {
    return NOOK_STATUS_OK;
}

}  // namespace gadget
}  // namespace nook

namespace nook {
namespace agent_runtime {

namespace {

std::atomic<uint32_t>& StubGlobalNextScriptId() {
    static std::atomic<uint32_t> next_script_id{1u};
    return next_script_id;
}

}  // namespace

NookStatus NookScriptRuntimeBridgeInitialize() {
    ++test_support::g_bridge_initialize_call_count;
    return NOOK_STATUS_OK;
}

ScriptRegistry::ScriptRegistry() {
    scripts_.reserve(64);
}

bool ScriptRegistry::CreateScript(const std::string& name,
                                  const std::string& source,
                                  uint32_t* script_id,
                                  std::string* error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ScriptRecord record;
    record.id = StubGlobalNextScriptId().fetch_add(1u, std::memory_order_relaxed);
    record.name = name;
    record.source = source;
    scripts_.push_back(std::move(record));
    if (script_id != nullptr) {
        *script_id = scripts_.back().id;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool ScriptRegistry::LoadScript(uint32_t script_id, std::string* error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(scripts_.begin(),
                           scripts_.end(),
                           [script_id](const ScriptRecord& record) {
                               return record.id == script_id;
                           });
    if (it == scripts_.end()) {
        if (error_message != nullptr) {
            *error_message = "script not found";
        }
        return false;
    }
    it->loaded = true;
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool ScriptRegistry::UnloadScript(uint32_t script_id, std::string* error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(scripts_.begin(),
                           scripts_.end(),
                           [script_id](const ScriptRecord& record) {
                               return record.id == script_id;
                           });
    if (it == scripts_.end()) {
        if (error_message != nullptr) {
            *error_message = "script not found";
        }
        return false;
    }
    it->loaded = false;
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

void ScriptRegistry::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    scripts_.clear();
}

}  // namespace agent_runtime
}  // namespace nook
