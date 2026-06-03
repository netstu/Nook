#include "communication/protocol/messages.h"
#include "gadget/nook_gadget_runtime.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace nook {
namespace test_support {

extern std::string g_registered_rpc_method;
extern int g_register_rpc_call_count;
extern int g_unregister_rpc_call_count;
extern int g_refresh_rpc_call_count;
void ResetGadgetRuntimeTestStubs();
bool HasRegisteredRpcHandler();

}  // namespace test_support
}  // namespace nook

namespace {

int g_control_call_count = 0;
int g_connect_call_count = 0;
int g_bridge_call_count = 0;
int g_runtime_ready_call_count = 0;
int g_asset_read_call_count = 0;
int g_loader_call_count = 0;
std::string g_last_asset_path;
std::string g_last_script_name;
std::string g_last_script_source;
std::string g_last_connect_host;
int g_last_connect_port = 0;

NookStatus FakeControlInitializer() {
    ++g_control_call_count;
    return NOOK_STATUS_OK;
}

NookStatus FakeConnectInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_connect_call_count;
    g_last_connect_host = config.interaction.host;
    g_last_connect_port = config.interaction.port;
    return NOOK_STATUS_OK;
}

NookStatus FakeBridgeInitializer() {
    ++g_bridge_call_count;
    return NOOK_STATUS_OK;
}

NookStatus FakeRuntimeReadyNotifier() {
    ++g_runtime_ready_call_count;
    return NOOK_STATUS_OK;
}

bool FakeAssetReader(const char* asset_path, std::string* contents) {
    ++g_asset_read_call_count;
    g_last_asset_path = asset_path != nullptr ? asset_path : "";
    if (contents == nullptr) {
        return false;
    }
    if (g_last_asset_path == "assets/nook-gadget/config.json") {
        *contents =
            "{\"startup_mode\":\"manual\",\"startup_script\":{\"mode\":\"asset\","
            "\"path\":\"assets/nook-gadget/startup.js\",\"required\":true}}";
        return true;
    }
    if (g_last_asset_path == "assets/nook-gadget/startup.js") {
        *contents = "send('manual-rpc-start');\n";
        return true;
    }
    return false;
}

bool FakeConnectModeAssetReader(const char* asset_path, std::string* contents) {
    ++g_asset_read_call_count;
    g_last_asset_path = asset_path != nullptr ? asset_path : "";
    if (contents == nullptr) {
        return false;
    }
    if (g_last_asset_path == "assets/nook-gadget/config.json") {
        *contents =
            "{\"interaction\":{\"type\":\"connect\",\"transport\":\"default\","
            "\"host\":\"127.0.0.1\",\"port\":27042},"
            "\"startup_mode\":\"manual\",\"startup_script\":{\"mode\":\"asset\","
            "\"path\":\"assets/nook-gadget/startup.js\",\"required\":true}}";
        return true;
    }
    if (g_last_asset_path == "assets/nook-gadget/startup.js") {
        *contents = "send('manual-rpc-start');\n";
        return true;
    }
    return false;
}

NookStatus FakeStartupScriptLoader(const char* script_name,
                                   const char* script_source,
                                   uint32_t* script_id) {
    ++g_loader_call_count;
    g_last_script_name = script_name != nullptr ? script_name : "";
    g_last_script_source = script_source != nullptr ? script_source : "";
    if (script_id != nullptr) {
        *script_id = 17;
    }
    return NOOK_STATUS_OK;
}

void ResetState() {
    g_control_call_count = 0;
    g_connect_call_count = 0;
    g_bridge_call_count = 0;
    g_runtime_ready_call_count = 0;
    g_asset_read_call_count = 0;
    g_loader_call_count = 0;
    g_last_asset_path.clear();
    g_last_script_name.clear();
    g_last_script_source.clear();
    g_last_connect_host.clear();
    g_last_connect_port = 0;
    nook::test_support::ResetGadgetRuntimeTestStubs();
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeAssetReader);
    nook::gadget::SetStartupScriptLoaderForTesting(&FakeStartupScriptLoader);
    nook::gadget::SetControlInitializerForTesting(&FakeControlInitializer);
    nook::gadget::SetBridgeInitializerForTesting(&FakeBridgeInitializer);
    nook::gadget::SetRuntimeReadyNotifierForTesting(&FakeRuntimeReadyNotifier);
    nook::gadget::SetConnectInitializerForTesting(&FakeConnectInitializer);

    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "manual startup listen mode runtime init must succeed");
    Require(g_control_call_count == 0,
            "listen mode runtime init must bypass eager listen control setup");
    Require(g_loader_call_count == 0,
            "manual startup listen mode must defer startup script load");
    Require(nook::test_support::HasRegisteredRpcHandler(),
            "runtime init must register the startup RPC handler");
    Require(nook::test_support::g_registered_rpc_method ==
                "nook.gadget.load-configured-startup",
            "runtime init must register the canonical startup RPC method");
    Require(nook::test_support::g_register_rpc_call_count == 1,
            "runtime init must register the RPC handler exactly once");
    Require(nook::test_support::g_refresh_rpc_call_count == 1,
            "runtime init must refresh agent callbacks after registering the RPC handler");

    nook::comm::RpcRequest request;
    request.method = "nook.gadget.load-configured-startup";
    request.args_json = "[]";
    nook::comm::RpcResponse response;
    Require(nook::gadget::TryHandleConfiguredStartupRpc(request, &response),
            "startup RPC must be recognized");
    Require(response.success,
            "startup RPC must load the configured startup script");
    Require(g_loader_call_count == 1,
            "startup RPC must invoke the script loader once");
    Require(g_last_script_name == "startup.js",
            "startup RPC must use the canonical startup script name");
    Require(g_last_script_source == "send('manual-rpc-start');\n",
            "startup RPC must load the packaged startup source");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeConnectModeAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "manual startup connect mode runtime init must succeed");
    Require(g_control_call_count == 0,
            "connect mode runtime init must bypass listen control");
    Require(g_connect_call_count == 1,
            "connect mode runtime init must initialize outbound control once");
    Require(g_last_connect_host == "127.0.0.1" && g_last_connect_port == 27042,
            "connect mode runtime init must forward the configured endpoint");
    Require(g_loader_call_count == 0,
            "manual startup connect mode must defer startup script load");
    response = nook::comm::RpcResponse{};
    Require(nook::gadget::TryHandleConfiguredStartupRpc(request, &response),
            "connect mode startup RPC must be recognized");
    Require(response.success,
            "connect mode startup RPC must load the configured startup script");
    Require(g_loader_call_count == 1,
            "connect mode startup RPC must invoke the script loader once");

    nook::gadget::ResetAssetFileReaderForTesting();
    nook::gadget::ResetStartupScriptLoaderForTesting();
    nook::gadget::ResetControlInitializerForTesting();
    nook::gadget::ResetBridgeInitializerForTesting();
    nook::gadget::ResetRuntimeReadyNotifierForTesting();
    nook::gadget::ResetConnectInitializerForTesting();
    nook::gadget::ResetRuntimeForTesting();
    return 0;
}
