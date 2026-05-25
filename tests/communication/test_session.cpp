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

#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/session/session.h"
#include "communication/session/session_manager.h"
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

    bool IsConnected() const override {
        return connected_.load();
    }

    TransportState GetState() const override {
        return connected_.load() ? TransportState::kConnected : TransportState::kDisconnected;
    }

    ssize_t Send(const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_.insert(sent_.end(), data, data + len);
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) override {
        std::unique_lock<std::mutex> lock(mutex_);

        auto has_data = [this]() {
            return !incoming_.empty() || !connected_.load();
        };

        if (timeout_ms >= 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), has_data);
        } else {
            cv_.wait(lock, has_data);
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

    int GetFd() const override {
        return -1;
    }

    const char* GetTypeName() const override {
        return "Fake";
    }

    void PushIncoming(const std::vector<uint8_t>& bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            incoming_.insert(incoming_.end(), bytes.begin(), bytes.end());
        }
        cv_.notify_all();
    }

    std::vector<uint8_t> SentBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sent_;
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

void TestSessionSendFrame() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));
    Frame frame(MessageType::kPing, 11u, {0xAA, 0xBB});
    assert(session.SendFrame(frame));

    Frame parsed;
    size_t consumed = 0;
    const std::vector<uint8_t> sent = raw->SentBytes();
    assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
    assert(parsed.GetType() == MessageType::kPing);
    assert(parsed.GetMsgId() == 11u);
    assert(parsed.GetPayload() == std::vector<uint8_t>({0xAA, 0xBB}));
}

void TestSessionRequestResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));
    assert(session.Start());

    Frame request(MessageType::kScriptCreate, session.NextMsgId(), {0x10});
    Frame response;

    std::thread responder([raw, request]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        raw->PushIncoming(SerializeFrame(MessageType::kScriptCreateResp, request.GetMsgId(), {0x20, 0x21}));
    });

    assert(session.SendRequest(request, &response, 1000));
    assert(response.GetType() == MessageType::kScriptCreateResp);
    assert(response.GetMsgId() == request.GetMsgId());
    assert(response.GetPayload() == std::vector<uint8_t>({0x20, 0x21}));

    responder.join();
    session.Stop();
}

void TestSessionMessageCallback() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool called = false;
    Frame delivered;

    session.SetMessageCallback([&](const Frame& frame) {
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            delivered = frame;
            called = true;
        }
        callback_cv.notify_one();
    });

    assert(session.Start());
    raw->PushIncoming(SerializeFrame(MessageType::kScriptMessage, 88u, {0x33, 0x44, 0x55}));

    std::unique_lock<std::mutex> lock(callback_mutex);
    const bool ok = callback_cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() { return called; });
    assert(ok);
    assert(delivered.GetType() == MessageType::kScriptMessage);
    assert(delivered.GetMsgId() == 88u);
    assert(delivered.GetPayload() == std::vector<uint8_t>({0x33, 0x44, 0x55}));
    lock.unlock();

    session.Stop();
}

void TestSessionTimeoutDoesNotBreakRecvLoop() {
    auto transport = std::make_unique<FakeTransport>();
    transport->Configure(TransportConfig{.connect_timeout_ms = 5000, .read_timeout_ms = 20});
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool called = false;

    session.SetMessageCallback([&](const Frame&) {
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            called = true;
        }
        callback_cv.notify_one();
    });

    assert(session.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    raw->PushIncoming(SerializeFrame(MessageType::kScriptMessage, 99u, {0x01}));

    std::unique_lock<std::mutex> lock(callback_mutex);
    const bool ok = callback_cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() { return called; });
    assert(ok);
    lock.unlock();

    session.Stop();
}

void TestSessionRejectsOversizedPayload() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));
    assert(session.Start());

    constexpr uint32_t kTooLargePayload = 16u * 1024u * 1024u + 1u;
    std::vector<uint8_t> header(Frame::kHeaderSize, 0);
    header[0] = static_cast<uint8_t>((kTooLargePayload >> 24) & 0xFF);
    header[1] = static_cast<uint8_t>((kTooLargePayload >> 16) & 0xFF);
    header[2] = static_cast<uint8_t>((kTooLargePayload >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(kTooLargePayload & 0xFF);
    header[4] = static_cast<uint8_t>((static_cast<uint16_t>(MessageType::kScriptMessage) >> 8) & 0xFF);
    header[5] = static_cast<uint8_t>(static_cast<uint16_t>(MessageType::kScriptMessage) & 0xFF);
    raw->PushIncoming(header);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!raw->IsConnected());

    session.Stop();
}

void TestSessionCloseCallbackRunsOnDisconnect() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));

    std::mutex close_mutex;
    std::condition_variable close_cv;
    bool closed = false;

    session.SetCloseCallback([&](uint32_t session_id, int peer_pid) {
        (void)session_id;
        (void)peer_pid;
        {
            std::lock_guard<std::mutex> lock(close_mutex);
            closed = true;
        }
        close_cv.notify_one();
    });

    assert(session.Start());
    raw->Disconnect();

    std::unique_lock<std::mutex> lock(close_mutex);
    const bool ok = close_cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() {
        return closed;
    });
    assert(ok);
    lock.unlock();

    session.Stop();
}

void TestSessionCloseCallbackUsesLatestPeerPid() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));

    std::mutex close_mutex;
    std::condition_variable close_cv;
    bool closed = false;
    int closed_peer_pid = -1;

    session.SetPeerPid(111);
    session.SetCloseCallback([&](uint32_t, int peer_pid) {
        {
            std::lock_guard<std::mutex> lock(close_mutex);
            closed = true;
            closed_peer_pid = peer_pid;
        }
        close_cv.notify_one();
    });

    assert(session.Start());
    session.SetPeerPid(222);
    raw->Disconnect();

    std::unique_lock<std::mutex> lock(close_mutex);
    const bool ok = close_cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() {
        return closed;
    });
    assert(ok);
    assert(closed_peer_pid == 222);
    lock.unlock();

    session.Stop();
}

void TestSessionRequestReturnsFalseWhenDisconnectedBeforeResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();

    Session session(std::move(transport));
    assert(session.Start());

    Frame request(MessageType::kScriptCreate, session.NextMsgId(), {0x42});
    Frame response;
    std::atomic<bool> completed{false};
    std::atomic<bool> ok{true};

    std::thread requester([&]() {
        ok.store(session.SendRequest(request, &response, 1000));
        completed.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    raw->Disconnect();

    requester.join();
    assert(completed.load());
    assert(!ok.load());
    session.Stop();
}

void TestSessionManager() {
    SessionManager manager;

    auto transport1 = std::make_unique<FakeTransport>();
    auto transport2 = std::make_unique<FakeTransport>();

    Session* s1 = manager.CreateSession(std::move(transport1));
    Session* s2 = manager.CreateSession(std::move(transport2));

    assert(s1 != nullptr);
    assert(s2 != nullptr);
    assert(s1 != s2);
    assert(manager.GetSession(s1->GetId()) == s1);
    assert(manager.GetSession(s2->GetId()) == s2);

    s2->SetPeerPid(12345);
    assert(manager.GetSessionByPid(12345) == s2);

    const std::vector<Session*> all = manager.GetAllSessions();
    assert(all.size() == 2);

    manager.RemoveSession(s1->GetId());
    assert(manager.GetSession(s1->GetId()) == nullptr);
    assert(manager.GetAllSessions().size() == 1);

    manager.Clear();
    assert(manager.GetAllSessions().empty());
}

void TestSessionManagerRemoveDoesNotDestroySessionImmediately() {
    SessionManager manager;
    auto transport = std::make_unique<FakeTransport>();
    Session* session = manager.CreateSession(std::move(transport));

    assert(session != nullptr);
    const uint32_t session_id = session->GetId();
    manager.RemoveSession(session_id);

    assert(manager.GetSession(session_id) == nullptr);
    assert(session->GetId() == session_id);

    manager.Clear();
}

void TestSessionStopSuppressesCloseCallback() {
    auto transport = std::make_unique<FakeTransport>();
    Session session(std::move(transport));

    std::atomic<bool> close_called{false};
    session.SetCloseCallback([&](uint32_t, int) {
        close_called.store(true, std::memory_order_release);
    });

    assert(session.Start());
    session.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!close_called.load(std::memory_order_acquire));
}

}  // namespace

int main() {
    TestSessionSendFrame();
    TestSessionRequestResponse();
    TestSessionMessageCallback();
    TestSessionTimeoutDoesNotBreakRecvLoop();
    TestSessionRejectsOversizedPayload();
    TestSessionCloseCallbackRunsOnDisconnect();
    TestSessionCloseCallbackUsesLatestPeerPid();
    TestSessionRequestReturnsFalseWhenDisconnectedBeforeResponse();
    TestSessionManager();
    TestSessionManagerRemoveDoesNotDestroySessionImmediately();
    TestSessionStopSuppressesCloseCallback();
    return 0;
}
