#pragma once

#include "../protocol/messages.h"
#include "../session/session.h"
#include "../transport/transport.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nook {
namespace comm {

class HostClient {
public:
    explicit HostClient(std::unique_ptr<Transport> transport);
    ~HostClient();

    bool Attach(int timeout_ms,
                const AttachRequest& request,
                AttachResponse* response,
                std::string* error_message = nullptr);
    bool Detach(int timeout_ms,
                const DetachRequest& request,
                DetachResponse* response,
                std::string* error_message = nullptr);
    bool Resume(int timeout_ms,
                const ResumeRequest& request,
                ResumeResponse* response,
                std::string* error_message = nullptr);
    bool EnumerateProcesses(int timeout_ms,
                            ProcessListResponse* response,
                            std::string* error_message = nullptr);
    bool EnumerateApps(int timeout_ms,
                       AppListResponse* response,
                       std::string* error_message = nullptr);

private:
    struct SequencedAgentReady {
        AgentReady ready;
        uint64_t sequence = 0;
    };

    bool EnsureSessionStarted(std::string* error_message);
    void HandleMessage(const Frame& frame);
    bool TryTakeMatchingAgentReadyLocked(uint32_t pid,
                                         const std::string& process_name,
                                         uint64_t min_sequence,
                                         AgentReady* ready);

    Transport* transport_ = nullptr;
    Session session_;
    bool session_started_ = false;

    mutable std::mutex agent_ready_mutex_;
    std::condition_variable agent_ready_cv_;
    std::vector<SequencedAgentReady> agent_ready_events_;
};

}  // namespace comm
}  // namespace nook
