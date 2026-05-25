#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/session_registry.h"
#include "server/zygote_control_rpc.h"

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

void RegisterControlReadySession(SessionRegistry* registry,
                                 int pid,
                                 const std::string& process_name,
                                 Session* session,
                                 uint32_t msg_id = 41u) {
    registry->RegisterAgentSession(pid, session);
    registry->RegisterAgentProcessName(pid, process_name);
    AgentReady ready;
    ready.pid = static_cast<uint32_t>(pid);
    ready.process_name = process_name;
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;
    registry->StoreAgentReadyFrame(pid,
                                   Frame(MessageType::kAgentReady, msg_id, EncodeAgentReady(ready)));
    registry->MarkAgentAuthoritativeReady(pid);
    registry->MarkAgentReadyStage(pid, ready.stage);
}

void TestWaitForZygoteControlDisconnectClearsOwnedTargetOnSessionRemoval() {
    SessionRegistry registry;
    auto zygote_session = std::make_unique<Session>(std::make_unique<StubTransport>());
    RegisterControlReadySession(&registry, 14535, "zygote64", zygote_session.get(), 88u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    assert(registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));

    std::thread remover([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        registry.RemoveAgentSessionByPid(14535);
    });

    std::string error_message;
    assert(WaitForZygoteControlDisconnectWithTimeoutForTest(&registry,
                                                            14535,
                                                            "zygote64",
                                                            1000,
                                                            &error_message));
    remover.join();

    assert(error_message.empty());
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

}  // namespace

int main() {
    TestWaitForZygoteControlDisconnectClearsOwnedTargetOnSessionRemoval();
}
