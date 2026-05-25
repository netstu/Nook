#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "communication/agent/agent_connection.h"
#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/transport/transport.h"

using namespace nook::comm;

namespace {

class FakeTransport final : public Transport {
public:
    FakeTransport() {
        state_ = TransportState::kConnected;
        connected_.store(true);
    }

    bool Connect() override {
        connected_.store(true);
        SetState(TransportState::kConnected);
        return true;
    }

    void Disconnect() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_.store(false);
        }
        cv_.notify_all();
        SetState(TransportState::kDisconnected);
    }

    bool IsConnected() const override { return connected_.load(); }
    TransportState GetState() const override {
        return connected_.load() ? TransportState::kConnected : TransportState::kDisconnected;
    }

    ssize_t Send(const uint8_t* data, size_t len) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sent_.insert(sent_.end(), data, data + len);
        }
        cv_.notify_all();
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) override {
        std::unique_lock<std::mutex> lock(mutex_);
        auto ready = [this]() {
            return !incoming_.empty() || !connected_.load();
        };

        if (timeout_ms >= 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
        } else {
            cv_.wait(lock, ready);
        }

        if (incoming_.empty()) {
            return connected_.load() ? 0 : -1;
        }

        size_t count = 0;
        while (count < len && !incoming_.empty()) {
            buf[count++] = incoming_.front();
            incoming_.pop_front();
        }
        return static_cast<ssize_t>(count);
    }

    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Fake"; }

    std::vector<uint8_t> WaitSentBytes(size_t min_size, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
            return sent_.size() >= min_size;
        });
        return sent_;
    }

    void ClearSent() {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_.clear();
    }

    void PushIncoming(const std::vector<uint8_t>& bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            incoming_.insert(incoming_.end(), bytes.begin(), bytes.end());
        }
        cv_.notify_all();
    }

    void PushIncomingByte(uint8_t value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            incoming_.push_back(value);
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<uint8_t> incoming_;
    std::vector<uint8_t> sent_;
    std::atomic<bool> connected_{false};
};

std::vector<uint8_t> SerializeFrame(MessageType type, uint32_t msg_id, std::vector<uint8_t> payload) {
    return Frame(type, msg_id, std::move(payload)).Serialize();
}

Frame WaitForSingleReplyFrame(FakeTransport* transport, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<uint8_t> sent = transport->WaitSentBytes(Frame::kHeaderSize, 50);
        if (sent.size() < Frame::kHeaderSize) {
            continue;
        }

        Frame parsed;
        size_t consumed = 0;
        if (Frame::Parse(sent.data(), sent.size(), &parsed, &consumed)) {
            return parsed;
        }
    }

    assert(false && "timed out waiting for parsed reply frame");
    return {};
}

void TestResumeHandlerRepliesWithResumeResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    AgentConnection connection(std::move(transport));

    std::atomic<uint32_t> handled_pid{0};
    connection.SetResumeHandler([&handled_pid](const ResumeRequest& request) {
        handled_pid.store(request.pid);
        ResumeResponse response;
        response.pid = request.pid;
        return response;
    });

    assert(connection.Connect());
    connection.StartRecvLoop();
    raw->ClearSent();

    ResumeRequest request;
    request.pid = 2100u;
    raw->PushIncoming(SerializeFrame(MessageType::kResumeRequest, 33u, EncodeResumeRequest(request)));

    Frame parsed = WaitForSingleReplyFrame(raw, 1000);
    assert(parsed.GetType() == MessageType::kResumeResponse);
    assert(parsed.GetMsgId() == 33u);

    ResumeResponse response;
    assert(DecodeResumeResponse(parsed.GetPayload().data(), parsed.GetPayload().size(), &response));
    assert(response.pid == 2100u);
    assert(response.error.code == 0);
    assert(handled_pid.load() == 2100u);
}

void TestRpcHandlerRepliesWithRpcResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    AgentConnection connection(std::move(transport));

    connection.SetRpcHandler([](const RpcRequest& request) {
        assert(request.script_id == 7u);
        assert(request.method == "ping");
        assert(request.args_json == "[\"hello\"]");

        RpcResponse response;
        response.script_id = request.script_id;
        response.success = true;
        response.result_json = "{\"value\":\"pong\"}";
        return response;
    });

    assert(connection.Connect());
    connection.StartRecvLoop();
    raw->ClearSent();

    RpcRequest request;
    request.script_id = 7u;
    request.method = "ping";
    request.args_json = "[\"hello\"]";
    raw->PushIncoming(SerializeFrame(MessageType::kRpcRequest, 34u, EncodeRpcRequest(request)));

    Frame parsed = WaitForSingleReplyFrame(raw, 1000);
    assert(parsed.GetType() == MessageType::kRpcResponse);
    assert(parsed.GetMsgId() == 34u);

    RpcResponse response;
    assert(DecodeRpcResponse(parsed.GetPayload().data(), parsed.GetPayload().size(), &response));
    assert(response.script_id == 7u);
    assert(response.success);
    assert(response.result_json == "{\"value\":\"pong\"}");
}

void TestSpawnInstallHandlerRepliesWithSpawnInstallResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    AgentConnection connection(std::move(transport));

    connection.SetSpawnInstallHandler([](const SpawnInstallRequest& request) {
        assert(request.target_package == "com.demo.target");
        assert(request.spawn_token == "spawn-token-21");
        assert(request.mode == "stable");

        SpawnInstallResponse response;
        response.success = true;
        return response;
    });

    assert(connection.Connect());
    connection.StartRecvLoop();
    raw->ClearSent();

    SpawnInstallRequest request;
    request.target_package = "com.demo.target";
    request.spawn_token = "spawn-token-21";
    request.mode = "stable";
    raw->PushIncoming(SerializeFrame(MessageType::kSpawnInstall, 35u, EncodeSpawnInstallRequest(request)));

    Frame parsed = WaitForSingleReplyFrame(raw, 1000);
    assert(parsed.GetType() == MessageType::kSpawnInstallResp);
    assert(parsed.GetMsgId() == 35u);

    SpawnInstallResponse response;
    assert(DecodeSpawnInstallResponse(parsed.GetPayload().data(), parsed.GetPayload().size(), &response));
    assert(response.success);
    assert(response.error.code == 0);
}

void TestSpawnUninstallHandlerRepliesWithSpawnUninstallResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    AgentConnection connection(std::move(transport));

    connection.SetSpawnUninstallHandler([](const SpawnUninstallRequest& request) {
        assert(request.spawn_token == "spawn-token-22");

        SpawnUninstallResponse response;
        response.success = true;
        return response;
    });

    assert(connection.Connect());
    connection.StartRecvLoop();
    raw->ClearSent();

    SpawnUninstallRequest request;
    request.spawn_token = "spawn-token-22";
    raw->PushIncoming(SerializeFrame(MessageType::kSpawnUninstall, 36u, EncodeSpawnUninstallRequest(request)));

    Frame parsed = WaitForSingleReplyFrame(raw, 1000);
    assert(parsed.GetType() == MessageType::kSpawnUninstallResp);
    assert(parsed.GetMsgId() == 36u);

    SpawnUninstallResponse response;
    assert(DecodeSpawnUninstallResponse(parsed.GetPayload().data(), parsed.GetPayload().size(), &response));
    assert(response.success);
    assert(response.error.code == 0);
}

void TestResumeHandlerRepliesWhenFrameArrivesInFragments() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    AgentConnection connection(std::move(transport));

    std::atomic<uint32_t> handled_pid{0};
    connection.SetResumeHandler([&handled_pid](const ResumeRequest& request) {
        handled_pid.store(request.pid);
        ResumeResponse response;
        response.pid = request.pid;
        return response;
    });

    assert(connection.Connect());
    connection.StartRecvLoop();
    raw->ClearSent();

    ResumeRequest request;
    request.pid = 4242u;
    const std::vector<uint8_t> bytes =
        SerializeFrame(MessageType::kResumeRequest, 77u, EncodeResumeRequest(request));
    assert(bytes.size() > Frame::kHeaderSize);

    for (size_t i = 0; i < Frame::kHeaderSize; ++i) {
        raw->PushIncomingByte(bytes[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    for (size_t i = Frame::kHeaderSize; i < bytes.size(); ++i) {
        raw->PushIncomingByte(bytes[i]);
    }

    Frame parsed = WaitForSingleReplyFrame(raw, 1000);
    assert(parsed.GetType() == MessageType::kResumeResponse);
    assert(parsed.GetMsgId() == 77u);

    ResumeResponse response;
    assert(DecodeResumeResponse(parsed.GetPayload().data(), parsed.GetPayload().size(), &response));
    assert(response.pid == 4242u);
    assert(response.error.code == 0);
    assert(handled_pid.load() == 4242u);
}

}  // namespace

int main() {
    TestResumeHandlerRepliesWithResumeResponse();
    TestRpcHandlerRepliesWithRpcResponse();
    TestSpawnInstallHandlerRepliesWithSpawnInstallResponse();
    TestSpawnUninstallHandlerRepliesWithSpawnUninstallResponse();
    TestResumeHandlerRepliesWhenFrameArrivesInFragments();
    return 0;
}
