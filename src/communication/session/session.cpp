#include "session.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nook {
namespace comm {
namespace {

std::atomic<uint32_t> g_next_session_id{1};

bool ShouldRetryReceive(const Transport* transport) {
    return transport != nullptr &&
           transport->IsConnected() &&
           transport->GetState() == TransportState::kConnected &&
           transport->GetLastError() == TransportError::kNone;
}

std::exception_ptr MakeSessionClosedException() {
    return std::make_exception_ptr(std::runtime_error("session closed"));
}

}  // namespace

Session::Session(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)),
      session_id_(g_next_session_id.fetch_add(1, std::memory_order_relaxed)) {}

Session::~Session() {
    Stop();
}

bool Session::Start() {
    if (transport_ == nullptr) {
        return false;
    }

    suppress_close_callback_.store(false, std::memory_order_release);
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    recv_thread_ = std::thread(&Session::RecvLoop, this);
    return true;
}

void Session::Stop() {
    suppress_close_callback_.store(true, std::memory_order_release);
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);

    if (transport_ != nullptr) {
        transport_->Disconnect();
    }

    if (recv_thread_.joinable() &&
        recv_thread_.get_id() != std::this_thread::get_id()) {
        recv_thread_.join();
    } else if (recv_thread_.joinable()) {
        recv_thread_.detach();
    }

    if (!was_running) {
        return;
    }

    std::lock_guard<std::mutex> lock(pending_mutex_);
    FailPendingRequestsLocked();
}

bool Session::SendFrame(const Frame& frame) {
    if (transport_ == nullptr) {
        return false;
    }

    const std::vector<uint8_t> bytes = frame.Serialize();
    std::lock_guard<std::mutex> lock(send_mutex_);
    return transport_->SendAll(bytes.data(), bytes.size());
}

bool Session::SendRequest(const Frame& request,
                          Frame* response,
                          int timeout_ms,
                          uint64_t* response_sequence) {
    if (response == nullptr) {
        return false;
    }

    std::promise<ReceivedFrame> promise;
    std::future<ReceivedFrame> future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.emplace(request.GetMsgId(), std::move(promise));
    }

    if (!SendFrame(request)) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(request.GetMsgId());
        return false;
    }

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(request.GetMsgId());
        return false;
    }

    try {
        ReceivedFrame received = future.get();
        *response = std::move(received.frame);
        if (response_sequence != nullptr) {
            *response_sequence = received.sequence;
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void Session::SetMessageCallback(std::function<void(const Frame&)> cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = std::move(cb);
}

void Session::SetCloseCallback(std::function<void(uint32_t, int)> cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    close_callback_ = std::move(cb);
}

uint32_t Session::NextMsgId() {
    return next_msg_id_.fetch_add(1, std::memory_order_relaxed);
}

void Session::SetPeerPid(int pid) {
    peer_pid_.store(pid, std::memory_order_release);
}

bool Session::IsAlive() const {
    return running_.load(std::memory_order_acquire) &&
           transport_ != nullptr &&
           transport_->IsConnected() &&
           transport_->GetState() == TransportState::kConnected;
}

uint32_t Session::ReadPayloadLength(const uint8_t* header) {
    return (static_cast<uint32_t>(header[0]) << 24) |
           (static_cast<uint32_t>(header[1]) << 16) |
           (static_cast<uint32_t>(header[2]) << 8) |
           static_cast<uint32_t>(header[3]);
}

void Session::RecvLoop() {
    while (running_.load(std::memory_order_acquire)) {
        uint8_t header[Frame::kHeaderSize] = {};
        if (!transport_->RecvAll(header, sizeof(header))) {
            if (ShouldRetryReceive(transport_.get())) {
                continue;
            }
            break;
        }

        const uint32_t payload_len = ReadPayloadLength(header);
        if (payload_len > Frame::kMaxPayloadSize) {
            transport_->Disconnect();
            break;
        }
        std::vector<uint8_t> bytes(Frame::kHeaderSize + payload_len);
        std::copy(header, header + Frame::kHeaderSize, bytes.begin());

        if (payload_len > 0 && !transport_->RecvAll(bytes.data() + Frame::kHeaderSize, payload_len)) {
            if (ShouldRetryReceive(transport_.get())) {
                continue;
            }
            break;
        }

        Frame frame;
        size_t consumed = 0;
        if (!Frame::Parse(bytes.data(), bytes.size(), &frame, &consumed)) {
            transport_->Disconnect();
            break;
        }
        ProcessFrame(frame);
    }

    running_.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        FailPendingRequestsLocked();
    }

    std::function<void(uint32_t, int)> close_callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        close_callback = close_callback_;
    }
    if (close_callback &&
        !suppress_close_callback_.load(std::memory_order_acquire)) {
        close_callback(session_id_, peer_pid_.load(std::memory_order_acquire));
    }
}

void Session::FailPendingRequestsLocked() {
    for (auto& entry : pending_requests_) {
        try {
            entry.second.set_exception(MakeSessionClosedException());
        } catch (const std::future_error&) {
        }
    }
    pending_requests_.clear();
}

void Session::ProcessFrame(const Frame& frame) {
    const uint64_t sequence =
        next_frame_sequence_.fetch_add(1, std::memory_order_acq_rel);
    current_dispatch_sequence_.store(sequence, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(frame.GetMsgId());
        if (it != pending_requests_.end()) {
            try {
                it->second.set_value(ReceivedFrame{frame, sequence});
            } catch (const std::future_error&) {
            }
            pending_requests_.erase(it);
            return;
        }
    }

    std::function<void(const Frame&)> callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = message_callback_;
    }
    if (callback) {
        callback(frame);
    }
}

}  // namespace comm
}  // namespace nook
