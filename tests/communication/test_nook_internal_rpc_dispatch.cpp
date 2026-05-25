#include <cassert>
#include <string>

#include "framework/NookCommInternal.h"

using namespace nook::framework;
using namespace nook::comm;

namespace {

void TestNamedInternalRpcHandlerDispatches() {
    UnregisterInternalRpcRequestHandler("nook.spawn.status");
    RpcRequest request;
    request.script_id = 7;
    request.method = "nook.spawn.status";
    request.args_json = "[]";

    RegisterInternalRpcRequestHandler("nook.spawn.status", [](const RpcRequest& inner) {
        RpcResponse response;
        response.script_id = inner.script_id;
        response.success = true;
        response.result_json = "{\"ok\":true}";
        return response;
    });

    const RpcResponse response = DispatchInternalRpcRequest(request);
    assert(response.script_id == 7u);
    assert(response.success);
    assert(response.result_json == "{\"ok\":true}");

    UnregisterInternalRpcRequestHandler("nook.spawn.status");
}

void TestWildcardInternalRpcHandlerDispatches() {
    UnregisterInternalRpcRequestHandler("*");
    RpcRequest request;
    request.script_id = 9;
    request.method = "nook.spawn.installForkHook";
    request.args_json = "{\"package\":\"com.demo.target\"}";

    RegisterInternalRpcRequestHandler("*", [](const RpcRequest& inner) {
        RpcResponse response;
        response.script_id = inner.script_id;
        response.success = true;
        response.result_json = inner.method;
        return response;
    });

    const RpcResponse response = DispatchInternalRpcRequest(request);
    assert(response.script_id == 9u);
    assert(response.success);
    assert(response.result_json == "nook.spawn.installForkHook");

    UnregisterInternalRpcRequestHandler("*");
}

void TestInternalRpcHandlerNotFoundReturnsError() {
    UnregisterInternalRpcRequestHandler("missing");
    UnregisterInternalRpcRequestHandler("*");

    RpcRequest request;
    request.script_id = 3;
    request.method = "missing";

    const RpcResponse response = DispatchInternalRpcRequest(request);
    assert(response.script_id == 3u);
    assert(!response.success);
    assert(response.error.code != 0);
    assert(response.error.message == "internal rpc handler not found");
}

}  // namespace

int main() {
    TestNamedInternalRpcHandlerDispatches();
    TestWildcardInternalRpcHandlerDispatches();
    TestInternalRpcHandlerNotFoundReturnsError();
    return 0;
}
