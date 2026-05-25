#include "host_client.h"

#include "../protocol/frame.h"
#include "../protocol/message_types.h"

#include <algorithm>
#include <chrono>

namespace nook {
namespace comm {

namespace {

bool IsRuntimeReady(const AgentReady& ready) {
    return ready.stage == AgentReadyStage::kRuntime;
}

}  // namespace

HostClient::HostClient(std::unique_ptr<Transport> transport)
    : transport_(transport.get()),
      session_(std::move(transport)) {
    session_.SetMessageCallback([this](const Frame& frame) {
        HandleMessage(frame);
    });
}

HostClient::~HostClient() {
    if (session_started_) {
        session_.Stop();
    }
}

bool HostClient::Attach(int timeout_ms,
                        const AttachRequest& request,
                        AttachResponse* response,
                        std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "attach response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(agent_ready_mutex_);
        agent_ready_events_.clear();
    }

    Frame response_frame;
    uint64_t response_sequence = 0;
    Frame request_frame(MessageType::kAttachRequest,
                        session_.NextMsgId(),
                        EncodeAttachRequest(request));
    if (!session_.SendRequest(request_frame,
                              &response_frame,
                              timeout_ms,
                              &response_sequence)) {
        if (error_message != nullptr) {
            *error_message = "send attach request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kAttachResponse ||
        !DecodeAttachResponse(response_frame.GetPayload().data(),
                              response_frame.GetPayload().size(),
                              response)) {
        if (error_message != nullptr) {
            *error_message = "decode attach response failed";
        }
        return false;
    }

    if (response->error.code != 0) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "attach failed"
                                 : response->error.message;
        }
        return false;
    }

    AgentReady ready;
    {
        std::unique_lock<std::mutex> wait_lock(agent_ready_mutex_);
        if (!TryTakeMatchingAgentReadyLocked(response->pid,
                                             response->process_name,
                                             response_sequence + 1,
                                             &ready)) {
            const bool ready_received = agent_ready_cv_.wait_for(
                wait_lock,
                std::chrono::milliseconds(timeout_ms),
                [&]() {
                    return TryTakeMatchingAgentReadyLocked(response->pid,
                                                          response->process_name,
                                                          response_sequence + 1,
                                                          &ready);
                });
            if (!ready_received) {
                if (error_message != nullptr) {
                    *error_message = "wait runtime agent ready timed out";
                }
                return false;
            }
        }
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostClient::Detach(int timeout_ms,
                        const DetachRequest& request,
                        DetachResponse* response,
                        std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "detach response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kDetachRequest,
                        session_.NextMsgId(),
                        EncodeDetachRequest(request));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send detach request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kDetachResponse ||
        !DecodeDetachResponse(response_frame.GetPayload().data(),
                              response_frame.GetPayload().size(),
                              response)) {
        if (error_message != nullptr) {
            *error_message = "decode detach response failed";
        }
        return false;
    }

    if (response->error.code != 0) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "detach failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostClient::Resume(int timeout_ms,
                        const ResumeRequest& request,
                        ResumeResponse* response,
                        std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "resume response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kResumeRequest,
                        session_.NextMsgId(),
                        EncodeResumeRequest(request));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send resume request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kResumeResponse ||
        !DecodeResumeResponse(response_frame.GetPayload().data(),
                              response_frame.GetPayload().size(),
                              response)) {
        if (error_message != nullptr) {
            *error_message = "decode resume response failed";
        }
        return false;
    }

    if (response->error.code != 0) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "resume failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostClient::EnsureSessionStarted(std::string* error_message) {
    if (transport_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "transport is null";
        }
        return false;
    }

    if (!transport_->IsConnected() && !transport_->Connect()) {
        if (error_message != nullptr) {
            *error_message = transport_->GetLastErrorMessage();
            if (error_message->empty()) {
                *error_message = "transport connect failed";
            }
        }
        return false;
    }

    if (!session_started_) {
        if (!session_.Start()) {
            if (error_message != nullptr) {
                *error_message = "session start failed";
            }
            return false;
        }
        session_started_ = true;
    }

    return true;
}

void HostClient::HandleMessage(const Frame& frame) {
    if (frame.GetType() != MessageType::kAgentReady) {
        return;
    }

    AgentReady ready;
    if (!DecodeAgentReady(frame.GetPayload().data(),
                          frame.GetPayload().size(),
                          &ready)) {
        return;
    }

    std::lock_guard<std::mutex> lock(agent_ready_mutex_);
    agent_ready_events_.push_back(SequencedAgentReady{
        ready,
        session_.GetCurrentDispatchSequence(),
    });
    agent_ready_cv_.notify_all();
}

bool HostClient::TryTakeMatchingAgentReadyLocked(uint32_t pid,
                                                 const std::string& process_name,
                                                 uint64_t min_sequence,
                                                 AgentReady* ready) {
    const auto it = std::find_if(agent_ready_events_.begin(),
                                 agent_ready_events_.end(),
                                 [&](const SequencedAgentReady& candidate) {
                                     return candidate.sequence >= min_sequence &&
                                            candidate.ready.pid == pid &&
                                            candidate.ready.process_name == process_name &&
                                            IsRuntimeReady(candidate.ready);
                                 });
    if (it == agent_ready_events_.end()) {
        return false;
    }

    if (ready != nullptr) {
        *ready = it->ready;
    }
    agent_ready_events_.erase(it);
    return true;
}

bool HostClient::EnumerateProcesses(int timeout_ms,
                                    ProcessListResponse* response,
                                    std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "process list response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kProcessListReq,
                        session_.NextMsgId(),
                        EncodeProcessListRequest(ProcessListRequest{}));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send process list request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kProcessListResp ||
        !DecodeProcessListResponse(response_frame.GetPayload().data(),
                                   response_frame.GetPayload().size(),
                                   response)) {
        if (error_message != nullptr) {
            *error_message = "decode process list response failed";
        }
        return false;
    }

    if (response->error.code != 0) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "process list failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostClient::EnumerateApps(int timeout_ms,
                               AppListResponse* response,
                               std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "app list response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kAppListReq,
                        session_.NextMsgId(),
                        EncodeAppListRequest(AppListRequest{}));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send app list request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kAppListResp ||
        !DecodeAppListResponse(response_frame.GetPayload().data(),
                               response_frame.GetPayload().size(),
                               response)) {
        if (error_message != nullptr) {
            *error_message = "decode app list response failed";
        }
        return false;
    }

    if (response->error.code != 0) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "app list failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

}  // namespace comm
}  // namespace nook
