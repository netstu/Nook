#pragma once

#include "../protocol/frame.h"
#include "../transport/transport.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace nook {
namespace comm {

class Session {
public:
    struct ReceivedFrame {
        Frame frame;
        uint64_t sequence = 0;
    };

    explicit Session(std::unique_ptr<Transport> transport);
    ~Session();

    bool Start();
    void Stop();

    bool SendFrame(const Frame& frame);
    bool SendRequest(const Frame& request,
                     Frame* response,
                     int timeout_ms = 5000,
                     uint64_t* response_sequence = nullptr);

    void SetMessageCallback(std::function<void(const Frame&)> cb);
    void SetCloseCallback(std::function<void(uint32_t, int)> cb);

    uint32_t GetId() const { return session_id_; }
    uint32_t NextMsgId();
    uint64_t GetCurrentDispatchSequence() const {
        return current_dispatch_sequence_.load(std::memory_order_acquire);
    }

    void SetPeerPid(int pid);
    int GetPeerPid() const { return peer_pid_.load(); }
    bool IsAlive() const;

private:
    void RecvLoop();
    void ProcessFrame(const Frame& frame);
    void FailPendingRequestsLocked();

    static uint32_t ReadPayloadLength(const uint8_t* header);

    std::unique_ptr<Transport> transport_;
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> suppress_close_callback_{false};

    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    std::function<void(const Frame&)> message_callback_;
    std::function<void(uint32_t, int)> close_callback_;

    std::mutex pending_mutex_;
    std::map<uint32_t, std::promise<ReceivedFrame>> pending_requests_;
    std::atomic<uint32_t> next_msg_id_{1};
    std::atomic<uint64_t> next_frame_sequence_{1};
    std::atomic<uint64_t> current_dispatch_sequence_{0};

    const uint32_t session_id_;
    std::atomic<int> peer_pid_{-1};
};

}  // namespace comm
}  // namespace nook
