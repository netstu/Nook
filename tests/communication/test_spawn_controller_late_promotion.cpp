#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/injector.h"
#include "server/session_registry.h"

using namespace nook::comm;
using namespace nook::server;

namespace {

class CaptureTransport final : public Transport {
public:
    CaptureTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }

    ssize_t Send(const uint8_t* data, size_t len) override {
        sent_.insert(sent_.end(), data, data + len);
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Capture"; }

private:
    std::vector<uint8_t> sent_;
};

class FakeInjector final : public Injector {
public:
    bool Spawn(const SpawnRequest&, const std::string&, int*, std::string*) override {
        return false;
    }

    bool InjectAgent(int, const std::string&, const std::string&, std::string*) override {
        return false;
    }

    bool InjectSpawnChildAgent(int pid,
                               const std::string& agent_path,
                               std::string* error_message) override {
        if (before_inject) {
            before_inject();
        }
        if (!allow_inject_return) {
            if (error_message != nullptr) {
                *error_message = "inject failed";
            }
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_inject_pid = pid;
            last_inject_agent_path = agent_path;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    bool FinalizeSpawn(const SpawnRequest&, std::string*) override {
        return false;
    }

    int GetLastInjectPid() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_inject_pid;
    }

    std::string GetLastInjectAgentPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_inject_agent_path;
    }

    void SetBeforeInject(std::function<void()> callback) {
        before_inject = std::move(callback);
    }

    void SetAllowInjectReturn(bool allow) {
        allow_inject_return = allow;
    }

private:
    mutable std::mutex mutex_;
    int last_inject_pid = 0;
    std::string last_inject_agent_path;
    std::function<void()> before_inject;
    bool allow_inject_return = true;
};

bool WaitForInjectedPid(FakeInjector* injector, int expected_pid, uint32_t timeout_ms = 1000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (injector->GetLastInjectPid() == expected_pid) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

#include "server/spawn_controller.cpp"

namespace {

void TestLatePromotionUsesSpawnOwnedHostWhenGlobalPidBindingDisappears() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    Session rebound_host(std::move(rebound_host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), 64001);
    registry.MarkSpawnSuspended(64001,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    registry.BindHostToPid(rebound_host.GetId(), 64001);
    registry.RemoveHostSession(rebound_host.GetId());

    SpawnSuspendedEntry before;
    assert(registry.GetSpawnSuspendedEntry(64001, &before));
    assert(before.suspended);
    assert(before.host_session_id == original_host.GetId());
    assert(before.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(registry.FindHostSessionByPid(64001) == nullptr);
    assert(registry.FindHostSession(original_host.GetId()) == &original_host);

    FakeInjector injector;
    ServerHandlerConfig config;
    config.agent_path = "__embedded_agent__";

    MaybePromoteLateBoundControlReadyChild(&registry,
                                           &injector,
                                           config,
                                           original_host.GetId(),
                                           64001);

    SpawnSuspendedEntry after_call;
    assert(registry.GetSpawnSuspendedEntry(64001, &after_call));
    assert(after_call.state == SpawnTransactionState::kWaitingRuntimeReady);

    assert(WaitForInjectedPid(&injector, 64001));
    assert(injector.GetLastInjectAgentPath() == "__embedded_agent__");

    SpawnSuspendedEntry after;
    assert(registry.GetSpawnSuspendedEntry(64001, &after));
    assert(after.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(after.host_session_id == original_host.GetId());
    assert(after.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

void TestLatePromotionCancelsIfRuntimeReadyAlreadyPresent() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    runtime_agent.SetPeerPid(64002);

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.BindHostToPid(original_host.GetId(), 64002);
    registry.MarkSpawnSuspended(64002,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    registry.RegisterAgentSession(64002, &runtime_agent);
    registry.RegisterAgentProcessName(64002, "com.demo.target");
    registry.MarkAgentReadyStage(64002, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(64002);
    registry.UpdateSpawnSuspendedAuthoritativeReady(64002,
                                                    PendingSpawnReadyStage::kRuntimeReady,
                                                    "com.demo.target");

    FakeInjector injector;
    ServerHandlerConfig config;
    config.agent_path = "__embedded_agent__";

    MaybePromoteLateBoundControlReadyChild(&registry,
                                           &injector,
                                           config,
                                           original_host.GetId(),
                                           64002);

    assert(!WaitForInjectedPid(&injector, 64002, 50));

    SpawnSuspendedEntry after;
    assert(registry.GetSpawnSuspendedEntry(64002, &after));
    assert(after.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestLatePromotionDoesNotWaitForSelfBootstrapGraceWhenControlOnly() {
    SessionRegistry registry;
    FakeInjector injector;

    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));
    registry.RegisterHostSession(&host);

    registry.MarkSpawnSuspended(64003,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(64003, SpawnTransactionState::kWaitingAgentReady);

    ServerHandlerConfig config;
    config.agent_path = "__embedded_agent__";

    const auto start = std::chrono::steady_clock::now();
    MaybePromoteLateBoundControlReadyChild(&registry,
                                           &injector,
                                           config,
                                           host.GetId(),
                                           64003);

    assert(WaitForInjectedPid(&injector, 64003, 500));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    assert(elapsed.count() < 650);
}

void TestLatePromotionDoesNotAdvanceRuntimeStateWhenInjectFails() {
    SessionRegistry registry;
    FakeInjector injector;
    injector.SetAllowInjectReturn(false);

    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));
    registry.RegisterHostSession(&host);

    registry.MarkSpawnSuspended(64004,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(64004, SpawnTransactionState::kWaitingAgentReady);

    ServerHandlerConfig config;
    config.agent_path = "__embedded_agent__";

    MaybePromoteLateBoundControlReadyChild(&registry,
                                           &injector,
                                           config,
                                           host.GetId(),
                                           64004);

    assert(!WaitForInjectedPid(&injector, 64004, 50));

    SpawnSuspendedEntry after;
    assert(registry.GetSpawnSuspendedEntry(64004, &after));
    assert(after.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(after.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

}  // namespace

int main() {
    TestLatePromotionUsesSpawnOwnedHostWhenGlobalPidBindingDisappears();
    TestLatePromotionCancelsIfRuntimeReadyAlreadyPresent();
    TestLatePromotionDoesNotWaitForSelfBootstrapGraceWhenControlOnly();
    TestLatePromotionDoesNotAdvanceRuntimeStateWhenInjectFails();
    return 0;
}
