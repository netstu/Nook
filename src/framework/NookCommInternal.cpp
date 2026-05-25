#include "NookCommInternal.h"

#include "nook/Nook.h"

#include <new>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace nook {
namespace framework {
namespace {

std::mutex g_pending_script_callback_error_mutex;
std::string g_pending_script_callback_error;

std::mutex g_internal_rpc_handlers_mutex;
std::unordered_map<std::string, RpcRequestHandler> g_internal_rpc_handlers;
RpcRequestHandler g_default_internal_rpc_handler;

}  // namespace

void SetPendingScriptCallbackError(std::string message) {
    std::lock_guard<std::mutex> lock(g_pending_script_callback_error_mutex);
    g_pending_script_callback_error = std::move(message);
}

std::string TakePendingScriptCallbackError() {
    std::lock_guard<std::mutex> lock(g_pending_script_callback_error_mutex);
    std::string message = std::move(g_pending_script_callback_error);
    g_pending_script_callback_error.clear();
    return message;
}

std::string MakeScriptCallbackErrorMessage(const char* fallback_message) {
    std::string message = TakePendingScriptCallbackError();
    if (!message.empty()) {
        return message;
    }
    return fallback_message != nullptr ? fallback_message : "script callback failed";
}

void RegisterInternalRpcRequestHandler(const std::string& method, RpcRequestHandler handler) {
    std::lock_guard<std::mutex> lock(g_internal_rpc_handlers_mutex);
    if (method == "*") {
        g_default_internal_rpc_handler = std::move(handler);
        return;
    }
    if (method.empty() || !handler) {
        g_internal_rpc_handlers.erase(method);
        return;
    }
    g_internal_rpc_handlers[method] = std::move(handler);
}

void UnregisterInternalRpcRequestHandler(const std::string& method) {
    std::lock_guard<std::mutex> lock(g_internal_rpc_handlers_mutex);
    if (method == "*") {
        g_default_internal_rpc_handler = {};
        return;
    }
    g_internal_rpc_handlers.erase(method);
}

bool HasInternalRpcRequestHandlers() {
    std::lock_guard<std::mutex> lock(g_internal_rpc_handlers_mutex);
    return static_cast<bool>(g_default_internal_rpc_handler) || !g_internal_rpc_handlers.empty();
}

comm::RpcResponse DispatchInternalRpcRequest(const comm::RpcRequest& request) {
    RpcRequestHandler handler;
    {
        std::lock_guard<std::mutex> lock(g_internal_rpc_handlers_mutex);
        auto it = g_internal_rpc_handlers.find(request.method);
        if (it != g_internal_rpc_handlers.end()) {
            handler = it->second;
        } else if (g_default_internal_rpc_handler) {
            handler = g_default_internal_rpc_handler;
        }
    }

    comm::RpcResponse response;
    response.script_id = request.script_id;
    if (!handler) {
        response.success = false;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "internal rpc handler not found";
        return response;
    }

    return handler(request);
}

void PrepareInheritedChildAgentActivation(const std::string& process_name,
                                          const std::string& spawn_token,
                                          bool arm_spawn_gate) {
    ResetInheritedConnectionStateForChild(process_name, spawn_token, arm_spawn_gate);
}

void ResetInheritedInternalSynchronizationForChild() {
    new (&g_pending_script_callback_error_mutex) std::mutex();
    new (&g_internal_rpc_handlers_mutex) std::mutex();
}

void ResetInheritedInternalRpcStateForChild() {
    {
        std::lock_guard<std::mutex> lock(g_pending_script_callback_error_mutex);
        g_pending_script_callback_error.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_internal_rpc_handlers_mutex);
        g_default_internal_rpc_handler = {};
        std::unordered_map<std::string, RpcRequestHandler> empty;
        g_internal_rpc_handlers.swap(empty);
    }
}

}  // namespace framework
}  // namespace nook
