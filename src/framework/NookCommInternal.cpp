#include "NookCommInternal.h"

#include "nook/Nook.h"

#include <new>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace nook {
namespace framework {
namespace {

std::mutex& PendingScriptCallbackErrorMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string& PendingScriptCallbackError() {
    static std::string message;
    return message;
}

std::mutex& InternalRpcHandlersMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, RpcRequestHandler>& InternalRpcHandlers() {
    static std::unordered_map<std::string, RpcRequestHandler> handlers;
    return handlers;
}

RpcRequestHandler& DefaultInternalRpcHandler() {
    static RpcRequestHandler handler;
    return handler;
}

}  // namespace

void SetPendingScriptCallbackError(std::string message) {
    std::lock_guard<std::mutex> lock(PendingScriptCallbackErrorMutex());
    PendingScriptCallbackError() = std::move(message);
}

std::string TakePendingScriptCallbackError() {
    std::lock_guard<std::mutex> lock(PendingScriptCallbackErrorMutex());
    std::string message = std::move(PendingScriptCallbackError());
    PendingScriptCallbackError().clear();
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
    std::lock_guard<std::mutex> lock(InternalRpcHandlersMutex());
    if (method == "*") {
        DefaultInternalRpcHandler() = std::move(handler);
        return;
    }
    if (method.empty() || !handler) {
        InternalRpcHandlers().erase(method);
        return;
    }
    InternalRpcHandlers()[method] = std::move(handler);
}

void UnregisterInternalRpcRequestHandler(const std::string& method) {
    std::lock_guard<std::mutex> lock(InternalRpcHandlersMutex());
    if (method == "*") {
        DefaultInternalRpcHandler() = {};
        return;
    }
    InternalRpcHandlers().erase(method);
}

bool HasInternalRpcRequestHandlers() {
    std::lock_guard<std::mutex> lock(InternalRpcHandlersMutex());
    return static_cast<bool>(DefaultInternalRpcHandler()) || !InternalRpcHandlers().empty();
}

comm::RpcResponse DispatchInternalRpcRequest(const comm::RpcRequest& request) {
    RpcRequestHandler handler;
    {
        std::lock_guard<std::mutex> lock(InternalRpcHandlersMutex());
        auto it = InternalRpcHandlers().find(request.method);
        if (it != InternalRpcHandlers().end()) {
            handler = it->second;
        } else if (DefaultInternalRpcHandler()) {
            handler = DefaultInternalRpcHandler();
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
    new (&PendingScriptCallbackErrorMutex()) std::mutex();
    new (&InternalRpcHandlersMutex()) std::mutex();
}

void ResetInheritedInternalRpcStateForChild() {
    {
        std::lock_guard<std::mutex> lock(PendingScriptCallbackErrorMutex());
        PendingScriptCallbackError().clear();
    }
    {
        std::lock_guard<std::mutex> lock(InternalRpcHandlersMutex());
        DefaultInternalRpcHandler() = {};
        std::unordered_map<std::string, RpcRequestHandler> empty;
        InternalRpcHandlers().swap(empty);
    }
}

}  // namespace framework
}  // namespace nook
