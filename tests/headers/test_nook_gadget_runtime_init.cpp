#include "gadget/nook_gadget_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace nook {
namespace test_support {

extern int g_register_rpc_call_count;
extern int g_unregister_rpc_call_count;
extern int g_refresh_rpc_call_count;
void ResetGadgetRuntimeTestStubs();
bool HasRegisteredRpcHandler();

}  // namespace test_support
}  // namespace nook

namespace {

int g_control_init_call_count = 0;
int g_connect_init_call_count = 0;
int g_bridge_init_call_count = 0;
int g_listen_init_call_count = 0;
int g_runtime_warmup_call_count = 0;
int g_runtime_ready_call_count = 0;
int g_startup_script_init_call_count = 0;
int g_wait_for_resume_call_count = 0;
std::string g_call_order;
std::string g_last_connect_host;
int g_last_connect_port = 0;
NookStatus g_control_init_status = NOOK_STATUS_OK;
NookStatus g_connect_init_status = NOOK_STATUS_OK;
NookStatus g_bridge_init_status = NOOK_STATUS_OK;
NookStatus g_listen_init_status = NOOK_STATUS_OK;
NookStatus g_runtime_warmup_status = NOOK_STATUS_OK;
NookStatus g_runtime_ready_status = NOOK_STATUS_OK;
NookStatus g_startup_script_init_status = NOOK_STATUS_OK;
NookStatus g_wait_for_resume_status = NOOK_STATUS_OK;
std::atomic<bool> g_blocking_wait_entered{false};
std::atomic<bool> g_blocking_wait_release{false};

NookStatus FakeControlInitializer() {
    ++g_control_init_call_count;
    g_call_order += "control;";
    return g_control_init_status;
}

NookStatus FakeConnectInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_connect_init_call_count;
    g_call_order += "connect;";
    g_last_connect_host = config.interaction.host;
    g_last_connect_port = config.interaction.port;
    return g_connect_init_status;
}

NookStatus FakeBridgeInitializer() {
    ++g_bridge_init_call_count;
    g_call_order += "bridge;";
    return g_bridge_init_status;
}

NookStatus FakeListenInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_listen_init_call_count;
    g_call_order += "listen;";
    if (config.interaction.type.empty()) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return g_listen_init_status;
}

NookStatus FakeRuntimeReadyNotifier() {
    ++g_runtime_ready_call_count;
    g_call_order += "runtime-ready;";
    return g_runtime_ready_status;
}

NookStatus FakeRuntimeWarmupInitializer() {
    ++g_runtime_warmup_call_count;
    g_call_order += "warmup;";
    return g_runtime_warmup_status;
}

NookStatus FakeStartupScriptInitializer() {
    ++g_startup_script_init_call_count;
    g_call_order += "startup;";
    return g_startup_script_init_status;
}

NookStatus FakeWaitForResumeInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_wait_for_resume_call_count;
    g_call_order += "wait;";
    if (config.interaction.on_load.empty()) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    return g_wait_for_resume_status;
}

NookStatus BlockingWaitForResumeInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_wait_for_resume_call_count;
    g_call_order += "wait;";
    if (config.interaction.on_load.empty()) {
        return NOOK_STATUS_INTERNAL_ERROR;
    }
    g_blocking_wait_entered.store(true, std::memory_order_release);
    while (!g_blocking_wait_release.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return NOOK_STATUS_OK;
}

bool FakeListenConfigAssetReader(const char*, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    *output =
        "{\"interaction\":{\"type\":\"listen\",\"transport\":\"default\",\"port\":27042},"
        "\"startup_mode\":\"auto-start\"}";
    return true;
}

bool FakeListenWaitConfigAssetReader(const char*, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    *output =
        "{\"interaction\":{\"type\":\"listen\",\"transport\":\"default\",\"on_load\":\"wait\",\"port\":27042},"
        "\"startup_mode\":\"auto-start\"}";
    return true;
}

bool FakeConnectConfigAssetReader(const char*, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    *output =
        "{\"interaction\":{\"type\":\"connect\",\"transport\":\"default\","
        "\"host\":\"127.0.0.1\",\"port\":27042},"
        "\"startup_mode\":\"auto-start\"}";
    return true;
}

bool FakeInvalidConnectConfigAssetReader(const char*, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    *output =
        "{\"interaction\":{\"type\":\"connect\",\"transport\":\"default\",\"port\":27042},"
        "\"startup_mode\":\"auto-start\"}";
    return true;
}

bool FakeUnsupportedInteractionAssetReader(const char*, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    *output =
        "{\"interaction\":{\"type\":\"pipe\",\"transport\":\"default\",\"port\":27042},"
        "\"startup_mode\":\"auto-start\"}";
    return true;
}

void ResetState() {
    g_control_init_call_count = 0;
    g_connect_init_call_count = 0;
    g_bridge_init_call_count = 0;
    g_listen_init_call_count = 0;
    g_runtime_warmup_call_count = 0;
    g_runtime_ready_call_count = 0;
    g_startup_script_init_call_count = 0;
    g_wait_for_resume_call_count = 0;
    g_call_order.clear();
    g_last_connect_host.clear();
    g_last_connect_port = 0;
    g_control_init_status = NOOK_STATUS_OK;
    g_connect_init_status = NOOK_STATUS_OK;
    g_bridge_init_status = NOOK_STATUS_OK;
    g_listen_init_status = NOOK_STATUS_OK;
    g_runtime_warmup_status = NOOK_STATUS_OK;
    g_runtime_ready_status = NOOK_STATUS_OK;
    g_startup_script_init_status = NOOK_STATUS_OK;
    g_wait_for_resume_status = NOOK_STATUS_OK;
    g_blocking_wait_entered.store(false, std::memory_order_release);
    g_blocking_wait_release.store(false, std::memory_order_release);
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
    nook::gadget::SetControlInitializerForTesting(&FakeControlInitializer);
    nook::gadget::SetConnectInitializerForTesting(&FakeConnectInitializer);
    nook::gadget::SetBridgeInitializerForTesting(&FakeBridgeInitializer);
    nook::gadget::SetListenInitializerForTesting(&FakeListenInitializer);
    nook::gadget::SetRuntimeWarmupInitializerForTesting(&FakeRuntimeWarmupInitializer);
    nook::gadget::SetRuntimeReadyNotifierForTesting(&FakeRuntimeReadyNotifier);
    nook::gadget::SetStartupScriptInitializerForTesting(&FakeStartupScriptInitializer);
    nook::gadget::SetOnLoadWaiterForTesting(&FakeWaitForResumeInitializer);

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::ResetAssetFileReaderForTesting();
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "default runtime init must succeed");
    Require(nook::gadget::IsRuntimeInitialized(),
            "default runtime init must mark the runtime initialized");
    Require(g_control_init_call_count == 0,
            "default runtime init must not eagerly create a passive listen control channel");
    Require(g_listen_init_call_count == 0,
            "default runtime init must not start a direct listen surface without a configured port");
    Require(g_runtime_ready_call_count == 0,
            "default runtime init must not send runtime-ready before a listen attach exists");
    Require(g_wait_for_resume_call_count == 0,
            "default runtime init must not wait for resume without on_load=wait");
    Require(g_runtime_warmup_call_count == 0,
            "default runtime init must not eagerly warm the runtime without on_load=wait");
    Require(g_call_order == "bridge;startup;",
            "default runtime init must preserve passive listen startup order");

    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "second runtime init must be idempotent");
    Require(g_control_init_call_count == 0,
            "second runtime init must not spuriously initialize a passive listen control channel");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeConnectConfigAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "connect runtime init must succeed");
    Require(g_control_init_call_count == 0,
            "connect runtime init must bypass listen control");
    Require(g_listen_init_call_count == 0,
            "connect runtime init must bypass the direct listen initializer");
    Require(g_connect_init_call_count == 1,
            "connect runtime init must call the connect initializer once");
    Require(g_last_connect_host == "127.0.0.1" && g_last_connect_port == 27042,
            "connect runtime init must forward the configured endpoint");
    Require(g_call_order == "connect;bridge;runtime-ready;startup;",
            "connect runtime init must preserve connect-first bring-up order");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeConnectConfigAssetReader);
    ResetState();
    g_connect_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "connect initializer failure must propagate");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "connect initializer failure must keep runtime uninitialized");
    Require(g_bridge_init_call_count == 0,
            "connect initializer failure must stop before bridge init");
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "connect initializer failure must retry instead of becoming terminal");
    Require(g_connect_init_call_count == 2,
            "connect initializer failure retry must reattempt outbound control");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeInvalidConnectConfigAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INVALID_ARGUMENT,
            "invalid connect config must fail early");
    Require(g_connect_init_call_count == 0,
            "invalid connect config must fail before connect init");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenConfigAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "listen runtime init must succeed without an eager control channel");
    Require(nook::gadget::IsRuntimeInitialized(),
            "listen runtime init must still mark the runtime initialized");
    Require(nook::test_support::HasRegisteredRpcHandler(),
            "listen runtime init must keep the RPC handler installed");
    Require(nook::test_support::g_register_rpc_call_count == 1,
            "listen runtime init must register the RPC handler once");
    Require(nook::test_support::g_unregister_rpc_call_count == 0,
            "listen runtime init must not roll back the RPC handler");
    Require(g_listen_init_call_count == 1,
            "listen runtime init with a configured port must call the direct listen initializer once");
    Require(g_runtime_ready_call_count == 0,
            "listen runtime init must defer runtime-ready until attach adopts the transport");
    Require(g_wait_for_resume_call_count == 0,
            "listen runtime init must not wait for resume unless on_load=wait");
    Require(g_runtime_warmup_call_count == 0,
            "listen runtime init must not eagerly warm the runtime unless on_load=wait");
    Require(g_call_order == "bridge;listen;startup;",
            "listen runtime init must preserve passive listen startup order");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenWaitConfigAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
            "listen wait runtime init must succeed");
    Require(g_listen_init_call_count == 1,
            "listen wait runtime init must start the direct listen initializer");
    Require(g_runtime_warmup_call_count == 1,
            "listen wait runtime init must warm the runtime before blocking");
    Require(g_wait_for_resume_call_count == 1,
            "listen wait runtime init must wait for host resume");
    Require(g_call_order == "bridge;listen;warmup;startup;wait;",
            "listen wait runtime init must wait only after startup initialization");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenWaitConfigAssetReader);
    nook::gadget::SetOnLoadWaiterForTesting(&BlockingWaitForResumeInitializer);
    ResetState();
    std::thread init_thread([]() {
        Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_OK,
                "blocking listen wait runtime init must eventually succeed");
    });
    for (int i = 0; i < 100; ++i) {
        if (g_blocking_wait_entered.load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(g_blocking_wait_entered.load(std::memory_order_acquire),
            "blocking listen wait runtime init must enter the wait gate");
    Require(g_runtime_warmup_call_count == 1,
            "blocking listen wait runtime init must warm the runtime before the wait gate");
    auto initialized_future =
        std::async(std::launch::async, []() { return nook::gadget::IsRuntimeInitialized(); });
    Require(initialized_future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready,
            "listen wait gate must not hold the runtime mutex while waiting for host resume");
    Require(!initialized_future.get(),
            "listen wait gate must report runtime not initialized before host resume");
    g_blocking_wait_release.store(true, std::memory_order_release);
    init_thread.join();
    Require(nook::gadget::IsRuntimeInitialized(),
            "blocking listen wait runtime init must mark the runtime initialized after resume");
    nook::gadget::SetOnLoadWaiterForTesting(&FakeWaitForResumeInitializer);

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenWaitConfigAssetReader);
    ResetState();
    g_wait_for_resume_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "listen wait resume-gate failure must propagate");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "listen wait resume-gate failure must keep runtime uninitialized");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenWaitConfigAssetReader);
    ResetState();
    g_runtime_warmup_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "listen wait runtime warmup failure must propagate");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "listen wait runtime warmup failure must keep runtime uninitialized");
    Require(g_wait_for_resume_call_count == 0,
            "listen wait runtime warmup failure must stop before the host wait gate");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenConfigAssetReader);
    ResetState();
    g_startup_script_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "startup script initializer failure must propagate");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "startup script initializer failure must keep runtime uninitialized");
    Require(nook::test_support::g_unregister_rpc_call_count == 1,
            "startup script initializer failure must roll back the RPC handler");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeListenConfigAssetReader);
    ResetState();
    g_listen_init_status = NOOK_STATUS_INTERNAL_ERROR;
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INTERNAL_ERROR,
            "listen initializer failure must propagate");
    Require(!nook::gadget::IsRuntimeInitialized(),
            "listen initializer failure must keep runtime uninitialized");
    Require(g_bridge_init_call_count == 1,
            "listen initializer failure must happen after bridge init");
    Require(g_runtime_ready_call_count == 0,
            "listen initializer failure must stop before runtime-ready notify");

    nook::gadget::ResetRuntimeForTesting();
    nook::gadget::SetAssetFileReaderForTesting(&FakeUnsupportedInteractionAssetReader);
    ResetState();
    Require(nook::gadget::InitializeRuntime() == NOOK_STATUS_INVALID_ARGUMENT,
            "unsupported interaction type must fail cleanly");
    Require(g_control_init_call_count == 0,
            "unsupported interaction type must fail before control init");
    Require(g_connect_init_call_count == 0,
            "unsupported interaction type must fail before connect init");

    nook::gadget::ResetAssetFileReaderForTesting();
    nook::gadget::ResetControlInitializerForTesting();
    nook::gadget::ResetConnectInitializerForTesting();
    nook::gadget::ResetBridgeInitializerForTesting();
    nook::gadget::ResetListenInitializerForTesting();
    nook::gadget::ResetRuntimeWarmupInitializerForTesting();
    nook::gadget::ResetRuntimeReadyNotifierForTesting();
    nook::gadget::ResetStartupScriptInitializerForTesting();
    nook::gadget::ResetOnLoadWaiterForTesting();
    nook::gadget::ResetRuntimeForTesting();
    return 0;
}
