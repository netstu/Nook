#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include "communication/handler/message_dispatcher.h"
#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/session_registry.h"

using namespace nook::comm;
using namespace nook::server;

namespace {

class NullTransport final : public Transport {
public:
    NullTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }
    ssize_t Send(const uint8_t*, size_t len) override { return static_cast<ssize_t>(len); }
    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Null"; }
};

void TestMessageDispatcherDispatchesRegisteredHandler() {
    MessageDispatcher dispatcher;
    Session session(std::make_unique<NullTransport>());

    bool called = false;
    dispatcher.RegisterHandler(MessageType::kSpawnRequest,
                               [&](Session& incoming_session, const Frame& frame) {
                                   called = true;
                                   assert(&incoming_session == &session);
                                   assert(frame.GetType() == MessageType::kSpawnRequest);
                                   assert(frame.GetMsgId() == 17u);
                               });

    Frame frame(MessageType::kSpawnRequest, 17u, {0xAA});
    assert(dispatcher.Dispatch(session, frame));
    assert(called);
}

void TestMessageDispatcherReturnsFalseForUnknownHandler() {
    MessageDispatcher dispatcher;
    Session session(std::make_unique<NullTransport>());
    Frame frame(MessageType::kPing, 1u, {});

    assert(!dispatcher.Dispatch(session, frame));
}

void TestSessionRegistryAssociatesHostAndAgentSessions() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<NullTransport>());

    Session* host_ptr = host.get();
    Session* agent_ptr = agent.get();

    registry.RegisterHostSession(host_ptr);
    registry.BindHostToPid(host_ptr->GetId(), 24680);
    registry.RegisterAgentSession(24680, agent_ptr);
    registry.RegisterAgentProcessName(24680, "zygote64");

    assert(registry.FindHostSession(host_ptr->GetId()) == host_ptr);
    assert(registry.FindHostSessionByPid(24680) == host_ptr);
    assert(registry.FindAgentSessionByPid(24680) == agent_ptr);
    assert(registry.FindAgentSessionByProcessName("zygote64") == agent_ptr);

    registry.RemoveHostSession(host_ptr->GetId());
    assert(registry.FindHostSession(host_ptr->GetId()) == nullptr);
    assert(registry.FindHostSessionByPid(24680) == nullptr);
    assert(registry.FindAgentSessionByPid(24680) == agent_ptr);

    registry.RemoveAgentSessionByPid(24680);
    assert(registry.FindAgentSessionByPid(24680) == nullptr);
    assert(registry.FindAgentSessionByProcessName("zygote64") == nullptr);
}

void TestSessionRegistryTracksSpawnStateLifecycle() {
    SessionRegistry registry;

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(24680, &entry));
    assert(!registry.IsSpawnSuspended(24680));

    registry.MarkSpawnSuspended(24680, 77u);
    assert(registry.IsSpawnSuspended(24680));
    assert(registry.GetSpawnSuspendedEntry(24680, &entry));
    assert(entry.pid == 24680);
    assert(entry.host_session_id == 77u);
    assert(entry.suspended);
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);

    assert(registry.UpdateSpawnState(24680, SpawnTransactionState::kReadyForScriptLoad));
    assert(registry.GetSpawnSuspendedEntry(24680, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);

    assert(registry.UpdateSpawnState(24680, SpawnTransactionState::kScriptLoadDispatched));
    assert(registry.GetSpawnSuspendedEntry(24680, &entry));
    assert(entry.state == SpawnTransactionState::kScriptLoadDispatched);

    registry.ClearSpawnSuspended(24680);
    assert(!registry.IsSpawnSuspended(24680));
    assert(!registry.GetSpawnSuspendedEntry(24680, &entry));
}

void TestSessionRegistryClosePathKeepsLatestAuthoritativeAgentBinding() {
    SessionRegistry registry;

    auto old_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto new_agent = std::make_unique<Session>(std::make_unique<NullTransport>());

    Session* old_agent_ptr = old_agent.get();
    Session* new_agent_ptr = new_agent.get();

    registry.RegisterAgentSession(14535, old_agent_ptr);
    registry.RegisterAgentProcessName(14535, "zygote64");

    registry.RegisterAgentSession(25969, new_agent_ptr);
    registry.RegisterAgentProcessName(25969, "zygote64");

    assert(registry.FindAgentSessionByProcessName("zygote64") == new_agent_ptr);
    assert(registry.FindAgentSessionByPid(25969) == new_agent_ptr);

    assert(registry.RemoveAgentSessionByPidIfMatches(14535, old_agent_ptr));
    assert(registry.FindAgentSessionByPid(14535) == nullptr);
    assert(registry.FindAgentSessionByPid(25969) == new_agent_ptr);
    assert(registry.FindAgentSessionByProcessName("zygote64") == new_agent_ptr);
}

void TestSessionRegistryTracksExplicitOwnedZygoteControlProcesses() {
    SessionRegistry registry;

    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");
    assert(registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(24535, "zygote64"));
    registry.ClearOwnedZygoteControlProcess("zygote64");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
}

void TestSessionRegistryAgentRemovalClearsAttachSideState() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<NullTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-components",
                                   24681,
                                   "com.demo.target",
                                   host->GetId());
    registry.RegisterAgentSession(24681, agent.get());
    registry.RegisterAgentProcessName(24681, "com.demo.target");
    registry.MarkAttachTimeoutPid(24681);

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-components", &pending));
    assert(registry.WasAttachTimeoutPid(24681));

    registry.RemoveAgentSessionByPid(24681);

    assert(!registry.GetPendingAttach("attach-token-components", &pending));
    assert(!registry.WasAttachTimeoutPid(24681));
}

void TestSessionRegistryRuntimeRemovalReboundClearsAttachSideState() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto control_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<NullTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-components-rebound",
                                   24682,
                                   "com.demo.target",
                                   host->GetId());

    registry.RegisterAgentSession(24682, control_agent.get());
    registry.RegisterControlReadyAgentSession(24682, control_agent.get());
    registry.RegisterAgentProcessName(24682, "zygote64");
    registry.MarkAgentAuthoritativeReady(24682);
    registry.MarkAgentReadyStage(24682, AgentReadyStage::kControl);

    registry.RegisterAgentSession(24682, runtime_agent.get());
    registry.RegisterAgentProcessName(24682, "com.demo.target");
    registry.MarkAgentReadyStage(24682, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(24682);
    registry.MarkAttachTimeoutPid(24682);

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-components-rebound", &pending));
    assert(registry.WasAttachTimeoutPid(24682));

    assert(registry.RemoveAgentSessionByPidIfMatches(24682, runtime_agent.get()));
    assert(registry.FindAgentSessionByPid(24682) == control_agent.get());
    assert(!registry.GetPendingAttach("attach-token-components-rebound", &pending));
    assert(!registry.WasAttachTimeoutPid(24682));
}

void TestSessionRegistryAgentClosePathUsesLatestPidBinding() {
    SessionRegistry registry;

    auto host = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto agent = std::make_unique<Session>(std::make_unique<NullTransport>());

    registry.RegisterHostSession(host.get());
    registry.RegisterPendingAttach("attach-token-components-latest-pid",
                                   24684,
                                   "com.demo.target",
                                   host->GetId());

    agent->SetPeerPid(24683);
    registry.RegisterAgentSession(24684, agent.get());
    registry.RegisterAgentProcessName(24684, "com.demo.target");
    registry.MarkAttachTimeoutPid(24684);
    agent->SetPeerPid(24684);

    PendingAttachEntry pending;
    assert(registry.GetPendingAttach("attach-token-components-latest-pid", &pending));
    assert(registry.WasAttachTimeoutPid(24684));
    assert(registry.FindAgentSessionByPid(24684) == agent.get());

    assert(registry.RemoveAgentSessionByPidIfMatches(agent->GetPeerPid(), agent.get()));

    assert(!registry.GetPendingAttach("attach-token-components-latest-pid", &pending));
    assert(!registry.WasAttachTimeoutPid(24684));
    assert(registry.FindAgentSessionByPid(24684) == nullptr);
}

}  // namespace

int main() {
    TestMessageDispatcherDispatchesRegisteredHandler();
    TestMessageDispatcherReturnsFalseForUnknownHandler();
    TestSessionRegistryAssociatesHostAndAgentSessions();
    TestSessionRegistryTracksSpawnStateLifecycle();
    TestSessionRegistryClosePathKeepsLatestAuthoritativeAgentBinding();
    TestSessionRegistryTracksExplicitOwnedZygoteControlProcesses();
    TestSessionRegistryAgentRemovalClearsAttachSideState();
    TestSessionRegistryRuntimeRemovalReboundClearsAttachSideState();
    TestSessionRegistryAgentClosePathUsesLatestPidBinding();
    return 0;
}
