#include <cassert>
#include <memory>

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

void TestWaitForAuthoritativeAgentSessionPrefersRuntimeOverControl() {
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

    registry.RegisterAgentSession(14535, runtime_agent.get());
    registry.RegisterAgentProcessName(14535, "com.demo.target");
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);

    Session* resolved =
        registry.WaitForAuthoritativeAgentSessionByIdentity(14535, "com.demo.target", 50);
    assert(resolved == runtime_agent.get());

    runtime_agent->Stop();
    control_agent->Stop();
}

}  // namespace

int main() {
    TestWaitForAuthoritativeAgentSessionPrefersRuntimeOverControl();
    return 0;
}
