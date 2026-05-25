#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
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

class FailingSendTransport final : public Transport {
public:
    FailingSendTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }

    ssize_t Send(const uint8_t*, size_t) override { return -1; }
    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "FailingSend"; }
};

class FailAfterSuccessfulSendsTransport final : public Transport {
public:
    explicit FailAfterSuccessfulSendsTransport(int successful_sends_before_failure)
        : successful_sends_before_failure_(successful_sends_before_failure) {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }

    ssize_t Send(const uint8_t* data, size_t len) override {
        if (send_calls_ >= successful_sends_before_failure_) {
            return -1;
        }
        ++send_calls_;
        sent_.insert(sent_.end(), data, data + len);
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "FailAfterSuccessfulSends"; }

    bool SendAll(const uint8_t* data, size_t len) override {
        ++frame_send_attempts_;
        if (frame_send_calls_ >= successful_sends_before_failure_) {
            return false;
        }
        ++frame_send_calls_;
        sent_.insert(sent_.end(), data, data + len);
        return true;
    }

    int GetFrameSendAttempts() const { return frame_send_attempts_; }
    int GetFrameSendSuccesses() const { return frame_send_calls_; }

private:
    int successful_sends_before_failure_;
    int send_calls_ = 0;
    int frame_send_calls_ = 0;
    int frame_send_attempts_ = 0;
    std::vector<uint8_t> sent_;
};

class CallbackOnSendTransport final : public Transport {
public:
    explicit CallbackOnSendTransport(std::function<void()> on_send)
        : on_send_(std::move(on_send)) {
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
    const char* GetTypeName() const override { return "CallbackOnSend"; }

    bool SendAll(const uint8_t* data, size_t len) override {
        if (on_send_) {
            on_send_();
        }
        sent_.insert(sent_.end(), data, data + len);
        return true;
    }

    std::vector<uint8_t> TakeSent() {
        std::vector<uint8_t> out = sent_;
        sent_.clear();
        return out;
    }

private:
    std::function<void()> on_send_;
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
        if (error_message != nullptr) {
            error_message->clear();
        }
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

    bool InjectAgent(int pid,
                     const std::string& agent_path,
                     const std::string& ready_token,
                     std::string* error_message) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_inject_pid = pid;
            last_inject_agent_path = agent_path;
            last_inject_ready_token = ready_token;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    bool InjectSpawnChildAgent(int pid,
                               const std::string& agent_path,
                               std::string* error_message) override {
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
    bool finalize_block_until_release = false;
    std::atomic<bool> allow_finalize_return{true};
    std::string finalize_error = "finalize failed";
    mutable std::mutex mutex_;
    std::string last_agent_path;
    int last_inject_pid = 0;
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

void TestAttachRequestWithoutTargetReturnsResolveFailure() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AttachRequest request;
    Frame frame(MessageType::kAttachRequest, 9u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAttachResponse);
    assert(response_frame.GetMsgId() == 9u);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.session_id == 0u);
    assert(response.pid == 0u);
    assert(response.process_name.empty());
    assert(response.error.code == -2);
    assert(!response.error.message.empty());
}

void TestAttachRequestResolveFailureReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher,
                           &registry,
                           &injector,
                           ServerHandlerConfig{
                               .enumerate_processes = []() {
                                   return std::vector<ProcessInfo>{};
                               },
                           });

    AttachRequest request;
    request.identifier = "com.missing.target";
    Frame frame(MessageType::kAttachRequest, 10u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAttachResponse);
    assert(response_frame.GetMsgId() == 10u);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.session_id == 0u);
    assert(response.pid == 0u);
    assert(response.process_name.empty());
    assert(response.error.code == -2);
    assert(!response.error.message.empty());
}

void TestAttachRequestWithoutInjectorReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeProcessManager process_manager;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher,
                           &registry,
                           nullptr,
                           ServerHandlerConfig{
                               .agent_path = "/data/local/tmp/nook/libnook-agent.so",
                               .enumerate_processes = [&process_manager]() {
                                   return process_manager.EnumerateProcesses();
                               },
                           });

    AttachRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kAttachRequest, 11u, EncodeAttachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAttachResponse);
    assert(response_frame.GetMsgId() == 11u);

    AttachResponse response;
    assert(DecodeAttachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.session_id == 0u);
    assert(response.pid == 200u);
    assert(response.process_name == "com.demo.target");
    assert(response.error.code == -3);
    assert(response.error.message == "attach injector failed");
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
}

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
    assert(registry.FindHostSessionByPid(24567) == &host);
    assert(registry.IsSpawnSuspended(24567));
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
    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame.GetPayload().data(),
                               response_frame.GetPayload().size(),
                               &response));
    assert(response.pid == 0u);
    assert(response.error.code == -4);
}

void TestLateAuthoritativeAgentReadyAfterSpawnTimeoutDoesNotLeaveBoundState() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 24568;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "/data/local/tmp/nook/libnook-agent.so",
        .spawn_ready_timeout_ms = 10,
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 14u, EncodeSpawnRequest(request));
    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    AgentReady ready;
    ready.pid = 24568u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = WaitForSpawnTokenArg(&injector);
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;
    assert(!ready.spawn_token.empty());
    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 141u, EncodeAgentReady(ready))));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame.GetPayload().data(),
                               response_frame.GetPayload().size(),
                               &response));
    assert(response.pid == 0u);
    assert(response.error.code == -4);

    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(ready.spawn_token, &pending));
    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(24568, &entry));
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
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

    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kAgentReady, 20u, EncodeAgentReady(ready))));

    assert(host_wire->TakeSent().empty());
}

void TestOrphanSpawnTokenAgentReadyWithoutPendingOrBoundHostIsDropped() {
    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60005;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-stale";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kAgentReady, 340u, EncodeAgentReady(ready))));

    assert(registry.FindAgentSessionByPid(60005) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.target") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(60005));
    assert(!registry.IsAgentRuntimeReady(60005));
    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(60005, &cached_ready));
}

void TestMismatchedPendingAttachAgentReadyIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingAttach("attach-token-expected",
                                   60006,
                                   "com.demo.target",
                                   host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60006;
    ready.process_name = "com.demo.other";
    ready.spawn_token = "attach-token-expected";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kAgentReady, 341u, EncodeAgentReady(ready))));

    assert(registry.FindAgentSessionByPid(60006) == nullptr);
    assert(registry.FindAgentSessionByProcessName("com.demo.other") == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(60006));
    assert(!registry.IsAgentRuntimeReady(60006));

    PendingAttachEntry pending_attach;
    assert(registry.GetPendingAttach("attach-token-expected", &pending_attach));

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrame(60006, &cached_ready));
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
    assert(pending.ready);
    assert(pending.pid == 60002);
    assert(pending.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(pending.resolved_process_name == "zygote64");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60002, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "zygote64");
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

    assert(dispatcher.Dispatch(agent, Frame(MessageType::kAgentReady, 3441u, EncodeAgentReady(ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-expected", &pending));
    assert(pending.spawn_token == "spawn-token-expected");
    assert(!pending.ready);
    assert(pending.pid <= 0);

    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(60077, &entry));
    assert(registry.FindHostSessionByPid(60077) == nullptr);
}

void TestRuntimeStageAgentReadyUpgradesPendingSpawnResolutionStage() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-upgrade", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady control_ready;
    control_ready.pid = 60009;
    control_ready.process_name = "zygote64";
    control_ready.spawn_token = "spawn-token-upgrade";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 360u, EncodeAgentReady(control_ready))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 361u, EncodeAgentReady(runtime_ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-upgrade", &pending));
    assert(pending.ready);
    assert(pending.pid == 60009);
    assert(pending.ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(pending.resolved_process_name == "com.demo.target");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60009, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.authoritative_process_name == "com.demo.target");
}

void TestRuntimeStageAgentReadyFromDifferentPidDoesNotStealControlResolvedSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-upgrade", "com.demo.target", host.GetId());

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady control_ready;
    control_ready.pid = 60009;
    control_ready.process_name = "zygote64";
    control_ready.spawn_token = "spawn-token-upgrade";
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 3601u, EncodeAgentReady(control_ready))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.pid = 60010;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 3602u, EncodeAgentReady(runtime_ready))));

    PendingSpawnEntry pending;
    assert(registry.GetPendingSpawn("spawn-token-upgrade", &pending));
    assert(pending.ready);
    assert(pending.pid == 60009);
    assert(pending.ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(pending.resolved_process_name == "zygote64");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60009, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "zygote64");

    SpawnSuspendedEntry other_entry;
    assert(!registry.GetSpawnSuspendedEntry(60010, &other_entry));
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
}

void TestRuntimeStageAgentReadyUpgradesExistingSpawnSuspendedEntryWithoutPendingSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60007);
    registry.MarkSpawnSuspended(60007,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60007;
    ready.process_name = "com.demo.target";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 349u, EncodeAgentReady(ready))));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60007, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.authoritative_process_name == "com.demo.target");
    assert(entry.target_process_name == "com.demo.target");
}

void TestControlStageAgentReadyPromotesOnlyAfterFinalizeCompletes() {
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
    Frame frame(MessageType::kSpawnRequest, 15u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    std::string spawn_token;
    const auto token_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < token_deadline) {
        spawn_token = FindSpawnTokenArg(injector.GetLastRequest());
        if (!spawn_token.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!spawn_token.empty());

    AgentReady ready;
    ready.pid = 60008u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = spawn_token;
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 351u, EncodeAgentReady(ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60008, 50));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 15u) {
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
    assert(WaitForInjectedPid(&injector, 60008, 2000));
    assert(injector.GetLastInjectAgentPath() == "__embedded_agent__");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60008, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
}

void TestRuntimeReadyDuringFinalizeIsReplayedAfterSpawnResponseOnlyOnce() {
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
    injector.spawn_pid = 60010;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 365u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60010u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 366u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60010, 50));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 367u, EncodeAgentReady(runtime_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[0].GetMsgId() == 365u);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    SpawnResponse response;
    assert(DecodeSpawnResponse(frames[0].GetPayload().data(),
                               frames[0].GetPayload().size(),
                               &response));
    assert(response.pid == 60010u);
    assert(response.error.code == 0);
    assert(injector.GetLastInjectPid() == 0);

    AgentReady replayed_ready;
    assert(DecodeAgentReady(frames[1].GetPayload().data(),
                            frames[1].GetPayload().size(),
                            &replayed_ready));
    assert(replayed_ready.pid == 60010u);
    assert(replayed_ready.stage == AgentReadyStage::kRuntime);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60010, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestRuntimeReadyAfterBindButBeforeSpawnResponseIsHeld() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.RegisterPendingSpawn("spawn-token-early-runtime",
                                  "com.demo.target",
                                  host.GetId());
    assert(registry.ResolvePendingSpawn("spawn-token-early-runtime",
                                        60011,
                                        "com.demo.target",
                                        AgentReadyStage::kControl));

    PendingSpawnEntry pending;
    assert(registry.BindHostToResolvedPendingSpawn("spawn-token-early-runtime",
                                                   60011,
                                                   &pending));
    assert(pending.pid == 60011);

    SpawnSuspendedEntry bound_entry;
    assert(registry.GetSpawnSuspendedEntry(60011, &bound_entry));
    assert(bound_entry.response_pending);
    assert(bound_entry.state == SpawnTransactionState::kWaitingRuntimeReady);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 60011u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.spawn_token = "spawn-token-early-runtime";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3671u,
                                     EncodeAgentReady(runtime_ready))));

    assert(host_wire->TakeSent().empty());

    Frame cached_ready;
    assert(registry.GetAgentReadyFrameByIdentity(60011, "com.demo.target", &cached_ready));

    SpawnSuspendedEntry updated_entry;
    assert(registry.GetSpawnSuspendedEntry(60011, &updated_entry));
    assert(updated_entry.response_pending);
    assert(updated_entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(updated_entry.state == SpawnTransactionState::kWaitingRuntimeReady);
}

void TestRuntimeReadyAtSpawnResponseSendBoundaryDoesNotPrecedeSpawnResponse() {
    SessionRegistry registry;
    std::unique_ptr<MessageDispatcher> dispatcher;
    std::unique_ptr<FakeInjector> injector;
    std::string spawn_token;
    bool injected_runtime_ready = false;

    auto host_transport = std::make_unique<CallbackOnSendTransport>([&]() {
        if (injected_runtime_ready || spawn_token.empty()) {
            return;
        }
        injected_runtime_ready = true;

        auto runtime_transport = std::make_unique<CaptureTransport>();
        Session runtime_agent(std::move(runtime_transport));

        AgentReady runtime_ready;
        runtime_ready.pid = 600111u;
        runtime_ready.process_name = "com.demo.target";
        runtime_ready.spawn_token = spawn_token;
        runtime_ready.arch = "arm64";
        runtime_ready.version = "0.1.0";
        runtime_ready.stage = AgentReadyStage::kRuntime;
        assert(dispatcher->Dispatch(runtime_agent,
                                    Frame(MessageType::kAgentReady,
                                          3672u,
                                          EncodeAgentReady(runtime_ready))));
    });
    CallbackOnSendTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    registry.RegisterHostSession(&host);

    injector = std::make_unique<FakeInjector>();
    injector->spawn_pid = 600111;
    injector->finalize_block_until_release = true;
    injector->allow_finalize_return.store(false);

    dispatcher = std::make_unique<MessageDispatcher>();
    RegisterServerHandlers(dispatcher.get(), &registry, injector.get(), ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3673u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher->Dispatch(host, frame));
    });

    spawn_token = WaitForSpawnTokenArg(injector.get());
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600111u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher->Dispatch(control_agent,
                                Frame(MessageType::kAgentReady,
                                      3674u,
                                      EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(injector.get(), 600111, 50));

    injector->allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(injected_runtime_ready);
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);
}

void TestRuntimeReadyDuringFinalizeRemainsHeldAfterPendingSpawnCleared() {
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
    injector.spawn_pid = 60026;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 386u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60026u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 387u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60026, 50));
    registry.MarkSpawnSuspended(60026,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target",
                                spawn_token);

    registry.ClearPendingSpawn(spawn_token);

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 388u, EncodeAgentReady(runtime_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[0].GetMsgId() == 386u);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    SpawnResponse response;
    assert(DecodeSpawnResponse(frames[0].GetPayload().data(),
                               frames[0].GetPayload().size(),
                               &response));
    assert(response.pid == 60026u);
    assert(response.error.code == 0);

    AgentReady replayed_ready;
    assert(DecodeAgentReady(frames[1].GetPayload().data(),
                            frames[1].GetPayload().size(),
                            &replayed_ready));
    assert(replayed_ready.pid == 60026u);
    assert(replayed_ready.stage == AgentReadyStage::kRuntime);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60026, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestRuntimeReadyAfterPendingSpawnConsumedButWithBoundSpawnContextIsNotDropped() {
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
    injector.spawn_pid = 600261;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
        .on_spawn_context_bound = [&](int pid, const std::string& spawn_token) {
            assert(pid == 600261);

            PendingSpawnEntry pending;
            assert(!registry.GetPendingSpawn(spawn_token, &pending));

            AgentReady runtime_ready;
            runtime_ready.pid = 600261u;
            runtime_ready.process_name = "com.demo.target";
            runtime_ready.spawn_token = spawn_token;
            runtime_ready.arch = "arm64";
            runtime_ready.version = "0.1.0";
            runtime_ready.stage = AgentReadyStage::kRuntime;
            assert(dispatcher.Dispatch(runtime_agent,
                                       Frame(MessageType::kAgentReady,
                                             3881u,
                                             EncodeAgentReady(runtime_ready))));
        },
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3880u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600261u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady,
                                     3882u,
                                     EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2u);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    SpawnResponse response;
    assert(DecodeSpawnResponse(frames[0].GetPayload().data(),
                               frames[0].GetPayload().size(),
                               &response));
    assert(response.pid == 600261u);
    assert(response.error.code == 0);

    AgentReady replayed_ready;
    assert(DecodeAgentReady(frames[1].GetPayload().data(),
                            frames[1].GetPayload().size(),
                            &replayed_ready));
    assert(replayed_ready.pid == 600261u);
    assert(replayed_ready.stage == AgentReadyStage::kRuntime);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600261, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestFinalizeWithoutRegisteredHostDoesNotLeaveGhostSuspendedSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;

    FakeInjector injector;
    injector.spawn_pid = 60027;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 389u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60027u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 390u, EncodeAgentReady(control_ready))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 391u, EncodeAgentReady(runtime_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    assert(host_wire->TakeSent().empty());

    assert(registry.FindHostSession(host.GetId()) == nullptr);
    SpawnSuspendedEntry entry;
    assert(!registry.GetSpawnSuspendedEntry(60027, &entry));

    PendingSpawnEntry pending;
    assert(!registry.GetPendingSpawn(spawn_token, &pending));
}

void TestRuntimeReadyReplayRequiresMatchingAuthoritativeProcessName() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60018;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 383u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60018u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 384u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60018, 50));

    registry.UpdateSpawnSuspendedAuthoritativeReady(60018,
                                                    PendingSpawnReadyStage::kRuntimeReady,
                                                    "com.demo.target");

    AgentReady mismatched_runtime_ready = control_ready;
    mismatched_runtime_ready.process_name = "com.demo.other";
    mismatched_runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(60018,
                                  Frame(MessageType::kAgentReady, 385u, EncodeAgentReady(mismatched_runtime_ready)));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
}

void TestMismatchedRuntimeReadyDoesNotUpgradeControlResolvedSpawn() {
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
    injector.spawn_pid = 60019;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 386u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60019u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 387u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60019, 50));

    AgentReady mismatched_runtime_ready = control_ready;
    mismatched_runtime_ready.process_name = "com.demo.other";
    mismatched_runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 388u, EncodeAgentReady(mismatched_runtime_ready))));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60019, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
    assert(entry.target_process_name == "com.demo.target");
}

void TestMismatchedRuntimeReadyWithoutPendingSpawnDoesNotUpgradeTargetBoundSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60021);
    registry.MarkSpawnSuspended(60021,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60021;
    ready.process_name = "com.demo.other";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 389u, EncodeAgentReady(ready))));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60021, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "zygote64");
    assert(entry.target_process_name == "com.demo.target");
    assert(!registry.IsAgentRuntimeReady(60021));
    assert(registry.FindRuntimeReadyAgentSessionByIdentity(60021, "com.demo.other") == nullptr);

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrameByIdentity(60021, "com.demo.other", &cached_ready));
}

void TestRuntimeReadyWithWrongSpawnTokenDoesNotUpgradeExistingSpawnContext() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60031);
    registry.MarkSpawnSuspended(60031,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target",
                                "spawn-token-expected");

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady ready;
    ready.pid = 60031;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-wrong";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kRuntime;

    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 3891u, EncodeAgentReady(ready))));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60031, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
    assert(entry.target_process_name == "com.demo.target");
    assert(!registry.IsAgentRuntimeReady(60031));

    Frame cached_ready;
    assert(!registry.GetAgentReadyFrameByIdentity(60031, "com.demo.target", &cached_ready));
}

void TestRuntimeReadyReboundClearsPreviousPidBindingForSameSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60041);
    registry.MarkSpawnSuspended(60041,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    registry.RegisterAgentSession(60040, &runtime_agent);
    registry.RegisterAgentProcessName(60040, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60040);
    registry.MarkAgentReadyStage(60040, AgentReadyStage::kControl);
    runtime_agent.SetPeerPid(60040);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 60041u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 3892u, EncodeAgentReady(runtime_ready))));

    assert(registry.FindAgentSessionByPid(60040) == nullptr);
    assert(registry.FindAgentSessionByPid(60041) == &runtime_agent);
    assert(runtime_agent.GetPeerPid() == 60041);
}

void TestFinalizeUsesSpawnSuspendedAuthoritativeStageOverGlobalRuntimeState() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60011;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 368u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60011u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 369u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60011, 50));

    registry.UpdateSpawnSuspendedAuthoritativeReady(60011,
                                                    PendingSpawnReadyStage::kRuntimeReady,
                                                    "com.demo.target");
    AgentReady cached_ready = control_ready;
    cached_ready.stage = AgentReadyStage::kRuntime;
    registry.StoreAgentReadyFrame(60011,
                                  Frame(MessageType::kAgentReady, 370u, EncodeAgentReady(cached_ready)));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60011, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
}

void TestSpawnFinalizeReplaySendFailureClearsSpawnTransactionState() {
    auto host_transport = std::make_unique<FailingSendTransport>();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 600142;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3793u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600142u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 3794u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 600142, 50));

    ScriptMessage message;
    message.script_id = 11u;
    message.message = "{\"type\":\"send\",\"payload\":\"replay-after-finalize\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage, 3795u, EncodeScriptMessage(message))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 3796u, EncodeAgentReady(runtime_ready))));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(600142);
    assert(cached_frames.empty());
    assert(!registry.IsSpawnSuspended(600142));
    assert(registry.FindPidByHostSession(host.GetId()) < 0);
}

void TestSpawnFinalizeReplayPartialScriptMessageFailureKeepsOnlyUnsentCachedMessages() {
    auto host_transport = std::make_unique<FailAfterSuccessfulSendsTransport>(3);
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 600144;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3797u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600144u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady,
                                     3798u,
                                     EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 600144, 50));

    ScriptMessage first_message;
    first_message.script_id = 11u;
    first_message.message = "{\"type\":\"send\",\"payload\":\"already-sent-after-finalize\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage,
                                     3799u,
                                     EncodeScriptMessage(first_message))));

    ScriptMessage second_message;
    second_message.script_id = 12u;
    second_message.message = "{\"type\":\"send\",\"payload\":\"not-sent-after-finalize\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage,
                                     3800u,
                                     EncodeScriptMessage(second_message))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3801u,
                                     EncodeAgentReady(runtime_ready))));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(600144);
    assert(cached_frames.size() == 1u);

    ScriptMessage cached_message;
    assert(DecodeScriptMessage(cached_frames[0].GetPayload().data(),
                               cached_frames[0].GetPayload().size(),
                               &cached_message));
    assert(cached_message.script_id == 12u);
    assert(cached_message.message == "{\"type\":\"send\",\"payload\":\"not-sent-after-finalize\"}");
}

void TestSpawnFinalizeReplayDoesNotSendCachedScriptMessagesAfterAgentReadyReplayFailure() {
    auto host_transport = std::make_unique<FailAfterSuccessfulSendsTransport>(1);
    FailAfterSuccessfulSendsTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 600145;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3802u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600145u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady,
                                     3803u,
                                     EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 600145, 50));

    ScriptMessage message;
    message.script_id = 13u;
    message.message = "{\"type\":\"send\",\"payload\":\"must-stay-cached-after-ready-fail\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage,
                                     3804u,
                                     EncodeScriptMessage(message))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3805u,
                                     EncodeAgentReady(runtime_ready))));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    assert(host_wire->GetFrameSendAttempts() == 2);
    assert(host_wire->GetFrameSendSuccesses() == 1);

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(600145);
    assert(cached_frames.size() == 1u);
    assert(cached_frames[0].GetType() == MessageType::kScriptMessage);
}

void TestSpawnFinalizeReplayWithoutCachedAgentReadyStillSendsCachedScriptMessages() {
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
    injector.spawn_pid = 600146;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3806u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 600146u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady,
                                     3807u,
                                     EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 600146, 50));

    ScriptMessage message;
    message.script_id = 14u;
    message.message = "{\"type\":\"send\",\"payload\":\"replay-without-ready-frame\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage,
                                     3808u,
                                     EncodeScriptMessage(message))));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3809u,
                                     EncodeAgentReady(runtime_ready))));

    registry.RegisterAgentProcessName(600146, "com.demo.other");

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kScriptMessage);

    ScriptMessage replayed;
    assert(DecodeScriptMessage(frames[1].GetPayload().data(),
                               frames[1].GetPayload().size(),
                               &replayed));
    assert(replayed.script_id == 14u);
    assert(replayed.message == "{\"type\":\"send\",\"payload\":\"replay-without-ready-frame\"}");
    assert(registry.TakeScriptMessageFrames(600146).empty());
}

void TestFinalizeUsesCurrentSpawnSuspendedRuntimeIdentityForReplay() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60012;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 3681u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60012u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 3691u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60012, 50));
    registry.MarkSpawnSuspended(60012,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target",
                                spawn_token);

    registry.UpdateSpawnSuspendedAuthoritativeReady(60012,
                                                    PendingSpawnReadyStage::kRuntimeReady,
                                                    "com.demo.runtime");

    AgentReady runtime_ready = control_ready;
    runtime_ready.process_name = "com.demo.runtime";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(60012, &control_agent);
    registry.RegisterAgentProcessName(60012, "com.demo.runtime");
    registry.MarkAgentReadyStage(60012, AgentReadyStage::kRuntime);
    registry.MarkAgentAuthoritativeReady(60012);
    registry.MarkAgentRuntimeReady(60012);
    registry.StoreAgentReadyFrame(60012,
                                  Frame(MessageType::kAgentReady, 3701u, EncodeAgentReady(runtime_ready)));

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);

    AgentReady replayed_ready;
    assert(DecodeAgentReady(frames[1].GetPayload().data(),
                            frames[1].GetPayload().size(),
                            &replayed_ready));
    assert(replayed_ready.stage == AgentReadyStage::kRuntime);
    assert(replayed_ready.process_name == "com.demo.runtime");
}

void TestFinalizeDoesNotPromoteFromGlobalRuntimeReadyWithoutAuthoritativeUpgrade() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60015;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 380u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60015u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 381u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60015, 50));

    registry.MarkAgentRuntimeReady(60015);

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60015, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

void TestScriptMessageDuringFinalizeIsReplayedAfterSpawnReadyBoundary() {
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
    injector.spawn_pid = 60012;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 371u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60012u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 372u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60012, 50));

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 373u, EncodeAgentReady(runtime_ready))));

    ScriptMessage message;
    message.script_id = 7u;
    message.message = "{\"type\":\"send\",\"payload\":\"early\"}";
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptMessage, 374u, EncodeScriptMessage(message))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 3);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);
    assert(frames[1].GetType() == MessageType::kAgentReady);
    assert(frames[2].GetType() == MessageType::kScriptMessage);

    ScriptMessage replayed;
    assert(DecodeScriptMessage(frames[2].GetPayload().data(),
                               frames[2].GetPayload().size(),
                               &replayed));
    assert(replayed.script_id == 7u);
    assert(replayed.message == "{\"type\":\"send\",\"payload\":\"early\"}");
}

void TestScriptMessageForReadySpawnForwardsImmediately() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60013);
    registry.MarkSpawnSuspended(60013,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target");
    registry.UpdateSpawnState(60013, SpawnTransactionState::kReadyForScriptLoad);
    registry.RegisterAgentSession(60013, &runtime_agent);
    registry.RegisterAgentProcessName(60013, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60013);
    registry.MarkAgentReadyStage(60013, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60013);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(60013);
    ScriptMessage message;
    message.script_id = 8u;
    message.message = "{\"type\":\"send\",\"payload\":\"ready\"}";
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptMessage, 375u, EncodeScriptMessage(message))));

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1);
    assert(frames[0].GetType() == MessageType::kScriptMessage);

    ScriptMessage forwarded;
    assert(DecodeScriptMessage(frames[0].GetPayload().data(),
                               frames[0].GetPayload().size(),
                               &forwarded));
    assert(forwarded.script_id == 8u);
    assert(forwarded.message == "{\"type\":\"send\",\"payload\":\"ready\"}");
}

void TestScriptMessageFromNonAuthoritativeRuntimeSessionIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60023);
    registry.MarkSpawnSuspended(60023,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");
    registry.RegisterAgentSession(60023, &control_agent);
    registry.RegisterControlReadyAgentSession(60023, &control_agent);
    registry.RegisterAgentProcessName(60023, "com.demo.target");
    registry.MarkAgentReadyStage(60023, AgentReadyStage::kControl);
    registry.MarkAgentAuthoritativeReady(60023);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady mismatched_runtime_ready;
    mismatched_runtime_ready.pid = 60023u;
    mismatched_runtime_ready.process_name = "com.demo.other";
    mismatched_runtime_ready.arch = "arm64";
    mismatched_runtime_ready.version = "0.1.0";
    mismatched_runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 376u, EncodeAgentReady(mismatched_runtime_ready))));

    ScriptMessage message;
    message.script_id = 9u;
    message.message = "{\"type\":\"send\",\"payload\":\"wrong-session\"}";
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptMessage, 377u, EncodeScriptMessage(message))));

    assert(host_wire->TakeSent().empty());
    assert(registry.TakeScriptMessageFrames(60023).empty());
}

void TestControlOnlySpawnDefersCachedScriptMessageUntilRuntimeReady() {
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
    injector.spawn_pid = 60014;
    injector.finalize_block_until_release = true;
    injector.allow_finalize_return.store(false);

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";
    Frame frame(MessageType::kSpawnRequest, 376u, EncodeSpawnRequest(request));

    std::thread spawn_thread([&]() {
        assert(dispatcher.Dispatch(host, frame));
    });

    const std::string spawn_token = WaitForSpawnTokenArg(&injector);
    assert(!spawn_token.empty());

    AgentReady control_ready;
    control_ready.pid = 60014u;
    control_ready.process_name = "com.demo.target";
    control_ready.spawn_token = spawn_token;
    control_ready.arch = "arm64";
    control_ready.version = "0.1.0";
    control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kAgentReady, 377u, EncodeAgentReady(control_ready))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!WaitForInjectedPid(&injector, 60014, 50));

    ScriptMessage message;
    message.script_id = 9u;
    message.message = "{\"type\":\"send\",\"payload\":\"control-only\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage, 378u, EncodeScriptMessage(message))));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host_wire->TakeSent().empty());

    injector.allow_finalize_return.store(true);
    spawn_thread.join();

    std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 1);
    assert(frames[0].GetType() == MessageType::kSpawnResponse);

    AgentReady runtime_ready = control_ready;
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(60014, &runtime_agent);
    registry.RegisterAgentProcessName(60014, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60014);
    registry.MarkAgentReadyStage(60014, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60014);
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 379u, EncodeAgentReady(runtime_ready))));

    frames = ParseFrames(host_wire->TakeSent());
    assert(frames.size() == 2);
    assert(frames[0].GetType() == MessageType::kAgentReady);
    assert(frames[1].GetType() == MessageType::kScriptMessage);

    ScriptMessage replayed;
    assert(DecodeScriptMessage(frames[1].GetPayload().data(),
                               frames[1].GetPayload().size(),
                               &replayed));
    assert(replayed.script_id == 9u);
    assert(replayed.message == "{\"type\":\"send\",\"payload\":\"control-only\"}");
    assert(registry.TakeScriptMessageFrames(60014).empty());
}

void TestRuntimeReadyReplaySendFailureKeepsCachedScriptMessages() {
    auto host_transport = std::make_unique<FailingSendTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600141);
    registry.MarkSpawnSuspended(600141,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600141, SpawnTransactionState::kWaitingRuntimeReady);

    ScriptMessage message;
    message.script_id = 10u;
    message.message = "{\"type\":\"send\",\"payload\":\"replay-after-runtime-ready\"}";
    registry.StoreScriptMessageFrame(600141,
                                     Frame(MessageType::kScriptMessage, 3791u, EncodeScriptMessage(message)));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 600141u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady, 3792u, EncodeAgentReady(runtime_ready))));

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(600141);
    assert(cached_frames.size() == 1u);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600141, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);

    ScriptMessage cached_message;
    assert(DecodeScriptMessage(cached_frames[0].GetPayload().data(),
                               cached_frames[0].GetPayload().size(),
                               &cached_message));
    assert(cached_message.script_id == 10u);
    assert(cached_message.message == "{\"type\":\"send\",\"payload\":\"replay-after-runtime-ready\"}");
}

void TestRuntimeReadyReplayPartialSendFailureKeepsOnlyUnsentCachedScriptMessages() {
    auto host_transport = std::make_unique<FailAfterSuccessfulSendsTransport>(2);
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600143);
    registry.MarkSpawnSuspended(600143,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600143, SpawnTransactionState::kWaitingRuntimeReady);

    ScriptMessage first_message;
    first_message.script_id = 10u;
    first_message.message = "{\"type\":\"send\",\"payload\":\"already-sent\"}";
    registry.StoreScriptMessageFrame(600143,
                                     Frame(MessageType::kScriptMessage,
                                           3793u,
                                           EncodeScriptMessage(first_message)));

    ScriptMessage second_message;
    second_message.script_id = 11u;
    second_message.message = "{\"type\":\"send\",\"payload\":\"not-sent\"}";
    registry.StoreScriptMessageFrame(600143,
                                     Frame(MessageType::kScriptMessage,
                                           3794u,
                                           EncodeScriptMessage(second_message)));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 600143u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3795u,
                                     EncodeAgentReady(runtime_ready))));

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(600143);
    assert(cached_frames.size() == 1u);

    ScriptMessage cached_message;
    assert(DecodeScriptMessage(cached_frames[0].GetPayload().data(),
                               cached_frames[0].GetPayload().size(),
                               &cached_message));
    assert(cached_message.script_id == 11u);
    assert(cached_message.message == "{\"type\":\"send\",\"payload\":\"not-sent\"}");
}

void TestRuntimeReadyWithoutBoundHostStillPromotesVisibleSpawnState() {
    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.MarkSpawnSuspended(600144,
                                77u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    assert(registry.SetSpawnResponsePending(600144, false));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady runtime_ready;
    runtime_ready.pid = 600144u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kAgentReady,
                                     3797u,
                                     EncodeAgentReady(runtime_ready))));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600144, &entry));
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadRespFromNonCurrentAgentSessionIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto current_transport = std::make_unique<CaptureTransport>();
    Session current_agent(std::move(current_transport));

    auto stale_transport = std::make_unique<CaptureTransport>();
    Session stale_agent(std::move(stale_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60024);
    registry.RegisterAgentSession(60024, &current_agent);
    registry.RegisterAgentProcessName(60024, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60024);
    registry.MarkAgentReadyStage(60024, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60024);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    stale_agent.SetPeerPid(60024);
    ScriptResponse response;
    response.script_id = 11u;
    response.success = true;
    assert(dispatcher.Dispatch(stale_agent,
                               Frame(MessageType::kScriptLoadResp, 380u, EncodeScriptResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestScriptLoadRespRestoresReadyForScriptLoadStateForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 60028);
    registry.MarkSpawnSuspended(60028,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(60028, SpawnTransactionState::kScriptLoadDispatched);
    registry.RegisterAgentSession(60028, &runtime_agent);
    registry.RegisterAgentProcessName(60028, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60028);
    registry.MarkAgentReadyStage(60028, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60028);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(60028);
    ScriptResponse response;
    response.script_id = 14u;
    response.success = true;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptLoadResp,
                                     381u,
                                     EncodeScriptResponse(response))));

    const Frame forwarded = ParseSingleFrame(host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoadResp);
    assert(forwarded.GetMsgId() == 381u);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60028, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestInvalidScriptLoadRespDoesNotRestoreSpawnState() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600280);
    registry.MarkSpawnSuspended(600280,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600280, SpawnTransactionState::kScriptLoadDispatched);
    registry.RegisterAgentSession(600280, &runtime_agent);
    registry.RegisterAgentProcessName(600280, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(600280);
    registry.MarkAgentReadyStage(600280, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(600280);
    runtime_agent.SetPeerPid(600280);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    std::vector<uint8_t> invalid_payload = {0x01, 0x02, 0x03};
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptLoadResp,
                                     3811u,
                                     invalid_payload)));

    assert(host_wire->TakeSent().empty());

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600280, &entry));
    assert(entry.state == SpawnTransactionState::kScriptLoadDispatched);
}

void TestScriptLoadSendFailureRestoresReadyForScriptLoadStateForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto failing_transport = std::make_unique<FailingSendTransport>();
    Session failing_agent(std::move(failing_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600281);
    registry.MarkSpawnSuspended(600281,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600281, SpawnTransactionState::kReadyForScriptLoad);
    registry.RegisterAgentSession(600281, &failing_agent);
    registry.RegisterAgentProcessName(600281, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(600281);
    registry.MarkAgentReadyStage(600281, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(600281);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 41u;

    Frame frame(MessageType::kScriptLoad, 3801u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);
    assert(response_frame.GetMsgId() == 3801u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600281, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadSuccessMarksScriptLoadDispatchedForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600283);
    registry.MarkSpawnSuspended(600283,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600283, SpawnTransactionState::kReadyForScriptLoad);
    registry.RegisterAgentSession(600283, &runtime_agent);
    registry.RegisterAgentProcessName(600283, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(600283);
    registry.MarkAgentReadyStage(600283, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(600283);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 42u;

    Frame frame(MessageType::kScriptLoad, 3803u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame forwarded = ParseSingleFrame(runtime_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoad);
    assert(forwarded.GetMsgId() == 3803u);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600283, &entry));
    assert(entry.state == SpawnTransactionState::kScriptLoadDispatched);
}

void TestScriptLoadSpawnNotReadyReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600284);
    registry.MarkSpawnSuspended(600284,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(600284, SpawnTransactionState::kReadyForScriptLoad);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 43u;

    Frame frame(MessageType::kScriptLoad, 3804u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);
    assert(response_frame.GetMsgId() == 3804u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(600284, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadWithoutRegistryReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 44u;

    Frame frame(MessageType::kScriptLoad, 3805u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);
    assert(response_frame.GetMsgId() == 3805u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}


void TestScriptCreateSendFailureReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto failing_transport = std::make_unique<FailingSendTransport>();
    Session failing_agent(std::move(failing_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600282);
    registry.RegisterAgentSession(600282, &failing_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 3802u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 3802u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestScriptCreateWithoutRegistryReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 38021u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 38021u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestScriptCreateSpawnNotReadyReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 6002821);
    registry.MarkSpawnSuspended(6002821,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(6002821, SpawnTransactionState::kWaitingRuntimeReady);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('blocked');";

    Frame frame(MessageType::kScriptCreate, 38022u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 38022u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script create");
}

void TestScriptUnloadSendFailureReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto failing_transport = std::make_unique<FailingSendTransport>();
    Session failing_agent(std::move(failing_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600283);
    registry.RegisterAgentSession(600283, &failing_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 42u;

    Frame frame(MessageType::kScriptUnload, 3803u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 3803u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestScriptUnloadSpawnNotReadyReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 6002831);
    registry.MarkSpawnSuspended(6002831,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(6002831, SpawnTransactionState::kWaitingRuntimeReady);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 420u;

    Frame frame(MessageType::kScriptUnload, 38031u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 38031u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for script unload");
}

void TestScriptUnloadWithoutRegistryReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    ScriptUnload unload;
    unload.script_id = 421u;

    Frame frame(MessageType::kScriptUnload, 38032u, EncodeScriptUnload(unload));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptUnloadResp);
    assert(response_frame.GetMsgId() == 38032u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestRpcRequestSendFailureReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto failing_transport = std::make_unique<FailingSendTransport>();
    Session failing_agent(std::move(failing_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 600284);
    registry.RegisterAgentSession(600284, &failing_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 43u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 3804u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 3804u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -4);
    assert(response.error.message == "agent session not ready for bound pid");
}

void TestRpcRequestWithoutRegistryReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 431u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 38042u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 38042u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestRpcRequestSpawnNotReadyReturnsImmediateHostError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 6002841);
    registry.MarkSpawnSuspended(6002841,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(6002841, SpawnTransactionState::kWaitingRuntimeReady);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    RpcRequest request;
    request.script_id = 430u;
    request.method = "ping";
    request.args_json = "[]";

    Frame frame(MessageType::kRpcRequest, 38041u, EncodeRpcRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kRpcResponse);
    assert(response_frame.GetMsgId() == 38041u);

    RpcResponse response;
    assert(DecodeRpcResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &response));
    assert(!response.success);
    assert(response.error.code == -5);
    assert(response.error.message == "spawned pid is not ready for rpc request");
}

void TestScriptLoadRespWithoutSuspendedOwnerHostStillRestoresReadyState() {
    auto rebound_host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* rebound_host_wire = rebound_host_transport.get();
    Session rebound_host(std::move(rebound_host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&rebound_host);
    registry.MarkSpawnSuspended(60029,
                                0xdeadbeefu,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(60029, SpawnTransactionState::kScriptLoadDispatched);
    registry.BindHostToPid(rebound_host.GetId(), 60029);
    registry.RegisterAgentSession(60029, &runtime_agent);
    registry.RegisterAgentProcessName(60029, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60029);
    registry.MarkAgentReadyStage(60029, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60029);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(60029);
    ScriptResponse response;
    response.script_id = 15u;
    response.success = true;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptLoadResp,
                                     382u,
                                     EncodeScriptResponse(response))));

    assert(rebound_host_wire->TakeSent().empty());

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60029, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestScriptLoadRespForSpawnUsesSuspendedHostOwnership() {
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
    registry.BindHostToPid(original_host.GetId(), 630291);
    registry.MarkSpawnSuspended(630291,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(630291, SpawnTransactionState::kScriptLoadDispatched);
    registry.RegisterAgentSession(630291, &runtime_agent);
    registry.RegisterAgentProcessName(630291, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(630291);
    registry.MarkAgentReadyStage(630291, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(630291);

    registry.BindHostToPid(rebound_host.GetId(), 630291);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    runtime_agent.SetPeerPid(630291);
    ScriptResponse response;
    response.script_id = 151u;
    response.success = true;
    assert(dispatcher.Dispatch(runtime_agent,
                               Frame(MessageType::kScriptLoadResp,
                                     3821u,
                                     EncodeScriptResponse(response))));

    const Frame forwarded = ParseSingleFrame(original_host_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptLoadResp);
    assert(forwarded.GetMsgId() == 3821u);
    assert(rebound_host_wire->TakeSent().empty());

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(630291, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
}

void TestSpawnRequestLateBindPromotesExistingControlReadyChild() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    injector.spawn_pid = 60004;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 14u, EncodeSpawnRequest(request));
    std::thread delayed_control_ready([&dispatcher, &control_agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 60004u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(control_agent,
                                   Frame(MessageType::kAgentReady, 346u, EncodeAgentReady(ready))));
    });

    assert(dispatcher.Dispatch(host, frame));
    delayed_control_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 14u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 60004u);
    assert(response.error.code == 0);
    assert(registry.FindHostSessionByPid(60004) == &host);
    assert(WaitForInjectedPid(&injector, 60004, 2000));
    assert(injector.GetLastInjectAgentPath() == "__embedded_agent__");

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60004, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
}

void TestLatePromotionIgnoresStaleGlobalRuntimeReadyBit() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.MarkAgentRuntimeReady(60016);

    FakeInjector injector;
    injector.spawn_pid = 60016;

    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{
        .agent_path = "__embedded_agent__",
    });

    SpawnRequest request;
    request.identifier = "com.demo.target";

    Frame frame(MessageType::kSpawnRequest, 382u, EncodeSpawnRequest(request));
    std::thread delayed_control_ready([&dispatcher, &control_agent, &injector]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        AgentReady ready;
        ready.pid = 60016u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = WaitForSpawnTokenArg(&injector);
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        assert(!ready.spawn_token.empty());
        assert(dispatcher.Dispatch(control_agent,
                                   Frame(MessageType::kAgentReady, 383u, EncodeAgentReady(ready))));
    });

    assert(dispatcher.Dispatch(host, frame));
    delayed_control_ready.join();

    const std::vector<Frame> frames = ParseFrames(host_wire->TakeSent());
    const Frame* response_frame = nullptr;
    for (const Frame& candidate : frames) {
        if (candidate.GetType() == MessageType::kSpawnResponse &&
            candidate.GetMsgId() == 382u) {
            response_frame = &candidate;
            break;
        }
    }
    assert(response_frame != nullptr);

    SpawnResponse response;
    assert(DecodeSpawnResponse(response_frame->GetPayload().data(),
                               response_frame->GetPayload().size(),
                               &response));
    assert(response.pid == 60016u);
    assert(response.error.code == 0);
    assert(WaitForInjectedPid(&injector, 60016, 2000));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60016, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

void TestLateControlStageAgentReadyDoesNotPoisonPinnedControlFallback() {
    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto stale_control_transport = std::make_unique<CaptureTransport>();
    Session stale_control_agent(std::move(stale_control_transport));
    assert(stale_control_agent.Start());

    SessionRegistry registry;
    registry.RegisterAgentSession(60025, &control_agent);
    registry.RegisterControlReadyAgentSession(60025, &control_agent);
    registry.RegisterAgentProcessName(60025, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60025);
    registry.MarkAgentReadyStage(60025, AgentReadyStage::kControl);
    registry.MarkSpawnSuspended(60025,
                                7u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(60025, SpawnTransactionState::kReadyForScriptLoad);

    AgentReady runtime_ready;
    runtime_ready.pid = 60025u;
    runtime_ready.process_name = "com.demo.target";
    runtime_ready.arch = "arm64";
    runtime_ready.version = "0.1.0";
    runtime_ready.stage = AgentReadyStage::kRuntime;
    registry.RegisterAgentSession(60025, &runtime_agent);
    registry.RegisterAgentProcessName(60025, "com.demo.target");
    registry.MarkAgentReadyStage(60025, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(60025);
    registry.StoreAgentReadyFrame(60025,
                                  Frame(MessageType::kAgentReady, 384u, EncodeAgentReady(runtime_ready)));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady stale_control_ready;
    stale_control_ready.pid = 60025u;
    stale_control_ready.process_name = "com.demo.target";
    stale_control_ready.arch = "arm64";
    stale_control_ready.version = "0.1.0";
    stale_control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(stale_control_agent,
                               Frame(MessageType::kAgentReady,
                                     385u,
                                     EncodeAgentReady(stale_control_ready))));

    assert(registry.FindAgentSessionByPid(60025) == &runtime_agent);
    assert(registry.FindControlReadyAgentSessionByPid(60025) == &control_agent);

    assert(registry.RemoveAgentSessionByPidIfMatches(60025, &runtime_agent));
    assert(registry.FindAgentSessionByPid(60025) == &control_agent);
    assert(registry.FindControlReadyAgentSessionByPid(60025) == &control_agent);

    AgentReadyStage final_stage = AgentReadyStage::kRuntime;
    assert(registry.GetAgentReadyStage(60025, &final_stage));
    assert(final_stage == AgentReadyStage::kControl);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60025, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
}

void TestLateControlStageAgentReadyDoesNotRegressTransactionRuntimeBoundaryWithoutGlobalRuntimeState() {
    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    auto stale_control_transport = std::make_unique<CaptureTransport>();
    Session stale_control_agent(std::move(stale_control_transport));
    assert(stale_control_agent.Start());

    SessionRegistry registry;
    registry.RegisterAgentSession(60026, &control_agent);
    registry.RegisterControlReadyAgentSession(60026, &control_agent);
    registry.RegisterAgentProcessName(60026, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(60026);
    registry.MarkAgentReadyStage(60026, AgentReadyStage::kControl);
    registry.MarkSpawnSuspended(60026,
                                8u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(60026, SpawnTransactionState::kReadyForScriptLoad);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady stale_control_ready;
    stale_control_ready.pid = 60026u;
    stale_control_ready.process_name = "com.demo.target";
    stale_control_ready.arch = "arm64";
    stale_control_ready.version = "0.1.0";
    stale_control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(stale_control_agent,
                               Frame(MessageType::kAgentReady,
                                     386u,
                                     EncodeAgentReady(stale_control_ready))));

    assert(registry.FindAgentSessionByPid(60026) == &control_agent);
    assert(registry.FindControlReadyAgentSessionByPid(60026) == &control_agent);

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(60026, &entry));
    assert(entry.state == SpawnTransactionState::kReadyForScriptLoad);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady);
    assert(entry.authoritative_process_name == "com.demo.target");
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

    Frame frame(MessageType::kScriptCreate, 826u, EncodeScriptCreate(create));
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

void TestInvalidScriptCreateRespIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 630201);
    registry.RegisterAgentSession(630201, &agent);
    agent.SetPeerPid(630201);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    std::vector<uint8_t> invalid_payload = {0x01, 0x02, 0x03};
    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kScriptCreateResp,
                                     8281u,
                                     invalid_payload)));

    assert(host_wire->TakeSent().empty());
}

void TestInvalidScriptUnloadRespIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 630311);
    registry.RegisterAgentSession(630311, &runtime_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kScriptUnloadResp, 43011u, std::vector<uint8_t>{0x01, 0x02, 0x03});
    assert(dispatcher.Dispatch(runtime_agent, frame));

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

    Frame frame(MessageType::kScriptCreate, 843u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(rebound_host, frame));

    const Frame response_frame = ParseSingleFrame(rebound_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptCreateResp);
    assert(response_frame.GetMsgId() == 843u);

    ScriptCreateResponse response;
    assert(DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                      response_frame.GetPayload().size(),
                                      &response));
    assert(!response.success);
    assert(response.error.code == -3);
    assert(response.error.message == "host session is not bound to a pid");
    assert(runtime_wire->TakeSent().empty());
}

void TestScriptLoadForReboundHostDoesNotTargetForeignSuspendedSpawn() {
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
    registry.BindHostToPid(original_host.GetId(), 630341);
    registry.MarkSpawnSuspended(630341,
                                original_host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(630341, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(630341, &runtime_agent);
    registry.RegisterAgentProcessName(630341, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(630341);
    registry.MarkAgentReadyStage(630341, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(630341);

    registry.BindHostToPid(rebound_host.GetId(), 630341);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptLoad load;
    load.script_id = 99u;

    Frame frame(MessageType::kScriptLoad, 8431u, EncodeScriptLoad(load));
    assert(dispatcher.Dispatch(rebound_host, frame));

    const Frame response_frame = ParseSingleFrame(rebound_host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kScriptLoadResp);
    assert(response_frame.GetMsgId() == 8431u);

    ScriptResponse response;
    assert(DecodeScriptResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(!response.success);
    assert(response.error.code == -3);
    assert(response.error.message == "host session is not bound to a pid");
    assert(runtime_wire->TakeSent().empty());
}

void TestResumeRequestClearsCachedSpawnScriptMessages() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63036);
    registry.MarkSpawnSuspended(63036,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63036, SpawnTransactionState::kReadyForScriptLoad);

    ScriptMessage message;
    message.script_id = 22u;
    message.message = "{\"type\":\"send\",\"payload\":\"stale-after-resume\"}";
    registry.StoreScriptMessageFrame(63036,
                                     Frame(MessageType::kScriptMessage,
                                           870u,
                                           EncodeScriptMessage(message)));

    FakeInjector injector;
    bool resumed = false;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher,
                           &registry,
                           &injector,
                           ServerHandlerConfig{
                               .resume_process = [&](int pid) {
                                   resumed = pid == 63036;
                                   return resumed;
                               },
                           });

    ResumeRequest request;
    request.pid = 63036u;
    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kResumeRequest,
                                     871u,
                                     EncodeResumeRequest(request))));

    assert(resumed);
    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 63036u);
    assert(response.error.code == 0);

    assert(!registry.IsSpawnSuspended(63036));
    assert(registry.TakeScriptMessageFrames(63036).empty());
}

void TestInvalidDetachRequestReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kDetachRequest, 872u, std::vector<uint8_t>{0x01});
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 872u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.session_id == 0u);
    assert(response.error.code == -1);
    assert(response.error.message == "invalid detach request");
}

void TestDetachRequestWithoutRegistryReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    DetachRequest request;
    request.session_id = 99u;
    Frame frame(MessageType::kDetachRequest, 873u, EncodeDetachRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kDetachResponse);
    assert(response_frame.GetMsgId() == 873u);

    DetachResponse response;
    assert(DecodeDetachResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.session_id == 99u);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
}

void TestInvalidResumeRequestReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kResumeRequest, 874u, std::vector<uint8_t>{0x02});
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 874u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 0u);
    assert(response.error.code == -1);
    assert(response.error.message == "invalid resume request");
}

void TestResumeRequestWithoutRegistryReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, nullptr, &injector, ServerHandlerConfig{});

    ResumeRequest request;
    request.pid = 63037u;
    Frame frame(MessageType::kResumeRequest, 875u, EncodeResumeRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kResumeResponse);
    assert(response_frame.GetMsgId() == 875u);

    ResumeResponse response;
    assert(DecodeResumeResponse(response_frame.GetPayload().data(),
                                response_frame.GetPayload().size(),
                                &response));
    assert(response.pid == 63037u);
    assert(response.error.code == -2);
    assert(response.error.message == "session registry unavailable");
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

void TestScriptPostForReadySpawnForwardsToRuntimeAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 630221);
    registry.MarkSpawnSuspended(630221,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(630221, SpawnTransactionState::kReadyForScriptLoad);
    registry.RegisterAgentSession(630221, &runtime_agent);
    registry.RegisterAgentProcessName(630221, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(630221);
    registry.MarkAgentReadyStage(630221, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(630221);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptPost post;
    post.script_id = 21u;
    post.message = "{\"type\":\"post\",\"payload\":\"spawn-ready\"}";
    post.data = {0x11, 0x22, 0x33};

    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kScriptPost,
                                     8301u,
                                     EncodeScriptPost(post))));

    const Frame forwarded = ParseSingleFrame(runtime_wire->TakeSent());
    assert(forwarded.GetType() == MessageType::kScriptPost);
    assert(forwarded.GetMsgId() == 8301u);

    ScriptPost decoded;
    assert(DecodeScriptPost(forwarded.GetPayload().data(),
                            forwarded.GetPayload().size(),
                            &decoded));
    assert(decoded.script_id == 21u);
    assert(decoded.message == "{\"type\":\"post\",\"payload\":\"spawn-ready\"}");
    assert(decoded.data == std::vector<uint8_t>({0x11, 0x22, 0x33}));
}

void TestScriptPostSpawnNotReadyDoesNotForwardToAgent() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 630222);
    registry.MarkSpawnSuspended(630222,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(630222, SpawnTransactionState::kWaitingRuntimeReady);
    registry.RegisterAgentSession(630222, &control_agent);
    registry.RegisterControlReadyAgentSession(630222, &control_agent);
    registry.RegisterAgentProcessName(630222, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(630222);
    registry.MarkAgentReadyStage(630222, AgentReadyStage::kControl);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptPost post;
    post.script_id = 22u;
    post.message = "{\"type\":\"post\",\"payload\":\"spawn-blocked\"}";

    assert(dispatcher.Dispatch(host,
                               Frame(MessageType::kScriptPost,
                                     8302u,
                                     EncodeScriptPost(post))));

    assert(host_wire->TakeSent().empty());
    assert(control_wire->TakeSent().empty());
}

void TestScriptMessageWithoutCacheWindowAndWithoutHostIsDropped() {
    auto agent_transport = std::make_unique<CaptureTransport>();
    Session agent(std::move(agent_transport));

    SessionRegistry registry;
    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    agent.SetPeerPid(630223);

    ScriptMessage message;
    message.script_id = 23u;
    message.message = "{\"type\":\"send\",\"payload\":\"orphan-runtime\"}";
    message.data = {0x44};

    assert(dispatcher.Dispatch(agent,
                               Frame(MessageType::kScriptMessage,
                                     8303u,
                                     EncodeScriptMessage(message))));

    assert(registry.TakeScriptMessageFrames(630223).empty());
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
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "com.demo.target");
    assert(entry.target_process_name == "com.demo.target");
}

void TestMismatchedControlAgentReadyDoesNotOverrideKnownControlIdentityForSpawn() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto wrong_control_transport = std::make_unique<CaptureTransport>();
    Session wrong_control_agent(std::move(wrong_control_transport));
    assert(wrong_control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63028);
    registry.MarkSpawnSuspended(63028,
                                host.GetId(),
                                PendingSpawnReadyStage::kControlReady,
                                "zygote64",
                                "com.demo.target");

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AgentReady wrong_control_ready;
    wrong_control_ready.pid = 63028u;
    wrong_control_ready.process_name = "com.demo.other";
    wrong_control_ready.arch = "arm64";
    wrong_control_ready.version = "0.1.0";
    wrong_control_ready.stage = AgentReadyStage::kControl;
    assert(dispatcher.Dispatch(wrong_control_agent,
                               Frame(MessageType::kAgentReady,
                                     837u,
                                     EncodeAgentReady(wrong_control_ready))));

    assert(host_wire->TakeSent().empty());
    assert(registry.FindAgentSessionByPid(63028) == nullptr);
    assert(registry.FindControlReadyAgentSessionByPid(63028) == nullptr);
    assert(!registry.IsAgentAuthoritativeReady(63028));
    assert(!registry.IsAgentControlReady(63028));
    assert(!registry.IsAgentRuntimeReady(63028));

    SpawnSuspendedEntry entry;
    assert(registry.GetSpawnSuspendedEntry(63028, &entry));
    assert(entry.state == SpawnTransactionState::kWaitingRuntimeReady);
    assert(entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady);
    assert(entry.authoritative_process_name == "zygote64");
    assert(entry.target_process_name == "com.demo.target");
}

void TestScriptCreateForSpawnPrefersAuthoritativeRuntimeIdentityOverTargetName() {
    auto host_transport = std::make_unique<CaptureTransport>();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* runtime_wire = runtime_transport.get();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63027);
    registry.MarkSpawnSuspended(63027,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.runtime",
                                "com.demo.target");
    registry.UpdateSpawnState(63027, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63027, &runtime_agent);
    registry.RegisterAgentProcessName(63027, "com.demo.runtime");
    registry.MarkAgentAuthoritativeReady(63027);
    registry.MarkAgentReadyStage(63027, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63027);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 836u, EncodeScriptCreate(create));
    assert(dispatcher.Dispatch(host, frame));

    const std::vector<Frame> forwarded_frames = ParseFrames(runtime_wire->TakeSent());
    assert(forwarded_frames.size() == 1u);
    assert(forwarded_frames[0].GetType() == MessageType::kScriptCreate);
    assert(forwarded_frames[0].GetMsgId() == 836u);
}

void TestScriptCreateStaysBlockedAfterRuntimeDisconnectInsteadOfFallingBackToControlSession() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* control_wire = control_transport.get();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63029);
    registry.MarkSpawnSuspended(63029,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63029, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63029, &control_agent);
    registry.RegisterControlReadyAgentSession(63029, &control_agent);
    registry.RegisterAgentProcessName(63029, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63029);
    registry.MarkAgentReadyStage(63029, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63029, &runtime_agent);
    registry.RegisterAgentProcessName(63029, "com.demo.target");
    registry.MarkAgentReadyStage(63029, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63029);

    assert(registry.RemoveAgentSessionByPidIfMatches(63029, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63029) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63029));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ScriptCreate create;
    create.session_id = host.GetId();
    create.name = "demo.js";
    create.source = "console.log('ok');";

    Frame frame(MessageType::kScriptCreate, 838u, EncodeScriptCreate(create));
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
    assert(control_wire->TakeSent().empty());
}

void TestScriptCreateRespFromControlSessionIsDroppedAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63030);
    registry.MarkSpawnSuspended(63030,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63030, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63030, &control_agent);
    registry.RegisterControlReadyAgentSession(63030, &control_agent);
    registry.RegisterAgentProcessName(63030, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63030);
    registry.MarkAgentReadyStage(63030, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63030, &runtime_agent);
    registry.RegisterAgentProcessName(63030, "com.demo.target");
    registry.MarkAgentReadyStage(63030, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63030);

    assert(registry.RemoveAgentSessionByPidIfMatches(63030, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63030) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63030));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    control_agent.SetPeerPid(63030);
    ScriptCreateResponse response;
    response.script_id = 3u;
    response.success = true;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptCreateResp,
                                     839u,
                                     EncodeScriptCreateResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestScriptLoadRespFromControlSessionIsDroppedAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63035);
    registry.MarkSpawnSuspended(63035,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63035, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63035, &control_agent);
    registry.RegisterControlReadyAgentSession(63035, &control_agent);
    registry.RegisterAgentProcessName(63035, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63035);
    registry.MarkAgentReadyStage(63035, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63035, &runtime_agent);
    registry.RegisterAgentProcessName(63035, "com.demo.target");
    registry.MarkAgentReadyStage(63035, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63035);

    assert(registry.RemoveAgentSessionByPidIfMatches(63035, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63035) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63035));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    control_agent.SetPeerPid(63035);
    ScriptResponse response;
    response.script_id = 13u;
    response.success = true;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptLoadResp,
                                     844u,
                                     EncodeScriptResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestRpcResponseFromControlSessionIsDroppedAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63031);
    registry.MarkSpawnSuspended(63031,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63031, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63031, &control_agent);
    registry.RegisterControlReadyAgentSession(63031, &control_agent);
    registry.RegisterAgentProcessName(63031, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63031);
    registry.MarkAgentReadyStage(63031, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63031, &runtime_agent);
    registry.RegisterAgentProcessName(63031, "com.demo.target");
    registry.MarkAgentReadyStage(63031, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63031);

    assert(registry.RemoveAgentSessionByPidIfMatches(63031, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63031) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63031));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    control_agent.SetPeerPid(63031);
    RpcResponse response;
    response.script_id = 4u;
    response.success = true;
    response.result_json = "{\"value\":\"pong\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kRpcResponse,
                                     840u,
                                     EncodeRpcResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestInvalidRpcResponseIsDropped() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 630312);
    registry.RegisterAgentSession(630312, &runtime_agent);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    Frame frame(MessageType::kRpcResponse, 43012u, std::vector<uint8_t>{0x04, 0x05});
    assert(dispatcher.Dispatch(runtime_agent, frame));

    assert(host_wire->TakeSent().empty());
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
    Frame frame(MessageType::kProcessListReq, 43021u, EncodeProcessListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kProcessListResp);
    assert(response_frame.GetMsgId() == 43021u);

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

void TestProcessListRequestWithoutEnumeratorReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    ProcessListRequest request;
    Frame frame(MessageType::kProcessListReq, 43022u, EncodeProcessListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kProcessListResp);
    assert(response_frame.GetMsgId() == 43022u);

    ProcessListResponse response;
    assert(DecodeProcessListResponse(response_frame.GetPayload().data(),
                                     response_frame.GetPayload().size(),
                                     &response));
    assert(response.error.code == -2);
    assert(response.error.message == "process enumeration unavailable");
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
        .enumerate_apps = [&process_manager]() {
            return process_manager.EnumerateApps();
        },
    });

    AppListRequest request;
    Frame frame(MessageType::kAppListReq, 43023u, EncodeAppListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAppListResp);
    assert(response_frame.GetMsgId() == 43023u);

    AppListResponse response;
    assert(DecodeAppListResponse(response_frame.GetPayload().data(),
                                 response_frame.GetPayload().size(),
                                 &response));
    assert(response.error.code == 0);
    assert(response.apps.size() == 2u);
    assert(response.apps[0].package_name == "com.android.systemui");
    assert(response.apps[1].package_name == "com.demo.target");
}

void TestAppListRequestWithoutEnumeratorReturnsImmediateError() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    SessionRegistry registry;
    registry.RegisterHostSession(&host);

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    AppListRequest request;
    Frame frame(MessageType::kAppListReq, 43024u, EncodeAppListRequest(request));
    assert(dispatcher.Dispatch(host, frame));

    const Frame response_frame = ParseSingleFrame(host_wire->TakeSent());
    assert(response_frame.GetType() == MessageType::kAppListResp);
    assert(response_frame.GetMsgId() == 43024u);

    AppListResponse response;
    assert(DecodeAppListResponse(response_frame.GetPayload().data(),
                                 response_frame.GetPayload().size(),
                                 &response));
    assert(response.error.code == -2);
    assert(response.error.message == "app enumeration unavailable");
}

void TestScriptUnloadRespFromControlSessionIsDroppedAfterRuntimeDisconnect() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63032);
    registry.MarkSpawnSuspended(63032,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63032, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63032, &control_agent);
    registry.RegisterControlReadyAgentSession(63032, &control_agent);
    registry.RegisterAgentProcessName(63032, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63032);
    registry.MarkAgentReadyStage(63032, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63032, &runtime_agent);
    registry.RegisterAgentProcessName(63032, "com.demo.target");
    registry.MarkAgentReadyStage(63032, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63032);

    assert(registry.RemoveAgentSessionByPidIfMatches(63032, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63032) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63032));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    control_agent.SetPeerPid(63032);
    ScriptResponse response;
    response.script_id = 5u;
    response.success = true;
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptUnloadResp,
                                     841u,
                                     EncodeScriptResponse(response))));

    assert(host_wire->TakeSent().empty());
}

void TestScriptMessageFromControlSessionIsCachedAfterRuntimeDisconnectFallback() {
    auto host_transport = std::make_unique<CaptureTransport>();
    CaptureTransport* host_wire = host_transport.get();
    Session host(std::move(host_transport));

    auto runtime_transport = std::make_unique<CaptureTransport>();
    Session runtime_agent(std::move(runtime_transport));
    assert(runtime_agent.Start());

    auto control_transport = std::make_unique<CaptureTransport>();
    Session control_agent(std::move(control_transport));
    assert(control_agent.Start());

    SessionRegistry registry;
    registry.RegisterHostSession(&host);
    registry.BindHostToPid(host.GetId(), 63033);
    registry.MarkSpawnSuspended(63033,
                                host.GetId(),
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");
    registry.UpdateSpawnState(63033, SpawnTransactionState::kReadyForScriptLoad);

    registry.RegisterAgentSession(63033, &control_agent);
    registry.RegisterControlReadyAgentSession(63033, &control_agent);
    registry.RegisterAgentProcessName(63033, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(63033);
    registry.MarkAgentReadyStage(63033, AgentReadyStage::kControl);

    registry.RegisterAgentSession(63033, &runtime_agent);
    registry.RegisterAgentProcessName(63033, "com.demo.target");
    registry.MarkAgentReadyStage(63033, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(63033);

    assert(registry.RemoveAgentSessionByPidIfMatches(63033, &runtime_agent));
    assert(registry.FindAgentSessionByPid(63033) == &control_agent);
    assert(!registry.IsAgentRuntimeReady(63033));

    FakeInjector injector;
    MessageDispatcher dispatcher;
    RegisterServerHandlers(&dispatcher, &registry, &injector, ServerHandlerConfig{});

    control_agent.SetPeerPid(63033);
    ScriptMessage message;
    message.script_id = 6u;
    message.message = "{\"type\":\"send\",\"payload\":\"control-after-runtime-disconnect\"}";
    assert(dispatcher.Dispatch(control_agent,
                               Frame(MessageType::kScriptMessage,
                                     842u,
                                     EncodeScriptMessage(message))));

    assert(host_wire->TakeSent().empty());

    std::vector<Frame> cached_frames = registry.TakeScriptMessageFrames(63033);
    assert(cached_frames.size() == 1u);
    assert(cached_frames[0].GetType() == MessageType::kScriptMessage);

    ScriptMessage cached_message;
    assert(DecodeScriptMessage(cached_frames[0].GetPayload().data(),
                               cached_frames[0].GetPayload().size(),
                               &cached_message));
    assert(cached_message.script_id == 6u);
    assert(cached_message.message == "{\"type\":\"send\",\"payload\":\"control-after-runtime-disconnect\"}");
}

}  // namespace

int main() {
    TestAttachRequestWithoutTargetReturnsResolveFailure();
    TestAttachRequestResolveFailureReturnsImmediateError();
    TestAttachRequestWithoutInjectorReturnsImmediateError();
    TestSpawnRequestMarksGateHeldChildAndBindsHost();
    TestSpawnRequestTimesOutWithoutAuthoritativeAgentReadyAndClearsPendingSpawn();
    TestLateAuthoritativeAgentReadyAfterSpawnTimeoutDoesNotLeaveBoundState();
    TestControlStageAgentReadyDoesNotForwardToBoundHost();
    TestOrphanSpawnTokenAgentReadyWithoutPendingOrBoundHostIsDropped();
    TestMismatchedPendingAttachAgentReadyIsDropped();
    TestControlStageAgentReadyWithMatchingSpawnTokenResolvesPendingSpawn();
    TestAgentReadyWithMatchingSpawnTokenButWrongPidDoesNotResolvePendingSpawn();
    TestRuntimeStageAgentReadyUpgradesPendingSpawnResolutionStage();
    TestRuntimeStageAgentReadyFromDifferentPidDoesNotStealControlResolvedSpawn();
    TestControlStageAgentReadyForSpawnedChildDoesNotInjectBeforeFinalize();
    TestRuntimeStageAgentReadyUpgradesExistingSpawnSuspendedEntryWithoutPendingSpawn();
    TestControlStageAgentReadyPromotesOnlyAfterFinalizeCompletes();
    TestRuntimeReadyDuringFinalizeIsReplayedAfterSpawnResponseOnlyOnce();
    TestRuntimeReadyAfterBindButBeforeSpawnResponseIsHeld();
    TestRuntimeReadyAtSpawnResponseSendBoundaryDoesNotPrecedeSpawnResponse();
    TestRuntimeReadyDuringFinalizeRemainsHeldAfterPendingSpawnCleared();
    TestRuntimeReadyAfterPendingSpawnConsumedButWithBoundSpawnContextIsNotDropped();
    TestFinalizeWithoutRegisteredHostDoesNotLeaveGhostSuspendedSpawn();
    TestRuntimeReadyReplayRequiresMatchingAuthoritativeProcessName();
    TestMismatchedRuntimeReadyDoesNotUpgradeControlResolvedSpawn();
    TestMismatchedRuntimeReadyWithoutPendingSpawnDoesNotUpgradeTargetBoundSpawn();
    TestRuntimeReadyWithWrongSpawnTokenDoesNotUpgradeExistingSpawnContext();
    TestRuntimeReadyReboundClearsPreviousPidBindingForSameSession();
    TestFinalizeUsesSpawnSuspendedAuthoritativeStageOverGlobalRuntimeState();
    TestSpawnFinalizeReplaySendFailureClearsSpawnTransactionState();
    TestSpawnFinalizeReplayPartialScriptMessageFailureKeepsOnlyUnsentCachedMessages();
    TestSpawnFinalizeReplayDoesNotSendCachedScriptMessagesAfterAgentReadyReplayFailure();
    TestSpawnFinalizeReplayWithoutCachedAgentReadyStillSendsCachedScriptMessages();
    TestFinalizeUsesCurrentSpawnSuspendedRuntimeIdentityForReplay();
    TestFinalizeDoesNotPromoteFromGlobalRuntimeReadyWithoutAuthoritativeUpgrade();
    TestScriptMessageDuringFinalizeIsReplayedAfterSpawnReadyBoundary();
    TestScriptMessageForReadySpawnForwardsImmediately();
    TestScriptMessageFromNonAuthoritativeRuntimeSessionIsDropped();
    TestControlOnlySpawnDefersCachedScriptMessageUntilRuntimeReady();
    TestRuntimeReadyReplaySendFailureKeepsCachedScriptMessages();
    TestRuntimeReadyReplayPartialSendFailureKeepsOnlyUnsentCachedScriptMessages();
    TestRuntimeReadyWithoutBoundHostStillPromotesVisibleSpawnState();
    TestScriptLoadRespFromNonCurrentAgentSessionIsDropped();
    TestScriptLoadRespRestoresReadyForScriptLoadStateForSpawn();
    TestInvalidScriptLoadRespDoesNotRestoreSpawnState();
    TestScriptLoadSendFailureRestoresReadyForScriptLoadStateForSpawn();
    TestScriptLoadSuccessMarksScriptLoadDispatchedForSpawn();
    TestScriptLoadSpawnNotReadyReturnsImmediateHostError();
    TestScriptLoadWithoutRegistryReturnsImmediateHostError();
    TestScriptCreateSendFailureReturnsImmediateHostError();
    TestScriptCreateWithoutRegistryReturnsImmediateHostError();
    TestScriptCreateSpawnNotReadyReturnsImmediateHostError();
    TestScriptUnloadSendFailureReturnsImmediateHostError();
    TestScriptUnloadSpawnNotReadyReturnsImmediateHostError();
    TestScriptUnloadWithoutRegistryReturnsImmediateHostError();
    TestRpcRequestSendFailureReturnsImmediateHostError();
    TestRpcRequestWithoutRegistryReturnsImmediateHostError();
    TestRpcRequestSpawnNotReadyReturnsImmediateHostError();
    TestScriptLoadRespWithoutSuspendedOwnerHostStillRestoresReadyState();
    TestScriptLoadRespForSpawnUsesSuspendedHostOwnership();
    TestSpawnRequestLateBindPromotesExistingControlReadyChild();
    TestLatePromotionIgnoresStaleGlobalRuntimeReadyBit();
    TestLateControlStageAgentReadyDoesNotPoisonPinnedControlFallback();
    TestLateControlStageAgentReadyDoesNotRegressTransactionRuntimeBoundaryWithoutGlobalRuntimeState();
    TestScriptCreateDoesNotTargetMismatchedRuntimeAgentForSpawn();
    TestScriptCreateRespFromMismatchedRuntimeAgentIsDroppedForSpawn();
    TestInvalidScriptCreateRespIsDropped();
    TestInvalidScriptUnloadRespIsDropped();
    TestScriptCreateRespForSpawnUsesSuspendedHostOwnership();
    TestScriptCreateForReboundHostDoesNotTargetForeignSuspendedSpawn();
    TestResumeRequestClearsCachedSpawnScriptMessages();
    TestInvalidDetachRequestReturnsImmediateError();
    TestDetachRequestWithoutRegistryReturnsImmediateError();
    TestInvalidResumeRequestReturnsImmediateError();
    TestResumeRequestWithoutRegistryReturnsImmediateError();
    TestRuntimeReadyForwardForSpawnUsesSuspendedHostOwnership();
    TestScriptPostForReadySpawnForwardsToRuntimeAgent();
    TestScriptPostSpawnNotReadyDoesNotForwardToAgent();
    TestScriptMessageWithoutCacheWindowAndWithoutHostIsDropped();
    TestScriptMessageFromMismatchedCurrentRuntimeAgentIsDroppedForSpawn();
    TestControlReadyCanReplaceMismatchedGlobalRuntimeTraceForSpawnTarget();
    TestMismatchedControlAgentReadyDoesNotOverrideKnownControlIdentityForSpawn();
    TestScriptCreateForSpawnPrefersAuthoritativeRuntimeIdentityOverTargetName();
    TestScriptCreateStaysBlockedAfterRuntimeDisconnectInsteadOfFallingBackToControlSession();
    TestScriptCreateRespFromControlSessionIsDroppedAfterRuntimeDisconnect();
    TestScriptLoadRespFromControlSessionIsDroppedAfterRuntimeDisconnect();
    TestRpcResponseFromControlSessionIsDroppedAfterRuntimeDisconnect();
    TestInvalidRpcResponseIsDropped();
    TestProcessListRequestReturnsProcesses();
    TestProcessListRequestWithoutEnumeratorReturnsImmediateError();
    TestAppListRequestReturnsApps();
    TestAppListRequestWithoutEnumeratorReturnsImmediateError();
    TestScriptUnloadRespFromControlSessionIsDroppedAfterRuntimeDisconnect();
    TestScriptMessageFromControlSessionIsCachedAfterRuntimeDisconnectFallback();
    return 0;
}
