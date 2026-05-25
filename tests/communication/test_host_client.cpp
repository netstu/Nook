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

#include "communication/host/host_client.h"
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

    void PushIncoming(const std::vector<uint8_t>& bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            incoming_.insert(incoming_.end(), bytes.begin(), bytes.end());
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

void TestAttach() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kAttachRequest);

        AttachRequest request;
        assert(DecodeAttachRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.identifier == "com.demo.target");

        AttachResponse response;
        response.session_id = 1u;
        response.pid = 321u;
        response.process_name = "com.demo.target";
        raw->PushIncoming(SerializeFrame(MessageType::kAttachResponse,
                                         parsed.GetMsgId(),
                                         EncodeAttachResponse(response)));

        AgentReady control_ready;
        control_ready.pid = 321u;
        control_ready.process_name = "com.demo.target";
        control_ready.spawn_token = "attach-token-control";
        control_ready.arch = "arm64";
        control_ready.version = "0.1.0";
        control_ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         99u,
                                         EncodeAgentReady(control_ready)));

        AgentReady runtime_ready = control_ready;
        runtime_ready.spawn_token = "attach-token-runtime";
        runtime_ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         100u,
                                         EncodeAgentReady(runtime_ready)));
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    AttachResponse response;
    std::string error_message;
    assert(client.Attach(1000, request, &response, &error_message));
    assert(error_message.empty());
    assert(response.session_id == 1u);
    assert(response.pid == 321u);
    assert(response.process_name == "com.demo.target");

    responder.join();
}

void TestAttachReportsRuntimeReadyTimeout() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kAttachRequest);

        AttachResponse response;
        response.session_id = 1u;
        response.pid = 321u;
        response.process_name = "com.demo.target";
        raw->PushIncoming(SerializeFrame(MessageType::kAttachResponse,
                                         parsed.GetMsgId(),
                                         EncodeAttachResponse(response)));
    });

    AttachRequest request;
    request.identifier = "com.demo.target";

    AttachResponse response;
    std::string error_message;
    assert(!client.Attach(20, request, &response, &error_message));
    assert(error_message == "wait runtime agent ready timed out");

    responder.join();
}

void TestDetach() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kDetachRequest);

        DetachRequest request;
        assert(DecodeDetachRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.session_id == 1u);

        DetachResponse response;
        response.session_id = request.session_id;
        raw->PushIncoming(SerializeFrame(MessageType::kDetachResponse,
                                         parsed.GetMsgId(),
                                         EncodeDetachResponse(response)));
    });

    DetachRequest request;
    request.session_id = 1u;

    DetachResponse response;
    std::string error_message;
    assert(client.Detach(1000, request, &response, &error_message));
    assert(error_message.empty());
    assert(response.session_id == 1u);

    responder.join();
}

void TestResume() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kResumeRequest);

        ResumeRequest request;
        assert(DecodeResumeRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.pid == 2100u);

        ResumeResponse response;
        response.pid = request.pid;
        raw->PushIncoming(SerializeFrame(MessageType::kResumeResponse,
                                         parsed.GetMsgId(),
                                         EncodeResumeResponse(response)));
    });

    ResumeRequest request;
    request.pid = 2100u;

    ResumeResponse response;
    std::string error_message;
    assert(client.Resume(1000, request, &response, &error_message));
    assert(error_message.empty());
    assert(response.pid == 2100u);

    responder.join();
}

void TestResumeError() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kResumeRequest);

        ResumeResponse response;
        response.pid = 2100u;
        response.error.code = -3;
        response.error.message = "not suspended";
        raw->PushIncoming(SerializeFrame(MessageType::kResumeResponse,
                                         parsed.GetMsgId(),
                                         EncodeResumeResponse(response)));
    });

    ResumeRequest request;
    request.pid = 2100u;

    ResumeResponse response;
    std::string error_message;
    assert(!client.Resume(1000, request, &response, &error_message));
    assert(error_message == "not suspended");

    responder.join();
}

void TestEnumerateProcesses() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kProcessListReq);

        ProcessListRequest request;
        assert(DecodeProcessListRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));

        ProcessListResponse response;
        response.processes.push_back(ProcessEntry{111u, "zygote64"});
        response.processes.push_back(ProcessEntry{222u, "com.demo.target"});
        raw->PushIncoming(SerializeFrame(MessageType::kProcessListResp,
                                         parsed.GetMsgId(),
                                         EncodeProcessListResponse(response)));
    });

    ProcessListResponse response;
    std::string error_message;
    assert(client.EnumerateProcesses(1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.processes.size() == 2u);
    assert(response.processes[0].pid == 111u);
    assert(response.processes[0].name == "zygote64");
    assert(response.processes[1].pid == 222u);
    assert(response.processes[1].name == "com.demo.target");

    responder.join();
}

void TestEnumerateApps() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostClient client(std::move(transport));

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kAppListReq);

        AppListRequest request;
        assert(DecodeAppListRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));

        AppListResponse response;
        response.apps.push_back(AppEntry{"com.android.settings"});
        response.apps.push_back(AppEntry{"com.demo.target"});
        raw->PushIncoming(SerializeFrame(MessageType::kAppListResp,
                                         parsed.GetMsgId(),
                                         EncodeAppListResponse(response)));
    });

    AppListResponse response;
    std::string error_message;
    assert(client.EnumerateApps(1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.apps.size() == 2u);
    assert(response.apps[0].package_name == "com.android.settings");
    assert(response.apps[1].package_name == "com.demo.target");

    responder.join();
}

}  // namespace

int main() {
    TestAttach();
    TestAttachReportsRuntimeReadyTimeout();
    TestDetach();
    TestResume();
    TestResumeError();
    TestEnumerateProcesses();
    TestEnumerateApps();
    return 0;
}
