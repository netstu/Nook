#include <cassert>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "communication/handler/message_dispatcher.h"
#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/injector.h"
#include "server/process_manager.h"
#include "server/server_handlers.h"
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

    std::vector<uint8_t> TakeSent() {
        std::vector<uint8_t> out = sent_;
        sent_.clear();
        return out;
    }

private:
    std::vector<uint8_t> sent_;
};

class FailAfterNSendsTransport final : public Transport {
public:
    explicit FailAfterNSendsTransport(size_t successful_sends_before_failure)
        : successful_sends_before_failure_(successful_sends_before_failure) {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }

    ssize_t Send(const uint8_t* data, size_t len) override {
        ++send_attempt_count_;
        if (send_success_count_ >= successful_sends_before_failure_) {
            return -1;
        }
        ++send_success_count_;
        sent_.insert(sent_.end(), data, data + len);
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "FailAfterNSends"; }

    std::vector<uint8_t> TakeSent() {
        std::vector<uint8_t> out = sent_;
        sent_.clear();
        return out;
    }

    size_t GetSendAttemptCount() const {
        return send_attempt_count_;
    }

    size_t GetSendSuccessCount() const {
        return send_success_count_;
    }

private:
    size_t successful_sends_before_failure_ = 0;
    size_t send_attempt_count_ = 0;
    size_t send_success_count_ = 0;
    std::vector<uint8_t> sent_;
};

class FakeInjector final : public Injector {
public:
    bool Spawn(const SpawnRequest& request,
               const std::string& agent_path,
               int* pid,
               std::string* error_message) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_request = request;
            last_agent_path = agent_path;
        }
        if (!spawn_ok) {
            if (error_message != nullptr) {
                *error_message = spawn_error;
            }
            return false;
        }
        if (pid != nullptr) {
            *pid = spawn_pid;
        }
        return true;
    }

    bool InjectAgent(int pid,
                     const std::string& agent_path,
                     const std::string& ready_token,
                     std::string* error_message) override {
        ++inject_call_count;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_inject_pid = pid;
            last_inject_agent_path = agent_path;
            last_inject_ready_token = ready_token;
        }
        if (inject_block_until_release) {
            while (!allow_inject_return.load()) {
                std::this_thread::yield();
            }
        }
        if (!inject_ok) {
            if (error_message != nullptr) {
                *error_message = inject_error;
            }
            ++inject_return_count;
            return false;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        ++inject_return_count;
        return true;
    }

    SpawnRequest GetLastRequest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_request;
    }

    int GetLastInjectPid() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_inject_pid;
    }

    std::string GetLastInjectAgentPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_inject_agent_path;
    }

    std::string GetLastInjectReadyToken() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_inject_ready_token;
    }

    std::string GetLastAgentPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_agent_path;
    }

    bool InjectSpawnChildAgent(int pid,
                               const std::string& agent_path,
                               std::string* error_message) override {
        ++inject_spawn_child_call_count;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_inject_pid = pid;
            last_inject_agent_path = agent_path;
        }
        if (!inject_ok) {
            if (error_message != nullptr) {
                *error_message = inject_error;
            }
            return false;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    bool FinalizeSpawn(const SpawnRequest& request,
                       std::string* error_message) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_finalize_request = request;
        }
        if (finalize_block_until_release) {
            while (!allow_finalize_return.load()) {
                std::this_thread::yield();
            }
        }
        if (!finalize_ok) {
            if (error_message != nullptr) {
                *error_message = finalize_error;
            }
            return false;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    bool spawn_ok = true;
    int spawn_pid = 0;
    std::string spawn_error = "spawn failed";
    bool finalize_ok = true;
    std::string finalize_error = "finalize failed";
    bool inject_ok = true;
    bool inject_block_until_release = false;
    std::atomic<bool> allow_inject_return{true};
    bool finalize_block_until_release = false;
    std::atomic<bool> allow_finalize_return{true};
    mutable std::mutex mutex_;
    int last_inject_pid = 0;
    std::string inject_error = "inject failed";
    int inject_call_count = 0;
    int inject_spawn_child_call_count = 0;
    std::atomic<int> inject_return_count{0};
    std::string last_agent_path;
    std::string last_inject_agent_path;
    std::string last_inject_ready_token;
    SpawnRequest last_request;
    SpawnRequest last_finalize_request;
};

class FakeProcessManager final {
public:
    std::vector<ProcessInfo> EnumerateProcesses() const {
        return {
            ProcessInfo{100, "zygote64"},
            ProcessInfo{200, "com.demo.target"},
        };
    }

    std::vector<AppInfo> EnumerateApps() const {
        return {
            AppInfo{"com.android.systemui"},
            AppInfo{"com.demo.target"},
        };
    }
};

Frame ParseSingleFrame(const std::vector<uint8_t>& bytes) {
    Frame frame;
    size_t consumed = 0;
    assert(Frame::Parse(bytes.data(), bytes.size(), &frame, &consumed));
    assert(consumed == bytes.size());
    return frame;
}

std::string FindSpawnTokenArg(const SpawnRequest& request) {
    constexpr const char* kPrefix = "--nook-spawn-token=";
    for (const std::string& arg : request.argv) {
        if (arg.rfind(kPrefix, 0) == 0) {
            return arg.substr(std::strlen(kPrefix));
        }
    }
    return {};
}

std::string WaitForSpawnTokenArg(FakeInjector* injector, uint32_t timeout_ms = 1000) {
    if (injector == nullptr) {
        return {};
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const std::string token = FindSpawnTokenArg(injector->GetLastRequest());
        if (!token.empty()) {
            return token;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return {};
}

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

bool WaitForInjectReturnCount(FakeInjector* injector, int expected_count, uint32_t timeout_ms = 1000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (injector->inject_return_count.load(std::memory_order_acquire) >= expected_count) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::string WaitForInjectReadyToken(FakeInjector* injector,
                                    int minimum_inject_call_count = 1,
                                    uint32_t timeout_ms = 1000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (injector->inject_call_count < minimum_inject_call_count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const std::string token = injector->GetLastInjectReadyToken();
        if (!token.empty()) {
            return token;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}

std::vector<Frame> ParseFrames(const std::vector<uint8_t>& bytes) {
    std::vector<Frame> frames;
    size_t offset = 0;
    while (offset < bytes.size()) {
        Frame frame;
        size_t consumed = 0;
        assert(Frame::Parse(bytes.data() + offset, bytes.size() - offset, &frame, &consumed));
        assert(consumed > 0);
        frames.push_back(frame);
        offset += consumed;
    }
    return frames;
}

void TestControlStageAgentReadyDoesNotForwardToBoundHost();
void TestScriptCreateRequiresRuntimeReadyForSpawn();
void TestSpawnRequestUsesRuntimeReadyStateWithoutCachedReadyFrame();
void TestControlStageAgentReadyWithMatchingSpawnTokenResolvesPendingSpawn();
void TestControlStageAgentReadyPromotesBeforeFinalizeCompletes();
void TestSpawnRequestDoesNotLatePromoteChildAlreadyPromotedByControlReady();
void TestAttachReplayKeepsCachedScriptMessagesWhenHostReplaySendFails();
void TestAttachReplayKeepsCachedScriptMessagesWhenAgentReadyReplaySendFails();
void TestAttachReplayDoesNotSendCachedScriptMessagesAfterAgentReadyReplayFailure();
void TestAttachReplayPartialScriptMessageFailureKeepsOnlyUnsentCachedMessages();

void TestSpawnRequestMarksGateHeldChildAndBindsHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24567;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    request.argv = {"--flag"};

    Frame frame(MessageType::kSpawnRequest, 11u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 24567u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 111u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 1);
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 11u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 24567u);
    assert(response.error.code == 0);
    assert(response.error.message.empty());
    assert(registry.FindHostSessionByPid(24567) == &host);
    assert(registry.IsSpawnSuspended(24567));
    const SpawnRequest spawn_request = injector.GetLastRequest();
    assert(spawn_request.identifier == "com.demo.target");
    assert(spawn_request.argv.size() == 2);
    assert(spawn_request.argv[1] == "--flag");
    assert(injector.GetLastAgentPath() == "/data/local/tmp/nook/libnook-agent.so");
}

void TestSpawnRequestDoesNotRequireCoarseSuspendCallback() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24567;
    bool suspend_called = false;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .suspend_process = [&suspend_called](int) {
            suspend_called = true;
            return false;
        },
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 110u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 24567u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 112u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 1);
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 110u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);
    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 24567u);
    assert(response.error.code == 0);
    assert(registry.IsSpawnSuspended(24567));
    assert(!suspend_called);
}

void TestAttachRequestInjectsAgentAndBindsHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kAttachRequest, 10u, EncodeAttachRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 200u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = injector.GetLastInjectReadyToken();
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 130u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(!frames.empty());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 10u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.session_id == host.GetId());
    assert(response.pid == 200u);
    assert(response.process_name == "com.demo.target");
    assert(injector.GetLastInjectPid() == 200);
    assert(injector.GetLastInjectAgentPath() == "/data/local/tmp/nook/libnook-agent.so");
    assert(registry.FindPidByHostSession(host.GetId()) == 200);
}

void TestAttachRequestReusesExistingReadyAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);
    registry.StoreAgentReadyFrame(200, Frame(MessageType::kAgentReady, 129u, EncodeAgentReady(ready)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kAttachRequest, 13u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2u);
    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 13u) {
            response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 129u) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);
    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 0);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);
}

void TestAttachRequestReusesExistingRuntimeReadySessionWithoutCachedReadyFrame() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kAttachRequest, 1313u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1u);
    assert(frames[0].GetType() == MessageType::kAttachResponse);
    assert(frames[0].GetMsgId() == 1313u);

    AttachResponse response;
    assert(DecodeAttachResponse(frames[0].GetPayload().data(),
                                frames[0].GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 0);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);
}

void TestAttachReplayKeepsCachedScriptMessagesWhenHostReplaySendFails() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(1);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    ScriptMessage message;
    message.script_id = 7u;
    message.message = "{\"type\":\"send\",\"payload\":\"cached-attach-replay\"}";
    registry.StoreScriptMessageFrame(200,
                                     Frame(MessageType::kScriptMessage,
                                           1301u,
                                           EncodeScriptMessage(message)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kAttachRequest,
                                     1302u,
                                     EncodeAttachRequest(request))));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1u);
    assert(frames[0].GetType() == MessageType::kAttachResponse);
    assert(frames[0].GetMsgId() == 1302u);

    const std::vector<Frame> cached_messages = registry.GetScriptMessageFrames(200);
    assert(cached_messages.size() == 1u);
    assert(cached_messages[0].GetType() == MessageType::kScriptMessage);
    assert(cached_messages[0].GetMsgId() == 1301u);
}

void TestAttachReplayKeepsCachedScriptMessagesWhenAgentReadyReplaySendFails() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(1);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(200,
                                  Frame(MessageType::kAgentReady,
                                        1303u,
                                        EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 7u;
    message.message = "{\"type\":\"send\",\"payload\":\"cached-after-ready-fail\"}";
    registry.StoreScriptMessageFrame(200,
                                     Frame(MessageType::kScriptMessage,
                                           1304u,
                                           EncodeScriptMessage(message)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kAttachRequest,
                                     1305u,
                                     EncodeAttachRequest(request))));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1u);
    assert(frames[0].GetType() == MessageType::kAttachResponse);
    assert(frames[0].GetMsgId() == 1305u);

    const std::vector<Frame> cached_messages = registry.GetScriptMessageFrames(200);
    assert(cached_messages.size() == 1u);
    assert(cached_messages[0].GetType() == MessageType::kScriptMessage);
    assert(cached_messages[0].GetMsgId() == 1304u);
}

void TestAttachReplayDoesNotSendCachedScriptMessagesAfterAgentReadyReplayFailure() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(1);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(200,
                                  Frame(MessageType::kAgentReady,
                                        1306u,
                                        EncodeAgentReady(ready)));

    ScriptMessage message;
    message.script_id = 7u;
    message.message = "{\"type\":\"send\",\"payload\":\"must-stay-cached\"}";
    registry.StoreScriptMessageFrame(200,
                                     Frame(MessageType::kScriptMessage,
                                           1307u,
                                           EncodeScriptMessage(message)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kAttachRequest,
                                     1308u,
                                     EncodeAttachRequest(request))));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1u);
    assert(frames[0].GetType() == MessageType::kAttachResponse);
    assert(frames[0].GetMsgId() == 1308u);
    assert(host_wire->GetSendAttemptCount() == 2u);
    assert(host_wire->GetSendSuccessCount() == 1u);

    const std::vector<Frame> cached_messages = registry.GetScriptMessageFrames(200);
    assert(cached_messages.size() == 1u);
    assert(cached_messages[0].GetType() == MessageType::kScriptMessage);
    assert(cached_messages[0].GetMsgId() == 1307u);
}

void TestAttachReplayPartialScriptMessageFailureKeepsOnlyUnsentCachedMessages() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(3);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &agent);
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(200,
                                  Frame(MessageType::kAgentReady,
                                        1309u,
                                        EncodeAgentReady(ready)));

    ScriptMessage first_message;
    first_message.script_id = 7u;
    first_message.message = "{\"type\":\"send\",\"payload\":\"already-sent\"}";
    registry.StoreScriptMessageFrame(200,
                                     Frame(MessageType::kScriptMessage,
                                           1310u,
                                           EncodeScriptMessage(first_message)));

    ScriptMessage second_message;
    second_message.script_id = 8u;
    second_message.message = "{\"type\":\"send\",\"payload\":\"not-sent\"}";
    registry.StoreScriptMessageFrame(200,
                                     Frame(MessageType::kScriptMessage,
                                           1311u,
                                           EncodeScriptMessage(second_message)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;
    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kAttachRequest,
                                     1312u,
                                     EncodeAttachRequest(request))));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 3u);
    assert(frames[0].GetType() == MessageType::kAttachResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);
    assert(frames[2].GetType() == MessageType::kScriptMessage);
    assert(host_wire->GetSendAttemptCount() == 4u);
    assert(host_wire->GetSendSuccessCount() == 3u);

    const std::vector<Frame> cached_messages = registry.GetScriptMessageFrames(200);
    assert(cached_messages.size() == 1u);
    assert(cached_messages[0].GetType() == MessageType::kScriptMessage);
    assert(cached_messages[0].GetMsgId() == 1311u);
}

void TestAttachRequestDoesNotReuseStaleGlobalRuntimeReadyWithoutAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.MarkAgentRuntimeReady(200);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kAttachRequest, 1314u, EncodeAttachRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 200u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = injector.GetLastInjectReadyToken();
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 1315u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 1u);
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 1314u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 200);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);
    assert(registry.FindAgentSessionByPid(200) == &agent);
}

void TestAttachRequestWaitsForMatchingRuntimeReadyInsteadOfStaleRuntimeBit() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.MarkAgentRuntimeReady(200);

    FakeInjector injector;
    injector.inject_block_until_release = true;
    injector.allow_inject_return.store(false);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.pid = 200u;
    Frame frame(MessageType::kAttachRequest, 1319u, EncodeAttachRequest(request));

    std::atomic<bool> attach_completed{false};
    std::thread attach_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
        attach_completed.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!attach_completed.load(std::memory_order_acquire));
    assert(host_wire->TakeSent().empty());

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = injector.GetLastInjectReadyToken();
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 1320u, EncodeAgentReady(ready))));

    attach_thread.join();
    assert(attach_completed.load(std::memory_order_acquire));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 2u);
    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 1319u) {
            response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 1320u) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 200);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 1));
}

void TestControlAgentReadyIgnoresStaleGlobalRuntimeReadyWithoutCurrentAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.MarkAgentRuntimeReady(200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 1316u, EncodeAgentReady(ready))));

    assert(registry.FindAgentSessionByPid(200) == &agent);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == &agent);
    assert(host_wire->TakeSent().empty());
}

void TestAttachRequestDoesNotReuseRuntimeReadySessionWhenIdentityMismatches() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto old_agent_transport = std::make_unique<CaptureTransport>();
    Session old_agent(std::move(old_agent_transport));

    auto new_agent_transport = std::make_unique<CaptureTransport>();
    Session new_agent(std::move(new_agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterAgentSession(200, &old_agent);
    registry.RegisterAgentProcessName(200, "zygote64");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = []() {
            return std::vector<ProcessInfo>{{200, "com.demo.target"}};
        },
    });

    AttachRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kAttachRequest, 1317u, EncodeAttachRequest(request));
    std::thread delayed_ready([&dispatcher, &new_agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 200u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = injector.GetLastInjectReadyToken();
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher.Dispatch(new_agent, Frame(MessageType::kAgentReady, 1318u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 1u);
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 1317u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 200);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == &new_agent);
}

void TestAttachRequestAfterDetachReplaysCachedReady() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto next_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* next_host_wire = next_host_transport.get();
    Session next_host(std::move(next_host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&next_host);
    registry.BindHostToPid(original_host.GetId(), 200);
    registry.RegisterAgentSession(200, &agent);

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);
    registry.StoreAgentReadyFrame(200, Frame(MessageType::kAgentReady, 135u, EncodeAgentReady(ready)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    DetachRequest detach_request;
    detach_request.session_id = original_host.GetId();
    assert(dispatcher.Dispatch(original_host,
                               Frame(MessageType::kDetachRequest, 136u, EncodeDetachRequest(detach_request))));
    {
        const Frame response_frame = ParseSingleFrame(original_host_wire->TakeSent());
        assert(response_frame.GetType() == MessageType::kDetachResponse);
    }

    AttachRequest attach_request;
    attach_request.pid = 200u;
    assert(dispatcher.Dispatch(next_host,
                               Frame(MessageType::kAttachRequest, 137u, EncodeAttachRequest(attach_request))));

    const std::vector<Frame> frames = ParseFrames(next_host_wire->TakeSent());
    assert(frames.size() == 2u);
    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 137u) {
            response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 135u) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 0);
    assert(registry.FindPidByHostSession(next_host.GetId()) == 200);
}

void TestAttachRequestAfterHostCloseReplaysCachedReady() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto next_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* next_host_wire = next_host_transport.get();
    Session next_host(std::move(next_host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&next_host);
    registry.BindHostToPid(original_host.GetId(), 200);
    registry.RegisterAgentSession(200, &agent);

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentProcessName(200, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(200);
    registry.MarkAgentReadyStage(200, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(200);
    registry.StoreAgentReadyFrame(200, Frame(MessageType::kAgentReady, 138u, EncodeAgentReady(ready)));

    registry.RemoveHostSession(original_host.GetId());
    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest attach_request;
    attach_request.pid = 200u;
    assert(dispatcher.Dispatch(next_host,
                               Frame(MessageType::kAttachRequest, 139u, EncodeAttachRequest(attach_request))));

    const std::vector<Frame> frames = ParseFrames(next_host_wire->TakeSent());
    assert(frames.size() == 2u);
    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 139u) {
            response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 138u) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(injector.GetLastInjectPid() == 0);
    assert(registry.FindPidByHostSession(next_host.GetId()) == 200);
}

void TestAttachRequestReturnsAfterAgentReadyBeforeInjectThreadCompletes() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.inject_block_until_release = true;
    injector.allow_inject_return.store(false);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 200u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = injector.GetLastInjectReadyToken();
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 131u, EncodeAgentReady(ready))));
    });

    Frame frame(MessageType::kAttachRequest, 132u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(!frames.empty());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 132u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);
    AttachResponse response;
    assert(DecodeAttachResponse(response_frame->GetPayload().data(),
                                response_frame->GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.pid == 200u);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 1));
    delayed_ready.join();
}

void TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateAgentReadyArrives() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto late_agent_transport = std::make_unique<CaptureTransport>();
    Session late_agent(std::move(late_agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.inject_block_until_release = true;
    injector.allow_inject_return.store(false);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kAttachRequest, 133u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAttachResponse);
    assert(response_frame.GetMsgId() == 133u);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == -3);
    assert(response.error.message == "attach agent-ready timeout");
    assert(registry.FindPidByHostSession(host.GetId()) < 0);

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 1));

    AgentReady late_ready;
    late_ready.pid = 200u;
    late_ready.process_name = "com.demo.target";
    late_ready.arch = "arm64";
    late_ready.version = "0.1.0";
    late_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(late_agent,
                               Frame(MessageType::kAgentReady, 134u, EncodeAgentReady(late_ready))));

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(registry.FindAgentSessionByPid(200) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(200));
    assert(!registry.IsAgentRuntimeReady(200));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(200, &cached_ready));
}

void TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateControlAgentReadyArrives() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto late_agent_transport = std::make_unique<CaptureTransport>();
    Session late_agent(std::move(late_agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.inject_block_until_release = true;
    injector.allow_inject_return.store(false);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kAttachRequest, 135u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAttachResponse);
    assert(response_frame.GetMsgId() == 135u);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == -3);
    assert(response.error.message == "attach agent-ready timeout");
    assert(registry.FindPidByHostSession(host.GetId()) < 0);

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 1));

    AgentReady late_ready;
    late_ready.pid = 200u;
    late_ready.process_name = "com.demo.target";
    late_ready.arch = "arm64";
    late_ready.version = "0.1.0";
    late_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(late_agent,
                               Frame(MessageType::kAgentReady, 136u, EncodeAgentReady(late_ready))));

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(registry.FindAgentSessionByPid(200) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(200));
    assert(!registry.IsAgentControlReady(200));
    assert(!registry.IsAgentRuntimeReady(200));
    assert(registry.FindControlReadyAgentSessionByIdentity(200, "com.demo.target") == nullptr);
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(200, &cached_ready));
}

void TestAttachTimeoutLateAgentScriptMessageDoesNotForwardAfterHostRebind() {
    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    SessionRegistry registry;
    registry.MarkAttachTimeoutPid(200);

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(rebound_host.GetId(), 200);

    stale_agent.SetPeerPid(200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"stale-after-attach-timeout\"}";
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kScriptMessage,
                                     137u,
                                     EncodeScriptMessage(message))));

    assert(rebound_host_wire->TakeSent().empty());
    assert(registry.TakeScriptMessageFrames(200).empty());
}

void TestAttachTimeoutLateAgentResponseDoesNotForwardAfterHostRebind() {
    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    SessionRegistry registry;
    registry.MarkAttachTimeoutPid(200);

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(rebound_host.GetId(), 200);

    stale_agent.SetPeerPid(200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreateResponse response;
    response.script_id = 2u;
    response.success = true;
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kScriptCreateResp,
                                     138u,
                                     EncodeScriptCreateResponse(response))));

    assert(rebound_host_wire->TakeSent().empty());
}

void TestReattachDoesNotAcceptLateRuntimeReadyFromPreviousAttachAttempt() {
    auto first_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* first_host_wire = first_host_transport.get();
    Session first_host(std::move(first_host_transport));

    auto second_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* second_host_wire = second_host_transport.get();
    Session second_host(std::move(second_host_transport));

    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&first_host);
    registry.RegisterHostSession(&second_host);

    FakeInjector injector;
    injector.inject_block_until_release = true;
    injector.allow_inject_return.store(false);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    assert(dispatcher.Dispatch(first_host,
                               Frame(MessageType::kAttachRequest,
                                     139u,
                                     EncodeAttachRequest(request))));

    const Frame first_response_frame = ParseSingleFrame(first_host_wire->TakeSent());
    AttachResponse first_response;
    assert(DecodeAttachResponse(first_response_frame.GetPayload().data(),
                                first_response_frame.GetPayload().size(),
                                &first_response));
    assert(first_response.error.code == -3);
    assert(first_response.error.message == "attach agent-ready timeout");
    assert(registry.FindPidByHostSession(first_host.GetId()) < 0);

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 1));

    injector.allow_inject_return.store(false);
    std::atomic<bool> second_attach_completed{false};
    std::thread second_attach_thread([&]() {
        assert(dispatcher.Dispatch(second_host,
                                   Frame(MessageType::kAttachRequest,
                                         140u,
                                         EncodeAttachRequest(request))));
        second_attach_completed.store(true, std::memory_order_release);
    });

    const std::string second_attach_token = WaitForInjectReadyToken(&injector, 2);
    assert(!second_attach_token.empty());
    assert(!second_attach_completed.load(std::memory_order_acquire));
    assert(second_host_wire->TakeSent().empty());

    injector.allow_inject_return.store(true);
    assert(WaitForInjectReturnCount(&injector, 2));

    AgentReady stale_ready;
    stale_ready.pid = 200u;
    stale_ready.process_name = "com.demo.target";
    stale_ready.arch = "arm64";
    stale_ready.version = "0.1.0";
    stale_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kAgentReady,
                                     141u,
                                     EncodeAgentReady(stale_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!second_attach_completed.load(std::memory_order_acquire));
    assert(second_host_wire->TakeSent().empty());

    AgentReady current_ready;
    current_ready.pid = 200u;
    current_ready.process_name = "com.demo.target";
    current_ready.spawn_token = second_attach_token;
    current_ready.arch = "arm64";
    current_ready.version = "0.1.0";
    current_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kAgentReady,
                                     142u,
                                     EncodeAgentReady(current_ready))));

    second_attach_thread.join();
    assert(second_attach_completed.load(std::memory_order_acquire));

    const std::vector<Frame> second_frames = ParseFrames(second_host_wire->TakeSent());
    assert(!second_frames.empty());
    const Frame* second_response_frame = nullptr;
    const Frame* replayed_ready_frame = nullptr;
    for (const Frame& candidate : second_frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 140u) {
            second_response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 142u) {
            replayed_ready_frame = &candidate;
        }
    }
    assert(second_response_frame != nullptr);
    assert(replayed_ready_frame != nullptr);

    AttachResponse second_response;
    assert(DecodeAttachResponse(second_response_frame->GetPayload().data(),
                                second_response_frame->GetPayload().size(),
                                &second_response));
    assert(second_response.error.code == 0);
    assert(second_response.pid == 200u);
    assert(registry.FindPidByHostSession(second_host.GetId()) == 200);
}

void TestSpawnRequestFailureReturnsError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_ok = false;
    injector.spawn_error = "ninjector spawn failed";

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 12u, EncodeSpawnRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame.GetPayload().data(),
                               response_frame.GetPayload().size(),
                               &response));
    assert(response.pid == 0u);
    assert(response.error.code != 0);
    assert(response.error.message == "ninjector spawn failed");
    assert(registry.FindHostSessionByPid(0) == nullptr);
}

void TestSpawnRequestTimesOutWithoutAuthoritativeAgentReadyAndClearsPendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24567;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 13u, EncodeSpawnRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kSpawnResponse);
    assert(response_frame.GetMsgId() == 13u);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame.GetPayload().data(),
                               response_frame.GetPayload().size(),
                               &response));
    assert(response.pid == 0u);
    assert(response.error.code == -4);
    assert(response.error.message == "spawn authoritative agent ready timed out");

    assert(registry.FindHostSessionByPid(24567) == nullptr);
    assert(!registry.IsSpawnSuspended(24567));
    assert(registry.FindAgentSessionByPid(24567) == nullptr);
    assert(!registry.IsInvalidatedAgentPid(24567));
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(FindSpawnTokenArg(injector.GetLastRequest()), &pending));
}

void TestSpawnRequestFinalizeFailureDoesNotBindHostOrKeepPendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24567;
    injector.finalize_ok = false;
    injector.finalize_error = "finalize failed for test";

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 17u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 24567u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 117u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 17u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 0u);
    assert(response.error.code == -5);
    assert(response.error.message == "finalize failed for test");

    assert(registry.FindHostSessionByPid(24567) == nullptr);
    assert(!registry.IsSpawnSuspended(24567));
    assert(registry.FindAgentSessionByPid(24567) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(24567));
    assert(!registry.IsAgentRuntimeReady(24567));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(24567, &cached_ready));
    assert(registry.TakeScriptMessageFrames(24567).empty());
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(FindSpawnTokenArg(injector.GetLastRequest()), &pending));
}

void TestSpawnRequestFinalizeFailureAfterHostCloseClearsPendingSpawnState() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24568;
    injector.finalize_ok = false;
    injector.finalize_error = "finalize failed after host close";
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 18u, EncodeSpawnRequest(request));
    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady ready;
    ready.pid = 24568u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = spawn_token;
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 118u, EncodeAgentReady(ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    registry.RemoveHostSession(host.GetId());
    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    assert(host_wire->TakeSent().empty());
    assert(registry.FindHostSession(host.GetId()) == nullptr);
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(24568));
    assert(registry.FindAgentSessionByPid(24568) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(24568));
    assert(!registry.IsAgentRuntimeReady(24568));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(24568, &cached_ready));
    assert(registry.TakeScriptMessageFrames(24568).empty());
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(spawn_token, &pending));
}

void TestDetachRequestUnbindsHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    DetachRequest request;
    request.session_id = host.GetId();

    Frame frame(MessageType::kDetachRequest, 14u, EncodeDetachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 14u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.session_id == host.GetId());
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
}

void TestDetachRequestCanBeIssuedFromDifferentHostSession() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto current_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* current_host_wire = current_host_transport.get();
    Session current_host(std::move(current_host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&current_host);
    registry.BindHostToPid(original_host.GetId(), 200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    DetachRequest request;
    request.session_id = original_host.GetId();

    Frame frame(MessageType::kDetachRequest, 15u, EncodeDetachRequest(request));
    assert(dispatcher.Dispatch(current_host, frame));

    const Frame response_frame = ParseSingleFrame(current_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 15u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == 0);
    assert(response.session_id == original_host.GetId());
    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);
}

void TestDetachRequestFailsWhileSpawnGateIsHeld() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 200);
    registry.MarkSpawnSuspended(200, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    DetachRequest request;
    request.session_id = host.GetId();

    Frame frame(MessageType::kDetachRequest, 16u, EncodeDetachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 16u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == -4);
    assert(response.error.message ==
           "spawned pid is still gate-held; resume or wait for failure cleanup first");
    assert(response.session_id == host.GetId());
    assert(registry.FindPidByHostSession(host.GetId()) == 200);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(200, &entry));
    assert(entry.host_session_id == host.GetId());
}

void TestDetachRequestUsesSuspendedOwnerWhenPidBindingIsRebound() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    Session rebound_host(std::move(rebound_host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(200, original_host.GetId());
    registry.BindHostToPid(rebound_host.GetId(), 200);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    DetachRequest request;
    request.session_id = original_host.GetId();

    Frame frame(MessageType::kDetachRequest, 161u, EncodeDetachRequest(request));
    assert(dispatcher.Dispatch(original_host, frame));

    const Frame response_frame = ParseSingleFrame(original_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 161u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.error.code == -4);
    assert(response.error.message ==
           "spawned pid is still gate-held; resume or wait for failure cleanup first");
    assert(response.session_id == original_host.GetId());
    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);
    assert(registry.FindPidByHostSession(rebound_host.GetId()) == 200);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(200, &entry));
    assert(entry.host_session_id == original_host.GetId());
}

void TestSessionRegistryTracksGateHeldEntries() {
    SessionRegistry registry;

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(200, &entry));
    assert(!registry.IsSpawnSuspended(200));

    registry.MarkSpawnSuspended(200, 77u);
    assert(registry.IsSpawnSuspended(200));
    assert(registry.GetSpawnSuspendedEntry(200, &entry));
    assert(entry.pid == 200);
    assert(entry.host_session_id == 77u);
    assert(entry.suspended);
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);

    registry.ClearSpawnSuspended(200);
    assert(!registry.IsSpawnSuspended(200));
    assert(!registry.GetSpawnSuspendedEntry(200, &entry));
}

void TestSessionRegistryRebindsHostToNewestPidOnly() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    registry.BindHostToPid(host.GetId(), 200);
    assert(registry.FindPidByHostSession(host.GetId()) == 200);
    assert(registry.FindHostSessionByPid(200) == &host);

    registry.BindHostToPid(host.GetId(), 201);
    assert(registry.FindPidByHostSession(host.GetId()) == 201);
    assert(registry.FindHostSessionByPid(201) == &host);
    assert(registry.FindHostSessionByPid(200) == nullptr);
}

void TestResumeRequestFailsForUnknownPid() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ResumeRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kResumeRequest, 16u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 16u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 200u);
    assert(response.error.code != 0);
}

void TestResumeRequestFailsBeforeAuthoritativeAgentReady() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 201);
    registry.MarkSpawnSuspended(201, host.GetId());
    int released_pid = -1;

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .resume_process = [&released_pid](int pid) {
            released_pid = pid;
            return true;
        },
    });

    ResumeRequest request;
    request.pid = 201u;

    Frame frame(MessageType::kResumeRequest, 160u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 201u);
    assert(response.error.code == -6);
    assert(response.error.message == "spawned process is not ready to resume");
    assert(released_pid == -1);
    assert(registry.IsSpawnSuspended(201));
}

void TestResumeRequestReleasesGateHeldProcess() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 200);
    registry.MarkSpawnSuspended(200, host.GetId());
    int released_pid = -1;

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .resume_process = [&released_pid](int pid) {
            released_pid = pid;
            return true;
        },
    });

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 170u, EncodeAgentReady(ready))));
    host_wire->TakeSent();

    ResumeRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kResumeRequest, 17u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 17u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 200u);
    assert(response.error.code == 0);
    assert(released_pid == 200);
    assert(!registry.IsSpawnSuspended(200));
}

void TestResumeRequestRejectsNonOwnerHostForSuspendedSpawn() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(200, original_host.GetId());
    registry.BindHostToPid(rebound_host.GetId(), 200);
    int released_pid = -1;

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .resume_process = [&released_pid](int pid) {
            released_pid = pid;
            return true;
        },
    });

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 171u, EncodeAgentReady(ready))));
    rebound_host_wire->TakeSent();

    ResumeRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kResumeRequest, 172u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(rebound_host, frame));

    const Frame response_frame = ParseSingleFrame(rebound_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 172u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 200u);
    assert(response.error.code == -7);
    assert(response.error.message == "spawned process is owned by another host session");
    assert(released_pid == -1);
    assert(registry.IsSpawnSuspended(200));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(200, &entry));
    assert(entry.host_session_id == original_host.GetId());
}

void TestResumeRequestKeepsGateHeldEntryWhenReleaseFails() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 200);
    registry.MarkSpawnSuspended(200, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .resume_process = [](int) {
            return false;
        },
    });

    AgentReady ready;
    ready.pid = 200u;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 180u, EncodeAgentReady(ready))));
    host_wire->TakeSent();

    ResumeRequest request;
    request.pid = 200u;

    Frame frame(MessageType::kResumeRequest, 18u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 18u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 200u);
    assert(response.error.code != 0);
    assert(registry.IsSpawnSuspended(200));
}

void TestResumeRequestAcceptsOnlyOneSuccessfulRelease() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 202);
    registry.MarkSpawnSuspended(202, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .resume_process = [](int) {
            return true;
        },
    });

    AgentReady ready;
    ready.pid = 202;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 161u, EncodeAgentReady(ready))));

    host_wire->TakeSent();

    ResumeRequest request;
    request.pid = 202u;

    assert(dispatcher.Dispatch(host, Frame(MessageType::kResumeRequest, 162u, EncodeResumeRequest(request))));
    ResumeResponse first;
    const Frame first_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(DecodeResumeResponse(first_frame.GetPayload().data(), first_frame.GetPayload().size(), &first));
    assert(first.error.code == 0);
    assert(!registry.IsSpawnSuspended(202));

    assert(dispatcher.Dispatch(host, Frame(MessageType::kResumeRequest, 163u, EncodeResumeRequest(request))));
    ResumeResponse second;
    const Frame second_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(DecodeResumeResponse(second_frame.GetPayload().data(), second_frame.GetPayload().size(), &second));
    assert(second.error.code != 0);
}

void TestAgentReadyForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 3333);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 3333;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    Frame frame(MessageType::kAgentReady, 19u, EncodeAgentReady(ready));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kAgentReady);
    assert(forwarded.GetMsgId() == 19u);

    AgentReady parsed;
    assert(DecodeAgentReady(forwarded.GetPayload().data(),
                            forwarded.GetPayload().size(),
                            &parsed));
    assert(parsed.pid == 3333u);
    assert(parsed.process_name == "com.demo.target");
    assert(registry.FindAgentSessionByPid(3333) == &agent);
    assert(agent.GetPeerPid() == 3333);
}

void TestSpawnRequestReplaysRuntimeAgentReadyToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 45678;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .suspend_process = [](int) {
            return true;
        },
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame spawn_frame(MessageType::kSpawnRequest, 34u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        AgentReady ready;
        ready.pid = 45678;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 33u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, spawn_frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 2);
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAgentReady) {
            ready_frame = &candidate;
            break;
        }
    }
    assert(ready_frame != nullptr);

    AgentReady parsed;
    assert(DecodeAgentReady(ready_frame->GetPayload().data(),
                            ready_frame->GetPayload().size(),
                            &parsed));
    assert(parsed.pid == 45678u);
    assert(parsed.process_name == "com.demo.target");
}

void TestSpawnRuntimeReadyReplayDoesNotSendCachedScriptMessagesAfterAgentReadyForwardFailure() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(1);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 45679);
    registry.MarkSpawnSuspended(45679,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(45679, SpawnTransactionState::kWaitingRuntimeReady);

    ScriptMessage message;
    message.script_id = 12u;
    message.message = "{\"type\":\"send\",\"payload\":\"must-not-replay-after-ready-fail\"}";
    registry.StoreScriptMessageFrame(45679,
                                     Frame(MessageType::kScriptMessage,
                                           3401u,
                                           EncodeScriptMessage(message)));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 45679u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3402u,
                                     EncodeAgentReady(runtime_ready))));

    assert(host_wire->GetSendAttemptCount() == 2u);
    assert(host_wire->GetSendSuccessCount() == 1u);

    const std::vector<Frame> cached_messages = registry.GetScriptMessageFrames(45679);
    assert(cached_messages.size() == 1u);
    assert(cached_messages[0].GetType() == MessageType::kScriptMessage);
    assert(cached_messages[0].GetMsgId() == 3401u);
}

void TestSpawnSuccessResponseSendFailureClearsSpawnTransactionState() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(0);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 45682;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 3403u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 45682u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent,
                                   Frame(MessageType::kAgentReady,
                                         3404u,
                                         EncodeAgentReady(ready))));
    });

    assert(dispatcher.Dispatch(host, frame));
    delayed_ready.join();

    assert(host_wire->GetSendAttemptCount() == 1u);
    assert(host_wire->GetSendSuccessCount() == 0u);
    assert(host_wire->TakeSent().empty());

    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(45682));
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(FindSpawnTokenArg(injector.GetLastRequest()), &pending));

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(45682, &entry));
}

void TestSpawnRequestUsesAuthoritativeAgentReadyPidInsteadOfInjectorPid() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 1;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame spawn_frame(MessageType::kSpawnRequest, 341u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 45678;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 342u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, spawn_frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() >= 2);

    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (response_frame == nullptr &&
            candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 341u) {
            response_frame = &candidate;
        }
        if (ready_frame == nullptr &&
            candidate.GetType() == MessageType::kAgentReady) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 45678u);
    assert(response.error.code == 0);
    assert(registry.FindHostSessionByPid(45678) == &host);
    assert(registry.FindHostSessionByPid(1) == nullptr);

    AgentReady parsed_ready;
    assert(DecodeAgentReady(ready_frame->GetPayload().data(),
                            ready_frame->GetPayload().size(),
                            &parsed_ready));
    assert(parsed_ready.pid == 45678u);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(45678, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(!registry.GetSpawnSuspendedEntry(1, &entry));
}

void TestSpawnRequestUsesRuntimeReadyStateWithoutCachedReadyFrame() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 45679;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame spawn_frame(MessageType::kSpawnRequest, 343u, EncodeSpawnRequest(request));
    std::thread delayed_ready([&dispatcher, &agent, &injector]() {
        const std::string spawn_token = WaitForSpawnTokenArg(&injector);
        assert(!spawn_token.empty());
        AgentReady ready;
        ready.pid = 45679u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = spawn_token;
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 344u, EncodeAgentReady(ready))));
    });
    assert(dispatcher.Dispatch(host, spawn_frame));
    delayed_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(!frames.empty());

    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (response_frame == nullptr &&
            candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 343u) {
            response_frame = &candidate;
        }
        if (ready_frame == nullptr &&
            candidate.GetType() == MessageType::kAgentReady) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 45679u);
    assert(response.error.code == 0);

    assert(registry.FindHostSessionByPid(45679) == &host);
    assert(registry.IsAgentAuthoritativeReady(45679));
    assert(registry.IsAgentRuntimeReady(45679));

    Frame cached_ready;
    assert(registry.GetAgentReadyFrame(45679, &cached_ready));
    AgentReady cached_ready_payload;
    assert(DecodeAgentReady(cached_ready.GetPayload().data(),
                            cached_ready.GetPayload().size(),
                            &cached_ready_payload));
    assert(cached_ready_payload.pid == 45679u);
    assert(cached_ready_payload.stage == AgentReadyStage::kRuntime);
    assert(cached_ready_payload.process_name == "com.demo.target");

    if (ready_frame != nullptr) {
        AgentReady ready;
        assert(DecodeAgentReady(ready_frame->GetPayload().data(),
                                ready_frame->GetPayload().size(),
                                &ready));
        assert(ready.pid == 45679u);
        assert(ready.stage == AgentReadyStage::kRuntime);
        assert(ready.process_name == "com.demo.target");
    }

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(45679, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestSpawnRequestHoldsRuntimeReadyUntilResponseEvenIfPendingSpawnClearsEarly() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 45680;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame spawn_frame(MessageType::kSpawnRequest, 345u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, spawn_frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 45680u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 346u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(WaitForInjectedPid(&injector, 45680));

    registry.ClearPendingSpawn(spawn_token);

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 347u, EncodeAgentReady(runtime_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[0].GetMsgId() == 345u);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    SpawnResponse response;
    assert(DecodeSpawnResponse(frames[0].GetPayload().data(),
                               frames[0].GetPayload().size(),
                               &response));
    assert(response.pid == 45680u);
    assert(response.error.code == 0);

    AgentReady replayed_ready;
    assert(DecodeAgentReady(frames[1].GetPayload().data(),
                            frames[1].GetPayload().size(),
                            &replayed_ready));
    assert(replayed_ready.pid == 45680u);
    assert(replayed_ready.stage == AgentReadyStage::kRuntime);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(45680, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestSpawnRequestHostDisconnectDuringFinalizeDoesNotReplayToClosedHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 45681;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame spawn_frame(MessageType::kSpawnRequest, 348u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, spawn_frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 45681u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 349u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(WaitForInjectedPid(&injector, 45681));

    registry.RemoveHostSession(host.GetId());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    control_agent.SetPeerPid(45681);
    ScriptMessage message;
    message.script_id = 1u;
    message.message = "{\"type\":\"send\",\"payload\":\"late-after-host-close\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage, 3491u, EncodeScriptMessage(message))));

    assert(host_wire->TakeSent().empty());
    assert(registry.FindHostSession(host.GetId()) == nullptr);
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
    assert(!registry.IsSpawnSuspended(45681));
    assert(registry.TakeScriptMessageFrames(45681).empty());
    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(spawn_token, &pending));
}

void TestAgentReadyWithMismatchedSpawnTokenDoesNotResolvePendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-expected", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60001;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-other";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 343u, EncodeAgentReady(ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-expected", &pending));
    assert(pending.spawn_token == "spawn-token-expected");
    assert(pending.process_name == "com.demo.target");
    assert(pending.host_session_id == host.GetId());
    assert(!pending.ready);
    assert(pending.pid <= 0);
}

void TestAgentReadyWithMatchingSpawnTokenButWrongPidDoesNotResolvePendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60002);
    registry.RegisterPendingSpawn("spawn-token-expected", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60077;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-expected";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 3432u, EncodeAgentReady(ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-expected", &pending));
    assert(pending.spawn_token == "spawn-token-expected");
    assert(pending.process_name == "com.demo.target");
    assert(pending.host_session_id == host.GetId());
    assert(!pending.ready);
    assert(pending.pid <= 0);

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(60077, &entry));
    assert(registry.FindHostSessionByPid(60077) == nullptr);
}

void TestOrphanSpawnTokenAgentReadyClearsPreRegisteredAgentSession() {
    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterAgentSession(60009, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60009;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-stale";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 3431u, EncodeAgentReady(ready))));

    assert(registry.FindAgentSessionByPid(60009) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(60009));
    assert(!registry.IsAgentRuntimeReady(60009));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(60009, &cached_ready));
}

void TestControlStageAgentReadyWithMatchingSpawnTokenResolvesPendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-expected", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60002;
    ready.process_name = "zygote64";
    ready.spawn_token = "spawn-token-expected";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 344u, EncodeAgentReady(ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-expected", &pending));
    assert(pending.spawn_token == "spawn-token-expected");
    assert(pending.process_name == "com.demo.target");
    assert(pending.host_session_id == host.GetId());
    assert(pending.ready);
    assert(pending.pid == 60002);
}

void TestControlStageAgentReadyForSpawnedChildDoesNotInjectBeforeFinalize() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-strict", "com.demo.target", host.GetId());
    registry.BindHostToPid(host.GetId(), 60003);
    registry.MarkSpawnSuspended(60003, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    AgentReady ready;
    ready.pid = 60003;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-strict";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 345u, EncodeAgentReady(ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(injector.GetLastInjectPid() == 0);
    assert(injector.inject_spawn_child_call_count == 0);
    assert(injector.inject_call_count == 0);
}

void TestControlStageAgentReadyResolvesPendingSpawnWithoutImmediatePromotion() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-early-promote", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    AgentReady ready;
    ready.pid = 60005;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-early-promote";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 347u, EncodeAgentReady(ready))));

    assert(registry.FindHostSessionByPid(60005) == &host);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60005, &entry));
    assert(entry.suspended);
    assert(entry.host_session_id == host.GetId());
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(injector.GetLastInjectPid() == 0);
    assert(injector.inject_spawn_child_call_count == 0);
    assert(injector.inject_call_count == 0);
}

void TestControlStageAgentReadyDoesNotInjectAgainWhenRuntimeSessionAlreadyPresent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-runtime-owned", "com.demo.target", host.GetId());
    registry.BindHostToPid(host.GetId(), 60006);
    registry.MarkSpawnSuspended(60006, host.GetId());
    registry.RegisterAgentSession(60006, &runtime_agent);
    registry.RegisterAgentProcessName(60006, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60006);
    registry.MarkAgentReadyStage(60006, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60006);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    AgentReady ready;
    ready.pid = 60006;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-runtime-owned";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 348u, EncodeAgentReady(ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(injector.GetLastInjectPid() == 0);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60006, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);
}

void TestSpawnRequestDoesNotLatePromoteChildAlreadyPromotedByControlReady() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60007;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 349u, EncodeSpawnRequest(request));
    std::thread delayed_control_ready([&dispatcher, &agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 60007u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent,
                                   Frame(MessageType::kAgentReady, 350u, EncodeAgentReady(ready))));
    });

    assert(dispatcher.Dispatch(host, frame));
    delayed_control_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 349u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 60007u);
    assert(response.error.code == 0);
    assert(WaitForInjectedPid(&injector, 60007));
    assert(injector.inject_spawn_child_call_count == 1);
    assert(injector.inject_call_count == 0);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60007, &entry));
    assert(entry.suspended);
    assert(entry.host_session_id == host.GetId());
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestControlStageAgentReadyPromotesBeforeFinalizeCompletes() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60008;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 351u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady ready;
    ready.pid = 60008u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = spawn_token;
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 352u, EncodeAgentReady(ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(WaitForInjectedPid(&injector, 60008));
    assert(injector.inject_spawn_child_call_count == 1);
    assert(injector.inject_call_count == 0);

    SpawnSuspendedEntry waiting_entry;
    assert(registry.GetSpawnSuspendedEntry(60008, &waiting_entry));
    assert(waiting_entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(waiting_entry.host_session_id == host.GetId());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 351u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 60008u);
    assert(response.error.code == 0);
}

void TestScriptMessageForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60001);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(60001);

    ScriptMessage message;
    message.script_id = 7;
    message.message = "{\"type\":\"send\",\"payload\":\"hello\"}";
    message.data = {0x41, 0x42};

    Frame frame(MessageType::kScriptMessage, 55u, EncodeScriptMessage(message));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptMessage);
    assert(forwarded.GetMsgId() == 55u);

    ScriptMessage parsed;
    assert(DecodeScriptMessage(forwarded.GetPayload().data(),
                               forwarded.GetPayload().size(),
                               &parsed));
    assert(parsed.script_id == 7u);
    assert(parsed.message == "{\"type\":\"send\",\"payload\":\"hello\"}");
    assert(parsed.data == std::vector<uint8_t>({0x41, 0x42}));
}

void TestScriptMessageAfterDetachIsNotReplayedToNextAttach() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto next_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* next_host_wire = next_host_transport.get();
    Session next_host(std::move(next_host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    constexpr int kPid = 200;

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&next_host);
    registry.BindHostToPid(original_host.GetId(), kPid);
    registry.RegisterAgentSession(kPid, &agent);

    AgentReady ready;
    ready.pid = static_cast<uint32_t>(kPid);
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentProcessName(kPid, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(kPid);
    registry.MarkAgentReadyStage(kPid, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(kPid);
    registry.StoreAgentReadyFrame(kPid,
                                  Frame(MessageType::kAgentReady, 551u, EncodeAgentReady(ready)));

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    DetachRequest detach_request;
    detach_request.session_id = original_host.GetId();
    assert(dispatcher.Dispatch(original_host,
                               Frame(MessageType::kDetachRequest, 552u, EncodeDetachRequest(detach_request))));
    {
        const Frame detach_response = ParseSingleFrame(original_host_wire->TakeSent());
        assert(detach_response.GetType() == MessageType::kDetachResponse);
    }
    assert(registry.FindPidByHostSession(original_host.GetId()) < 0);

    agent.SetPeerPid(kPid);
    ScriptMessage message;
    message.script_id = 7;
    message.message = "{\"type\":\"send\",\"payload\":\"stale-after-detach\"}";
    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kScriptMessage, 553u, EncodeScriptMessage(message))));

    AttachRequest attach_request;
    attach_request.pid = static_cast<uint32_t>(kPid);
    assert(dispatcher.Dispatch(next_host,
                               Frame(MessageType::kAttachRequest, 554u, EncodeAttachRequest(attach_request))));

    const std::vector<Frame> frames = ParseFrames(next_host_wire->TakeSent());
    assert(frames.size() == 2u);
    const Frame* response_frame = nullptr;
    const Frame* ready_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kAttachResponse &&
            candidate.GetMsgId() == 554u) {
            response_frame = &candidate;
        } else if (candidate.GetType() == MessageType::kAgentReady &&
                   candidate.GetMsgId() == 551u) {
            ready_frame = &candidate;
        }
    }
    assert(response_frame != nullptr);
    assert(ready_frame != nullptr);
}

void TestStaleAgentAfterHostCloseIsNotAcceptedForReboundHost() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    constexpr int kPid = 201;

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), kPid);
    registry.MarkSpawnSuspended(kPid,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.RegisterAgentSession(kPid, &stale_agent);
    registry.RegisterAgentProcessName(kPid, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(kPid);
    registry.MarkAgentReadyStage(kPid, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(kPid);

    registry.RemoveHostSession(original_host.GetId());
    registry.BindHostToPid(rebound_host.GetId(), kPid);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    stale_agent.SetPeerPid(kPid);
    ScriptMessage message;
    message.script_id = 8u;
    message.message = "{\"type\":\"send\",\"payload\":\"stale-after-host-close-rebind\"}";
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kScriptMessage,
                                     555u,
                                     EncodeScriptMessage(message))));

    assert(rebound_host_wire->TakeSent().empty());
    assert(registry.TakeScriptMessageFrames(kPid).empty());
}

void TestStaleAgentResponseAfterHostCloseIsNotAcceptedForReboundHost() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    constexpr int kPid = 202;

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), kPid);
    registry.MarkSpawnSuspended(kPid,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.RegisterAgentSession(kPid, &stale_agent);
    registry.RegisterAgentProcessName(kPid, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(kPid);
    registry.MarkAgentReadyStage(kPid, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(kPid);

    registry.RemoveHostSession(original_host.GetId());
    registry.BindHostToPid(rebound_host.GetId(), kPid);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    stale_agent.SetPeerPid(kPid);
    RpcResponse response;
    response.script_id = 9u;
    response.success = true;
    response.result_json = "\"pong\"";
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kRpcResponse,
                                     556u,
                                     EncodeRpcResponse(response))));

    assert(rebound_host_wire->TakeSent().empty());
}

void TestInvalidatedPidRejectsContextFreeAgentReadyAfterHostClose() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto stale_agent_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_agent_transport));

    constexpr int kPid = 203;

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), kPid);
    registry.MarkSpawnSuspended(kPid,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.RegisterAgentSession(kPid, &stale_agent);
    registry.RegisterAgentProcessName(kPid, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(kPid);
    registry.MarkAgentReadyStage(kPid, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(kPid);

    registry.RemoveHostSession(original_host.GetId());
    registry.BindHostToPid(rebound_host.GetId(), kPid);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = static_cast<uint32_t>(kPid);
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kAgentReady,
                                     557u,
                                     EncodeAgentReady(ready))));

    assert(rebound_host_wire->TakeSent().empty());
    assert(registry.FindAgentSessionByPid(kPid) == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(kPid));
    assert(!registry.IsAgentRuntimeReady(kPid));
}

void TestSpawnRequestReplaysEarlyScriptMessageToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 61234;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .suspend_process = [](int) {
            return true;
        },
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame spawn_frame(MessageType::kSpawnRequest, 72u, EncodeSpawnRequest(request));
    std::thread delayed_ready_and_message([&dispatcher, &agent, &injector]() {
        AgentReady ready;
        ready.pid = 61234;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 70u, EncodeAgentReady(ready))));

        agent.SetPeerPid(61234);
        ScriptMessage message;
        message.script_id = 1;
        message.message = "{\"type\":\"send\",\"payload\":\"early\"}";
        assert(dispatcher.Dispatch(agent,
                                   Frame(MessageType::kScriptMessage, 71u, EncodeScriptMessage(message))));
    });
    assert(dispatcher.Dispatch(host, spawn_frame));
    delayed_ready_and_message.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(!frames.empty());
    const Frame* message_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kScriptMessage) {
            message_frame = &candidate;
            break;
        }
    }
    assert(message_frame != nullptr);

    ScriptMessage parsed;
    assert(DecodeScriptMessage(message_frame->GetPayload().data(),
                               message_frame->GetPayload().size(),
                               &parsed));
    assert(parsed.message == "{\"type\":\"send\",\"payload\":\"early\"}");
}

void TestScriptPostForwardsToBoundAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 62001);
    registry.RegisterAgentSession(62001, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptPost post;
    post.script_id = 9;
    post.message = "{\"type\":\"post\",\"payload\":\"ping\"}";
    post.data = {0x10, 0x20};

    Frame frame(MessageType::kScriptPost, 81u, EncodeScriptPost(post));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(agent_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptPost);
    assert(forwarded.GetMsgId() == 81u);

    ScriptPost parsed;
    assert(DecodeScriptPost(forwarded.GetPayload().data(),
                            forwarded.GetPayload().size(),
                            &parsed));
    assert(parsed.script_id == 9u);
    assert(parsed.message == "{\"type\":\"post\",\"payload\":\"ping\"}");
    assert(parsed.data == std::vector<uint8_t>({0x10, 0x20}));
}

void TestScriptCreateForwardsToBoundAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63001);
    registry.RegisterAgentSession(63001, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 82u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(agent_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptCreate);
    assert(forwarded.GetMsgId() == 82u);
}

void TestScriptCreateReturnsImmediateErrorWithoutAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63011);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 821u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 821u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code != 0);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestScriptCreateRespForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63002);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(63002);

    ScriptCreateResponse response;
    response.script_id = 99u;
    response.success = true;

    Frame frame(MessageType::kScriptCreateResp, 83u, EncodeScriptCreateResponse(response));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptCreateResp);
    assert(forwarded.GetMsgId() == 83u);
}

void TestScriptLoadForwardsToBoundAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63003);
    registry.RegisterAgentSession(63003, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 99u;

    Frame frame(MessageType::kScriptLoad, 84u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(agent_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoad);
    assert(forwarded.GetMsgId() == 84u);
}

void TestScriptLoadRequiresAuthoritativeAgentReadyForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63013);
    registry.MarkSpawnSuspended(63013, host.GetId());
    registry.RegisterAgentSession(63013, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 99u;

    Frame frame(MessageType::kScriptLoad, 842u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script load");
    assert(agent_wire->TakeSent().empty());
}

void TestScriptLoadReturnsImmediateErrorWithoutAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63012);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 99u;

    Frame frame(MessageType::kScriptLoad, 841u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);
    assert(response_frame.GetMsgId() == 841u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code != 0);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestScriptLoadRespForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63004);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(63004);

    ScriptResponse response;
    response.script_id = 99u;
    response.success = true;

    Frame frame(MessageType::kScriptLoadResp, 85u, EncodeScriptResponse(response));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoadResp);
    assert(forwarded.GetMsgId() == 85u);
}

void TestScriptLoadRespRestoresReadyForScriptLoadStateForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63036);
    registry.MarkSpawnSuspended(63036,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63036, SpawnTransactionState::kScriptLoadDispatched);
    registry.RegisterAgentSession(63036, &runtime_agent);
    registry.RegisterAgentProcessName(63036, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63036);
    registry.MarkAgentReadyStage(63036, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63036);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(63036);
    ScriptResponse response;
    response.script_id = 100u;
    response.success = true;

    Frame frame(MessageType::kScriptLoadResp, 851u, EncodeScriptResponse(response));
    assert(dispatcher.Dispatch(runtime_agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoadResp);
    assert(forwarded.GetMsgId() == 851u);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63036, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadRespWithoutSuspendedOwnerHostStillRestoresReadyState() {
    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(63037,
                                0xdeadbeefu,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63037, SpawnTransactionState::kScriptLoadDispatched);
    registry.BindHostToPid(rebound_host.GetId(), 63037);
    registry.RegisterAgentSession(63037, &runtime_agent);
    registry.RegisterAgentProcessName(63037, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63037);
    registry.MarkAgentReadyStage(63037, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63037);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(63037);
    ScriptResponse response;
    response.script_id = 101u;
    response.success = true;

    Frame frame(MessageType::kScriptLoadResp, 852u, EncodeScriptResponse(response));
    assert(dispatcher.Dispatch(runtime_agent, frame));

    assert(rebound_host_wire->TakeSent().empty());

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63037, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadRespHostSendFailureStillRestoresReadyState() {
    auto host_transport = std::make_unique<FailAfterNSendsTransport>(0);
    FailAfterNSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.MarkSpawnSuspended(63038,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63038, SpawnTransactionState::kScriptLoadDispatched);
    registry.BindHostToPid(host.GetId(), 63038);
    registry.RegisterAgentSession(63038, &runtime_agent);
    registry.RegisterAgentProcessName(63038, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63038);
    registry.MarkAgentReadyStage(63038, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63038);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(63038);
    ScriptResponse response;
    response.script_id = 102u;
    response.success = true;

    Frame frame(MessageType::kScriptLoadResp, 853u, EncodeScriptResponse(response));
    assert(dispatcher.Dispatch(runtime_agent, frame));

    assert(host_wire->GetSendAttemptCount() == 1u);
    assert(host_wire->GetSendSuccessCount() == 0u);
    assert(host_wire->TakeSent().empty());

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63038, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptUnloadForwardsToBoundAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63005);
    registry.RegisterAgentSession(63005, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 99u;

    Frame frame(MessageType::kScriptUnload, 86u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(agent_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptUnload);
    assert(forwarded.GetMsgId() == 86u);
}

void TestScriptUnloadReturnsImmediateErrorForInvalidRequest() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kScriptUnload, 857u, {});
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 857u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -1);
    assert(response.error.message == "invalid script unload request");
}

void TestScriptUnloadReturnsImmediateErrorWhenRegistryUnavailable() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, nullptr, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 16u;

    Frame frame(MessageType::kScriptUnload, 858u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 858u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestScriptUnloadRespForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63006);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(63006);

    ScriptResponse response;
    response.script_id = 99u;
    response.success = true;

    Frame frame(MessageType::kScriptUnloadResp, 87u, EncodeScriptResponse(response));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptUnloadResp);
    assert(forwarded.GetMsgId() == 87u);
}

void TestRpcRequestForwardsToBoundAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63007);
    registry.RegisterAgentSession(63007, &agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 9u;
    request.method = "ping";
    request.args_json = "[\"hello\"]";

    Frame frame(MessageType::kRpcRequest, 90u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(agent_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kRpcRequest);
    assert(forwarded.GetMsgId() == 90u);
}

void TestRpcRequestReturnsImmediateErrorForInvalidRequest() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kRpcRequest, 859u, {});
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 859u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -1);
    assert(response.error.message == "invalid rpc request");
}

void TestRpcRequestReturnsImmediateErrorWhenRegistryUnavailable() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, nullptr, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 17u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 860u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 860u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestScriptPostDoesNotTargetControlFallbackAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63016);
    registry.MarkSpawnSuspended(63016,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63016, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63016, &control_agent);
    registry.RegisterControlReadyAgentSession(63016, &control_agent);
    registry.RegisterAgentProcessName(63016, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63016);
    registry.MarkAgentReadyStage(63016, AgentReadyStage::kControl);

    AgentReady runtime_ready;
    runtime_ready.pid = 63016u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(63016, &runtime_agent);
    registry.RegisterAgentProcessName(63016, "com.demo.target");
    registry.MarkAgentReadyStage(63016, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63016);
    registry.StoreAgentReadyFrame(63016,
                                  Frame(MessageType::kAgentReady, 825u, EncodeAgentReady(runtime_ready)));

    assert(registry.RemoveAgentSessionByPidIfMatches(63016, &runtime_agent));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptPost post;
    post.script_id = 7u;
    post.message = "{\"type\":\"send\",\"payload\":\"late-post\"}";

    Frame frame(MessageType::kScriptPost, 826u, EncodeScriptPost(post));
    assert(dispatcher.Dispatch(host, frame));

    assert(host_wire->TakeSent().empty());
    assert(runtime_wire->TakeSent().empty());
    assert(control_wire->TakeSent().empty());
}

void TestRpcRequestDoesNotTargetControlFallbackAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63017);
    registry.MarkSpawnSuspended(63017,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63017, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63017, &control_agent);
    registry.RegisterControlReadyAgentSession(63017, &control_agent);
    registry.RegisterAgentProcessName(63017, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63017);
    registry.MarkAgentReadyStage(63017, AgentReadyStage::kControl);

    AgentReady runtime_ready;
    runtime_ready.pid = 63017u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(63017, &runtime_agent);
    registry.RegisterAgentProcessName(63017, "com.demo.target");
    registry.MarkAgentReadyStage(63017, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63017);
    registry.StoreAgentReadyFrame(63017,
                                  Frame(MessageType::kAgentReady, 827u, EncodeAgentReady(runtime_ready)));

    assert(registry.RemoveAgentSessionByPidIfMatches(63017, &runtime_agent));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 9u;
    request.method = "ping";
    request.args_json = "[\"late-rpc\"]";

    Frame frame(MessageType::kRpcRequest, 828u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 828u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for rpc request");
    assert(runtime_wire->TakeSent().empty());
    assert(control_wire->TakeSent().empty());
}

void TestScriptUnloadDoesNotTargetControlFallbackAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63018);
    registry.MarkSpawnSuspended(63018,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63018, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63018, &control_agent);
    registry.RegisterControlReadyAgentSession(63018, &control_agent);
    registry.RegisterAgentProcessName(63018, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63018);
    registry.MarkAgentReadyStage(63018, AgentReadyStage::kControl);

    AgentReady runtime_ready;
    runtime_ready.pid = 63018u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(63018, &runtime_agent);
    registry.RegisterAgentProcessName(63018, "com.demo.target");
    registry.MarkAgentReadyStage(63018, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63018);
    registry.StoreAgentReadyFrame(63018,
                                  Frame(MessageType::kAgentReady, 829u, EncodeAgentReady(runtime_ready)));

    assert(registry.RemoveAgentSessionByPidIfMatches(63018, &runtime_agent));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 11u;

    Frame frame(MessageType::kScriptUnload, 830u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 830u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script unload");
    assert(runtime_wire->TakeSent().empty());
    assert(control_wire->TakeSent().empty());
}

void TestScriptUnloadReturnsImmediateErrorWithoutAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63038);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 12u;

    Frame frame(MessageType::kScriptUnload, 853u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 853u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestScriptUnloadReturnsImmediateErrorWithoutBoundPid() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 14u;

    Frame frame(MessageType::kScriptUnload, 855u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 855u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -3);
    assert(response.error.message == "host session is not bound to a pid");
}

void TestRpcRequestReturnsImmediateErrorWithoutAgentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63039);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 13u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 854u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 854u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestRpcRequestReturnsImmediateErrorWithoutBoundPid() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 15u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 856u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 856u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -3);
    assert(response.error.message == "host session is not bound to a pid");
}

void TestRpcResponseForwardsToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63008);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(63008);

    RpcResponse response;
    response.script_id = 9u;
    response.success = true;
    response.result_json = "{\"value\":\"pong\"}";

    Frame frame(MessageType::kRpcResponse, 91u, EncodeRpcResponse(response));
    assert(dispatcher.Dispatch(agent, frame));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kRpcResponse);
    assert(forwarded.GetMsgId() == 91u);
}

void TestProcessListRequestReturnsProcesses() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
    });

    ProcessListRequest request;
    Frame frame(MessageType::kProcessListReq, 88u, EncodeProcessListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kProcessListResp);
    assert(response_frame.GetMsgId() == 88u);

    ProcessListResponse response;
    assert(DecodeProcessListResponse(response_frame.GetPayload().data(),
                                     response_frame.GetPayload().size(),
                                     &response));
    assert(response.error.code == 0);
    assert(response.processes.size() == 2u);
    assert(response.processes[0].pid == 100u);
    assert(response.processes[0].name == "zygote64");
    assert(response.processes[1].pid == 200u);
    assert(response.processes[1].name == "com.demo.target");
}

void TestAppListRequestReturnsApps() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .enumerate_processes = [&process_manager]() {
            return process_manager.EnumerateProcesses();
        },
        .enumerate_apps = [&process_manager]() {
            return process_manager.EnumerateApps();
        },
    });

    AppListRequest request;
    Frame frame(MessageType::kAppListReq, 89u, EncodeAppListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAppListResp);
    assert(response_frame.GetMsgId() == 89u);

    AppListResponse response;
    assert(DecodeAppListResponse(response_frame.GetPayload().data(),
                                 response_frame.GetPayload().size(),
                                 &response));
    assert(response.error.code == 0);
    assert(response.apps.size() == 2u);
    assert(response.apps[0].package_name == "com.android.systemui");
    assert(response.apps[1].package_name == "com.demo.target");
}

void TestControlStageAgentReadyDoesNotForwardToBoundHost() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 3334);
    registry.MarkSpawnSuspended(3334, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 3334;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    Frame frame(MessageType::kAgentReady, 20u, EncodeAgentReady(ready));
    assert(dispatcher.Dispatch(agent, frame));

    assert(host_wire->TakeSent().empty());
    assert(registry.FindAgentSessionByPid(3334) == &agent);
    assert(registry.FindAuthoritativeAgentSessionByPid(3334) == &agent);
    assert(registry.IsAgentAuthoritativeReady(3334));
    AgentReadyStage stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(3334, &stage));
    assert(stage == AgentReadyStage::kControl);
    assert(registry.IsAgentControlReady(3334));
    assert(!registry.IsAgentRuntimeReady(3334));

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(3334, &cached_ready));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(3334, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);
}

void TestRuntimeAgentReadyReplacesEarlierControlStageConnection() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 4334);
    registry.MarkSpawnSuspended(4334, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady control_ready;
    control_ready.pid = 4334;
    control_ready.process_name = "zygote64";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 120u, EncodeAgentReady(control_ready))));

    assert(registry.FindAgentSessionByPid(4334) == &control_agent);
    assert(registry.FindAuthoritativeAgentSessionByPid(4334) == &control_agent);
    assert(registry.IsAgentAuthoritativeReady(4334));
    assert(host_wire->TakeSent().empty());

    AgentReady runtime_ready;
    runtime_ready.pid = 4334;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 121u, EncodeAgentReady(runtime_ready))));

    assert(registry.FindAgentSessionByPid(4334) == &runtime_agent);
    assert(registry.IsAgentAuthoritativeReady(4334));
    assert(registry.IsAgentRuntimeReady(4334));
    AgentReadyStage final_stage = AgentReadyStage::kControl;
    assert(registry.GetAgentReadyStage(4334, &final_stage));
    assert(final_stage == AgentReadyStage::kRuntime);

    Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kAgentReady);

    Frame cached_ready;
    assert(registry.GetAgentReadyFrame(4334, &cached_ready));
    AgentReady parsed_ready;
    assert(DecodeAgentReady(cached_ready.GetPayload().data(),
                            cached_ready.GetPayload().size(),
                            &parsed_ready));
    assert(parsed_ready.stage == AgentReadyStage::kRuntime);
    assert(parsed_ready.process_name == "com.demo.target");
}

void TestLateControlAgentReadyDoesNotRegressRuntimeStage() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 5334);
    registry.MarkSpawnSuspended(5334, host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 5334;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 130u, EncodeAgentReady(runtime_ready))));

    Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kAgentReady);
    assert(registry.FindAgentSessionByPid(5334) == &runtime_agent);
    assert(registry.IsAgentAuthoritativeReady(5334));
    assert(registry.IsAgentRuntimeReady(5334));

    AgentReady control_ready;
    control_ready.pid = 5334;
    control_ready.process_name = "zygote64";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 131u, EncodeAgentReady(control_ready))));

    assert(host_wire->TakeSent().empty());
    assert(registry.FindAgentSessionByPid(5334) == &runtime_agent);
    assert(registry.FindAuthoritativeAgentSessionByPid(5334) == &runtime_agent);
    assert(registry.IsAgentAuthoritativeReady(5334));
    assert(registry.IsAgentRuntimeReady(5334));

    AgentReadyStage final_stage = AgentReadyStage::kControl;
    assert(registry.GetAgentReadyStage(5334, &final_stage));
    assert(final_stage == AgentReadyStage::kRuntime);

    Frame cached_ready;
    assert(registry.GetAgentReadyFrame(5334, &cached_ready));
    AgentReady parsed_ready;
    assert(DecodeAgentReady(cached_ready.GetPayload().data(),
                            cached_ready.GetPayload().size(),
                            &parsed_ready));
    assert(parsed_ready.stage == AgentReadyStage::kRuntime);
    assert(parsed_ready.process_name == "com.demo.target");
}

void TestScriptCreateStillTargetsRuntimeAgentAfterLateControlReady() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 5335);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 5335;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 132u, EncodeAgentReady(runtime_ready))));

    AgentReady control_ready;
    control_ready.pid = 5335;
    control_ready.process_name = "zygote64";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 133u, EncodeAgentReady(control_ready))));

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 134u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(runtime_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptCreate);
    assert(forwarded.GetMsgId() == 134u);
    assert(control_wire->TakeSent().empty());
}

void TestScriptCreateRequiresRuntimeReadyForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* agent_wire = agent_transport.get();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63014);
    registry.MarkSpawnSuspended(63014, host.GetId());
    registry.RegisterAgentSession(63014, &agent);
    registry.UpdateSpawnState(63014, SpawnTransactionState::kWaitingRuntimeReady);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 822u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script create");
    assert(agent_wire->TakeSent().empty());
}

void TestScriptCreateDoesNotTargetControlFallbackAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63015);
    registry.MarkSpawnSuspended(63015,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63015, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63015, &control_agent);
    registry.RegisterControlReadyAgentSession(63015, &control_agent);
    registry.RegisterAgentProcessName(63015, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63015);
    registry.MarkAgentReadyStage(63015, AgentReadyStage::kControl);

    AgentReady runtime_ready;
    runtime_ready.pid = 63015u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(63015, &runtime_agent);
    registry.RegisterAgentProcessName(63015, "com.demo.target");
    registry.MarkAgentReadyStage(63015, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63015);
    registry.StoreAgentReadyFrame(63015,
                                  Frame(MessageType::kAgentReady, 823u, EncodeAgentReady(runtime_ready)));

    assert(registry.RemoveAgentSessionByPidIfMatches(63015, &runtime_agent));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 824u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script create");
    assert(runtime_wire->TakeSent().empty());
    assert(control_wire->TakeSent().empty());
}

void TestScriptCreateDoesNotTargetMismatchedRuntimeAgentForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto wrong_runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* wrong_runtime_wire = wrong_runtime_transport.get();
    Session wrong_runtime_agent(std::move(wrong_runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63019);
    registry.MarkSpawnSuspended(63019,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63019, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63019, &wrong_runtime_agent);
    registry.RegisterAgentProcessName(63019, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(63019);
    registry.MarkAgentReadyStage(63019, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63019);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 827u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
    assert(wrong_runtime_wire->TakeSent().empty());
}

void TestScriptCreateRespFromMismatchedRuntimeAgentIsDroppedForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto wrong_runtime_transport = std::make_unique<CaptureTransport>();
    Session wrong_runtime_agent(std::move(wrong_runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63020);
    registry.MarkSpawnSuspended(63020,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63020, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63020, &wrong_runtime_agent);
    registry.RegisterAgentProcessName(63020, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(63020);
    registry.MarkAgentReadyStage(63020, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63020);
    wrong_runtime_agent.SetPeerPid(63020);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreateResponse response;
    response.script_id = 1u;
    response.success = true;
    assert(dispatcher.Dispatch(wrong_runtime_agent,
                               Frame(MessageType::kScriptCreateResp,
                                     828u,
                                     EncodeScriptCreateResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestScriptCreateRespForSpawnUsesSuspendedHostOwnership() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), 63021);
    registry.MarkSpawnSuspended(63021,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63021, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63021, &runtime_agent);
    registry.RegisterAgentProcessName(63021, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63021);
    registry.MarkAgentReadyStage(63021, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63021);
    runtime_agent.SetPeerPid(63021);

    registry.BindHostToPid(rebound_host.GetId(), 63021);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreateResponse response;
    response.script_id = 2u;
    response.success = true;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptCreateResp,
                                     829u,
                                     EncodeScriptCreateResponse(response))));

    const Frame forwarded = ParseSingleFrame(original_host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptCreateResp);
    assert(forwarded.GetMsgId() == 829u);
    assert(rebound_host_wire->TakeSent().empty());
}

void TestScriptCreateForSpawnUsesSuspendedOwnerWhenPidBindingIsRebound() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), 63025);
    registry.MarkSpawnSuspended(63025,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63025, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63025, &runtime_agent);
    registry.RegisterAgentProcessName(63025, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63025);
    registry.MarkAgentReadyStage(63025, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63025);

    registry.BindHostToPid(rebound_host.GetId(), 63025);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = original_host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 834u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(original_host, frame));

    assert(original_host_wire->TakeSent().empty());
    const std::vector<Frame> forwarded_frames = ParseFrames(runtime_wire->TakeSent());
    assert(forwarded_frames.size() == 1u);
    assert(forwarded_frames[0].GetType() == MessageType::kScriptCreate);
    assert(forwarded_frames[0].GetMsgId() == 834u);
}

void TestScriptCreateForReboundHostDoesNotTargetForeignSuspendedSpawn() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), 63034);
    registry.MarkSpawnSuspended(63034,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63034, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63034, &runtime_agent);
    registry.RegisterAgentProcessName(63034, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63034);
    registry.MarkAgentReadyStage(63034, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63034);

    registry.BindHostToPid(rebound_host.GetId(), 63034);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = rebound_host.GetId();
    create.name = "demo.js";
    create.source = "console.log('wrong owner');";

    Frame frame(MessageType::kScriptCreate, 836u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(rebound_host, frame));

    const Frame response_frame = ParseSingleFrame(rebound_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 836u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -3);
    assert(response.error.message == "host session is not bound to a pid");
    assert(runtime_wire->TakeSent().empty());
}

void TestRuntimeReadyForwardForSpawnUsesSuspendedHostOwnership() {
    auto original_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* original_host_wire = original_host_transport.get();
    Session original_host(std::move(original_host_transport));

    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&original_host);
    registry.RegisterHostSession(&rebound_host);
    registry.BindHostToPid(original_host.GetId(), 63022);
    registry.MarkSpawnSuspended(63022,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63022, SpawnTransactionState::kWaitingRuntimeReady);

    registry.BindHostToPid(rebound_host.GetId(), 63022);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 63022u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 830u, EncodeAgentReady(runtime_ready))));

    const Frame forwarded = ParseSingleFrame(original_host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kAgentReady);
    assert(forwarded.GetMsgId() == 830u);
    assert(rebound_host_wire->TakeSent().empty());
}

void TestRuntimeReadyForwardDoesNotFallbackToReboundHostWhenSuspendedOwnerSessionIsMissing() {
    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(63026,
                                0xdeadbeefu,
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63026, SpawnTransactionState::kWaitingRuntimeReady);
    registry.BindHostToPid(rebound_host.GetId(), 63026);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 63026u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 835u, EncodeAgentReady(runtime_ready))));

    assert(rebound_host_wire->TakeSent().empty());
}

void TestScriptMessageFromMismatchedCurrentRuntimeAgentIsDroppedForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto wrong_runtime_transport = std::make_unique<CaptureTransport>();
    Session wrong_runtime_agent(std::move(wrong_runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63023);
    registry.MarkSpawnSuspended(63023,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63023, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63023, &wrong_runtime_agent);
    registry.RegisterAgentProcessName(63023, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(63023);
    registry.MarkAgentReadyStage(63023, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63023);
    wrong_runtime_agent.SetPeerPid(63023);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptMessage message;
    message.script_id = 12u;
    message.message = "{\"type\":\"send\",\"payload\":\"wrong-runtime\"}";
    assert(dispatcher.Dispatch(wrong_runtime_agent,
                               Frame(MessageType::kScriptMessage,
                                     831u,
                                     EncodeScriptMessage(message))));

    assert(host_wire->TakeSent().empty());
    assert(registry.TakeScriptMessageFrames(63023).empty());
}

void TestLateControlAgentReadyDoesNotRegressTransactionRuntimeBoundaryWithoutGlobalRuntimeState() {
    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    auto stale_control_transport = std::make_unique<CaptureTransport>();
    Session stale_control_agent(std::move(stale_control_transport));
    assert(stale_control_agent.Start());

    SessionRegistry registry;
    registry.RegisterAgentSession(63016, &control_agent);
    registry.RegisterControlReadyAgentSession(63016, &control_agent);
    registry.RegisterAgentProcessName(63016, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63016);
    registry.MarkAgentReadyStage(63016, AgentReadyStage::kControl);
    registry.MarkSpawnSuspended(63016,
                                8u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63016, SpawnTransactionState::kReadyForScriptLoad);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady stale_control_ready;
    stale_control_ready.pid = 63016u;
    stale_control_ready.process_name = "com.demo.target";
    stale_control_ready.arch = "arm64";
    stale_control_ready.version = "0.1.0";
    stale_control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(stale_control_agent,
                               Frame(MessageType::kAgentReady,
                                     825u,
                                     EncodeAgentReady(stale_control_ready))));

    assert(registry.FindAgentSessionByPid(63016) == &control_agent);
    assert(registry.FindControlReadyAgentSessionByPid(63016) == &control_agent);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63016, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.authoritative_process_name == "com.demo.target");
}

void TestControlReadyCanReplaceMismatchedGlobalRuntimeTraceForSpawnTarget() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto wrong_runtime_transport = std::make_unique<CaptureTransport>();
    Session wrong_runtime_agent(std::move(wrong_runtime_transport));
    assert(wrong_runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63024);
    registry.MarkSpawnSuspended(63024,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");

    registry.RegisterAgentSession(63024, &wrong_runtime_agent);
    registry.RegisterAgentProcessName(63024, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(63024);
    registry.MarkAgentReadyStage(63024, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63024);

    AgentReady wrong_runtime_ready;
    wrong_runtime_ready.pid = 63024u;
    wrong_runtime_ready.process_name = "com.demo.other";
    wrong_runtime_ready.arch = "arm64";
    wrong_runtime_ready.version = "0.1.0";
    wrong_runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(63024,
                                  Frame(MessageType::kAgentReady,
                                        832u,
                                        EncodeAgentReady(wrong_runtime_ready)));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady control_ready;
    control_ready.pid = 63024u;
    control_ready.process_name = "com.demo.target";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady,
                                     833u,
                                     EncodeAgentReady(control_ready))));

    assert(!registry.IsAgentRuntimeReady(63024));
    assert(registry.FindControlReadyAgentSessionByPid(63024) == &control_agent);
    assert(registry.FindAgentSessionByPid(63024) == &control_agent);
    assert(registry.FindRuntimeReadyAgentSessionByIdentity(63024, "com.demo.target") == nullptr);
    assert(registry.FindRuntimeReadyAgentSessionByIdentity(63024, "com.demo.other") == nullptr);
    assert(registry.IsAgentControlReady(63024));

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrameByIdentity(63024, "com.demo.other", &cached_ready));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63024, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingAgentReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
    assert(entry.target_process_name == "com.demo.target");
}

void TestMismatchedRuntimeAgentReadyDoesNotKeepPreRegisteredWrongRuntimeSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto wrong_runtime_transport = std::make_unique<CaptureTransport>();
    Session wrong_runtime_agent(std::move(wrong_runtime_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63026);
    registry.MarkSpawnSuspended(63026,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");

    registry.RegisterAgentSession(63026, &control_agent);
    registry.RegisterControlReadyAgentSession(63026, &control_agent);
    registry.RegisterAgentProcessName(63026, "com.demo.target");
    registry.MarkAgentReadyStage(63026, AgentReadyStage::kControl);
    registry.MarkAgentAuthoritativeReady(63026);

    registry.RegisterAgentSession(63026, &wrong_runtime_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady wrong_runtime_ready;
    wrong_runtime_ready.pid = 63026u;
    wrong_runtime_ready.process_name = "com.demo.other";
    wrong_runtime_ready.arch = "arm64";
    wrong_runtime_ready.version = "0.1.0";
    wrong_runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(wrong_runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     834u,
                                     EncodeAgentReady(wrong_runtime_ready))));

    assert(registry.FindAgentSessionByPid(63026) == &control_agent);
    assert(registry.FindControlReadyAgentSessionByPid(63026) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63026));
    assert(registry.FindRuntimeReadyAgentSessionByIdentity(63026, "com.demo.other") == nullptr);
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrameByIdentity(63026, "com.demo.other", &cached_ready));
}

void TestLateControlAgentReadyAtRuntimeBoundaryDoesNotKeepPreRegisteredCurrentSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    auto stale_control_transport = std::make_unique<CaptureTransport>();
    Session stale_control_agent(std::move(stale_control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63027);
    registry.MarkSpawnSuspended(63027,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63027, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63027, &runtime_agent);
    registry.RegisterAgentProcessName(63027, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63027);
    registry.MarkAgentReadyStage(63027, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63027);

    registry.RegisterAgentSession(63027, &stale_control_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady stale_control_ready;
    stale_control_ready.pid = 63027u;
    stale_control_ready.process_name = "com.demo.target";
    stale_control_ready.arch = "arm64";
    stale_control_ready.version = "0.1.0";
    stale_control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(stale_control_agent,
                               Frame(MessageType::kAgentReady,
                                     835u,
                                     EncodeAgentReady(stale_control_ready))));

    assert(registry.FindAgentSessionByPid(63027) == &runtime_agent);
    assert(registry.IsAgentRuntimeReady(63027));
}

}  // namespace

int main() {
    TestSpawnRequestMarksGateHeldChildAndBindsHost();
    TestSpawnRequestDoesNotRequireCoarseSuspendCallback();
    TestAttachRequestInjectsAgentAndBindsHost();
    TestAttachRequestReusesExistingReadyAgentSession();
    TestAttachRequestReusesExistingRuntimeReadySessionWithoutCachedReadyFrame();
    TestAttachReplayKeepsCachedScriptMessagesWhenHostReplaySendFails();
    TestAttachReplayKeepsCachedScriptMessagesWhenAgentReadyReplaySendFails();
    TestAttachReplayDoesNotSendCachedScriptMessagesAfterAgentReadyReplayFailure();
    TestAttachReplayPartialScriptMessageFailureKeepsOnlyUnsentCachedMessages();
    TestAttachRequestDoesNotReuseStaleGlobalRuntimeReadyWithoutAgentSession();
    TestAttachRequestWaitsForMatchingRuntimeReadyInsteadOfStaleRuntimeBit();
    TestControlAgentReadyIgnoresStaleGlobalRuntimeReadyWithoutCurrentAgentSession();
    TestAttachRequestDoesNotReuseRuntimeReadySessionWhenIdentityMismatches();
    TestAttachRequestAfterDetachReplaysCachedReady();
    TestAttachRequestAfterHostCloseReplaysCachedReady();
    TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateAgentReadyArrives();
    TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateControlAgentReadyArrives();
    TestAttachTimeoutLateAgentScriptMessageDoesNotForwardAfterHostRebind();
    TestAttachTimeoutLateAgentResponseDoesNotForwardAfterHostRebind();
    TestReattachDoesNotAcceptLateRuntimeReadyFromPreviousAttachAttempt();
    TestSpawnRequestFailureReturnsError();
    TestSpawnRequestTimesOutWithoutAuthoritativeAgentReadyAndClearsPendingSpawn();
    TestSpawnRequestFinalizeFailureDoesNotBindHostOrKeepPendingSpawn();
    TestDetachRequestUnbindsHost();
    TestDetachRequestCanBeIssuedFromDifferentHostSession();
    TestDetachRequestFailsWhileSpawnGateIsHeld();
    TestDetachRequestUsesSuspendedOwnerWhenPidBindingIsRebound();
    TestSessionRegistryTracksGateHeldEntries();
    TestSessionRegistryRebindsHostToNewestPidOnly();
    TestResumeRequestFailsForUnknownPid();
    TestResumeRequestFailsBeforeAuthoritativeAgentReady();
    TestResumeRequestReleasesGateHeldProcess();
    TestResumeRequestRejectsNonOwnerHostForSuspendedSpawn();
    TestResumeRequestKeepsGateHeldEntryWhenReleaseFails();
    TestResumeRequestAcceptsOnlyOneSuccessfulRelease();
    TestAgentReadyForwardsToBoundHost();
    TestControlStageAgentReadyDoesNotForwardToBoundHost();
    TestRuntimeAgentReadyReplacesEarlierControlStageConnection();
    TestLateControlAgentReadyDoesNotRegressRuntimeStage();
    TestScriptCreateStillTargetsRuntimeAgentAfterLateControlReady();
    TestSpawnRequestReplaysRuntimeAgentReadyToBoundHost();
    TestSpawnRuntimeReadyReplayDoesNotSendCachedScriptMessagesAfterAgentReadyForwardFailure();
    TestSpawnSuccessResponseSendFailureClearsSpawnTransactionState();
    TestSpawnRequestUsesAuthoritativeAgentReadyPidInsteadOfInjectorPid();
    TestSpawnRequestUsesRuntimeReadyStateWithoutCachedReadyFrame();
    TestSpawnRequestHoldsRuntimeReadyUntilResponseEvenIfPendingSpawnClearsEarly();
    TestSpawnRequestHostDisconnectDuringFinalizeDoesNotReplayToClosedHost();
    TestAgentReadyWithMismatchedSpawnTokenDoesNotResolvePendingSpawn();
    TestAgentReadyWithMatchingSpawnTokenButWrongPidDoesNotResolvePendingSpawn();
    TestOrphanSpawnTokenAgentReadyClearsPreRegisteredAgentSession();
    TestControlStageAgentReadyWithMatchingSpawnTokenResolvesPendingSpawn();
    TestControlStageAgentReadyForSpawnedChildDoesNotInjectBeforeFinalize();
    TestControlStageAgentReadyResolvesPendingSpawnWithoutImmediatePromotion();
    TestControlStageAgentReadyDoesNotInjectAgainWhenRuntimeSessionAlreadyPresent();
    TestControlStageAgentReadyPromotesBeforeFinalizeCompletes();
    TestSpawnRequestDoesNotLatePromoteChildAlreadyPromotedByControlReady();
    TestScriptMessageForwardsToBoundHost();
    TestScriptMessageAfterDetachIsNotReplayedToNextAttach();
    TestStaleAgentAfterHostCloseIsNotAcceptedForReboundHost();
    TestStaleAgentResponseAfterHostCloseIsNotAcceptedForReboundHost();
    TestInvalidatedPidRejectsContextFreeAgentReadyAfterHostClose();
    TestSpawnRequestReplaysEarlyScriptMessageToBoundHost();
    TestScriptPostForwardsToBoundAgent();
    TestScriptPostDoesNotTargetControlFallbackAfterRuntimeDisconnect();
    TestScriptCreateForwardsToBoundAgent();
    TestScriptCreateRequiresRuntimeReadyForSpawn();
    TestScriptCreateDoesNotTargetControlFallbackAfterRuntimeDisconnect();
    TestScriptCreateDoesNotTargetMismatchedRuntimeAgentForSpawn();
    TestScriptCreateRespFromMismatchedRuntimeAgentIsDroppedForSpawn();
    TestScriptCreateRespForSpawnUsesSuspendedHostOwnership();
    TestScriptCreateForSpawnUsesSuspendedOwnerWhenPidBindingIsRebound();
    TestScriptCreateForReboundHostDoesNotTargetForeignSuspendedSpawn();
    TestRuntimeReadyForwardForSpawnUsesSuspendedHostOwnership();
    TestRuntimeReadyForwardDoesNotFallbackToReboundHostWhenSuspendedOwnerSessionIsMissing();
    TestScriptMessageFromMismatchedCurrentRuntimeAgentIsDroppedForSpawn();
    TestLateControlAgentReadyDoesNotRegressTransactionRuntimeBoundaryWithoutGlobalRuntimeState();
    TestControlReadyCanReplaceMismatchedGlobalRuntimeTraceForSpawnTarget();
    TestMismatchedRuntimeAgentReadyDoesNotKeepPreRegisteredWrongRuntimeSession();
    TestLateControlAgentReadyAtRuntimeBoundaryDoesNotKeepPreRegisteredCurrentSession();
    TestScriptCreateReturnsImmediateErrorWithoutAgentSession();
    TestScriptCreateRespForwardsToBoundHost();
    TestScriptLoadForwardsToBoundAgent();
    TestScriptLoadRequiresAuthoritativeAgentReadyForSpawn();
    TestScriptLoadReturnsImmediateErrorWithoutAgentSession();
    TestScriptLoadRespForwardsToBoundHost();
    TestScriptLoadRespRestoresReadyForScriptLoadStateForSpawn();
    TestScriptLoadRespWithoutSuspendedOwnerHostStillRestoresReadyState();
    TestScriptLoadRespHostSendFailureStillRestoresReadyState();
    TestScriptUnloadForwardsToBoundAgent();
    TestScriptUnloadReturnsImmediateErrorForInvalidRequest();
    TestScriptUnloadReturnsImmediateErrorWhenRegistryUnavailable();
    TestScriptUnloadReturnsImmediateErrorWithoutAgentSession();
    TestScriptUnloadReturnsImmediateErrorWithoutBoundPid();
    TestScriptUnloadRespForwardsToBoundHost();
    TestRpcRequestForwardsToBoundAgent();
    TestRpcRequestReturnsImmediateErrorForInvalidRequest();
    TestRpcRequestReturnsImmediateErrorWhenRegistryUnavailable();
    TestRpcRequestReturnsImmediateErrorWithoutAgentSession();
    TestRpcRequestReturnsImmediateErrorWithoutBoundPid();
    TestRpcRequestDoesNotTargetControlFallbackAfterRuntimeDisconnect();
    TestScriptUnloadDoesNotTargetControlFallbackAfterRuntimeDisconnect();
    TestRpcResponseForwardsToBoundHost();
    TestProcessListRequestReturnsProcesses();
    TestAppListRequestReturnsApps();
    return 0;
}
