#include <cassert>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

class AliveStubTransport final : public Transport {
public:
    AliveStubTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }
    ssize_t Send(const uint8_t*, size_t len) override { return static_cast<ssize_t>(len); }
    ssize_t Recv(uint8_t*, size_t, int = -1) override {
        if (!IsConnected()) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return -1;
    }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "AliveStub"; }
};

struct RpcCall {
    int pid = -1;
    std::string process_name;
    uint32_t timeout_ms = 0;
    RpcRequest request;
};

class FakeRpcTransport {
public:
    std::vector<RpcCall> calls;
    std::vector<bool> ok_results;
    std::vector<RpcResponse> responses;
    std::vector<std::string> errors;
    size_t cursor = 0;

    bool Invoke(SessionRegistry*,
                int pid,
                const std::string& process_name,
                uint32_t timeout_ms,
                const RpcRequest& request,
                RpcResponse* response,
                std::string* error_message) {
        calls.push_back(RpcCall{pid, process_name, timeout_ms, request});
        const size_t index = cursor++;
        const bool ok = index < ok_results.size() ? ok_results[index] : false;
        if (response != nullptr && index < responses.size()) {
            *response = responses[index];
        }
        if (error_message != nullptr) {
            *error_message = index < errors.size() ? errors[index] : "";
        }
        return ok;
    }
};

struct SessionSendCall {
    Session* session = nullptr;
    uint32_t timeout_ms = 0;
    RpcRequest request;
};

class FakeSessionSender {
public:
    std::vector<SessionSendCall> calls;
    bool ok = true;
    RpcResponse response{0u, true, "{\"ok\":true}", {}};
    std::string error;

    bool Invoke(Session* session,
                uint32_t timeout_ms,
                const RpcRequest& request,
                RpcResponse* out_response,
                std::string* out_error) {
        calls.push_back(SessionSendCall{session, timeout_ms, request});
        if (out_response != nullptr) {
            *out_response = response;
        }
        if (out_error != nullptr) {
            *out_error = error;
        }
        return ok;
    }
};

struct SpawnInstallCall {
    Session* session = nullptr;
    uint32_t timeout_ms = 0;
    SpawnInstallRequest request;
};

class FakeSpawnInstallSender {
public:
    std::vector<SpawnInstallCall> calls;
    bool ok = true;
    SpawnInstallResponse response{true, {}};
    std::string error;

    bool Invoke(Session* session,
                uint32_t timeout_ms,
                const SpawnInstallRequest& request,
                SpawnInstallResponse* out_response,
                std::string* out_error) {
        calls.push_back(SpawnInstallCall{session, timeout_ms, request});
        if (out_response != nullptr) {
            *out_response = response;
        }
        if (out_error != nullptr) {
            *out_error = error;
        }
        return ok;
    }
};

struct SpawnUninstallCall {
    Session* session = nullptr;
    uint32_t timeout_ms = 0;
    SpawnUninstallRequest request;
};

class FakeSpawnUninstallSender {
public:
    std::vector<SpawnUninstallCall> calls;
    bool ok = true;
    SpawnUninstallResponse response{true, {}};
    std::string error;

    bool Invoke(Session* session,
                uint32_t timeout_ms,
                const SpawnUninstallRequest& request,
                SpawnUninstallResponse* out_response,
                std::string* out_error) {
        calls.push_back(SpawnUninstallCall{session, timeout_ms, request});
        if (out_response != nullptr) {
            *out_response = response;
        }
        if (out_error != nullptr) {
            *out_error = error;
        }
        return ok;
    }
};

void RegisterControlReadySession(SessionRegistry* registry,
                                 int pid,
                                 const std::string& process_name,
                                 Session* session,
                                 uint32_t msg_id = 41u) {
    registry->RegisterAgentSession(pid, session);
    registry->RegisterControlReadyAgentSession(pid, session);
    registry->RegisterAgentProcessName(pid, process_name);
    AgentReady ready;
    ready.pid = static_cast<uint32_t>(pid);
    ready.process_name = process_name;
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;
    registry->StoreAgentReadyFrame(pid, Frame(MessageType::kAgentReady, msg_id, EncodeAgentReady(ready)));
    registry->MarkAgentAuthoritativeReady(pid);
    registry->MarkAgentReadyStage(pid, ready.stage);
}

void RegisterLiveControlReadySession(SessionRegistry* registry,
                                     int pid,
                                     const std::string& process_name,
                                     Session* session,
                                     uint32_t msg_id = 41u) {
    assert(session != nullptr);
    assert(session->Start());
    RegisterControlReadySession(registry, pid, process_name, session, msg_id);
}

void TestFormatZygoteControlLogStateUsesStableShape() {
    assert(FormatZygoteControlLogState("ready-wait",
                                       "begin",
                                       14535,
                                       "zygote64",
                                       "nook.spawn.status",
                                       "soft",
                                       "") ==
           "zygote-control stage=ready-wait event=begin pid=14535 process=zygote64 method=nook.spawn.status class=soft");
}

void TestFormatZygoteControlLogStateIncludesErrorWhenPresent() {
    assert(FormatZygoteControlLogState("install",
                                       "rpc-fail",
                                       14535,
                                       "zygote64",
                                       "nook.spawn.installForkHook",
                                       "soft",
                                       "zygote control rpc timeout") ==
           "zygote-control stage=install event=rpc-fail pid=14535 process=zygote64 method=nook.spawn.installForkHook class=soft error=zygote control rpc timeout");
}

void TestFormatZygoteControlLogStateOmitsPidWhenUnknown() {
    assert(FormatZygoteControlLogState("uninstall",
                                       "rpc-begin",
                                       -1,
                                       "zygote64",
                                       "nook.spawn.uninstallForkHook",
                                       "hard",
                                       "") ==
           "zygote-control stage=uninstall event=rpc-begin pid=unknown process=zygote64 method=nook.spawn.uninstallForkHook class=hard");
}

void TestWaitForZygoteControlReadyAcceptsReadyStatus() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true};
    rpc.responses = {RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}}};
    rpc.errors = {""};

    SessionRegistry registry;
    std::string error_message;
    assert(WaitForZygoteControlReady(&registry,
                                     14535,
                                     "zygote64",
                                     &error_message,
                                     [&rpc](SessionRegistry* inner_registry,
                                            int pid,
                                            const std::string& process_name,
                                            uint32_t timeout_ms,
                                            const RpcRequest& request,
                                            RpcResponse* response,
                                            std::string* inner_error) {
                                         return rpc.Invoke(inner_registry,
                                                           pid,
                                                           process_name,
                                                           timeout_ms,
                                                           request,
                                                           response,
                                                           inner_error);
                                     }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.status");
    assert(rpc.calls[0].request.args_json == "{}");
    assert(rpc.calls[0].timeout_ms == 250u);
}

void TestWaitForZygoteControlReadyRetriesUntilReady() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":false,\"state\":\"booting\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}},
    };
    rpc.errors = {"", ""};

    SessionRegistry registry;
    std::string error_message;
    assert(WaitForZygoteControlReady(&registry,
                                     14535,
                                     "zygote64",
                                     &error_message,
                                     [&rpc](SessionRegistry* inner_registry,
                                            int pid,
                                            const std::string& process_name,
                                            uint32_t timeout_ms,
                                            const RpcRequest& request,
                                            RpcResponse* response,
                                            std::string* inner_error) {
                                         return rpc.Invoke(inner_registry,
                                                           pid,
                                                           process_name,
                                                           timeout_ms,
                                                           request,
                                                           response,
                                                           inner_error);
                                     }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 2u);
}

void TestWaitForZygoteControlReadyTimeoutIncludesLastRpcError() {
    FakeRpcTransport rpc;
    rpc.ok_results = {false, false, false};
    rpc.responses = {RpcResponse{}, RpcResponse{}, RpcResponse{}};
    rpc.errors = {
        "zygote control-ready agent session not found: zygote64",
        "zygote control rpc timeout",
        "zygote control rpc timeout",
    };

    SessionRegistry registry;
    std::string error_message;
    assert(!WaitForZygoteControlReadyWithTimeoutForTest(
        &registry,
        14535,
        "zygote64",
        60,
        5,
        1,
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry,
                              pid,
                              process_name,
                              timeout_ms,
                              request,
                              response,
                              inner_error);
        }));
    assert(error_message == "zygote control-ready wait timed out: zygote control status rpc failed");
    assert(rpc.calls.size() >= 1u);
}

void TestInstallZygoteForkHookWaitsForReadyThenInstalls() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"installed\":true}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", "", "", ""};

    SessionRegistry registry;
    std::string error_message;
    assert(InstallZygoteForkHook(&registry,
                                 14535,
                                 "zygote64",
                                 "__embedded_agent__",
                                 "com.demo.target",
                                 "spawn-token-7",
                                 &error_message,
                                 [&rpc](SessionRegistry* inner_registry,
                                        int pid,
                                        const std::string& process_name,
                                        uint32_t timeout_ms,
                                        const RpcRequest& request,
                                        RpcResponse* response,
                                        std::string* inner_error) {
                                     return rpc.Invoke(inner_registry,
                                                       pid,
                                                       process_name,
                                                       timeout_ms,
                                                       request,
                                                       response,
                                                       inner_error);
                                 }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 4u);
    assert(rpc.calls[1].request.method == "nook.spawn.status");
    assert(rpc.calls[2].request.method == "nook.spawn.installForkHook");
    assert(rpc.calls[2].request.args_json ==
           "{\"target_package\":\"com.demo.target\",\"spawn_token\":\"spawn-token-7\",\"mode\":\"stable\"}");
    assert(rpc.calls[2].timeout_ms == 5000u);
    assert(rpc.calls[3].request.method == "nook.spawn.status");
}

void TestInstallZygoteForkHookClearsResidualArmedStateBeforeInstall() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}},
        RpcResponse{0u, true, "{\"ok\":true}", {}},
        RpcResponse{0u, true, "{\"installed\":true}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", "", "", "", ""};

    SessionRegistry registry;
    std::string error_message;
    assert(InstallZygoteForkHook(&registry,
                                 14535,
                                 "zygote64",
                                 "__embedded_agent__",
                                 "com.demo.target",
                                 "spawn-token-7",
                                 &error_message,
                                 [&rpc](SessionRegistry* inner_registry,
                                        int pid,
                                        const std::string& process_name,
                                        uint32_t timeout_ms,
                                        const RpcRequest& request,
                                        RpcResponse* response,
                                        std::string* inner_error) {
                                     return rpc.Invoke(inner_registry,
                                                       pid,
                                                       process_name,
                                                       timeout_ms,
                                                       request,
                                                       response,
                                                       inner_error);
                                 }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 5u);
    assert(rpc.calls[0].request.method == "nook.spawn.status");
    assert(rpc.calls[1].request.method == "nook.spawn.status");
    assert(rpc.calls[2].request.method == "nook.spawn.clearForkHook");
    assert(rpc.calls[3].request.method == "nook.spawn.installForkHook");
    assert(rpc.calls[4].request.method == "nook.spawn.status");
}

void TestInstallZygoteForkHookWaitsForControlSessionDisconnectAfterInstall() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"installed\":true}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", "", "", ""};

    SessionRegistry registry;
    auto zygote_session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, zygote_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    std::thread remover([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        registry.RemoveAgentSessionByPid(14535);
    });

    std::string error_message;
    assert(InstallZygoteForkHook(&registry,
                                 14535,
                                 "zygote64",
                                 "__embedded_agent__",
                                 "com.demo.target",
                                 "spawn-token-7",
                                 &error_message,
                                 [&rpc](SessionRegistry* inner_registry,
                                        int pid,
                                        const std::string& process_name,
                                        uint32_t timeout_ms,
                                        const RpcRequest& request,
                                        RpcResponse* response,
                                        std::string* inner_error) {
                                     return rpc.Invoke(inner_registry,
                                                       pid,
                                                       process_name,
                                                       timeout_ms,
                                                       request,
                                                       response,
                                                       inner_error);
                                 }));
    remover.join();

    assert(error_message.empty());
    assert(rpc.calls.size() == 4u);
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestWaitForZygoteControlDisconnectFailsWhenSessionDoesNotClose() {
    SessionRegistry registry;
    auto zygote_session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, zygote_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kControl);

    std::string error_message;
    assert(!WaitForZygoteControlDisconnectWithTimeoutForTest(&registry,
                                                             14535,
                                                             "zygote64",
                                                             20,
                                                             &error_message));
    assert(error_message == "zygote control disconnect wait timed out");
}

void TestInstallZygoteForkHookStopsWhenReadyWaitFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {false, false};
    rpc.responses = {RpcResponse{}, RpcResponse{}};
    rpc.errors = {
        "zygote control rpc timeout",
        "zygote control rpc timeout",
    };

    SessionRegistry registry;
    std::string error_message;
    assert(!InstallZygoteForkHook(&registry,
                                  14535,
                                  "zygote64",
                                  "__embedded_agent__",
                                  "com.demo.target",
                                  "spawn-token-7",
                                  &error_message,
                                  [&rpc](SessionRegistry* inner_registry,
                                         int pid,
                                         const std::string& process_name,
                                         uint32_t timeout_ms,
                                         const RpcRequest& request,
                                         RpcResponse* response,
                                         std::string* inner_error) {
                                      return rpc.Invoke(inner_registry,
                                                        pid,
                                                        process_name,
                                                        timeout_ms,
                                                        request,
                                                        response,
                                                        inner_error);
                                  }));
    assert(error_message == "zygote control-ready wait timed out: zygote control status rpc failed");
    for (const RpcCall& call : rpc.calls) {
        assert(call.request.method != "nook.spawn.installForkHook");
    }
}

void TestInstallZygoteForkHookPrefersDedicatedSpawnInstallMessage() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", "", ""};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 61u);

    FakeSpawnInstallSender spawn_sender;
    std::string error_message;
    assert(InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-7",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(error_message.empty());
    assert(spawn_sender.calls.size() == 1u);
    assert(spawn_sender.calls[0].session == session.get());
    assert(spawn_sender.calls[0].timeout_ms == 5000u);
    assert(spawn_sender.calls[0].request.target_package == "com.demo.target");
    assert(spawn_sender.calls[0].request.spawn_token == "spawn-token-7");
    assert(spawn_sender.calls[0].request.mode == "stable");
    assert(rpc.calls.size() == 3u);
    assert(rpc.calls[0].request.method == "nook.spawn.status");
    assert(rpc.calls[1].request.method == "nook.spawn.status");
    assert(rpc.calls[2].request.method == "nook.spawn.status");
}

void TestInstallZygoteForkHookFallsBackToRpcWhenDedicatedInstallTimesOut() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"installed\":true}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", "", "", ""};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 62u);

    FakeSpawnInstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "zygote control rpc timeout";

    std::string error_message;
    assert(InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-7",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(error_message.empty());
    assert(spawn_sender.calls.size() == 1u);
    assert(rpc.calls.size() == 4u);
    assert(rpc.calls[2].request.method == "nook.spawn.installForkHook");
    assert(registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedOnDedicatedInstallHardFailure() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
    };
    rpc.errors = {"", ""};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 62u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");
    assert(session->IsAlive());
    assert(registry.FindControlReadyAgentSessionByIdentity(14535, "zygote64") == session.get());

    FakeSpawnInstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "spawn install hard failed";

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-hard-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.size() == 1u);
    assert(!error_message.empty());
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedWhenRpcFallbackInstallFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, false};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{},
    };
    rpc.errors = {"", "", "rpc fallback install failed"};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 63u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "zygote control rpc timeout";

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-rpc-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.size() == 1u);
    assert(error_message == "rpc fallback install failed");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedWhenReadyWaitFailsAfterFallbackInstall() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, false};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{},
    };
    rpc.errors = {"", "", "", "ready wait failed"};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 66u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "zygote control rpc timeout";

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-ready-wait-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.size() == 1u);
    assert(error_message == "zygote control-ready wait timed out: zygote control status rpc failed");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedWhenStatusRpcFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, false};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{},
    };
    rpc.errors = {"", "status rpc failed"};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 64u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-status-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.empty());
    assert(error_message == "status rpc failed");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedWhenClearRpcFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, false};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}},
        RpcResponse{},
    };
    rpc.errors = {"", "", "clear rpc failed"};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 65u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-clear-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.empty());
    assert(error_message == "clear rpc failed");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookClearsOwnedWhenInitialReadyWaitFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {false, false, false};
    rpc.responses = {RpcResponse{}, RpcResponse{}, RpcResponse{}};
    rpc.errors = {
        "initial ready wait failed",
        "initial ready wait failed",
        "initial ready wait failed",
    };

    SessionRegistry registry;
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-initial-ready-fail",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(spawn_sender.calls.empty());
    assert(!error_message.empty());
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestInstallZygoteForkHookDoesNotMarkOwnedWhenReadyWaitFails() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true, true, true, false};
    rpc.responses = {
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"ready\":true,\"state\":\"idle\"}", {}},
        RpcResponse{0u, true, "{\"installed\":true}", {}},
        RpcResponse{},
    };
    rpc.errors = {
        "",
        "",
        "",
        "zygote control status rpc failed",
    };

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 65u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnInstallSender spawn_sender;

    std::string error_message;
    assert(!InstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        "__embedded_agent__",
        "com.demo.target",
        "spawn-token-8",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnInstallRequest& request,
                        SpawnInstallResponse* response,
                        std::string* inner_error) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, inner_error);
        }));
    assert(error_message == "zygote control-ready wait timed out: zygote control status rpc failed");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

void TestUninstallZygoteForkHookDispatchesRpc() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true};
    rpc.responses = {RpcResponse{0u, true, "{\"uninstalled\":true}", {}}};
    rpc.errors = {""};

    SessionRegistry registry;
    std::string error_message;
    assert(UninstallZygoteForkHook(&registry,
                                   14535,
                                   "zygote64",
                                   &error_message,
                                   [&rpc](SessionRegistry* inner_registry,
                                          int pid,
                                          const std::string& process_name,
                                          uint32_t timeout_ms,
                                          const RpcRequest& request,
                                          RpcResponse* response,
                                          std::string* inner_error) {
                                       return rpc.Invoke(inner_registry,
                                                         pid,
                                                         process_name,
                                                         timeout_ms,
                                                         request,
                                                         response,
                                                         inner_error);
                                   }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.uninstallForkHook");
    assert(rpc.calls[0].request.args_json == "{}");
    assert(rpc.calls[0].timeout_ms == 5000u);
}

void TestUninstallZygoteForkHookPrefersDedicatedSpawnUninstallMessage() {
    FakeRpcTransport rpc;

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 63u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnUninstallSender spawn_sender;
    std::string error_message;
    assert(UninstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& request,
                        SpawnUninstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(error_message.empty());
    assert(spawn_sender.calls.size() == 1u);
    assert(spawn_sender.calls[0].session == session.get());
    assert(spawn_sender.calls[0].timeout_ms == 5000u);
    assert(spawn_sender.calls[0].request.spawn_token.empty());
    assert(rpc.calls.empty());
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
}

void TestUninstallZygoteForkHookFallsBackToRpcWhenDedicatedUninstallTimesOut() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true};
    rpc.responses = {RpcResponse{0u, true, "{\"uninstalled\":true}", {}}};
    rpc.errors = {""};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 64u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnUninstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "zygote control rpc timeout";

    std::string error_message;
    assert(UninstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& request,
                        SpawnUninstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(error_message.empty());
    assert(spawn_sender.calls.size() == 1u);
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.uninstallForkHook");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
}

void TestUninstallZygoteForkHookDoesNotUseDedicatedUninstallWithoutOwnedTarget() {
    FakeRpcTransport rpc;
    rpc.ok_results = {true};
    rpc.responses = {RpcResponse{0u, true, "{\"uninstalled\":true}", {}}};
    rpc.errors = {""};

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<StubTransport>());
    RegisterControlReadySession(&registry, 14535, "zygote64", session.get(), 67u);

    FakeSpawnUninstallSender spawn_sender;

    std::string error_message;
    assert(UninstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& request,
                        SpawnUninstallResponse* response,
                        std::string* error_message) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, error_message);
        }));
    assert(error_message.empty());
    assert(spawn_sender.calls.empty());
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.uninstallForkHook");
}

void TestUninstallZygoteForkHookSkipsRpcWaitWhenOwnedTargetHasNoImmediateSession() {
    SessionRegistry registry;
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeRpcTransport rpc;
    FakeSpawnUninstallSender spawn_sender;
    std::string error_message;

    const auto start = std::chrono::steady_clock::now();
    assert(!UninstallZygoteForkHookWithSendersForTest(
        &registry,
        14535,
        "zygote64",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry,
                              pid,
                              process_name,
                              timeout_ms,
                              request,
                              response,
                              inner_error);
        },
        [&spawn_sender](Session* session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& request,
                        SpawnUninstallResponse* response,
                        std::string* inner_error) {
            return spawn_sender.Invoke(session,
                                       timeout_ms,
                                       request,
                                       response,
                                       inner_error);
        }));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(spawn_sender.calls.empty());
    assert(rpc.calls.empty());
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
    assert(elapsed.count() < 500);
}

void TestSendSpawnUninstallToControlSessionRequiresOwnedTarget() {
    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", session.get(), 68u);

    FakeSpawnUninstallSender spawn_sender;
    SpawnUninstallRequest request;
    SpawnUninstallResponse response;
    std::string error_message;

    assert(!SendSpawnUninstallToControlSessionForTest(
        &registry,
        14535,
        "zygote64",
        5000,
        request,
        &response,
        &error_message,
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& inner_request,
                        SpawnUninstallResponse* inner_response,
                        std::string* inner_error) {
            return spawn_sender.Invoke(target_session,
                                       timeout_ms,
                                       inner_request,
                                       inner_response,
                                       inner_error);
        }));
    assert(error_message == "zygote control target is not explicitly owned");
    assert(spawn_sender.calls.empty());
}

void TestSendSpawnUninstallToControlSessionRejectsSupersededOwnedPid() {
    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 24535, "zygote64", session.get(), 69u);
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    FakeSpawnUninstallSender spawn_sender;
    SpawnUninstallRequest request;
    SpawnUninstallResponse response;
    std::string error_message;

    assert(!SendSpawnUninstallToControlSessionForTest(
        &registry,
        24535,
        "zygote64",
        5000,
        request,
        &response,
        &error_message,
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& inner_request,
                        SpawnUninstallResponse* inner_response,
                        std::string* inner_error) {
            return spawn_sender.Invoke(target_session,
                                       timeout_ms,
                                       inner_request,
                                       inner_response,
                                       inner_error);
        }));
    assert(error_message == "zygote control target is not explicitly owned");
    assert(spawn_sender.calls.empty());
}

void TestUninstallZygoteForkHookPropagatesRpcError() {
    FakeRpcTransport rpc;
    rpc.ok_results = {false};
    rpc.responses = {RpcResponse{}};
    rpc.errors = {"zygote control rpc timeout"};

    SessionRegistry registry;
    std::string error_message;
    assert(!UninstallZygoteForkHook(&registry,
                                    14535,
                                    "zygote64",
                                    &error_message,
                                    [&rpc](SessionRegistry* inner_registry,
                                           int pid,
                                           const std::string& process_name,
                                           uint32_t timeout_ms,
                                           const RpcRequest& request,
                                           RpcResponse* response,
                                           std::string* inner_error) {
                                        return rpc.Invoke(inner_registry,
                                                          pid,
                                                          process_name,
                                                          timeout_ms,
                                                          request,
                                                          response,
                                                          inner_error);
                                    }));
    assert(error_message == "zygote control rpc timeout");
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.uninstallForkHook");
}

void TestUninstallZygoteForkHookTreatsMissingUsapSessionAsSoftSkip() {
    FakeRpcTransport rpc;
    rpc.ok_results = {false};
    rpc.responses = {RpcResponse{}};
    rpc.errors = {"zygote control-ready agent session not found: usap32"};

    SessionRegistry registry;
    registry.MarkOwnedZygoteControlProcess(14536, "usap32");
    std::string error_message;
    assert(UninstallZygoteForkHook(&registry,
                                   -1,
                                   "usap32",
                                   &error_message,
                                   [&rpc](SessionRegistry* inner_registry,
                                          int pid,
                                          const std::string& process_name,
                                          uint32_t timeout_ms,
                                          const RpcRequest& request,
                                          RpcResponse* response,
                                          std::string* inner_error) {
                                       return rpc.Invoke(inner_registry,
                                                         pid,
                                                         process_name,
                                                         timeout_ms,
                                                         request,
                                                         response,
                                                         inner_error);
                                   }));
    assert(error_message.empty());
    assert(!registry.IsOwnedZygoteControlProcess("usap32"));
    assert(rpc.calls.empty());
}

void TestUninstallZygoteForkHookClearsOwnedOnDedicatedUsapSoftSkip() {
    FakeRpcTransport rpc;

    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14536, "usap32", session.get(), 66u);
    registry.MarkOwnedZygoteControlProcess(14536, "usap32");

    FakeSpawnUninstallSender spawn_sender;
    spawn_sender.ok = false;
    spawn_sender.error = "zygote control-ready agent session not found: usap32";

    std::string error_message;
    assert(UninstallZygoteForkHookWithSendersForTest(
        &registry,
        14536,
        "usap32",
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        },
        [&spawn_sender](Session* target_session,
                        uint32_t timeout_ms,
                        const SpawnUninstallRequest& request,
                        SpawnUninstallResponse* response,
                        std::string* inner_error) {
            return spawn_sender.Invoke(target_session, timeout_ms, request, response, inner_error);
        }));
    assert(error_message.empty());
    assert(!registry.IsOwnedZygoteControlProcess("usap32"));
}

void TestCallZygoteControlRpcUsesExactPidSessionWhenPresent() {
    SessionRegistry registry;
    auto pid_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto other_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", pid_session.get(), 41u);
    RegisterLiveControlReadySession(&registry, 24535, "zygote64-alt", other_session.get(), 42u);
    assert(registry.IsAgentAuthoritativeReady(14535));
    assert(registry.IsAgentControlReady(14535));
    assert(registry.FindControlReadyAgentSessionByPid(14535) == pid_session.get());

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message.empty());
    assert(sender.calls.size() == 1u);
    assert(sender.calls[0].session == pid_session.get());
    assert(sender.calls[0].timeout_ms == 123u);
}

void TestCallZygoteControlRpcPrefersControlReadySessionOverGenericRuntimeSession() {
    SessionRegistry registry;
    auto control_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto runtime_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", control_session.get(), 49u);

    registry.RegisterAgentSession(14535, runtime_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message.empty());
    assert(sender.calls.size() == 1u);
    assert(sender.calls[0].session == control_session.get());
}

void TestWaitForZygoteControlReadyAcceptsRuntimeAuthoritativeSessionWhenNoControlSessionExists() {
    SessionRegistry registry;
    auto runtime_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    assert(runtime_session->Start());
    registry.RegisterAgentSession(14535, runtime_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentRuntimeReady(14535);

    std::string error_message;
    FakeRpcTransport rpc;
    rpc.ok_results = {true};
    rpc.responses = {RpcResponse{0u, true, "{\"ready\":true,\"state\":\"armed\"}", {}}};
    rpc.errors = {""};

    assert(WaitForZygoteControlReadyWithTimeoutForTest(
        &registry,
        14535,
        "zygote64",
        10,
        1,
        1,
        &error_message,
        [&rpc](SessionRegistry* inner_registry,
               int pid,
               const std::string& process_name,
               uint32_t timeout_ms,
               const RpcRequest& request,
               RpcResponse* response,
               std::string* inner_error) {
            return rpc.Invoke(inner_registry, pid, process_name, timeout_ms, request, response, inner_error);
        }));
    assert(error_message.empty());
    assert(rpc.calls.size() == 1u);
    assert(rpc.calls[0].request.method == "nook.spawn.status");
}

void TestWaitForControlReadyAgentSessionByIdentityPrefersControlSessionOverRuntimeSession() {
    SessionRegistry registry;
    auto control_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto runtime_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", control_session.get(), 71u);

    registry.RegisterAgentSession(14535, runtime_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);
    registry.MarkAgentReadyStage(14535, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(14535);

    Session* resolved = registry.WaitForControlReadyAgentSessionByIdentity(14535, "zygote64", 10);
    assert(resolved == control_session.get());
}

void TestCallZygoteControlRpcUsesReboundProcessNameForTargetPid() {
    SessionRegistry registry;
    auto old_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto new_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", old_session.get(), 43u);

    std::thread producer([&registry, &new_session]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RegisterLiveControlReadySession(&registry, 25969, "zygote64", new_session.get(), 44u);
    });

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(CallZygoteControlRpcWithSenderForTest(
        &registry,
        25969,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    producer.join();
    assert(error_message.empty());
    assert(sender.calls.size() == 1u);
    assert(sender.calls[0].session == new_session.get());
}

void TestCallZygoteControlRpcRejectsStaleProcessNameForWrongPid() {
    SessionRegistry registry;
    auto old_session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, old_session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(!CallZygoteControlRpcWithSenderForTest(
        &registry,
        25969,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message == "zygote control-ready agent session not found pid=25969 process=zygote64");
    assert(sender.calls.empty());
}

void TestCallZygoteControlRpcDoesNotUseClosedSession() {
    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    assert(registry.RemoveAgentSessionByPidIfMatches(14535, session.get()));

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(!CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        50,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(sender.calls.empty());
}

void TestCallZygoteControlRpcUsesReconnectedSessionForSamePid() {
    SessionRegistry registry;
    auto old_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto new_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", old_session.get(), 45u);
    assert(registry.RemoveAgentSessionByPidIfMatches(14535, old_session.get()));

    std::thread producer([&registry, &new_session]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RegisterLiveControlReadySession(&registry, 14535, "zygote64", new_session.get(), 46u);
    });

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    producer.join();
    assert(error_message.empty());
    assert(sender.calls.size() == 1u);
    assert(sender.calls[0].session == new_session.get());
}

void TestCallZygoteControlRpcUsesLateProcessNameSessionWhenPidUnknown() {
    SessionRegistry registry;
    auto old_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    auto new_session = std::make_unique<Session>(std::make_unique<AliveStubTransport>());
    RegisterLiveControlReadySession(&registry, 14535, "zygote64", old_session.get(), 47u);
    assert(registry.RemoveAgentSessionByPidIfMatches(14535, old_session.get()));

    std::thread producer([&registry, &new_session]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RegisterLiveControlReadySession(&registry, 25969, "zygote64", new_session.get(), 48u);
    });

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(CallZygoteControlRpcWithSenderForTest(
        &registry,
        -1,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    producer.join();
    assert(error_message.empty());
    assert(sender.calls.size() == 1u);
    assert(sender.calls[0].session == new_session.get());
}

void TestCallZygoteControlRpcRequiresReadyFrameBeforeUsingSession() {
    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(!CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        123,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(sender.calls.empty());
}

void TestCallZygoteControlRpcRequiresControlReadySession() {
    SessionRegistry registry;
    auto session = std::make_unique<Session>(std::make_unique<StubTransport>());
    registry.RegisterAgentSession(14535, session.get());
    registry.RegisterAgentProcessName(14535, "zygote64");
    registry.MarkAgentAuthoritativeReady(14535);

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;
    assert(!CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        30,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(sender.calls.empty());
}

void TestCallZygoteControlRpcUsesBoundedSessionWaitBudget() {
    SessionRegistry registry;

    FakeSessionSender sender;
    RpcRequest request;
    request.method = "nook.spawn.status";
    request.args_json = "{}";
    RpcResponse response;
    std::string error_message;

    const auto start = std::chrono::steady_clock::now();
    assert(!CallZygoteControlRpcWithSenderForTest(
        &registry,
        14535,
        "zygote64",
        30,
        request,
        &response,
        &error_message,
        [&sender](Session* target_session,
                  uint32_t timeout_ms,
                  const RpcRequest& inner_request,
                  RpcResponse* inner_response,
                  std::string* inner_error) {
            return sender.Invoke(target_session, timeout_ms, inner_request, inner_response, inner_error);
        }));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(sender.calls.empty());
    assert(elapsed.count() < 500);
}

}  // namespace

int main() {
    TestFormatZygoteControlLogStateUsesStableShape();
    TestFormatZygoteControlLogStateIncludesErrorWhenPresent();
    TestFormatZygoteControlLogStateOmitsPidWhenUnknown();
    TestWaitForZygoteControlReadyAcceptsReadyStatus();
    TestWaitForZygoteControlReadyRetriesUntilReady();
    TestWaitForZygoteControlReadyTimeoutIncludesLastRpcError();
    TestInstallZygoteForkHookWaitsForReadyThenInstalls();
    TestInstallZygoteForkHookClearsResidualArmedStateBeforeInstall();
    TestInstallZygoteForkHookWaitsForControlSessionDisconnectAfterInstall();
    TestWaitForZygoteControlDisconnectFailsWhenSessionDoesNotClose();
    TestInstallZygoteForkHookStopsWhenReadyWaitFails();
    TestInstallZygoteForkHookPrefersDedicatedSpawnInstallMessage();
    TestInstallZygoteForkHookFallsBackToRpcWhenDedicatedInstallTimesOut();
    TestInstallZygoteForkHookClearsOwnedOnDedicatedInstallHardFailure();
    TestInstallZygoteForkHookClearsOwnedWhenRpcFallbackInstallFails();
    TestInstallZygoteForkHookClearsOwnedWhenReadyWaitFailsAfterFallbackInstall();
    TestInstallZygoteForkHookClearsOwnedWhenStatusRpcFails();
    TestInstallZygoteForkHookClearsOwnedWhenClearRpcFails();
    TestInstallZygoteForkHookClearsOwnedWhenInitialReadyWaitFails();
    TestInstallZygoteForkHookDoesNotMarkOwnedWhenReadyWaitFails();
    TestUninstallZygoteForkHookDispatchesRpc();
    TestUninstallZygoteForkHookPrefersDedicatedSpawnUninstallMessage();
    TestUninstallZygoteForkHookFallsBackToRpcWhenDedicatedUninstallTimesOut();
    TestUninstallZygoteForkHookDoesNotUseDedicatedUninstallWithoutOwnedTarget();
    TestUninstallZygoteForkHookSkipsRpcWaitWhenOwnedTargetHasNoImmediateSession();
    TestSendSpawnUninstallToControlSessionRequiresOwnedTarget();
    TestSendSpawnUninstallToControlSessionRejectsSupersededOwnedPid();
    TestUninstallZygoteForkHookPropagatesRpcError();
    TestUninstallZygoteForkHookTreatsMissingUsapSessionAsSoftSkip();
    TestUninstallZygoteForkHookClearsOwnedOnDedicatedUsapSoftSkip();
    TestCallZygoteControlRpcUsesExactPidSessionWhenPresent();
    TestCallZygoteControlRpcPrefersControlReadySessionOverGenericRuntimeSession();
    TestWaitForZygoteControlReadyAcceptsRuntimeAuthoritativeSessionWhenNoControlSessionExists();
    TestWaitForControlReadyAgentSessionByIdentityPrefersControlSessionOverRuntimeSession();
    TestCallZygoteControlRpcUsesReboundProcessNameForTargetPid();
    TestCallZygoteControlRpcRejectsStaleProcessNameForWrongPid();
    TestCallZygoteControlRpcDoesNotUseClosedSession();
    TestCallZygoteControlRpcUsesReconnectedSessionForSamePid();
    TestCallZygoteControlRpcUsesLateProcessNameSessionWhenPidUnknown();
    TestCallZygoteControlRpcRequiresReadyFrameBeforeUsingSession();
    TestCallZygoteControlRpcRequiresControlReadySession();
    TestCallZygoteControlRpcUsesBoundedSessionWaitBudget();
    return 0;
}
