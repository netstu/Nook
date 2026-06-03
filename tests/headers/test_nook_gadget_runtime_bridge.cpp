#include "gadget/nook_gadget_runtime.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_connect_init_call_count = 0;
std::string g_last_connect_host;
int g_last_connect_port = 0;
int g_bridge_init_call_count = 0;
NookStatus g_bridge_init_status = NOOK_STATUS_OK;
int g_runtime_ready_call_count = 0;
NookStatus g_runtime_ready_status = NOOK_STATUS_OK;
int g_startup_script_init_call_count = 0;
NookStatus g_startup_script_init_status = NOOK_STATUS_OK;
NookStatus g_connect_init_status = NOOK_STATUS_OK;

NookStatus FakeControlInitializer() {
    return NOOK_STATUS_OK;
}

NookStatus FakeConnectInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_connect_init_call_count;
    g_last_connect_host = config.interaction.host;
    g_last_connect_port = config.interaction.port;
    return g_connect_init_status;
}

NookStatus FakeBridgeInitializer() {
    ++g_bridge_init_call_count;
    return g_bridge_init_status;
}

NookStatus FakeRuntimeReadyNotifier() {
    ++g_runtime_ready_call_count;
    return g_runtime_ready_status;
}

NookStatus FakeStartupScriptInitializer() {
    ++g_startup_script_init_call_count;
    return g_startup_script_init_status;
}

void ResetState() {
    g_connect_init_call_count = 0;
    g_last_connect_host.clear();
    g_last_connect_port = 0;
    g_bridge_init_call_count = 0;
    g_bridge_init_status = NOOK_STATUS_OK;
    g_runtime_ready_call_count = 0;
    g_runtime_ready_status = NOOK_STATUS_OK;
    g_startup_script_init_call_count = 0;
    g_startup_script_init_status = NOOK_STATUS_OK;
    g_connect_init_status = NOOK_STATUS_OK;
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
    nook::gadget::SetControlInitializerForTesting(&FakeControlInitializer);
    nook::gadget::SetConnectInitializerForTesting(&FakeConnectInitializer);
    nook::gadget::SetBridgeInitializerForTesting(&FakeBridgeInitializer);
    nook::gadget::SetRuntimeReadyNotifierForTesting(&FakeRuntimeReadyNotifier);
    nook::gadget::SetStartupScriptInitializerForTesting(&FakeStartupScriptInitializer);

    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "gadget runtime init must succeed when bridge init succeeds");
    Require(g_bridge_init_call_count == 1,
            "gadget runtime init must invoke bridge initializer exactly once");
    Require(g_runtime_ready_call_count == 0,
            "listen-mode gadget runtime init must not invoke runtime-ready before attach");
    Require(g_startup_script_init_call_count == 1,
            "gadget runtime init must invoke startup script initializer exactly once");
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "repeated gadget runtime init must remain idempotent");
    Require(g_bridge_init_call_count == 1,
            "repeated gadget runtime init must not re-run bridge initializer");
    Require(g_runtime_ready_call_count == 0,
            "repeated listen-mode gadget runtime init must not re-run runtime-ready notifier");
    Require(g_startup_script_init_call_count == 1,
            "repeated gadget runtime init must not re-run startup script initializer");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting([](const char*, std::string* output) {
        if (output == nullptr) {
            return false;
        }
        *output =
            "{\"interaction\":{\"type\":\"connect\",\"transport\":\"default\","
            "\"host\":\"127.0.0.1\",\"port\":27042},"
            "\"startup_mode\":\"auto-start\"}";
        return true;
    });
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "connect-mode gadget runtime init must succeed when bridge init succeeds");
    Require(g_connect_init_call_count == 1,
            "connect-mode gadget runtime init must invoke the connect initializer exactly once");
    Require(g_last_connect_host == "127.0.0.1" && g_last_connect_port == 27042,
            "connect-mode gadget runtime init must forward the configured endpoint");
    Require(g_bridge_init_call_count == 1,
            "connect-mode gadget runtime init must still invoke bridge initializer exactly once");
    Require(g_runtime_ready_call_count == 1,
            "connect-mode gadget runtime init must still invoke runtime-ready notifier exactly once");
    Require(g_startup_script_init_call_count == 1,
            "connect-mode gadget runtime init must still invoke startup script initializer exactly once");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting([](const char*, std::string* output) {
        if (output == nullptr) {
            return false;
        }
        *output =
            "{\"interaction\":{\"type\":\"connect\",\"transport\":\"default\","
            "\"host\":\"127.0.0.1\",\"port\":27042},"
            "\"startup_mode\":\"auto-start\"}";
        return true;
    });
    ResetState();
    g_connect_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "connect-mode gadget runtime init must propagate connect failures");
    Require(g_connect_init_call_count == 1,
            "connect-mode gadget runtime init must attempt outbound connect once before failing");
    Require(g_bridge_init_call_count == 0,
            "connect failure must stop before bridge init");
    Require(g_runtime_ready_call_count == 0,
            "connect failure must stop before runtime-ready notifier");
    Require(g_startup_script_init_call_count == 0,
            "connect failure must stop before startup script initializer");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "connect failure must leave the runtime uninitialized");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::ResetAssetFileReaderForTesting();
    ResetState();
    g_bridge_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "gadget runtime init must propagate bridge initializer failure");
    Require(g_bridge_init_call_count == 1,
            "failing gadget runtime init must still attempt bridge init exactly once");
    Require(g_runtime_ready_call_count == 0,
            "runtime-ready notifier must not run when bridge init fails");
    Require(g_startup_script_init_call_count == 0,
            "startup script init must not run when bridge init fails");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "gadget runtime must remain uninitialized after bridge init failure");

    nook::gadget::ResetRuntimeForTesting();
    ResetState();
    g_startup_script_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "gadget runtime init must propagate startup script init failure");
    Require(g_bridge_init_call_count == 1,
            "bridge init must still run before startup script failure is returned");
    Require(g_runtime_ready_call_count == 0,
            "listen-mode startup script failure must not imply a pre-attach runtime-ready send");
    Require(g_startup_script_init_call_count == 1,
            "startup script init failure path must still attempt startup script init exactly once");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "gadget runtime must remain uninitialized after startup script init failure");

    nook::gadget::ResetControlInitializerForTesting();
    nook::gadget::ResetConnectInitializerForTesting();
    nook::gadget::ResetBridgeInitializerForTesting();
    nook::gadget::ResetRuntimeReadyNotifierForTesting();
    nook::gadget::ResetStartupScriptInitializerForTesting();
    nook::gadget::ResetAssetFileReaderForTesting();
    nook::gadget::ResetRuntimeForTesting();
    return 0;
}
