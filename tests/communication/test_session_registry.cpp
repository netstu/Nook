#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/session_registry.h"

using namespace nook::comm;
using namespace nook::server;

namespace {

class StubTransport final : public Transport {
public:
    StubTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }
    ssize_t Send(const uint8_t*, size_t len) override { return static_cast<ssize_t>(len); }
    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Stub"; }
};

void TestRegistryFindsAgentSessionByProcessName() {
    SessionRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    Session agent(std::move(transport));

    registry.RegisterAgentSession(4321, &agent);
    registry.RegisterAgentProcessName(4321, "zygote64");

    assert(registry.FindAgentSessionByPid(4321) == &agent);
    assert(registry.FindAgentSessionByProcessName("zygote64") == &agent);
    assert(registry.FindAgentSessionByProcessName("usap64") == nullptr);

    registry.RemoveAgentSessionByPid(4321);
    assert(registry.FindAgentSessionByProcessName("zygote64") == nullptr);
}

void TestRegistryWaitsForAgentSessionByProcessName() {
    SessionRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    Session agent(std::move(transport));

    std::thread producer([&registry, &agent]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        registry.RegisterAgentSession(4321, &agent);
        registry.RegisterAgentProcessName(4321, "zygote64");
        AgentReady ready;
        ready.pid = 4321u;
        ready.process_name = "zygote64";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        registry.StoreAgentReadyFrame(4321, Frame(MessageType::kAgentReady, 9u, EncodeAgentReady(ready)));
    });

    Session* resolved = registry.WaitForAgentSessionByProcessName("zygote64", 1000);
    producer.join();

    assert(resolved == &agent);
}

void TestRegistryProcessNameMappingFollowsLatestPid() {
    SessionRegistry registry;
    auto old_transport = std::make_unique<StubTransport>();
    auto new_transport = std::make_unique<StubTransport>();
    Session old_agent(std::move(old_transport));
    Session new_agent(std::move(new_transport));

    registry.RegisterAgentSession(14535, &old_agent);
    registry.RegisterAgentProcessName(14535, "zygote64");
    assert(registry.FindAgentSessionByProcessName("zygote64") == &old_agent);

    registry.RegisterAgentSession(25969, &new_agent);
    registry.RegisterAgentProcessName(25969, "zygote64");

    assert(registry.FindAgentSessionByPid(14535) == &old_agent);
    assert(registry.FindAgentSessionByPid(25969) == &new_agent);
    assert(registry.FindAgentSessionByProcessName("zygote64") == &new_agent);
}

void TestRegistryFindControlReadySessionByIdentityUsesControlProcessName() {
    SessionRegistry registry;
    auto control_transport = std::make_unique<StubTransport>();
    auto runtime_transport = std::make_unique<StubTransport>();
    Session control_agent(std::move(control_transport));
    Session runtime_agent(std::move(runtime_transport));

    registry.RegisterAgentSession(8001, &control_agent);
    registry.RegisterControlReadyAgentSession(8001, &control_agent);
    registry.RegisterAgentProcessName(8001, "zygote64");
    registry.MarkAgentAuthoritativeReady(8001);
    registry.MarkAgentReadyStage(8001, AgentReadyStage::kControl);

    assert(registry.FindControlReadyAgentSessionByIdentity(8001, "zygote64") == &control_agent);
    assert(registry.FindControlReadyAgentSessionByIdentity(8001, "com.demo.target") == nullptr);

    registry.RegisterAgentSession(8001, &runtime_agent);
    registry.RegisterAgentProcessName(8001, "com.demo.target");
    registry.MarkAgentReadyStage(8001, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(8001);

    assert(registry.FindControlReadyAgentSessionByIdentity(8001, "zygote64") == &control_agent);
    assert(registry.FindControlReadyAgentSessionByIdentity(8001, "com.demo.target") == nullptr);
}

void TestRegistryConditionalAgentRemovalOnlyClearsMatchingSession() {
    SessionRegistry registry;
    auto old_transport = std::make_unique<StubTransport>();
    auto new_transport = std::make_unique<StubTransport>();
    Session old_agent(std::move(old_transport));
    Session new_agent(std::move(new_transport));

    registry.RegisterAgentSession(4321, &old_agent);
    registry.RegisterAgentSession(4321, &new_agent);

    registry.RemoveAgentSessionByPid(4321);
    assert(registry.FindAgentSessionByPid(4321) == nullptr);
}

void TestRegistryConditionalRemovalPreservesLatestProcessNameBinding() {
    SessionRegistry registry;
    auto old_transport = std::make_unique<StubTransport>();
    auto new_transport = std::make_unique<StubTransport>();
    Session old_agent(std::move(old_transport));
    Session new_agent(std::move(new_transport));

    registry.RegisterAgentSession(14535, &old_agent);
    registry.RegisterAgentProcessName(14535, "zygote64");

    registry.RegisterAgentSession(25969, &new_agent);
    registry.RegisterAgentProcessName(25969, "zygote64");

    assert(registry.FindAgentSessionByProcessName("zygote64") == &new_agent);

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, &old_agent));
    assert(registry.FindAgentSessionByPid(14535) == nullptr);
    assert(registry.FindAgentSessionByPid(25969) == &new_agent);
    assert(registry.FindAgentSessionByProcessName("zygote64") == &new_agent);
}

void TestRemoveHostSessionClearsOwnedPendingSpawnEntries() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    auto other_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    Session other_host(std::move(other_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterHostSession(&other_host);
    registry.RegisterPendingSpawn("spawn-token-owned", "com.demo.target", host.GetId());
    registry.RegisterPendingSpawn("spawn-token-other", "com.demo.other", other_host.GetId());

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-owned", &pending));
    assert(registry.GetPendingSpawn("spawn-token-other", &pending));

    registry.RemoveHostSession(host.GetId());

    assert(!registry.GetPendingSpawn("spawn-token-owned", &pending));
    assert(registry.GetPendingSpawn("spawn-token-other", &pending));
    assert(pending.host_session_id == other_host.GetId());
}

void TestRemoveHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-resolved-unbound",
                                  "com.demo.target",
                                  host.GetId());

    registry.RegisterAgentSession(7601, &agent);
    registry.RegisterAgentProcessName(7601, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7601);
    registry.MarkAgentReadyStage(7601, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7601);

    AgentReady ready;
    ready.pid = 7601u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7601,
                                  Frame(MessageType::kAgentReady, 31u, EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"resolved-unbound\"}";
    registry.StoreScriptMessageFrame(7601,
                                     Frame(MessageType::kScriptMessage, 32u, EncodeScriptMessage(message)));

    assert(registry.ResolvePendingSpawn("spawn-token-resolved-unbound",
                                        7601,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-resolved-unbound", &pending));
    assert(pending.ready);
    assert(pending.pid == 7601);
    assert(registry.FindAgentSessionByPid(7601) == &agent);
    assert(registry.IsAgentRuntimeReady(7601));

    registry.RemoveHostSession(host.GetId());

    assert(!registry.GetPendingSpawn("spawn-token-resolved-unbound", &pending));
    assert(registry.FindAgentSessionByPid(7601) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7601));
    assert(!registry.IsAgentRuntimeReady(7601));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(7601, &cached_ready));
    assert(registry.TakeScriptMessageFrames(7601).empty());
    assert(registry.IsInvalidatedAgentPid(7601));
}

void TestRemoveHostSessionClearsOwnedPendingAttachEntries() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    auto other_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    Session other_host(std::move(other_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterHostSession(&other_host);
    registry.RegisterPendingAttach("attach-token-owned",
                                   7101,
                                   "com.demo.target",
                                   host.GetId());
    registry.RegisterPendingAttach("attach-token-other",
                                   7102,
                                   "com.demo.other",
                                   other_host.GetId());

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-owned", &pending));
    assert(registry.GetPendingAttach("attach-token-other", &pending));

    registry.RemoveHostSession(host.GetId());

    assert(!registry.GetPendingAttach("attach-token-owned", &pending));
    assert(registry.GetPendingAttach("attach-token-other", &pending));
    assert(pending.host_session_id == other_host.GetId());
}

void TestRemoveHostSessionClearsOwnedAttachTimeoutPids() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    auto other_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    Session other_host(std::move(other_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterHostSession(&other_host);
    registry.BindHostToPid(host.GetId(), 7201);
    registry.BindHostToPid(other_host.GetId(), 7202);
    registry.MarkAttachTimeoutPid(7201);
    registry.MarkAttachTimeoutPid(7202);

    assert(registry.WasAttachTimeoutPid(7201));
    assert(registry.WasAttachTimeoutPid(7202));

    registry.RemoveHostSession(host.GetId());

    assert(!registry.WasAttachTimeoutPid(7201));
    assert(registry.WasAttachTimeoutPid(7202));
}

void TestRemoveHostSessionClearsOwnedSpawnSuspendedEntriesAndCachedMessages() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    auto other_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    Session other_host(std::move(other_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterHostSession(&other_host);
    registry.BindHostToPid(host.GetId(), 7101);
    registry.BindHostToPid(other_host.GetId(), 7102);
    registry.MarkSpawnSuspended(7101, host.GetId());
    registry.MarkSpawnSuspended(7102, other_host.GetId());

    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"owned-host\"}";
    registry.StoreScriptMessageFrame(7101,
                                     Frame(MessageType::kScriptMessage, 24u, EncodeScriptMessage(message)));
    registry.StoreScriptMessageFrame(7102,
                                     Frame(MessageType::kScriptMessage, 25u, EncodeScriptMessage(message)));

    assert(registry.IsSpawnSuspended(7101));
    assert(registry.IsSpawnSuspended(7102));

    registry.RemoveHostSession(host.GetId());

    assert(!registry.IsSpawnSuspended(7101));
    assert(registry.IsSpawnSuspended(7102));
    assert(registry.TakeScriptMessageFrames(7101).empty());
    assert(!registry.TakeScriptMessageFrames(7102).empty());
}

void TestRemoveHostSessionClearsOwnedSuspendedAgentState() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 7103);
    registry.MarkSpawnSuspended(7103,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.RegisterAgentSession(7103, &agent);
    registry.RegisterAgentProcessName(7103, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7103);
    registry.MarkAgentReadyStage(7103, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7103);

    AgentReady ready;
    ready.pid = 7103u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7103,
                                  Frame(MessageType::kAgentReady, 29u, EncodeAgentReady(ready)));

    assert(registry.FindAgentSessionByPid(7103) == &agent);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == &agent);
    assert(registry.IsAgentAuthoritativeReady(7103));
    assert(registry.IsAgentRuntimeReady(7103));

    registry.RemoveHostSession(host.GetId());

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(7103));
    assert(registry.FindAgentSessionByPid(7103) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7103));
    assert(!registry.IsAgentRuntimeReady(7103));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(7103, &cached_ready));
}

void TestBindHostToPidClearsOldOwnedSpawnSuspendedEntriesAndCachedMessages() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 7201);
    registry.MarkSpawnSuspended(7201, host.GetId());
    registry.RegisterAgentSession(7201, &agent);
    registry.RegisterAgentProcessName(7201, "com.demo.rebind-old");
    registry.MarkAgentAuthoritativeReady(7201);
    registry.MarkAgentReadyStage(7201, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7201);

    ScriptMessage message;
    message.script_id = 2u;
    message.message = "{\"type\":\"send\",\"payload\":\"rebind-old\"}";
    registry.StoreScriptMessageFrame(7201,
                                     Frame(MessageType::kScriptMessage, 26u, EncodeScriptMessage(message)));

    assert(registry.IsSpawnSuspended(7201));
    assert(registry.FindPidByHostSession(host.GetId()) == 7201);

    registry.BindHostToPid(host.GetId(), 7202);

    assert(registry.FindPidByHostSession(host.GetId()) == 7202);
    assert(!registry.IsSpawnSuspended(7201));
    assert(registry.TakeScriptMessageFrames(7201).empty());
    assert(registry.FindAgentSessionByPid(7201) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.rebind-old") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7201));
    assert(!registry.IsAgentRuntimeReady(7201));
    assert(registry.IsInvalidatedAgentPid(7201));
}

void TestUnbindHostSessionClearsOwnedSpawnSuspendedEntriesAndCachedMessages() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 7301);
    registry.MarkSpawnSuspended(7301, host.GetId());
    registry.RegisterAgentSession(7301, &agent);
    registry.RegisterAgentProcessName(7301, "com.demo.unbind-old");
    registry.MarkAgentAuthoritativeReady(7301);
    registry.MarkAgentReadyStage(7301, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7301);

    ScriptMessage message;
    message.script_id = 3u;
    message.message = "{\"type\":\"send\",\"payload\":\"unbind-old\"}";
    registry.StoreScriptMessageFrame(7301,
                                     Frame(MessageType::kScriptMessage, 27u, EncodeScriptMessage(message)));

    assert(registry.IsSpawnSuspended(7301));
    assert(registry.FindPidByHostSession(host.GetId()) == 7301);

    registry.UnbindHostSession(host.GetId());

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(7301));
    assert(registry.TakeScriptMessageFrames(7301).empty());
    assert(registry.FindAgentSessionByPid(7301) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.unbind-old") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7301));
    assert(!registry.IsAgentRuntimeReady(7301));
    assert(registry.IsInvalidatedAgentPid(7301));
}

void TestUnbindHostSessionClearsOwnedPendingAttachAndTimeoutState() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkAttachTimeoutPid(7303);
    registry.RegisterPendingAttach("attach-token-unbind",
                                   7303,
                                   "com.demo.target",
                                   host.GetId());

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-unbind", &pending));
    assert(registry.WasAttachTimeoutPid(7303));

    registry.UnbindHostSession(host.GetId());

    assert(!registry.GetPendingAttach("attach-token-unbind", &pending));
    assert(!registry.WasAttachTimeoutPid(7303));
}

void TestUnbindHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-unbind-resolved",
                                  "com.demo.target",
                                  host.GetId());
    registry.RegisterAgentSession(7602, &agent);
    registry.RegisterAgentProcessName(7602, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7602);
    registry.MarkAgentReadyStage(7602, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7602);

    AgentReady ready;
    ready.pid = 7602u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7602,
                                  Frame(MessageType::kAgentReady, 33u, EncodeAgentReady(ready)));

    assert(registry.ResolvePendingSpawn("spawn-token-unbind-resolved",
                                        7602,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-unbind-resolved", &pending));
    assert(registry.FindAgentSessionByPid(7602) == &agent);

    registry.UnbindHostSession(host.GetId());

    assert(!registry.GetPendingSpawn("spawn-token-unbind-resolved", &pending));
    assert(registry.FindAgentSessionByPid(7602) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7602));
    assert(!registry.IsAgentRuntimeReady(7602));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(7602, &cached_ready));
    assert(registry.IsInvalidatedAgentPid(7602));
}

void TestClearPendingSpawnClearsResolvedAgentStateBeforeBind() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-clear-resolved",
                                  "com.demo.target",
                                  host.GetId());

    registry.RegisterAgentSession(7603, &agent);
    registry.RegisterAgentProcessName(7603, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7603);
    registry.MarkAgentReadyStage(7603, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7603);

    AgentReady ready;
    ready.pid = 7603u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7603,
                                  Frame(MessageType::kAgentReady, 33u, EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"clear-resolved\"}";
    registry.StoreScriptMessageFrame(7603,
                                     Frame(MessageType::kScriptMessage, 34u, EncodeScriptMessage(message)));

    assert(registry.ResolvePendingSpawn("spawn-token-clear-resolved",
                                        7603,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-clear-resolved", &pending));
    assert(pending.ready);
    assert(pending.pid == 7603);
    assert(registry.FindAgentSessionByPid(7603) == &agent);
    assert(registry.IsAgentRuntimeReady(7603));

    registry.ClearPendingSpawn("spawn-token-clear-resolved");

    assert(!registry.GetPendingSpawn("spawn-token-clear-resolved", &pending));
    assert(registry.FindAgentSessionByPid(7603) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7603));
    assert(!registry.IsAgentRuntimeReady(7603));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(7603, &cached_ready));
    assert(registry.TakeScriptMessageFrames(7603).empty());
    assert(registry.IsInvalidatedAgentPid(7603));
}

void TestBindHostToResolvedPendingSpawnClearsOldOwnedSpawnSuspendedEntriesAndCachedMessages() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 7401);
    registry.MarkSpawnSuspended(7401, host.GetId());
    registry.RegisterAgentSession(7401, &agent);
    registry.RegisterAgentProcessName(7401, "com.demo.resolved-rebind-old");
    registry.MarkAgentAuthoritativeReady(7401);
    registry.MarkAgentReadyStage(7401, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7401);

    ScriptMessage old_message;
    old_message.script_id = 4u;
    old_message.message = "{\"type\":\"send\",\"payload\":\"resolved-rebind-old\"}";
    registry.StoreScriptMessageFrame(7401,
                                     Frame(MessageType::kScriptMessage, 28u, EncodeScriptMessage(old_message)));

    registry.RegisterPendingSpawn("spawn-token-rebind", "com.demo.target", host.GetId());
    assert(registry.ResolvePendingSpawn("spawn-token-rebind",
                                        7402,
                                        "com.demo.target",
                                        AgentReadyStage::kControl));

    PendingSpawnEntry pending;
    assert(registry.BindHostToResolvedPendingSpawn("spawn-token-rebind", 7402, &pending));
    assert(pending.pid == 7402);

    assert(registry.FindPidByHostSession(host.GetId()) == 7402);
    assert(!registry.IsSpawnSuspended(7401));
    assert(registry.TakeScriptMessageFrames(7401).empty());
    assert(registry.FindAgentSessionByPid(7401) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.resolved-rebind-old") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7401));
    assert(!registry.IsAgentRuntimeReady(7401));
    assert(registry.IsInvalidatedAgentPid(7401));
    SpawnSuspendedEntry rebound_entry;
    assert(registry.GetSpawnSuspendedEntry(7402, &rebound_entry));
    assert(rebound_entry.suspended);
    assert(rebound_entry.response_pending);
    assert(rebound_entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestBindHostToResolvedPendingSpawnUsesRuntimeReadyStateWhenAlreadyResolved() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-runtime-bind", "com.demo.target", host.GetId());
    assert(registry.ResolvePendingSpawn("spawn-token-runtime-bind",
                                        7404,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.BindHostToResolvedPendingSpawn("spawn-token-runtime-bind", 7404, &pending));
    assert(pending.ready_stage == PendingSpawnReadyStage::kRuntimeReady);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(7404, &entry));
    assert(entry.response_pending);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestClearPendingSpawnPreservesResolvedPidAfterHostBind() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-clear-bound", "com.demo.target", host.GetId());
    registry.RegisterAgentSession(7405, &agent);
    registry.RegisterAgentProcessName(7405, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7405);
    registry.MarkAgentReadyStage(7405, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7405);

    assert(registry.ResolvePendingSpawn("spawn-token-clear-bound",
                                        7405,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.BindHostToResolvedPendingSpawn("spawn-token-clear-bound", 7405, &pending));

    registry.ClearPendingSpawn("spawn-token-clear-bound");

    assert(registry.FindPidByHostSession(host.GetId()) == 7405);
    assert(registry.FindAgentSessionByPid(7405) == &agent);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == &agent);
    assert(registry.IsAgentRuntimeReady(7405));
    assert(!registry.IsInvalidatedAgentPid(7405));
    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(7405, &entry));
    assert(entry.suspended);
}

void TestMarkSpawnSuspendedUsesControlReadyPreRuntimeState() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7405,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(7405, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestMarkSpawnSuspendedUsesRuntimeReadyHeldStateBeforeResponseRelease() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7406,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(7406, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestClearingSpawnResponsePendingPromotesRuntimeReadySpawnToScriptLoadReady() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7407,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    SpawnSuspendedEntry before;
    assert(registry.GetSpawnSuspendedEntry(7407, &before));
    assert(before.response_pending == false);
    assert(before.state == SpawnTransactionState::kWaitingRuntimeReady);

    assert(registry.SetSpawnResponsePending(7407, true));
    assert(registry.SetSpawnResponsePending(7407, false));

    SpawnSuspendedEntry after;
    assert(registry.GetSpawnSuspendedEntry(7407, &after));
    assert(after.response_pending == false);
    assert(after.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(after.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestMarkSpawnRuntimeReadyVisiblePromotesOnlyAfterResponseRelease() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(74071,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    SpawnSuspendedEntry before;
    assert(registry.GetSpawnSuspendedEntry(74071, &before));
    assert(before.response_pending == false);
    assert(before.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(before.state == SpawnTransactionState::kWaitingRuntimeReady);

    assert(!registry.MarkSpawnRuntimeReadyVisible(74071));

    assert(registry.UpdateSpawnSuspendedAuthoritativeReady(74071,
                                                           PendingSpawnReadyStage::kRuntimeReady,
                                                           "com.demo.target"));
    assert(registry.SetSpawnResponsePending(74071, true));
    assert(!registry.MarkSpawnRuntimeReadyVisible(74071));

    SpawnSuspendedEntry held;
    assert(registry.GetSpawnSuspendedEntry(74071, &held));
    assert(held.response_pending);
    assert(held.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(held.state == SpawnTransactionState::kWaitingRuntimeReady);

    assert(registry.SetSpawnResponsePending(74071, false));
    assert(registry.MarkSpawnRuntimeReadyVisible(74071));

    SpawnSuspendedEntry after;
    assert(registry.GetSpawnSuspendedEntry(74071, &after));
    assert(after.response_pending == false);
    assert(after.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(after.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestSpawnBlockedForScriptOperationsTracksHeldPreRuntimeStates() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(74072,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(registry.IsSpawnBlockedForScriptOperations(74072));
    assert(registry.UpdateSpawnSuspendedAuthoritativeReady(74072,
                                                           PendingSpawnReadyStage::kRuntimeReady,
                                                           "com.demo.target"));
    assert(registry.IsSpawnBlockedForScriptOperations(74072));
    assert(registry.MarkSpawnRuntimeReadyVisible(74072));
    assert(!registry.IsSpawnBlockedForScriptOperations(74072));
}

void TestCanExposeSpawnRuntimeReadyImmediatelyRequiresReleasedRuntimeBoundary() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(74073,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(!registry.CanExposeSpawnRuntimeReadyImmediately(74073));
    assert(registry.UpdateSpawnSuspendedAuthoritativeReady(74073,
                                                           PendingSpawnReadyStage::kRuntimeReady,
                                                           "com.demo.target"));
    assert(registry.SetSpawnResponsePending(74073, true));
    assert(!registry.CanExposeSpawnRuntimeReadyImmediately(74073));
    assert(registry.SetSpawnResponsePending(74073, false));
    assert(registry.CanExposeSpawnRuntimeReadyImmediately(74073));
}

void TestClearSpawnTransactionByPidClearsOwnedAgentState() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));
    auto agent_transport = std::make_unique<StubTransport>();
    Session agent(std::move(agent_transport));

    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 7403);
    registry.MarkSpawnSuspended(7403,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target",
                                "spawn-token-clear-transaction");
    registry.RegisterAgentSession(7403, &agent);
    registry.RegisterAgentProcessName(7403, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7403);
    registry.MarkAgentReadyStage(7403, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7403);

    AgentReady ready;
    ready.pid = 7403u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7403,
                                  Frame(MessageType::kAgentReady, 35u, EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 2u;
    message.message = "{\"type\":\"send\",\"payload\":\"clear-transaction\"}";
    registry.StoreScriptMessageFrame(7403,
                                     Frame(MessageType::kScriptMessage, 36u, EncodeScriptMessage(message)));

    assert(registry.FindPidByHostSession(host.GetId()) == 7403);
    assert(registry.IsSpawnSuspended(7403));
    assert(registry.FindAgentSessionByPid(7403) == &agent);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == &agent);
    assert(registry.IsAgentRuntimeReady(7403));

    registry.ClearSpawnTransactionByPid(7403);

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(7403));
    assert(registry.FindAgentSessionByPid(7403) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(7403));
    assert(!registry.IsAgentRuntimeReady(7403));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(7403, &cached_ready));
    assert(registry.TakeScriptMessageFrames(7403).empty());
    assert(registry.IsInvalidatedAgentPid(7403));
}

void TestFindOwnedSpawnPidByHostSessionFallsBackWhenCoarseBindingIsLost() {
    SessionRegistry registry;
    auto original_transport = std::make_unique<StubTransport>();
    auto rebound_transport = std::make_unique<StubTransport>();
    Session original_host(std::move(original_transport));
    Session rebound_host(std::move(rebound_transport));

    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(7501, original_host.GetId());

    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);
    assert(registry.FindOwnedSpawnPidByHostSession(original_host.GetId()) == 7501);

    registry.BindHostToPid(rebound_host.GetId(), 7501);

    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);
    assert(registry.FindOwnedSpawnPidByHostSession(original_host.GetId()) == 7501);
    assert(registry.FindPidByHostSession(rebound_host.GetId()) == 7501);
}

void TestPendingSpawnResolutionStageUpgradesMonotonically() {
    SessionRegistry registry;
    registry.RegisterPendingSpawn("spawn-token-stage", "com.demo.target", 7u);

    assert(registry.ResolvePendingSpawn("spawn-token-stage",
                                        4321,
                                        "zygote64",
                                        AgentReadyStage::kControl));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-stage", &pending));
    assert(pending.ready);
    assert(pending.pid == 4321);
    assert(pending.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(pending.resolved_process_name == "zygote64");

    assert(registry.ResolvePendingSpawn("spawn-token-stage",
                                        4321,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));
    assert(registry.GetPendingSpawn("spawn-token-stage", &pending));
    assert(pending.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(pending.resolved_process_name == "com.demo.target");

    assert(registry.ResolvePendingSpawn("spawn-token-stage",
                                        4321,
                                        "zygote64",
                                        AgentReadyStage::kControl));
    assert(registry.GetPendingSpawn("spawn-token-stage", &pending));
    assert(pending.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(pending.resolved_process_name == "com.demo.target");
}

void TestPendingSpawnRuntimeUpgradeRejectsDifferentPidAfterControlResolution() {
    SessionRegistry registry;
    registry.RegisterPendingSpawn("spawn-token-stage-pid", "com.demo.target", 7u);

    assert(registry.ResolvePendingSpawn("spawn-token-stage-pid",
                                        4321,
                                        "zygote64",
                                        AgentReadyStage::kControl));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-stage-pid", &pending));
    assert(pending.ready);
    assert(pending.pid == 4321);
    assert(pending.ready_stage == PendingSpawnReadyStage::kControlReady);

    assert(!registry.ResolvePendingSpawn("spawn-token-stage-pid",
                                         4322,
                                         "com.demo.target",
                                         AgentReadyStage::kRuntime));

    assert(registry.GetPendingSpawn("spawn-token-stage-pid", &pending));
    assert(pending.ready);
    assert(pending.pid == 4321);
    assert(pending.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(pending.resolved_process_name == "zygote64");
}

void TestConditionalAgentRemovalClearsCachedReadyAndScriptMessages() {
    SessionRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    Session agent(std::move(transport));

    registry.RegisterAgentSession(7001, &agent);
    registry.RegisterAgentProcessName(7001, "com.demo.target");

    AgentReady ready;
    ready.pid = 7001u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(7001, Frame(MessageType::kAgentReady, 21u, EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"orphan\"}";
    registry.StoreScriptMessageFrame(7001,
                                     Frame(MessageType::kScriptMessage, 22u, EncodeScriptMessage(message)));

    Frame cached_ready;
    assert(registry.GetAgentReadyFrame(7001, &cached_ready));
    assert(!registry.TakeScriptMessageFrames(7001).empty());
    registry.StoreScriptMessageFrame(7001,
                                     Frame(MessageType::kScriptMessage, 23u, EncodeScriptMessage(message)));

    assert(registry.RemoveAgentSessionByPidIfMatches(7001, &agent));
    assert(!registry.GetAgentReadyFrame(7001, &cached_ready));
    assert(registry.TakeScriptMessageFrames(7001).empty());
    assert(registry.FindAgentSessionByPid(7001) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
}

void TestPidReuseDoesNotLeakOldProcessNameBindingToNewSession() {
    SessionRegistry registry;

    auto old_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto new_agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(4321, old_agent.get());
    registry.RegisterAgentProcessName(4321, "zygote64");
    assert(registry.FindAgentSessionByProcessName("zygote64") == old_agent.get());

    registry.RegisterAgentSession(4321, new_agent.get());

    assert(registry.FindAgentSessionByPid(4321) == new_agent.get());
    assert(registry.FindAgentSessionByProcessName("zygote64") == nullptr);
}

void TestWaitForAgentSessionByIdentityIgnoresStaleProcessNameBindingForDifferentPid() {
    SessionRegistry registry;

    auto old_agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, old_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    Session* resolved = registry.WaitForAgentSessionByIdentity(25969, "zygote64", 50);
    assert(resolved == nullptr);
}

void TestWaitForAgentSessionByIdentityReturnsReboundProcessNameForTargetPid() {
    SessionRegistry registry;

    auto old_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto new_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(old_agent->Start());
    assert(new_agent->Start());

    registry.RegisterAgentSession(14535, old_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    std::thread producer([&registry, &new_agent]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        registry.RegisterAgentSession(25969, new_agent.get());
        registry.RegisterAgentProcessName(25969, "zygote64");
    });

    Session* resolved = registry.WaitForAgentSessionByIdentity(25969, "zygote64", 1000);
    producer.join();

    assert(resolved == new_agent.get());
}

void TestWaitForAgentSessionByIdentityFallsBackToProcessNameWhenPidUnknown() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    Session* resolved = registry.WaitForAgentSessionByIdentity(-1, "zygote64", 50);
    assert(resolved == agent.get());
}

void TestWaitForAuthoritativeAgentSessionByIdentityRequiresAuthoritativeReadyFrame() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    Session* resolved = registry.WaitForAuthoritativeAgentSessionByIdentity(14535, "zygote64", 50);
    assert(resolved == nullptr);

    registry.MarkAgentAuthoritativeReady(14535);

    resolved = registry.WaitForAuthoritativeAgentSessionByIdentity(14535, "zygote64", 50);
    assert(resolved == agent.get());
}

void TestWaitForControlReadyAgentSessionByIdentityRequiresRecordedReadyStage() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);

    Session* resolved = registry.WaitForControlReadyAgentSessionByIdentity(14535, "zygote64", 50);
    assert(resolved == nullptr);

    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    resolved = registry.WaitForControlReadyAgentSessionByIdentity(14535, "zygote64", 50);
    assert(resolved == agent.get());
}

void TestWaitForRuntimeReadyAgentSessionByIdentityRequiresRuntimeStageAndIdentity() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentRuntimeReady(14535);

    Session* resolved = registry.WaitForRuntimeReadyAgentSessionByIdentity(14535, "com.demo.target", 50);
    assert(resolved == nullptr);

    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);

    resolved = registry.WaitForRuntimeReadyAgentSessionByIdentity(14535, "com.demo.target", 50);
    assert(resolved == agent.get());

    assert(registry.WaitForRuntimeReadyAgentSessionByIdentity(14535, "zygote64", 20) == nullptr);
}

void TestAuthoritativeReadyDoesNotRequireCachedRuntimeReadyFrame() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(14535, &cached_ready));
    assert(!registry.IsAgentAuthoritativeReady(14535));

    registry.MarkAgentAuthoritativeReady(14535);

    assert(registry.IsAgentAuthoritativeReady(14535));
    assert(registry.FindAuthoritativeAgentSessionByPid(14535) == agent.get());
    assert(!registry.GetAgentReadyFrame(14535, &cached_ready));
}

void TestRuntimeReadyDoesNotRequireCachedRuntimeReadyFrame() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(24535, agent.get());
    registry.RegisterAgentProcessName(24535, "com.demo.target");

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(24535, &cached_ready));
    assert(!registry.IsAgentRuntimeReady(24535));

    registry.MarkAgentRuntimeReady(24535);

    assert(registry.IsAgentRuntimeReady(24535));
    assert(!registry.GetAgentReadyFrame(24535, &cached_ready));

    registry.RemoveAgentSessionByPid(24535);
    assert(!registry.IsAgentRuntimeReady(24535));
}

void TestWaitForAgentRuntimeReadyDoesNotRequireCachedRuntimeReadyFrame() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(34535, agent.get());
    registry.RegisterAgentProcessName(34535, "com.demo.target");

    assert(!registry.WaitForAgentRuntimeReady(34535, 20));

    registry.MarkAgentRuntimeReady(34535);

    assert(registry.WaitForAgentRuntimeReady(34535, 20));
}

void TestFindRuntimeReadyAgentSessionRequiresRuntimeStage() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(34536, agent.get());
    registry.RegisterAgentProcessName(34536, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(34536);
    registry.MarkAgentRuntimeReady(34536);
    registry.MarkAgentReadyStage(34536, AgentReadyStage::kControl);

    assert(registry.FindRuntimeReadyAgentSessionByIdentity(34536, "com.demo.target") == nullptr);
    assert(registry.WaitForRuntimeReadyAgentSessionByIdentity(34536, "com.demo.target", 20) == nullptr);

    registry.MarkAgentReadyStage(34536, AgentReadyStage::kRuntime);

    assert(registry.FindRuntimeReadyAgentSessionByIdentity(34536, "com.demo.target") == agent.get());
    assert(registry.WaitForRuntimeReadyAgentSessionByIdentity(34536, "com.demo.target", 20) == agent.get());
}

void TestGetAgentReadyFrameByIdentityRequiresMatchingProcessName() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(45678, agent.get());
    registry.RegisterAgentProcessName(45678, "com.demo.target");

    AgentReady ready;
    ready.pid = 45678u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-45678";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(45678, Frame(MessageType::kAgentReady, 31u, EncodeAgentReady(ready)));

    Frame cached_ready;
    assert(registry.GetAgentReadyFrameByIdentity(45678, "com.demo.target", &cached_ready));

    ready.process_name = "com.demo.other";
    registry.StoreAgentReadyFrame(45678, Frame(MessageType::kAgentReady, 32u, EncodeAgentReady(ready)));
    assert(!registry.GetAgentReadyFrameByIdentity(45678, "com.demo.target", &cached_ready));
    assert(!registry.GetAgentReadyFrameByIdentity(45678, "com.demo.other", &cached_ready));
}

void TestPendingSpawnResolutionRejectsMismatchedProcessName() {
    SessionRegistry registry;

    registry.RegisterPendingSpawn("spawn-token-mismatch", "com.demo.target", 77);

    assert(!registry.ResolvePendingSpawn("spawn-token-mismatch",
                                         45679,
                                         "com.demo.other",
                                         AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-mismatch", &pending));
    assert(!pending.ready);
    assert(pending.pid == -1);
    assert(pending.ready_stage == PendingSpawnReadyStage::kNone);
}

void TestPendingSpawnResolutionRejectsMismatchedPidForAlreadyBoundHost() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 45678);
    registry.RegisterPendingSpawn("spawn-token-pid-mismatch", "com.demo.target", host->GetId());

    assert(!registry.ResolvePendingSpawn("spawn-token-pid-mismatch",
                                         45679,
                                         "com.demo.target",
                                         AgentReadyStage::kRuntime));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-pid-mismatch", &pending));
    assert(!pending.ready);
    assert(pending.pid == -1);
    assert(pending.ready_stage == PendingSpawnReadyStage::kNone);
}

void TestWaitForPendingSpawnExitsPromptlyOnShutdown() {
    SessionRegistry registry;
    registry.RegisterPendingSpawn("spawn-token", "com.demo.target", 7u);

    std::atomic<bool> completed{false};
    std::atomic<bool> result{true};
    std::thread waiter([&]() {
        int pid = 0;
        result.store(registry.WaitForPendingSpawn("spawn-token", 5000, &pid),
                     std::memory_order_release);
        completed.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    registry.Shutdown();
    waiter.join();

    assert(completed.load(std::memory_order_acquire));
    assert(!result.load(std::memory_order_acquire));
}

void TestShutdownClearsPendingAttachAndAttachTimeoutState() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkAttachTimeoutPid(7202);
    registry.RegisterPendingAttach("attach-token-shutdown",
                                   7202,
                                   "com.demo.target",
                                   host->GetId());

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-shutdown", &pending));
    assert(registry.WasAttachTimeoutPid(7202));

    registry.Shutdown();

    assert(!registry.GetPendingAttach("attach-token-shutdown", &pending));
    assert(!registry.WasAttachTimeoutPid(7202));
}

void TestResolveAgentReadySpawnContextPrefersPendingSpawnThenSuspendedEntry() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-context", "com.demo.target", host->GetId());
    registry.MarkSpawnSuspended(14560,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.fallback",
                                "spawn-token-context");

    AgentReadySpawnContext context;
    assert(registry.ResolveAgentReadySpawnContext(14560,
                                                  "spawn-token-context",
                                                  "zygote64",
                                                  &context));
    assert(context.has_pending_spawn_context);
    assert(!context.has_pending_attach_context);
    assert(!context.pending_attach_matches);
    assert(context.has_spawn_suspended_context);
    assert(!context.spawn_token_mismatches_existing_spawn_context);
    assert(context.has_suspended_entry);
    assert(context.expected_spawn_process_name == "com.demo.target");
    assert(context.suspended_entry.target_process_name == "com.demo.fallback");
}

void TestResolveAgentReadySpawnContextFallsBackToSuspendedRuntimeIdentity() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(14561,
                                host->GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "",
                                "spawn-token-runtime");

    AgentReadySpawnContext context;
    assert(registry.ResolveAgentReadySpawnContext(14561,
                                                  "spawn-token-runtime",
                                                  "com.demo.target",
                                                  &context));
    assert(!context.has_pending_spawn_context);
    assert(context.has_spawn_suspended_context);
    assert(context.has_suspended_entry);
    assert(context.expected_spawn_process_name == "com.demo.target");
    assert(!context.spawn_token_mismatches_existing_spawn_context);
}

void TestResolveAgentReadySpawnContextMatchesPendingAttachIdentity() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-context",
                                   14562,
                                   "com.demo.target",
                                   host->GetId());

    AgentReadySpawnContext context;
    assert(registry.ResolveAgentReadySpawnContext(14562,
                                                  "attach-token-context",
                                                  "com.demo.target",
                                                  &context));
    assert(!context.has_pending_spawn_context);
    assert(context.has_pending_attach_context);
    assert(context.pending_attach_matches);
    assert(!context.has_spawn_suspended_context);
    assert(!context.has_suspended_entry);
    assert(context.expected_spawn_process_name.empty());
}

void TestResolveAgentReadySpawnContextDetectsSuspendedSpawnTokenMismatch() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(14563,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target",
                                "spawn-token-owned");

    AgentReadySpawnContext context;
    assert(registry.ResolveAgentReadySpawnContext(14563,
                                                  "spawn-token-other",
                                                  "com.demo.target",
                                                  &context));
    assert(context.has_spawn_suspended_context);
    assert(context.has_suspended_entry);
    assert(context.spawn_token_mismatches_existing_spawn_context);
    assert(context.expected_spawn_process_name == "com.demo.target");
}

void TestShouldDropStaleAttachAgentReadyRequiresUnownedPendingAttachConflict() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-stale",
                                   14564,
                                   "com.demo.target",
                                   host->GetId());

    AgentReadyDropContext context;
    assert(registry.ShouldDropStaleAttachAgentReady(14564,
                                                    "com.demo.target",
                                                    context));

    context.has_pending_spawn_context = true;
    assert(!registry.ShouldDropStaleAttachAgentReady(14564,
                                                     "com.demo.target",
                                                     context));
}

void TestShouldDropForeignAttachLikeAgentReadyRequiresSpawnTokenWithoutContext() {
    SessionRegistry registry;
    AgentReadyDropContext context;

    assert(registry.ShouldDropForeignAttachLikeAgentReady("foreign-token", context));

    context.pending_attach_matches = true;
    assert(!registry.ShouldDropForeignAttachLikeAgentReady("foreign-token", context));

    context.pending_attach_matches = false;
    context.has_spawn_suspended_context = true;
    assert(!registry.ShouldDropForeignAttachLikeAgentReady("foreign-token", context));
}

void TestShouldDropMismatchedSpawnTokenAgentReadyReflectsContextFlag() {
    SessionRegistry registry;
    AgentReadyDropContext context;
    assert(!registry.ShouldDropMismatchedSpawnTokenAgentReady(context));
    context.spawn_token_mismatches_existing_spawn_context = true;
    assert(registry.ShouldDropMismatchedSpawnTokenAgentReady(context));
}

void TestShouldDropOrphanAttachAgentReadyRequiresTimeoutUnboundAndUnowned() {
    SessionRegistry registry;
    registry.MarkAttachTimeoutPid(14565);

    AgentReadyDropContext context;
    assert(registry.ShouldDropOrphanAttachAgentReady("", 14565, context));

    context.has_bound_host = true;
    assert(!registry.ShouldDropOrphanAttachAgentReady("", 14565, context));

    context.has_bound_host = false;
    context.owned_zygote_control_target = true;
    assert(!registry.ShouldDropOrphanAttachAgentReady("", 14565, context));
}

void TestShouldDropInvalidatedUnownedAgentReadyRequiresInvalidatedPid() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-invalidated",
                                  "com.demo.target",
                                  host->GetId());
    registry.RegisterAgentSession(14566, agent.get());
    registry.RegisterAgentProcessName(14566, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14566);
    registry.MarkAgentReadyStage(14566, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14566);
    assert(registry.ResolvePendingSpawn("spawn-token-invalidated",
                                        14566,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));
    registry.ClearPendingSpawn("spawn-token-invalidated");
    assert(registry.IsInvalidatedAgentPid(14566));

    AgentReadyDropContext context;
    assert(registry.ShouldDropInvalidatedUnownedAgentReady("", 14566, context));

    context.owned_zygote_control_target = true;
    assert(!registry.ShouldDropInvalidatedUnownedAgentReady("", 14566, context));
}

void TestEvaluateAgentReadyEarlyDropReturnsOrphanSpawnTokenAndDropsSession() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(145661, agent.get());
    registry.RegisterAgentProcessName(145661, "com.demo.target");

    AgentReadyDropContext context;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(145661,
                                             "orphan-token",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kOrphanSpawnToken);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(145661) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsMismatchedPendingAttachAndDropsSession() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-mismatch",
                                   145662,
                                   "com.demo.target",
                                   host->GetId());
    registry.RegisterAgentSession(145662, agent.get());
    registry.RegisterAgentProcessName(145662, "com.demo.other");

    AgentReadyDropContext context;
    context.has_pending_attach_context = true;
    context.pending_attach_matches = false;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(145662,
                                             "attach-token-mismatch",
                                             "com.demo.other",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kMismatchedPendingAttach);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(145662) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsStaleAttachAndDropsSession() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-stale",
                                   145663,
                                   "com.demo.target",
                                   host->GetId());
    registry.RegisterAgentSession(145663, agent.get());
    registry.RegisterAgentProcessName(145663, "com.demo.target");

    AgentReadyDropContext context;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(145663,
                                             "",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kStaleAttachWhileNewAttachPending);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(145663) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsForeignAttachLikeAndDropsSession() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.RegisterAgentSession(145664, agent.get());
    registry.RegisterAgentProcessName(145664, "com.demo.target");

    AgentReadyDropContext context;
    context.has_bound_host = true;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(145664,
                                             "foreign-token",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kForeignAttachLike);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(145664) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsMismatchedSpawnTokenAndDropsSession() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(1456641, agent.get());
    registry.RegisterAgentProcessName(1456641, "com.demo.target");

    AgentReadyDropContext context;
    context.has_spawn_suspended_context = true;
    context.spawn_token_mismatches_existing_spawn_context = true;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(1456641,
                                             "mismatch-token",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kMismatchedSpawnToken);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(1456641) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsOrphanAttachAndDropsSession() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.MarkAttachTimeoutPid(1456642);
    registry.RegisterAgentSession(1456642, agent.get());
    registry.RegisterAgentProcessName(1456642, "com.demo.target");

    AgentReadyDropContext context;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(1456642,
                                             "",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kOrphanAttach);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(1456642) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsInvalidatedUnownedAndDropsSession() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-invalidated-eval",
                                  "com.demo.target",
                                  host->GetId());
    registry.RegisterAgentSession(145665, agent.get());
    registry.RegisterAgentProcessName(145665, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(145665);
    registry.MarkAgentReadyStage(145665, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(145665);
    assert(registry.ResolvePendingSpawn("spawn-token-invalidated-eval",
                                        145665,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));
    registry.ClearPendingSpawn("spawn-token-invalidated-eval");

    AgentReadyDropContext context;
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(145665,
                                             "",
                                             "com.demo.target",
                                             agent.get(),
                                             context);

    assert(decision.reason == AgentReadyEarlyDropReason::kInvalidatedUnowned);
    assert(!decision.dropped_session);
    assert(registry.FindAgentSessionByPid(145665) == nullptr);
}

void TestEvaluateAgentReadyEarlyDropReturnsKnownSpawnControlIdentityMismatchAndDropsSession() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(1456651, agent.get());
    registry.RegisterAgentProcessName(1456651, "com.demo.other");

    AgentReadyDropContext context;
    context.has_spawn_suspended_context = true;
    context.has_suspended_entry = true;
    context.runtime_ready = false;
    context.suspended_entry.suspended = true;
    context.suspended_entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    context.suspended_entry.authoritative_process_name = "zygote64";
    context.suspended_entry.target_process_name = "com.demo.target";
    const AgentReadyEarlyDropDecision decision =
        registry.EvaluateAgentReadyEarlyDrop(1456651,
                                             "",
                                             "com.demo.other",
                                             agent.get(),
                                             context);

    assert(decision.reason ==
           AgentReadyEarlyDropReason::kMismatchedControlStageKnownSpawnIdentity);
    assert(decision.dropped_session);
    assert(registry.FindAgentSessionByPid(1456651) == nullptr);
}

void TestEvaluateAgentReadyPreRegistrationDropsLateControlAtRuntimeBoundary() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(1456652, agent.get());
    registry.RegisterAgentProcessName(1456652, "com.demo.target");

    SpawnSuspendedEntry entry;
    entry.pid = 1456652;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    const AgentReadyPreRegistrationDecision decision =
        registry.EvaluateAgentReadyPreRegistration(1456652,
                                                   "com.demo.target",
                                                   false,
                                                   false,
                                                   false,
                                                   agent.get(),
                                                   entry);

    assert(decision.action ==
           AgentReadyPreRegistrationAction::kDropLateControlAtRuntimeBoundary);
    assert(decision.dropped_session);
    assert(!decision.reset_runtime_trace);
    assert(registry.FindAgentSessionByPid(1456652) == nullptr);
}

void TestEvaluateAgentReadyPreRegistrationDropsLateControlFromNonCurrentSession() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(1456653, agent.get());
    registry.RegisterAgentProcessName(1456653, "com.demo.target");

    const AgentReadyPreRegistrationDecision decision =
        registry.EvaluateAgentReadyPreRegistration(1456653,
                                                   "com.demo.target",
                                                   false,
                                                   true,
                                                   false,
                                                   agent.get(),
                                                   SpawnSuspendedEntry{});

    assert(decision.action ==
           AgentReadyPreRegistrationAction::kDropLateControlFromNonCurrentSession);
    assert(decision.dropped_session);
    assert(!decision.reset_runtime_trace);
    assert(registry.FindAgentSessionByPid(1456653) == nullptr);
}

void TestEvaluateAgentReadyPreRegistrationResetsMismatchedRuntimeTrace() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(1456654, agent.get());
    registry.RegisterAgentProcessName(1456654, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(1456654);
    registry.MarkAgentReadyStage(1456654, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(1456654);

    const AgentReadyPreRegistrationDecision decision =
        registry.EvaluateAgentReadyPreRegistration(1456654,
                                                   "com.demo.target",
                                                   false,
                                                   false,
                                                   true,
                                                   agent.get(),
                                                   SpawnSuspendedEntry{});

    assert(decision.action ==
           AgentReadyPreRegistrationAction::kResetMismatchedRuntimeTrace);
    assert(!decision.dropped_session);
    assert(decision.reset_runtime_trace);
    AgentReadyStage stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(1456654, &stage));
    assert(stage == AgentReadyStage::kControl);
    assert(!registry.IsAgentRuntimeReady(1456654));
}

void TestEvaluateAgentReadyForwardingReturnsForwardRuntimeToHostWhenBoundaryReleased() {
    SessionRegistry registry;
    registry.MarkSpawnSuspended(1456655,
                                41u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.ReleaseSpawnResponseBoundary(1456655));

    const AgentReadyForwardDecision decision =
        registry.EvaluateAgentReadyForwarding(1456655, true, true, true);

    assert(decision.action == AgentReadyForwardAction::kForwardRuntimeToHost);
    assert(decision.can_expose_runtime_ready_immediately);
}

void TestEvaluateAgentReadyForwardingReturnsHoldRuntimeUntilSpawnResponse() {
    SessionRegistry registry;
    registry.MarkSpawnSuspended(1456656,
                                42u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.SetSpawnResponsePending(1456656, true));

    const AgentReadyForwardDecision decision =
        registry.EvaluateAgentReadyForwarding(1456656, true, true, true);

    assert(decision.action == AgentReadyForwardAction::kHoldRuntimeUntilSpawnResponse);
    assert(!decision.can_expose_runtime_ready_immediately);
}

void TestEvaluateAgentReadyForwardingReturnsDropMismatchedRuntimeForHost() {
    SessionRegistry registry;

    const AgentReadyForwardDecision decision =
        registry.EvaluateAgentReadyForwarding(1456657, true, false, true);

    assert(decision.action == AgentReadyForwardAction::kDropMismatchedRuntimeForHost);
    assert(!decision.can_expose_runtime_ready_immediately);
}

void TestEvaluateAgentReadyForwardingReturnsExposeRuntimeWithoutHost() {
    SessionRegistry registry;
    registry.MarkSpawnSuspended(1456658,
                                43u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.ReleaseSpawnResponseBoundary(1456658));

    const AgentReadyForwardDecision decision =
        registry.EvaluateAgentReadyForwarding(1456658, true, true, false);

    assert(decision.action == AgentReadyForwardAction::kExposeRuntimeWithoutHost);
    assert(decision.can_expose_runtime_ready_immediately);
}

void TestDeriveAgentReadyContextComputesRuntimeFieldsFromRegistry() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456659);
    registry.MarkSpawnSuspended(1456659,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target",
                                "spawn-token-derived");
    registry.RegisterAgentSession(1456659, agent.get());
    registry.RegisterAgentProcessName(1456659, "com.demo.target");

    const DerivedAgentReadyContext derived =
        registry.DeriveAgentReadyContext(1456659,
                                         "spawn-token-derived",
                                         "com.demo.target",
                                         AgentReadyStage::kRuntime,
                                         agent.get());

    assert(derived.runtime_ready);
    assert(derived.host == host.get());
    assert(derived.expected_spawn_process_name == "com.demo.target");
    assert(derived.runtime_spawn_process_name_matches);
    assert(!derived.mismatched_runtime_trace_for_spawn_target);
    assert(!derived.runtime_already_recorded);
    assert(derived.current_agent_session_matches);
    assert(!derived.stale_control_after_runtime);
}

void TestDeriveAgentReadyContextComputesStaleControlAfterRuntime() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto stale_control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());
    assert(stale_control_agent->Start());

    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456660);
    registry.MarkSpawnSuspended(1456660,
                                host->GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target",
                                "spawn-token-derived-stale");
    registry.RegisterAgentSession(1456660, runtime_agent.get());
    registry.RegisterAgentProcessName(1456660, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(1456660);
    registry.MarkAgentReadyStage(1456660, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(1456660);
    registry.RegisterControlReadyAgentSession(1456660, control_agent.get());

    const DerivedAgentReadyContext derived =
        registry.DeriveAgentReadyContext(1456660,
                                         "spawn-token-derived-stale",
                                         "com.demo.target",
                                         AgentReadyStage::kControl,
                                         stale_control_agent.get());

    assert(!derived.runtime_ready);
    assert(derived.runtime_already_recorded);
    assert(!derived.current_agent_session_matches);
    assert(derived.stale_control_after_runtime);
}

void TestResolvePostFinalizeSpawnContextPrefersSuspendedRuntimeBoundary() {
    SessionRegistry registry;
    registry.MarkSpawnSuspended(1456661,
                                44u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.runtime",
                                "com.demo.target");

    PendingSpawnEntry fallback;
    fallback.process_name = "com.demo.fallback";
    fallback.ready_stage = PendingSpawnReadyStage::kControlReady;
    fallback.resolved_process_name = "zygote64";

    const PostFinalizeSpawnContext context =
        registry.ResolvePostFinalizeSpawnContext(1456661,
                                                 fallback.ready_stage,
                                                 fallback);

    assert(context.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(context.runtime_process_name == "com.demo.runtime");
}

void TestResolvePostFinalizeSpawnContextFallsBackWhenSuspendedEntryMissing() {
    SessionRegistry registry;

    PendingSpawnEntry fallback;
    fallback.process_name = "com.demo.fallback";
    fallback.ready_stage = PendingSpawnReadyStage::kControlReady;
    fallback.resolved_process_name = "zygote64";

    const PostFinalizeSpawnContext context =
        registry.ResolvePostFinalizeSpawnContext(1456662,
                                                 fallback.ready_stage,
                                                 fallback);

    assert(context.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(context.runtime_process_name == "com.demo.fallback");
}

void TestEvaluateLatePromotionEligibilityReturnsEligibleForControlReadyOwnedSpawn() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(1456663,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(1456663, SpawnTransactionState::kWaitingRuntimeReady);

    const LatePromotionEvaluation evaluation =
        registry.EvaluateLatePromotionEligibility(1456663,
                                                  host->GetId(),
                                                  "__embedded_agent__");

    assert(evaluation.eligibility == LatePromotionEligibility::kEligible);
    assert(evaluation.has_entry);
    assert(evaluation.entry.host_session_id == host->GetId());
}

void TestEvaluateLatePromotionEligibilityRejectsRuntimeReadySpawn() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(1456664,
                                host->GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    const LatePromotionEvaluation evaluation =
        registry.EvaluateLatePromotionEligibility(1456664,
                                                  host->GetId(),
                                                  "__embedded_agent__");

    assert(evaluation.eligibility == LatePromotionEligibility::kNotControlReady);
}

void TestEvaluateLatePromotionEligibilityRejectsMismatchedOwnerOrState() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(1456665,
                                999u,
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(1456665, SpawnTransactionState::kReadyForScriptLoad);

    const LatePromotionEvaluation evaluation =
        registry.EvaluateLatePromotionEligibility(1456665,
                                                  host->GetId(),
                                                  "__embedded_agent__");

    assert(evaluation.eligibility == LatePromotionEligibility::kSpawnEntryMismatch);
}

void TestRecheckLatePromotionBeforeInjectDetectsRuntimeReadyAndTransactionChanges() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());

    registry.MarkSpawnSuspended(1456666,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.RegisterAgentSession(1456666, runtime_agent.get());
    registry.RegisterAgentProcessName(1456666, "com.demo.target");
    registry.MarkAgentReadyStage(1456666, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(1456666);

    assert(registry.RecheckLatePromotionBeforeInject(1456666,
                                                     host->GetId()) ==
           LatePromotionRecheckResult::kRuntimeReadyBeforeInject);

    registry.MarkSpawnSuspended(1456667,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnSuspendedAuthoritativeReady(1456667,
                                                    PendingSpawnReadyStage::kRuntimeReady,
                                                    "com.demo.target");
    assert(registry.RecheckLatePromotionBeforeInject(1456667,
                                                     host->GetId()) ==
           LatePromotionRecheckResult::kTransactionChanged);
}

void TestCleanupTimedOutSpawnTransactionClearsPendingAndPidState() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-timeout-cleanup",
                                  "com.demo.target",
                                  host->GetId());
    registry.RegisterAgentSession(1456668, agent.get());
    registry.RegisterAgentProcessName(1456668, "com.demo.target");
    assert(registry.ResolvePendingSpawn("spawn-token-timeout-cleanup",
                                        1456668,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    registry.CleanupTimedOutSpawnTransaction("spawn-token-timeout-cleanup", 1456668);

    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn("spawn-token-timeout-cleanup", &pending));
    assert(registry.FindAgentSessionByPid(1456668) == nullptr);
}

void TestCleanupFailedBoundSpawnTransactionClearsBoundState() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456669);
    registry.MarkSpawnSuspended(1456669, host->GetId());
    registry.RegisterPendingSpawn("spawn-token-failed-cleanup",
                                  "com.demo.target",
                                  host->GetId());
    registry.RegisterAgentSession(1456669, agent.get());
    registry.RegisterAgentProcessName(1456669, "com.demo.target");

    registry.CleanupFailedBoundSpawnTransaction(host->GetId(),
                                                1456669,
                                                "spawn-token-failed-cleanup",
                                                true);

    assert(registry.FindPidByHostSession(host->GetId()) < 0);
    assert(registry.FindAgentSessionByPid(1456669) == nullptr);
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn("spawn-token-failed-cleanup", &pending));
}

void TestConsumeOrClearPendingSpawnConsumesWhenPresentAndClearsOtherwise() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-consume-cleanup",
                                  "com.demo.target",
                                  host->GetId());

    PendingSpawnEntry consumed;
    registry.ConsumeOrClearPendingSpawn("spawn-token-consume-cleanup", &consumed);
    assert(consumed.spawn_token == "spawn-token-consume-cleanup");
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn("spawn-token-consume-cleanup", &pending));

    registry.RegisterPendingSpawn("spawn-token-clear-cleanup",
                                  "com.demo.other",
                                  host->GetId());
    registry.ConsumeOrClearPendingSpawn("spawn-token-clear-cleanup", nullptr);
    assert(!registry.GetPendingSpawn("spawn-token-clear-cleanup", &pending));
}

void TestCleanupDroppedSuccessfulSpawnResponseClearsSpawnTransaction() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456670);
    registry.MarkSpawnSuspended(1456670, host->GetId());

    registry.CleanupDroppedSuccessfulSpawnResponse(1456670);

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(1456670, &entry));
}

void TestBindSuspendedSpawnAfterFinalizeUsesHeldStateAndReportsRuntimeWait() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());

    PendingSpawnEntry resolved_pending_spawn;
    resolved_pending_spawn.process_name = "com.demo.target";

    PostFinalizeSpawnContext post_finalize_context;
    post_finalize_context.ready_stage = PendingSpawnReadyStage::kControlReady;
    post_finalize_context.runtime_process_name = "com.demo.target";

    const BindSuspendedSpawnAfterFinalizeResult result =
        registry.BindSuspendedSpawnAfterFinalize(1456671,
                                                 host->GetId(),
                                                 post_finalize_context,
                                                 resolved_pending_spawn,
                                                 "spawn-token-bind-finalize");

    assert(result.bound);
    assert(result.waiting_runtime_ready);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456671, &entry));
    assert(entry.response_pending);
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

void TestPrepareResolvedPendingSpawnHandoffBindsAndConsumesForRegisteredHost() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-handoff-bind",
                                  "com.demo.target",
                                  host->GetId());
    assert(registry.ResolvePendingSpawn("spawn-token-handoff-bind",
                                        1456672,
                                        "com.demo.target",
                                        AgentReadyStage::kRuntime));

    const ResolvedPendingSpawnHandoffResult result =
        registry.PrepareResolvedPendingSpawnHandoff("spawn-token-handoff-bind",
                                                    1456672,
                                                    host->GetId(),
                                                    host.get());

    assert(result.disposition ==
           ResolvedPendingSpawnHandoffDisposition::kBoundToRegisteredHost);
    assert(result.pending_spawn.pid == 1456672);
    assert(result.pending_spawn.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(registry.FindPidByHostSession(host->GetId()) == 1456672);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456672, &entry));
    assert(entry.suspended);
    assert(entry.response_pending);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);

    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn("spawn-token-handoff-bind", &pending));
}

void TestPrepareResolvedPendingSpawnHandoffConsumesWithoutBindingForMissingHost() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto foreign_host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-handoff-consume",
                                  "com.demo.target",
                                  host->GetId());
    assert(registry.ResolvePendingSpawn("spawn-token-handoff-consume",
                                        1456673,
                                        "com.demo.target",
                                        AgentReadyStage::kControl));

    const ResolvedPendingSpawnHandoffResult result =
        registry.PrepareResolvedPendingSpawnHandoff("spawn-token-handoff-consume",
                                                    1456673,
                                                    host->GetId(),
                                                    foreign_host.get());

    assert(result.disposition ==
           ResolvedPendingSpawnHandoffDisposition::kConsumedWithoutRegisteredHost);
    assert(result.pending_spawn.pid == 1456673);
    assert(registry.FindPidByHostSession(host->GetId()) < 0);

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(1456673, &entry));
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn("spawn-token-handoff-consume", &pending));
}

void TestIsRegisteredHostSessionRequiresCurrentRegisteredPointer() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto other = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());

    assert(registry.IsRegisteredHostSession(host->GetId(), host.get()));
    assert(!registry.IsRegisteredHostSession(host->GetId(), other.get()));
    registry.RemoveHostSession(host->GetId());
    assert(!registry.IsRegisteredHostSession(host->GetId(), host.get()));
}

void TestReleaseSpawnResponseBoundaryAndResolvePostFinalizeTracksRuntimePromotion() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());

    PendingSpawnEntry resolved_pending_spawn;
    resolved_pending_spawn.process_name = "com.demo.target";

    PostFinalizeSpawnContext post_finalize_context;
    post_finalize_context.ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    post_finalize_context.runtime_process_name = "com.demo.target";

    const BindSuspendedSpawnAfterFinalizeResult bind_result =
        registry.BindSuspendedSpawnAfterFinalize(1456672,
                                                 host->GetId(),
                                                 post_finalize_context,
                                                 resolved_pending_spawn,
                                                 "spawn-token-release-finalize");
    assert(bind_result.bound);

    PostFinalizeSpawnContext released_context;
    assert(registry.ReleaseSpawnResponseBoundaryAndResolvePostFinalize(
        1456672,
        post_finalize_context.ready_stage,
        resolved_pending_spawn,
        &released_context));
    assert(released_context.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(released_context.runtime_process_name == "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456672, &entry));
    assert(!entry.response_pending);
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestCommitSpawnResponseSuccessPromotesRuntimeReadyAndRequestsReplay() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());

    PendingSpawnEntry resolved_pending_spawn;
    resolved_pending_spawn.process_name = "com.demo.target";
    resolved_pending_spawn.ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    resolved_pending_spawn.resolved_process_name = "com.demo.target";

    const BindSuspendedSpawnAfterFinalizeResult bind_result =
        registry.BindSuspendedSpawnAfterFinalize(1456674,
                                                 host->GetId(),
                                                 PostFinalizeSpawnContext{
                                                     PendingSpawnReadyStage::kRuntimeReady,
                                                     "com.demo.target",
                                                 },
                                                 resolved_pending_spawn,
                                                 "spawn-token-commit-runtime");
    assert(bind_result.bound);

    const SpawnResponseCommitResult commit_result =
        registry.CommitSpawnResponseSuccess(1456674,
                                            PendingSpawnReadyStage::kRuntimeReady,
                                            resolved_pending_spawn);

    assert(commit_result.released_boundary);
    assert(commit_result.should_replay_runtime_ready);
    assert(commit_result.context.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(commit_result.context.runtime_process_name == "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456674, &entry));
    assert(!entry.response_pending);
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestCommitSpawnResponseSuccessKeepsControlReadyHeldWithoutReplay() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());

    PendingSpawnEntry resolved_pending_spawn;
    resolved_pending_spawn.process_name = "com.demo.target";
    resolved_pending_spawn.ready_stage = PendingSpawnReadyStage::kControlReady;
    resolved_pending_spawn.resolved_process_name = "com.demo.target";

    const BindSuspendedSpawnAfterFinalizeResult bind_result =
        registry.BindSuspendedSpawnAfterFinalize(1456675,
                                                 host->GetId(),
                                                 PostFinalizeSpawnContext{
                                                     PendingSpawnReadyStage::kControlReady,
                                                     "com.demo.target",
                                                 },
                                                 resolved_pending_spawn,
                                                 "spawn-token-commit-control");
    assert(bind_result.bound);

    const SpawnResponseCommitResult commit_result =
        registry.CommitSpawnResponseSuccess(1456675,
                                            PendingSpawnReadyStage::kControlReady,
                                            resolved_pending_spawn);

    assert(commit_result.released_boundary);
    assert(!commit_result.should_replay_runtime_ready);
    assert(commit_result.context.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(commit_result.context.runtime_process_name == "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456675, &entry));
    assert(!entry.response_pending);
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestCommitRuntimeReadyVisibilityPromotesRuntimeReadySpawn() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(1456676,
                                host->GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.SetSpawnResponsePending(1456676, false));

    const RuntimeReadyCommitResult result =
        registry.CommitRuntimeReadyVisibility(1456676);

    assert(result.runtime_visible);
    assert(result.should_replay_script_messages);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(1456676, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestResolveHostBoundAgentRequestTargetUsesRuntimeOwnedSpawnAgent() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456677);
    registry.MarkSpawnSuspended(1456677,
                                host->GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(1456677, SpawnTransactionState::kReadyForScriptLoad);
    registry.RegisterAgentSession(1456677, runtime_agent.get());
    registry.RegisterAgentProcessName(1456677, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(1456677);
    registry.MarkAgentReadyStage(1456677, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(1456677);

    const HostBoundAgentRequestTarget target =
        registry.ResolveHostBoundAgentRequestTarget(host->GetId());

    assert(target.error == HostBoundAgentLookupError::kNone);
    assert(target.pid == 1456677);
    assert(target.agent == runtime_agent.get());
}

void TestResolveHostBoundAgentRequestTargetBlocksSpawnWhileRuntimeNotReady() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 1456678);
    registry.MarkSpawnSuspended(1456678,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(1456678, SpawnTransactionState::kWaitingRuntimeReady);

    const HostBoundAgentRequestTarget target =
        registry.ResolveHostBoundAgentRequestTarget(host->GetId());

    assert(target.pid == 1456678);
    assert(target.agent == nullptr);
    assert(target.error == HostBoundAgentLookupError::kSpawnNotReady);
}

void TestDoesRuntimeSpawnProcessNameMatchFollowsCurrentSpawnSemantics() {
    SessionRegistry registry;

    assert(registry.DoesRuntimeSpawnProcessNameMatch(false, "com.demo.target", "com.demo.other"));
    assert(registry.DoesRuntimeSpawnProcessNameMatch(true, "", "com.demo.other"));
    assert(registry.DoesRuntimeSpawnProcessNameMatch(true, "com.demo.target", ""));
    assert(registry.DoesRuntimeSpawnProcessNameMatch(true, "com.demo.target", "com.demo.target"));
    assert(!registry.DoesRuntimeSpawnProcessNameMatch(true, "com.demo.target", "com.demo.other"));
}

void TestHasRuntimeRecordedForSpawnUsesBoundaryAndAcceptedTraceRules() {
    SessionRegistry registry;
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14567, runtime_agent.get());
    registry.RegisterAgentProcessName(14567, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14567);
    registry.MarkAgentReadyStage(14567, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14567);

    SpawnSuspendedEntry entry;
    entry.pid = 14567;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.HasRuntimeRecordedForSpawn(14567,
                                               entry,
                                               "com.demo.target",
                                               "com.demo.target",
                                               false,
                                               true));

    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    assert(registry.HasRuntimeRecordedForSpawn(14567,
                                               entry,
                                               "com.demo.target",
                                               "com.demo.target",
                                               false,
                                               true));
}

void TestShouldDropLateControlAgentReadyAtRuntimeBoundaryRequiresControlStage() {
    SessionRegistry registry;

    SpawnSuspendedEntry entry;
    entry.pid = 14568;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.ShouldDropLateControlAgentReadyAtRuntimeBoundary(14568,
                                                                     entry,
                                                                     "com.demo.target",
                                                                     false));
    assert(!registry.ShouldDropLateControlAgentReadyAtRuntimeBoundary(14568,
                                                                      entry,
                                                                      "com.demo.target",
                                                                      true));
}

void TestShouldDropLateControlAgentReadyFromNonCurrentSessionRequiresRecordedRuntime() {
    SessionRegistry registry;
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());
    assert(control_agent->Start());

    registry.RegisterAgentSession(14569, runtime_agent.get());
    registry.RegisterAgentProcessName(14569, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14569);
    registry.MarkAgentReadyStage(14569, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14569);

    SpawnSuspendedEntry entry;
    entry.pid = 14569;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    entry.authoritative_process_name = "zygote64";
    entry.target_process_name = "com.demo.target";

    assert(registry.ShouldDropLateControlAgentReadyFromNonCurrentSession(
        14569,
        entry,
        "com.demo.target",
        "com.demo.target",
        false,
        true,
        false));
    assert(!registry.ShouldDropLateControlAgentReadyFromNonCurrentSession(
        14569,
        entry,
        "com.demo.target",
        "com.demo.target",
        false,
        true,
        true));
}

void TestPlanAgentReadyRegistrationForMatchingRuntimeReady() {
    SessionRegistry registry;
    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(true,
                                            true,
                                            false,
                                            true,
                                            true);

    assert(plan.register_runtime_globally);
    assert(!plan.remove_runtime_session_for_mismatch);
    assert(!plan.register_control_globally);
    assert(!plan.register_control_identity);
    assert(plan.clear_pending_attach);
    assert(plan.upgrade_spawn_authoritative_ready);
    assert(plan.spawn_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(plan.resolve_pending_spawn);
    assert(plan.eligible_runtime_ready);
}

void TestPlanAgentReadyRegistrationForMismatchedRuntimeReady() {
    SessionRegistry registry;
    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(true,
                                            false,
                                            false,
                                            true,
                                            true);

    assert(!plan.register_runtime_globally);
    assert(plan.remove_runtime_session_for_mismatch);
    assert(!plan.register_control_globally);
    assert(!plan.register_control_identity);
    assert(!plan.clear_pending_attach);
    assert(!plan.upgrade_spawn_authoritative_ready);
    assert(plan.spawn_ready_stage == PendingSpawnReadyStage::kNone);
    assert(!plan.resolve_pending_spawn);
    assert(!plan.eligible_runtime_ready);
}

void TestPlanAgentReadyRegistrationForControlReadyBeforeRuntimeRecorded() {
    SessionRegistry registry;
    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(false,
                                            true,
                                            false,
                                            false,
                                            true);

    assert(!plan.register_runtime_globally);
    assert(!plan.remove_runtime_session_for_mismatch);
    assert(plan.register_control_globally);
    assert(plan.register_control_identity);
    assert(!plan.clear_pending_attach);
    assert(plan.upgrade_spawn_authoritative_ready);
    assert(plan.spawn_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(plan.resolve_pending_spawn);
    assert(!plan.eligible_runtime_ready);
}

void TestPlanAgentReadyRegistrationForLateControlAfterRuntimeRecorded() {
    SessionRegistry registry;
    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(false,
                                            true,
                                            true,
                                            false,
                                            true);

    assert(!plan.register_runtime_globally);
    assert(!plan.remove_runtime_session_for_mismatch);
    assert(!plan.register_control_globally);
    assert(plan.register_control_identity);
    assert(!plan.clear_pending_attach);
    assert(plan.upgrade_spawn_authoritative_ready);
    assert(plan.spawn_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(plan.resolve_pending_spawn);
    assert(!plan.eligible_runtime_ready);
}

void TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReady() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingSpawn("spawn-token-14570",
                                  "com.demo.target",
                                  host->GetId());
    registry.RegisterPendingAttach("spawn-token-14570",
                                   14570,
                                   "com.demo.target",
                                   host->GetId());
    registry.MarkSpawnSuspended(14570,
                                host->GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target",
                                "spawn-token-14570");

    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(true,
                                            true,
                                            false,
                                            true,
                                            true);

    AgentReady ready;
    ready.pid = 14570u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-14570";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    const AgentReadyRegistrationResult result =
        registry.ApplyAgentReadyRegistrationPlan(0,
                                                 14570,
                                                 ready.spawn_token,
                                                 ready.process_name,
                                                 ready.stage,
                                                 Frame(MessageType::kAgentReady,
                                                       14570u,
                                                       EncodeAgentReady(ready)),
                                                 agent.get(),
                                                 plan);

    assert(!result.removed_runtime_session_for_mismatch);
    assert(result.cleared_pending_attach);
    assert(result.upgraded_spawn_authoritative_ready);
    assert(result.resolved_pending_spawn);
    assert(registry.FindAgentSessionByPid(14570) == agent.get());
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == agent.get());
    assert(registry.IsAgentAuthoritativeReady(14570));
    assert(registry.IsAgentRuntimeReady(14570));
    assert(registry.FindPidByHostSession(host->GetId()) == 14570);
    assert(registry.FindHostSessionByPid(14570) == host.get());

    AgentReadyStage stage = AgentReadyStage::kControl;
    assert(registry.GetAgentReadyStage(14570, &stage));
    assert(stage == AgentReadyStage::kRuntime);

    PendingAttachEntry pending_attach;
    assert(!registry.GetPendingAttach("spawn-token-14570", &pending_attach));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(14570, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.authoritative_process_name == "com.demo.target");

    PendingSpawnEntry pending_spawn;
    assert(registry.GetPendingSpawn("spawn-token-14570", &pending_spawn));
    assert(pending_spawn.ready);
    assert(pending_spawn.pid == 14570);
    assert(pending_spawn.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(pending_spawn.resolved_process_name == "com.demo.target");

    Frame cached_ready;
    assert(registry.GetAgentReadyFrameByIdentity(14570, "com.demo.target", &cached_ready));
}

void TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReadyClearsPreviousPidBinding() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterAgentSession(14569, agent.get());
    registry.RegisterAgentProcessName(14569, "com.demo.target");

    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(true,
                                            true,
                                            false,
                                            false,
                                            false);

    AgentReady ready;
    ready.pid = 14570u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    const AgentReadyRegistrationResult result =
        registry.ApplyAgentReadyRegistrationPlan(14569,
                                                 14570,
                                                 "",
                                                 ready.process_name,
                                                 ready.stage,
                                                 Frame(MessageType::kAgentReady,
                                                       14570u,
                                                       EncodeAgentReady(ready)),
                                                 agent.get(),
                                                 plan);

    assert(result.removed_previous_pid_session);
    assert(!result.removed_runtime_session_for_mismatch);
    assert(registry.FindAgentSessionByPid(14570) == agent.get());
    assert(registry.FindAgentSessionByPid(14569) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == agent.get());
}

void TestApplyAgentReadyRegistrationPlanForRuntimeMismatchRemovesSessionOnly() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(14571, agent.get());
    registry.RegisterAgentProcessName(14571, "com.demo.other");

    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(true,
                                            false,
                                            false,
                                            true,
                                            true);

    AgentReady ready;
    ready.pid = 14571u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-14571";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    const AgentReadyRegistrationResult result =
        registry.ApplyAgentReadyRegistrationPlan(0,
                                                 14571,
                                                 ready.spawn_token,
                                                 ready.process_name,
                                                 ready.stage,
                                                 Frame(MessageType::kAgentReady,
                                                       14571u,
                                                       EncodeAgentReady(ready)),
                                                 agent.get(),
                                                 plan);

    assert(result.removed_runtime_session_for_mismatch);
    assert(!result.cleared_pending_attach);
    assert(!result.upgraded_spawn_authoritative_ready);
    assert(registry.FindAgentSessionByPid(14571) == nullptr);
    assert(!registry.IsAgentRuntimeReady(14571));
}

void TestApplyAgentReadyRegistrationPlanForControlReadyRegistersControlIdentity() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterHostSession(host.get());
    registry.MarkSpawnSuspended(14572,
                                host->GetId(),
                                PendingSpawnReadyStage::kNone,
                                "",
                                "com.demo.target",
                                "spawn-token-14572");

    const AgentReadyRegistrationPlan plan =
        registry.PlanAgentReadyRegistration(false,
                                            true,
                                            false,
                                            false,
                                            true);

    AgentReady ready;
    ready.pid = 14572u;
    ready.process_name = "zygote64";
    ready.spawn_token = "spawn-token-14572";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    const AgentReadyRegistrationResult result =
        registry.ApplyAgentReadyRegistrationPlan(0,
                                                 14572,
                                                 ready.spawn_token,
                                                 ready.process_name,
                                                 ready.stage,
                                                 Frame(MessageType::kAgentReady,
                                                       14572u,
                                                       EncodeAgentReady(ready)),
                                                 agent.get(),
                                                 plan);

    assert(!result.removed_runtime_session_for_mismatch);
    assert(!result.cleared_pending_attach);
    assert(result.upgraded_spawn_authoritative_ready);
    assert(registry.FindAgentSessionByPid(14572) == agent.get());
    assert(registry.FindControlReadyAgentSessionByPid(14572) == agent.get());
    assert(registry.IsAgentAuthoritativeReady(14572));
    assert(!registry.IsAgentRuntimeReady(14572));

    AgentReadyStage stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(14572, &stage));
    assert(stage == AgentReadyStage::kControl);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(14572, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "zygote64");
}

void TestWaitForAgentSessionDisconnectByIdentityReturnsAfterPidRemoval() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    std::thread remover([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        registry.RemoveAgentSessionByPid(14535);
    });

    assert(registry.WaitForAgentSessionDisconnectByIdentity(14535, "zygote64", 1000));
    remover.join();
}

void TestWaitForAgentSessionDisconnectByIdentityTimesOutWhileStillConnected() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    assert(!registry.WaitForAgentSessionDisconnectByIdentity(14535, "zygote64", 50));
}

void TestWaitForAgentSessionDisconnectByIdentityDoesNotIgnoreSupersededSamePidSession() {
    SessionRegistry registry;
    auto old_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto new_agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, old_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.RegisterAgentSession(14535, new_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, new_agent.get()));
    assert(!registry.WaitForAgentSessionDisconnectByIdentity(14535, "zygote64", 50));

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, old_agent.get()));
    assert(registry.WaitForAgentSessionDisconnectByIdentity(14535, "zygote64", 50));
}

void TestWaitForAgentSessionDisconnectByIdentityExitsPromptlyOnShutdown() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    std::atomic<bool> completed{false};
    std::atomic<bool> result{true};
    std::thread waiter([&]() {
        result.store(registry.WaitForAgentSessionDisconnectByIdentity(14535, "zygote64", 5000),
                     std::memory_order_release);
        completed.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    registry.Shutdown();
    waiter.join();

    assert(completed.load(std::memory_order_acquire));
    assert(!result.load(std::memory_order_acquire));
}

void TestOwnedZygoteControlProcessLifecycleIsExplicit() {
    SessionRegistry registry;

    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));

    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");
    assert(registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(24535, "zygote64"));
    assert(!registry.IsOwnedZygoteControlProcess("zygote"));

    registry.ClearOwnedZygoteControlProcess("zygote64");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestOwnedZygoteControlTargetDoesNotLeakAcrossPidReuse() {
    SessionRegistry registry;

    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(25969, "zygote64"));

    registry.MarkOwnedZygoteControlProcess(25969, "zygote64");
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(25969, "zygote64"));
}

void TestOwnedZygoteControlTargetClearsWhenAgentSessionIsRemoved() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(agent->Start());

    registry.RegisterAgentSession(14535, agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, agent.get()));

    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestFindControlReadyAgentSessionByIdentityRejectsSupersededSameNamePid() {
    SessionRegistry registry;

    auto old_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto new_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(old_agent->Start());
    assert(new_agent->Start());

    registry.RegisterAgentSession(14535, old_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    registry.RegisterAgentSession(25969, new_agent.get());
    registry.RegisterAgentProcessName(25969, "zygote64");
    registry.MarkAgentAuthoritativeReady(25969);
    registry.MarkAgentReadyStage(25969, AgentReadyStage::kControl);

    assert(registry.FindControlReadyAgentSessionByProcessName("zygote64") == new_agent.get());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "zygote64") == nullptr);
    assert(registry.FindControlReadyAgentSessionByIdentity(25969, "zygote64") == new_agent.get());
}

void TestControlReadySessionRemainsPinnedAfterRuntimeSessionRebind() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14535, control_agent.get());
    registry.RegisterControlReadyAgentSession(14535, control_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    assert(registry.FindControlReadyAgentSessionByPid(14535) == control_agent.get());
    assert(registry.FindAgentSessionByPid(14535) == control_agent.get());

    registry.RegisterAgentSession(14535, runtime_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);

    assert(registry.FindControlReadyAgentSessionByPid(14535) == control_agent.get());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "com.demo.target") == control_agent.get());
    assert(registry.FindAgentSessionByPid(14535) == runtime_agent.get());
}

void TestFindRuntimeReadyAgentSessionForSpawnUsesResolvedRuntimeIdentity() {
    SessionRegistry registry;

    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14536, runtime_agent.get());
    registry.RegisterAgentProcessName(14536, "com.demo.runtime");
    registry.MarkAgentAuthoritativeReady(14536);
    registry.MarkAgentReadyStage(14536, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14536);

    SpawnSuspendedEntry entry;
    entry.pid = 14536;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.runtime";
    entry.target_process_name = "com.demo.target";

    assert(registry.FindRuntimeReadyAgentSessionForSpawn(14536, entry) == runtime_agent.get());

    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    entry.authoritative_process_name = "zygote64";
    assert(registry.FindRuntimeReadyAgentSessionForSpawn(14536, entry) == nullptr);
}

void TestFindRuntimeReadyAgentSessionForSpawnReturnsNullAfterRuntimeDisconnect() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14537, control_agent.get());
    registry.RegisterControlReadyAgentSession(14537, control_agent.get());
    registry.RegisterAgentProcessName(14537, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14537);
    registry.MarkAgentReadyStage(14537, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14537, runtime_agent.get());
    registry.RegisterAgentProcessName(14537, "com.demo.target");
    registry.MarkAgentReadyStage(14537, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14537);

    SpawnSuspendedEntry entry;
    entry.pid = 14537;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.FindRuntimeReadyAgentSessionForSpawn(14537, entry) == runtime_agent.get());
    assert(registry.RemoveAgentSessionByPidIfMatches(14537, runtime_agent.get()));
    assert(registry.FindRuntimeReadyAgentSessionForSpawn(14537, entry) == nullptr);
}

void TestFindControlReadyAgentSessionForSpawnUsesKnownControlIdentity() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());

    registry.RegisterAgentSession(14538, control_agent.get());
    registry.RegisterControlReadyAgentSession(14538, control_agent.get());
    registry.RegisterAgentProcessName(14538, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14538);
    registry.MarkAgentReadyStage(14538, AgentReadyStage::kControl);

    SpawnSuspendedEntry entry;
    entry.pid = 14538;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    entry.authoritative_process_name = "zygote64";
    entry.target_process_name = "com.demo.target";

    assert(registry.FindControlReadyAgentSessionForSpawn(14538, entry, "") == control_agent.get());
    assert(registry.FindControlReadyAgentSessionForSpawn(14538, entry, "com.demo.target") == control_agent.get());
    assert(registry.FindControlReadyAgentSessionForSpawn(14538, entry, "com.demo.other") == nullptr);
}

void TestSpawnHostResponseAcceptsOnlyResolvedRuntimeAgent() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14539, control_agent.get());
    registry.RegisterControlReadyAgentSession(14539, control_agent.get());
    registry.RegisterAgentProcessName(14539, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14539);
    registry.MarkAgentReadyStage(14539, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14539, runtime_agent.get());
    registry.RegisterAgentProcessName(14539, "com.demo.target");
    registry.MarkAgentReadyStage(14539, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14539);

    SpawnSuspendedEntry entry;
    entry.pid = 14539;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.IsAcceptedHostResponseAgentSessionForSpawn(14539, entry, runtime_agent.get()));
    assert(!registry.IsAcceptedHostResponseAgentSessionForSpawn(14539, entry, control_agent.get()));

    assert(registry.RemoveAgentSessionByPidIfMatches(14539, runtime_agent.get()));
    assert(!registry.IsAcceptedHostResponseAgentSessionForSpawn(14539, entry, control_agent.get()));
}

void TestSpawnScriptMessageAcceptanceFallsBackToControlAfterRuntimeDisconnect() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14540, control_agent.get());
    registry.RegisterControlReadyAgentSession(14540, control_agent.get());
    registry.RegisterAgentProcessName(14540, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14540);
    registry.MarkAgentReadyStage(14540, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14540, runtime_agent.get());
    registry.RegisterAgentProcessName(14540, "com.demo.target");
    registry.MarkAgentReadyStage(14540, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14540);

    SpawnSuspendedEntry entry;
    entry.pid = 14540;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";
    entry.state = SpawnTransactionState::kReadyForScriptLoad;

    assert(registry.IsAcceptedScriptMessageAgentSessionForSpawn(14540, entry, runtime_agent.get()));
    assert(!registry.IsAcceptedScriptMessageAgentSessionForSpawn(14540, entry, control_agent.get()));

    assert(registry.RemoveAgentSessionByPidIfMatches(14540, runtime_agent.get()));
    assert(registry.IsAcceptedScriptMessageAgentSessionForSpawn(14540, entry, control_agent.get()));
}

void TestFindHostRequestAgentSessionForSpawnUsesRuntimeWhenAvailable() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14541, control_agent.get());
    registry.RegisterControlReadyAgentSession(14541, control_agent.get());
    registry.RegisterAgentProcessName(14541, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14541);
    registry.MarkAgentReadyStage(14541, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14541, runtime_agent.get());
    registry.RegisterAgentProcessName(14541, "com.demo.target");
    registry.MarkAgentReadyStage(14541, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14541);

    SpawnSuspendedEntry entry;
    entry.pid = 14541;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";
    entry.state = SpawnTransactionState::kReadyForScriptLoad;

    assert(registry.FindHostRequestAgentSessionForSpawn(14541, entry) == runtime_agent.get());
}

void TestFindHostRequestAgentSessionForSpawnBlocksAfterRuntimeDisconnect() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14542, control_agent.get());
    registry.RegisterControlReadyAgentSession(14542, control_agent.get());
    registry.RegisterAgentProcessName(14542, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14542);
    registry.MarkAgentReadyStage(14542, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14542, runtime_agent.get());
    registry.RegisterAgentProcessName(14542, "com.demo.target");
    registry.MarkAgentReadyStage(14542, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14542);

    SpawnSuspendedEntry entry;
    entry.pid = 14542;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";
    entry.state = SpawnTransactionState::kReadyForScriptLoad;

    assert(registry.FindHostRequestAgentSessionForSpawn(14542, entry) == runtime_agent.get());
    assert(registry.RemoveAgentSessionByPidIfMatches(14542, runtime_agent.get()));
    assert(registry.FindHostRequestAgentSessionForSpawn(14542, entry) == nullptr);
}

void TestHasAuthoritativeRuntimeBoundaryForSpawnRequiresRuntimeStageMatch() {
    SessionRegistry registry;

    SpawnSuspendedEntry entry;
    entry.pid = 14543;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.HasAuthoritativeRuntimeBoundaryForSpawn(14543, entry, ""));
    assert(registry.HasAuthoritativeRuntimeBoundaryForSpawn(14543, entry, "com.demo.target"));
    assert(!registry.HasAuthoritativeRuntimeBoundaryForSpawn(14543, entry, "com.demo.other"));

    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    assert(!registry.HasAuthoritativeRuntimeBoundaryForSpawn(14543, entry, "com.demo.target"));

    entry.suspended = false;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    assert(!registry.HasAuthoritativeRuntimeBoundaryForSpawn(14543, entry, "com.demo.target"));
}

void TestHasMismatchedRuntimeTraceForSpawnDetectsForeignRuntimeIdentity() {
    SessionRegistry registry;

    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14544, runtime_agent.get());
    registry.RegisterAgentProcessName(14544, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(14544);
    registry.MarkAgentReadyStage(14544, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14544);

    SpawnSuspendedEntry entry;
    entry.pid = 14544;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    entry.authoritative_process_name = "com.demo.target";
    entry.target_process_name = "com.demo.target";

    assert(registry.HasMismatchedRuntimeTraceForSpawn(14544,
                                                      entry,
                                                      "com.demo.target",
                                                      ""));
    assert(!registry.HasMismatchedRuntimeTraceForSpawn(14544,
                                                       entry,
                                                       "",
                                                       "com.demo.other"));
    assert(registry.HasMismatchedRuntimeTraceForSpawn(14544,
                                                      entry,
                                                      "",
                                                      "com.demo.target"));

    registry.ClearAgentRuntimeReadyState(14544);
    assert(!registry.HasMismatchedRuntimeTraceForSpawn(14544,
                                                       entry,
                                                       "com.demo.target",
                                                       ""));
}

void TestHasAcceptedRuntimeTraceForSpawnRequiresMatchingRuntimeIdentity() {
    SessionRegistry registry;

    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14545, runtime_agent.get());
    registry.RegisterAgentProcessName(14545, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14545);
    registry.MarkAgentReadyStage(14545, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14545);

    assert(registry.HasAcceptedRuntimeTraceForSpawn(14545, "com.demo.target"));
    assert(!registry.HasAcceptedRuntimeTraceForSpawn(14545, "com.demo.other"));
    assert(registry.HasAcceptedRuntimeTraceForSpawn(14545, ""));

    registry.ClearAgentRuntimeReadyState(14545);
    assert(!registry.HasAcceptedRuntimeTraceForSpawn(14545, "com.demo.target"));
}

void TestResetMismatchedRuntimeTraceForSpawnClearsRuntimeReadyAndForcesControlStage() {
    SessionRegistry registry;

    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(145451, runtime_agent.get());
    registry.RegisterAgentProcessName(145451, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(145451);
    registry.MarkAgentReadyStage(145451, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(145451);

    AgentReadyStage stage = AgentReadyStage::kControl;
    assert(registry.GetAgentReadyStage(145451, &stage));
    assert(stage == AgentReadyStage::kRuntime);
    assert(registry.IsAgentRuntimeReady(145451));

    assert(registry.ResetMismatchedRuntimeTraceForSpawn(145451));

    assert(!registry.IsAgentRuntimeReady(145451));
    assert(registry.GetAgentReadyStage(145451, &stage));
    assert(stage == AgentReadyStage::kControl);
}

void TestHasKnownSpawnControlIdentityMismatchRejectsUnexpectedName() {
    SessionRegistry registry;

    SpawnSuspendedEntry entry;
    entry.pid = 14546;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    entry.authoritative_process_name = "zygote64";
    entry.target_process_name = "com.demo.target";

    assert(!registry.HasKnownSpawnControlIdentityMismatchForSpawn(14546, entry, ""));
    assert(!registry.HasKnownSpawnControlIdentityMismatchForSpawn(14546,
                                                                  entry,
                                                                  "zygote64"));
    assert(!registry.HasKnownSpawnControlIdentityMismatchForSpawn(14546,
                                                                  entry,
                                                                  "com.demo.target"));
    assert(registry.HasKnownSpawnControlIdentityMismatchForSpawn(14546,
                                                                 entry,
                                                                 "com.demo.other"));

    entry.suspended = false;
    assert(!registry.HasKnownSpawnControlIdentityMismatchForSpawn(14546,
                                                                  entry,
                                                                  "com.demo.other"));
}

void TestIsAcceptedCurrentAgentSessionForPidTracksCurrentInvalidatedAndTimeoutStates() {
    SessionRegistry registry;

    auto current_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto other_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(current_agent->Start());
    assert(other_agent->Start());

    registry.RegisterAgentSession(14547, current_agent.get());
    assert(registry.IsAcceptedCurrentAgentSessionForPid(14547, current_agent.get()));
    assert(!registry.IsAcceptedCurrentAgentSessionForPid(14547, other_agent.get()));

    registry.RemoveAgentSessionByPid(14547);
    assert(registry.IsAcceptedCurrentAgentSessionForPid(14547, other_agent.get()));

    registry.MarkAttachTimeoutPid(14547);
    assert(!registry.IsAcceptedCurrentAgentSessionForPid(14547, other_agent.get()));

    registry.ClearAttachTimeoutPid(14547);
    assert(registry.IsAcceptedCurrentAgentSessionForPid(14547, other_agent.get()));
}

void TestIsAcceptedControlStageAgentSessionForSpawnUsesControlIdentityAndFallback() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14548, control_agent.get());
    registry.RegisterControlReadyAgentSession(14548, control_agent.get());
    registry.RegisterAgentProcessName(14548, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14548);
    registry.MarkAgentReadyStage(14548, AgentReadyStage::kControl);

    SpawnSuspendedEntry entry;
    entry.pid = 14548;
    entry.suspended = true;
    entry.authoritative_ready_stage = PendingSpawnReadyStage::kControlReady;
    entry.authoritative_process_name = "zygote64";
    entry.target_process_name = "com.demo.target";

    assert(registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                               entry,
                                                               "com.demo.target",
                                                               control_agent.get()));
    assert(registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                               entry,
                                                               "com.demo.other",
                                                               control_agent.get()));
    assert(!registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                                entry,
                                                                "com.demo.other",
                                                                runtime_agent.get()));

    registry.RegisterAgentSession(14548, runtime_agent.get());
    registry.RegisterAgentProcessName(14548, "com.demo.target");
    assert(!registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                                entry,
                                                                "",
                                                                control_agent.get()));
    assert(registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                               entry,
                                                               "",
                                                               runtime_agent.get()));

    entry.suspended = false;
    assert(registry.IsAcceptedControlStageAgentSessionForSpawn(14548,
                                                               entry,
                                                               "",
                                                               runtime_agent.get()));
}

void TestRuntimeSessionRemovalFallsBackToControlReadyState() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14535, control_agent.get());
    registry.RegisterControlReadyAgentSession(14535, control_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    AgentReady runtime_ready;
    runtime_ready.pid = 14535u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;

    registry.RegisterAgentSession(14535, runtime_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);
    registry.StoreAgentReadyFrame(14535,
                                  Frame(MessageType::kAgentReady, 28u, EncodeAgentReady(runtime_ready)));

    assert(registry.FindAgentSessionByPid(14535) == runtime_agent.get());
    assert(registry.IsAgentRuntimeReady(14535));

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, runtime_agent.get()));

    assert(registry.FindAgentSessionByPid(14535) == control_agent.get());
    assert(registry.FindControlReadyAgentSessionByPid(14535) == control_agent.get());
    assert(!registry.IsAgentRuntimeReady(14535));

    AgentReadyStage fallback_stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(14535, &fallback_stage));
    assert(fallback_stage == AgentReadyStage::kControl);

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(14535, &cached_ready));
}

void TestRuntimeSessionRemovalRestoresControlIdentityBinding() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(14535, control_agent.get());
    registry.RegisterControlReadyAgentSession(14535, control_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    registry.RegisterAgentSession(14535, runtime_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);

    assert(registry.FindAgentSessionByPid(14535) == runtime_agent.get());
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == runtime_agent.get());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "com.demo.target") == control_agent.get());

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, runtime_agent.get()));

    assert(registry.FindAgentSessionByPid(14535) == control_agent.get());
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(registry.FindAgentSessionByProcessName("zygote64") == control_agent.get());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "zygote64") == control_agent.get());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "com.demo.target") == nullptr);
}

void TestControlReadyLookupRejectsStoppedPinnedSession() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());

    registry.RegisterAgentSession(14535, control_agent.get());
    registry.RegisterControlReadyAgentSession(14535, control_agent.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    assert(registry.FindControlReadyAgentSessionByPid(14535) == control_agent.get());

    control_agent->Stop();

    assert(registry.FindControlReadyAgentSessionByPid(14535) == nullptr);
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "zygote64") == nullptr);
    assert(registry.WaitForControlReadyAgentSessionByIdentity(14535, "zygote64", 20) == nullptr);
}

void TestAttachTimeoutPidLifecycleIsExplicit() {
    SessionRegistry registry;

    assert(!registry.WasAttachTimeoutPid(200));

    registry.MarkAttachTimeoutPid(200);
    assert(registry.WasAttachTimeoutPid(200));

    registry.ClearAttachTimeoutPid(200);
    assert(!registry.WasAttachTimeoutPid(200));
}

void TestBindHostToPidDoesNotClearAttachTimeoutPid() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.MarkAttachTimeoutPid(7104);

    registry.BindHostToPid(host->GetId(), 7104);

    assert(registry.FindPidByHostSession(host->GetId()) == 7104);
    assert(registry.WasAttachTimeoutPid(7104));
}

void TestRegisterAgentSessionClearsAttachTimeoutPid() {
    SessionRegistry registry;

    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.MarkAttachTimeoutPid(7105);
    assert(registry.WasAttachTimeoutPid(7105));

    registry.RegisterAgentSession(7105, agent.get());

    assert(!registry.WasAttachTimeoutPid(7105));
    assert(registry.FindAgentSessionByPid(7105) == agent.get());
}

void TestWaitForRuntimeReadyAgentSessionByTokenExitsWhenPendingAttachClears() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-clear",
                                   7201,
                                   "com.demo.target",
                                   host->GetId());

    auto start = std::chrono::steady_clock::now();
    std::thread clearer([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        registry.ClearPendingAttach("attach-token-clear");
    });

    Session* resolved = registry.WaitForRuntimeReadyAgentSessionByToken(7201,
                                                                        "com.demo.target",
                                                                        "attach-token-clear",
                                                                        1000);
    clearer.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    assert(resolved == nullptr);
    assert(elapsed.count() < 300);
}

void TestRemoveAgentSessionByPidClearsPendingAttachForPid() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-agent-remove",
                                   7204,
                                   "com.demo.target",
                                   host->GetId());
    registry.RegisterAgentSession(7204, agent.get());
    registry.RegisterAgentProcessName(7204, "com.demo.target");

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-agent-remove", &pending));

    registry.RemoveAgentSessionByPid(7204);

    assert(!registry.GetPendingAttach("attach-token-agent-remove", &pending));
}

void TestRemoveAgentSessionByPidClearsSuspendedAndCachedMessageStateWithoutInvalidatingPid() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.BindHostToPid(host->GetId(), 7207);
    registry.MarkSpawnSuspended(7207, host->GetId());
    registry.RegisterAgentSession(7207, agent.get());
    registry.RegisterAgentProcessName(7207, "com.demo.remove-agent");

    ScriptMessage message;
    message.script_id = 9u;
    message.message = "{\"type\":\"send\",\"payload\":\"remove-agent\"}";
    registry.StoreScriptMessageFrame(7207,
                                     Frame(MessageType::kScriptMessage, 34u, EncodeScriptMessage(message)));

    assert(registry.IsSpawnSuspended(7207));
    assert(!registry.TakeScriptMessageFrames(7207).empty());

    registry.StoreScriptMessageFrame(7207,
                                     Frame(MessageType::kScriptMessage, 35u, EncodeScriptMessage(message)));

    registry.RemoveAgentSessionByPid(7207);

    assert(!registry.IsSpawnSuspended(7207));
    assert(registry.TakeScriptMessageFrames(7207).empty());
    assert(!registry.IsInvalidatedAgentPid(7207));
}

void TestRemoveAgentSessionByPidIfMatchesClearsAttachTimeoutPid() {
    SessionRegistry registry;
    auto agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.MarkAttachTimeoutPid(7205);
    assert(registry.WasAttachTimeoutPid(7205));
    registry.RegisterAgentSession(7205, agent.get());

    assert(registry.RemoveAgentSessionByPidIfMatches(7205, agent.get()));
    assert(!registry.WasAttachTimeoutPid(7205));
}

void TestRemoveAgentSessionByPidIfMatchesClearsAttachSideStateOnRebound() {
    SessionRegistry registry;
    auto host = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-rebound",
                                   7206,
                                   "com.demo.target",
                                   host->GetId());

    registry.RegisterAgentSession(7206, control_agent.get());
    registry.RegisterControlReadyAgentSession(7206, control_agent.get());
    registry.RegisterAgentProcessName(7206, "zygote64");
    registry.MarkAgentAuthoritativeReady(7206);
    registry.MarkAgentReadyStage(7206, AgentReadyStage::kControl);

    registry.RegisterAgentSession(7206, runtime_agent.get());
    registry.RegisterAgentProcessName(7206, "com.demo.target");
    registry.MarkAgentReadyStage(7206, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7206);
    registry.MarkAttachTimeoutPid(7206);

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-rebound", &pending));
    assert(registry.WasAttachTimeoutPid(7206));

    assert(registry.RemoveAgentSessionByPidIfMatches(7206, runtime_agent.get()));
    assert(registry.FindAgentSessionByPid(7206) == control_agent.get());
    assert(!registry.GetPendingAttach("attach-token-rebound", &pending));
    assert(!registry.WasAttachTimeoutPid(7206));
}

void TestDropAgentReadySessionIfMatchesMatchesExistingRemovalSemantics() {
    SessionRegistry registry;
    auto control_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<StubTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(7210, control_agent.get());
    registry.RegisterControlReadyAgentSession(7210, control_agent.get());
    registry.RegisterAgentProcessName(7210, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7210);
    registry.MarkAgentReadyStage(7210, AgentReadyStage::kControl);

    registry.RegisterAgentSession(7210, runtime_agent.get());
    registry.RegisterAgentProcessName(7210, "com.demo.target");
    registry.MarkAgentReadyStage(7210, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7210);

    assert(registry.DropAgentReadySessionIfMatches(7210, runtime_agent.get()));
    assert(registry.FindAgentSessionByPid(7210) == control_agent.get());
    assert(registry.FindControlReadyAgentSessionByPid(7210) == control_agent.get());
    assert(!registry.IsAgentRuntimeReady(7210));

    AgentReadyStage stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(7210, &stage));
    assert(stage == AgentReadyStage::kControl);
}

void TestUpdateSpawnStateDoesNotRegressAfterRuntimeReadyBoundary() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7801,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(registry.UpdateSpawnState(7801, SpawnTransactionState::kReadyForScriptLoad));
    assert(!registry.UpdateSpawnState(7801, SpawnTransactionState::kWaitingRuntimeReady));
    assert(!registry.UpdateSpawnState(7801, SpawnTransactionState::kWaitingAgentReady));
    assert(registry.UpdateSpawnState(7801, SpawnTransactionState::kScriptLoadDispatched));
    assert(registry.UpdateSpawnState(7801, SpawnTransactionState::kReadyForScriptLoad));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(7801, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestMarkSpawnScriptLoadInFlightAndCompleteFollowRoundTrip() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7802,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(registry.MarkSpawnRuntimeReadyVisible(7802));
    assert(registry.MarkSpawnScriptLoadInFlight(7802));

    SpawnSuspendedEntry in_flight;
    assert(registry.GetSpawnSuspendedEntry(7802, &in_flight));
    assert(in_flight.state == SpawnTransactionState::kScriptLoadDispatched);

    assert(registry.MarkSpawnScriptLoadComplete(7802));

    SpawnSuspendedEntry complete;
    assert(registry.GetSpawnSuspendedEntry(7802, &complete));
    assert(complete.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestReleaseSpawnResponseBoundaryPromotesRuntimeReadyStateAndReturnsUpdatedEntry() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7803,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.SetSpawnResponsePending(7803, true));

    SpawnSuspendedEntry released;
    assert(registry.ReleaseSpawnResponseBoundary(7803, &released));
    assert(released.response_pending == false);
    assert(released.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(released.authoritative_process_name == "com.demo.target");
    assert(released.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestReleaseSpawnResponseBoundaryKeepsControlReadyStateHeld() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(7804,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.SetSpawnResponsePending(7804, true));

    SpawnSuspendedEntry released;
    assert(registry.ReleaseSpawnResponseBoundary(7804, &released));
    assert(released.response_pending == false);
    assert(released.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(released.authoritative_process_name == "com.demo.target");
    assert(released.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestFindSuspendedSpawnOwnerHostSessionIdRequiresSuspendedEntry() {
    SessionRegistry registry;
    auto host_transport = std::make_unique<StubTransport>();
    Session host(std::move(host_transport));

    registry.RegisterHostSession(&host);
    assert(registry.FindSuspendedSpawnOwnerHostSessionId(7901) == 0);

    registry.MarkSpawnSuspended(7901,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.FindSuspendedSpawnOwnerHostSessionId(7901) == host.GetId());

    registry.ClearSpawnSuspended(7901);
    assert(registry.FindSuspendedSpawnOwnerHostSessionId(7901) == 0);
}

void TestIsForeignSuspendedSpawnOwnerUsesRecordedOwnerHost() {
    SessionRegistry registry;
    auto owner_transport = std::make_unique<StubTransport>();
    Session owner(std::move(owner_transport));
    auto other_transport = std::make_unique<StubTransport>();
    Session other(std::move(other_transport));

    registry.RegisterHostSession(&owner);
    registry.RegisterHostSession(&other);
    registry.MarkSpawnSuspended(7902,
                                owner.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(!registry.IsForeignSuspendedSpawnOwner(7902, owner.GetId()));
    assert(registry.IsForeignSuspendedSpawnOwner(7902, other.GetId()));
    assert(!registry.IsForeignSuspendedSpawnOwner(7902, 0));
    assert(!registry.IsForeignSuspendedSpawnOwner(7903, other.GetId()));
}

}  // namespace

int main() {
    TestRegistryFindsAgentSessionByProcessName();
    TestRegistryWaitsForAgentSessionByProcessName();
    TestRegistryProcessNameMappingFollowsLatestPid();
    TestRegistryConditionalAgentRemovalOnlyClearsMatchingSession();
    TestRegistryConditionalRemovalPreservesLatestProcessNameBinding();
    TestRemoveHostSessionClearsOwnedPendingSpawnEntries();
    TestRemoveHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind();
    TestRemoveHostSessionClearsOwnedPendingAttachEntries();
    TestRemoveHostSessionClearsOwnedAttachTimeoutPids();
    TestRemoveHostSessionClearsOwnedSpawnSuspendedEntriesAndCachedMessages();
    TestRemoveHostSessionClearsOwnedSuspendedAgentState();
    TestBindHostToPidClearsOldOwnedSpawnSuspendedEntriesAndCachedMessages();
    TestUnbindHostSessionClearsOwnedSpawnSuspendedEntriesAndCachedMessages();
    TestUnbindHostSessionClearsOwnedPendingAttachAndTimeoutState();
    TestUnbindHostSessionClearsResolvedPendingSpawnAgentStateBeforeBind();
    TestClearPendingSpawnClearsResolvedAgentStateBeforeBind();
    TestBindHostToResolvedPendingSpawnClearsOldOwnedSpawnSuspendedEntriesAndCachedMessages();
    TestBindHostToResolvedPendingSpawnUsesRuntimeReadyStateWhenAlreadyResolved();
    TestClearPendingSpawnPreservesResolvedPidAfterHostBind();
    TestMarkSpawnSuspendedUsesControlReadyPreRuntimeState();
    TestMarkSpawnSuspendedUsesRuntimeReadyHeldStateBeforeResponseRelease();
    TestClearingSpawnResponsePendingPromotesRuntimeReadySpawnToScriptLoadReady();
    TestMarkSpawnRuntimeReadyVisiblePromotesOnlyAfterResponseRelease();
    TestSpawnBlockedForScriptOperationsTracksHeldPreRuntimeStates();
    TestCanExposeSpawnRuntimeReadyImmediatelyRequiresReleasedRuntimeBoundary();
    TestClearSpawnTransactionByPidClearsOwnedAgentState();
    TestFindOwnedSpawnPidByHostSessionFallsBackWhenCoarseBindingIsLost();
    TestPendingSpawnResolutionStageUpgradesMonotonically();
    TestPendingSpawnRuntimeUpgradeRejectsDifferentPidAfterControlResolution();
    TestConditionalAgentRemovalClearsCachedReadyAndScriptMessages();
    TestPidReuseDoesNotLeakOldProcessNameBindingToNewSession();
    TestWaitForAgentSessionByIdentityIgnoresStaleProcessNameBindingForDifferentPid();
    TestWaitForAgentSessionByIdentityReturnsReboundProcessNameForTargetPid();
    TestWaitForAgentSessionByIdentityFallsBackToProcessNameWhenPidUnknown();
    TestWaitForAuthoritativeAgentSessionByIdentityRequiresAuthoritativeReadyFrame();
    TestWaitForControlReadyAgentSessionByIdentityRequiresRecordedReadyStage();
    TestWaitForRuntimeReadyAgentSessionByIdentityRequiresRuntimeStageAndIdentity();
    TestAuthoritativeReadyDoesNotRequireCachedRuntimeReadyFrame();
    TestRuntimeReadyDoesNotRequireCachedRuntimeReadyFrame();
    TestWaitForAgentRuntimeReadyDoesNotRequireCachedRuntimeReadyFrame();
    TestFindRuntimeReadyAgentSessionRequiresRuntimeStage();
    TestGetAgentReadyFrameByIdentityRequiresMatchingProcessName();
    TestPendingSpawnResolutionRejectsMismatchedProcessName();
    TestPendingSpawnResolutionRejectsMismatchedPidForAlreadyBoundHost();
    TestWaitForPendingSpawnExitsPromptlyOnShutdown();
    TestShutdownClearsPendingAttachAndAttachTimeoutState();
    TestResolveAgentReadySpawnContextPrefersPendingSpawnThenSuspendedEntry();
    TestResolveAgentReadySpawnContextFallsBackToSuspendedRuntimeIdentity();
    TestResolveAgentReadySpawnContextMatchesPendingAttachIdentity();
    TestResolveAgentReadySpawnContextDetectsSuspendedSpawnTokenMismatch();
    TestShouldDropStaleAttachAgentReadyRequiresUnownedPendingAttachConflict();
    TestShouldDropForeignAttachLikeAgentReadyRequiresSpawnTokenWithoutContext();
    TestShouldDropMismatchedSpawnTokenAgentReadyReflectsContextFlag();
    TestShouldDropOrphanAttachAgentReadyRequiresTimeoutUnboundAndUnowned();
    TestShouldDropInvalidatedUnownedAgentReadyRequiresInvalidatedPid();
    TestEvaluateAgentReadyEarlyDropReturnsOrphanSpawnTokenAndDropsSession();
    TestEvaluateAgentReadyEarlyDropReturnsMismatchedPendingAttachAndDropsSession();
    TestEvaluateAgentReadyEarlyDropReturnsStaleAttachAndDropsSession();
    TestEvaluateAgentReadyEarlyDropReturnsForeignAttachLikeAndDropsSession();
    TestEvaluateAgentReadyEarlyDropReturnsInvalidatedUnownedAndDropsSession();
    TestDoesRuntimeSpawnProcessNameMatchFollowsCurrentSpawnSemantics();
    TestHasRuntimeRecordedForSpawnUsesBoundaryAndAcceptedTraceRules();
    TestShouldDropLateControlAgentReadyAtRuntimeBoundaryRequiresControlStage();
    TestShouldDropLateControlAgentReadyFromNonCurrentSessionRequiresRecordedRuntime();
    TestPlanAgentReadyRegistrationForMatchingRuntimeReady();
    TestPlanAgentReadyRegistrationForMismatchedRuntimeReady();
    TestPlanAgentReadyRegistrationForControlReadyBeforeRuntimeRecorded();
    TestPlanAgentReadyRegistrationForLateControlAfterRuntimeRecorded();
    TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReady();
    TestApplyAgentReadyRegistrationPlanForMatchingRuntimeReadyClearsPreviousPidBinding();
    TestApplyAgentReadyRegistrationPlanForRuntimeMismatchRemovesSessionOnly();
    TestApplyAgentReadyRegistrationPlanForControlReadyRegistersControlIdentity();
    TestWaitForAgentSessionDisconnectByIdentityReturnsAfterPidRemoval();
    TestWaitForAgentSessionDisconnectByIdentityTimesOutWhileStillConnected();
    TestWaitForAgentSessionDisconnectByIdentityDoesNotIgnoreSupersededSamePidSession();
    TestWaitForAgentSessionDisconnectByIdentityExitsPromptlyOnShutdown();
    TestOwnedZygoteControlProcessLifecycleIsExplicit();
    TestOwnedZygoteControlTargetDoesNotLeakAcrossPidReuse();
    TestOwnedZygoteControlTargetClearsWhenAgentSessionIsRemoved();
    TestFindControlReadyAgentSessionByIdentityRejectsSupersededSameNamePid();
    TestControlReadySessionRemainsPinnedAfterRuntimeSessionRebind();
    TestFindRuntimeReadyAgentSessionForSpawnUsesResolvedRuntimeIdentity();
    TestFindRuntimeReadyAgentSessionForSpawnReturnsNullAfterRuntimeDisconnect();
    TestFindControlReadyAgentSessionForSpawnUsesKnownControlIdentity();
    TestSpawnHostResponseAcceptsOnlyResolvedRuntimeAgent();
    TestSpawnScriptMessageAcceptanceFallsBackToControlAfterRuntimeDisconnect();
    TestFindHostRequestAgentSessionForSpawnUsesRuntimeWhenAvailable();
    TestFindHostRequestAgentSessionForSpawnBlocksAfterRuntimeDisconnect();
    TestHasAuthoritativeRuntimeBoundaryForSpawnRequiresRuntimeStageMatch();
    TestHasMismatchedRuntimeTraceForSpawnDetectsForeignRuntimeIdentity();
    TestHasAcceptedRuntimeTraceForSpawnRequiresMatchingRuntimeIdentity();
    TestResetMismatchedRuntimeTraceForSpawnClearsRuntimeReadyAndForcesControlStage();
    TestHasKnownSpawnControlIdentityMismatchRejectsUnexpectedName();
    TestIsAcceptedCurrentAgentSessionForPidTracksCurrentInvalidatedAndTimeoutStates();
    TestIsAcceptedControlStageAgentSessionForSpawnUsesControlIdentityAndFallback();
    TestRuntimeSessionRemovalFallsBackToControlReadyState();
    TestRuntimeSessionRemovalRestoresControlIdentityBinding();
    TestControlReadyLookupRejectsStoppedPinnedSession();
    TestAttachTimeoutPidLifecycleIsExplicit();
    TestBindHostToPidDoesNotClearAttachTimeoutPid();
    TestRegisterAgentSessionClearsAttachTimeoutPid();
    TestWaitForRuntimeReadyAgentSessionByTokenExitsWhenPendingAttachClears();
    TestRemoveAgentSessionByPidClearsPendingAttachForPid();
    TestRemoveAgentSessionByPidClearsSuspendedAndCachedMessageStateWithoutInvalidatingPid();
    TestRemoveAgentSessionByPidIfMatchesClearsAttachTimeoutPid();
    TestRemoveAgentSessionByPidIfMatchesClearsAttachSideStateOnRebound();
    TestDropAgentReadySessionIfMatchesMatchesExistingRemovalSemantics();
    TestUpdateSpawnStateDoesNotRegressAfterRuntimeReadyBoundary();
    TestMarkSpawnScriptLoadInFlightAndCompleteFollowRoundTrip();
    TestPrepareResolvedPendingSpawnHandoffBindsAndConsumesForRegisteredHost();
    TestPrepareResolvedPendingSpawnHandoffConsumesWithoutBindingForMissingHost();
    TestCommitSpawnResponseSuccessPromotesRuntimeReadyAndRequestsReplay();
    TestCommitSpawnResponseSuccessKeepsControlReadyHeldWithoutReplay();
    TestCommitRuntimeReadyVisibilityPromotesRuntimeReadySpawn();
    TestResolveHostBoundAgentRequestTargetUsesRuntimeOwnedSpawnAgent();
    TestResolveHostBoundAgentRequestTargetBlocksSpawnWhileRuntimeNotReady();
    TestReleaseSpawnResponseBoundaryPromotesRuntimeReadyStateAndReturnsUpdatedEntry();
    TestReleaseSpawnResponseBoundaryKeepsControlReadyStateHeld();
    TestFindSuspendedSpawnOwnerHostSessionIdRequiresSuspendedEntry();
    TestIsForeignSuspendedSpawnOwnerUsesRecordedOwnerHost();
    return 0;
}
