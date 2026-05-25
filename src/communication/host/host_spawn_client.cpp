#include "host_spawn_client.h"

#include "../protocol/frame.h"
#include "../protocol/message_types.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace nook {
namespace comm {

namespace {

bool IsRuntimeReady(const AgentReady& ready) {
    return ready.stage == AgentReadyStage::kRuntime;
}

}  // namespace

HostSpawnClient::HostSpawnClient(std::unique_ptr<Transport> transport)
    : transport_(transport.get()),
      session_(std::move(transport)) {
    session_.SetMessageCallback([this](const Frame& frame) {
        HandleMessage(frame);
    });
}

HostSpawnClient::~HostSpawnClient() {
    if (session_started_) {
        session_.Stop();
    }
}

bool HostSpawnClient::EnsureSessionStarted(std::string* error_message) {
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

HostSpawnResult HostSpawnClient::SpawnAndWait(const SpawnRequest& request,
                                             const HostSpawnOptions& options) {
    HostSpawnResult result;
    if (request.identifier.empty()) {
        result.error_message = "spawn identifier is empty";
        return result;
    }

    if (!EnsureSessionStarted(&result.error_message)) {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(agent_ready_mutex_);
        agent_ready_events_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(script_message_mutex_);
        script_messages_.clear();
    }

    const int response_timeout_ms =
        options.response_timeout_ms > 0
            ? options.response_timeout_ms
            : HostSpawnOptions{}.response_timeout_ms;
    const int agent_ready_timeout_ms =
        options.agent_ready_timeout_ms > 0
            ? options.agent_ready_timeout_ms
            : HostSpawnOptions{}.agent_ready_timeout_ms;

    Frame response_frame;
    uint64_t response_sequence = 0;
    Frame request_frame(MessageType::kSpawnRequest,
                        session_.NextMsgId(),
                        EncodeSpawnRequest(request));
    if (!session_.SendRequest(request_frame,
                              &response_frame,
                              response_timeout_ms,
                              &response_sequence)) {
        result.error_message = "wait spawn response timed out";
        return result;
    }

    if (response_frame.GetType() != MessageType::kSpawnResponse) {
        result.error_message = "unexpected response type";
        return result;
    }

    if (!DecodeSpawnResponse(response_frame.GetPayload().data(),
                             response_frame.GetPayload().size(),
                             &result.spawn_response)) {
        result.error_message = "decode spawn response failed";
        return result;
    }
    result.spawn_response_received = true;

    if (result.spawn_response.error.code != 0) {
        result.error_message = result.spawn_response.error.message.empty()
                                   ? "spawn failed"
                                   : result.spawn_response.error.message;
        return result;
    }

    // SpawnResponse now only confirms a gate-held child exists; the host must still
    // wait for AgentReady before creating/loading scripts or releasing that gate.
    {
        std::lock_guard<std::mutex> lock(agent_ready_mutex_);
        if (TryTakeMatchingAgentReadyLocked(result.spawn_response.pid,
                                            request.identifier,
                                            response_sequence + 1,
                                            &result.agent_ready)) {
            result.agent_ready_received = true;
            return result;
        }
    }

    std::unique_lock<std::mutex> lock(agent_ready_mutex_);
    const bool ready = agent_ready_cv_.wait_for(
        lock,
        std::chrono::milliseconds(agent_ready_timeout_ms),
        [&]() {
            return TryTakeMatchingAgentReadyLocked(result.spawn_response.pid,
                                                   request.identifier,
                                                   response_sequence + 1,
                                                   &result.agent_ready);
        });
    result.agent_ready_received = ready;
    if (!ready) {
        result.error_message = "wait runtime agent ready timed out";
    }
    return result;
}

bool HostSpawnClient::Resume(uint32_t pid,
                             int timeout_ms,
                             ResumeResponse* response,
                             std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "resume response is null";
        }
        return false;
    }

    if (pid == 0u) {
        if (error_message != nullptr) {
            *error_message = "resume pid is zero";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    ResumeRequest request;
    request.pid = pid;

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

bool HostSpawnClient::CreateScript(const ScriptCreate& create,
                                   int timeout_ms,
                                   ScriptCreateResponse* response,
                                   std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "script create response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    ScriptCreate request = create;
    if (request.session_id == 0u) {
        request.session_id = session_.GetId();
    }

    Frame response_frame;
    Frame request_frame(MessageType::kScriptCreate,
                        session_.NextMsgId(),
                        EncodeScriptCreate(request));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send script create failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kScriptCreateResp ||
        !DecodeScriptCreateResponse(response_frame.GetPayload().data(),
                                    response_frame.GetPayload().size(),
                                    response)) {
        if (error_message != nullptr) {
            *error_message = "decode script create response failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "script create failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostSpawnClient::LoadScript(const ScriptLoad& load,
                                 int timeout_ms,
                                 ScriptResponse* response,
                                 std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "script load response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kScriptLoad,
                        session_.NextMsgId(),
                        EncodeScriptLoad(load));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send script load failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kScriptLoadResp ||
        !DecodeScriptResponse(response_frame.GetPayload().data(),
                              response_frame.GetPayload().size(),
                              response)) {
        if (error_message != nullptr) {
            *error_message = "decode script load response failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "script load failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostSpawnClient::UnloadScript(const ScriptUnload& unload,
                                   int timeout_ms,
                                   ScriptResponse* response,
                                   std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "script unload response is null";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kScriptUnload,
                        session_.NextMsgId(),
                        EncodeScriptUnload(unload));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send script unload failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kScriptUnloadResp ||
        !DecodeScriptResponse(response_frame.GetPayload().data(),
                              response_frame.GetPayload().size(),
                              response)) {
        if (error_message != nullptr) {
            *error_message = "decode script unload response failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "script unload failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostSpawnClient::CallRpc(const RpcRequest& request,
                              int timeout_ms,
                              RpcResponse* response,
                              std::string* error_message) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "rpc response is null";
        }
        return false;
    }

    if (request.method.empty()) {
        if (error_message != nullptr) {
            *error_message = "rpc method is empty";
        }
        return false;
    }

    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame response_frame;
    Frame request_frame(MessageType::kRpcRequest,
                        session_.NextMsgId(),
                        EncodeRpcRequest(request));
    if (!session_.SendRequest(request_frame, &response_frame, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "send rpc request failed or timed out";
        }
        return false;
    }

    if (response_frame.GetType() != MessageType::kRpcResponse ||
        !DecodeRpcResponse(response_frame.GetPayload().data(),
                           response_frame.GetPayload().size(),
                           response)) {
        if (error_message != nullptr) {
            *error_message = "decode rpc response failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                                 ? "rpc call failed"
                                 : response->error.message;
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool HostSpawnClient::WaitForScriptMessage(int timeout_ms, ScriptMessage* message) {
    if (message == nullptr) {
        return false;
    }

    std::unique_lock<std::mutex> lock(script_message_mutex_);
    return script_message_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [&]() {
            return TryTakeScriptMessageLocked(message);
        });
}

bool HostSpawnClient::SendScriptPost(const ScriptPost& post, std::string* error_message) {
    if (!EnsureSessionStarted(error_message)) {
        return false;
    }

    Frame frame(MessageType::kScriptPost,
                session_.NextMsgId(),
                EncodeScriptPost(post));
    if (!session_.SendFrame(frame)) {
        if (error_message != nullptr) {
            *error_message = "send script post failed";
        }
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

void HostSpawnClient::HandleMessage(const Frame& frame) {
    if (frame.GetType() == MessageType::kAgentReady) {
        AgentReady ready;
        if (!DecodeAgentReady(frame.GetPayload().data(), frame.GetPayload().size(), &ready)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(agent_ready_mutex_);
            agent_ready_events_.push_back(
                SequencedAgentReady{std::move(ready), session_.GetCurrentDispatchSequence()});
        }
        agent_ready_cv_.notify_all();
        return;
    }

    if (frame.GetType() != MessageType::kScriptMessage) {
        return;
    }

    ScriptMessage message;
    if (!DecodeScriptMessage(frame.GetPayload().data(), frame.GetPayload().size(), &message)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(script_message_mutex_);
        script_messages_.push_back(std::move(message));
    }
    script_message_cv_.notify_all();
}

bool HostSpawnClient::TryTakeMatchingAgentReadyLocked(uint32_t pid,
                                                      const std::string& process_name,
                                                      uint64_t min_sequence,
                                                      AgentReady* ready) const {
    auto self = const_cast<HostSpawnClient*>(this);
    auto it = std::find_if(self->agent_ready_events_.begin(),
                           self->agent_ready_events_.end(),
                           [pid, &process_name, min_sequence](const SequencedAgentReady& item) {
                               if (item.sequence < min_sequence ||
                                   item.ready.pid != pid ||
                                   !IsRuntimeReady(item.ready)) {
                                   return false;
                               }
                               if (process_name.empty() || item.ready.process_name.empty()) {
                                   return true;
                               }
                               return item.ready.process_name == process_name;
                           });
    if (it == self->agent_ready_events_.end()) {
        return false;
    }

    if (ready != nullptr) {
        *ready = it->ready;
    }
    self->agent_ready_events_.erase(it);
    return true;
}

bool HostSpawnClient::TryTakeScriptMessageLocked(ScriptMessage* message) const {
    auto self = const_cast<HostSpawnClient*>(this);
    if (self->script_messages_.empty()) {
        return false;
    }

    if (message != nullptr) {
        *message = self->script_messages_.front();
    }
    self->script_messages_.erase(self->script_messages_.begin());
    return true;
}

}  // namespace comm
}  // namespace nook
