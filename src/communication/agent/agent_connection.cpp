#include "agent_connection.h"

#include "../protocol/frame.h"

#include <algorithm>
#include <utility>
#include <vector>
#if defined(__ANDROID__)
#include <android/log.h>
#define NOOK_AGENT_CONN_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookCommApi", __VA_ARGS__))
#define NOOK_AGENT_CONN_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookCommApi", __VA_ARGS__))
#else
#define NOOK_AGENT_CONN_LOGI(...) ((void)0)
#define NOOK_AGENT_CONN_LOGE(...) ((void)0)
#endif
namespace nook {
namespace comm {
namespace {

uint32_t ReadPayloadLength(const uint8_t* header) {
    return (static_cast<uint32_t>(header[0]) << 24) |
           (static_cast<uint32_t>(header[1]) << 16) |
           (static_cast<uint32_t>(header[2]) << 8) |
           static_cast<uint32_t>(header[3]);
}

bool ShouldRetryReceive(const Transport* transport) {
    return transport != nullptr &&
           transport->IsConnected() &&
           transport->GetState() == TransportState::kConnected &&
           transport->GetLastError() == TransportError::kNone;
}

bool SendReplyFrame(Transport* transport,
                    std::mutex* send_mutex,
                    const Frame& reply) {
    if (transport == nullptr || send_mutex == nullptr || !transport->IsConnected()) {
        return false;
    }

    const std::vector<uint8_t> bytes = reply.Serialize();
    std::lock_guard<std::mutex> lock(*send_mutex);
    return transport->SendAll(bytes.data(), bytes.size());
}

}  // namespace

AgentConnection::AgentConnection(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)) {}

AgentConnection::~AgentConnection() {
    StopRecvLoop();
    if (transport_ != nullptr && transport_->IsConnected()) {
        transport_->Disconnect();
    }
}

bool AgentConnection::Connect() {
    if (transport_ == nullptr) {
        return false;
    }

    if (!transport_->Connect()) {
        return false;
    }

    return true;
}

bool AgentConnection::IsConnected() const {
    return transport_ != nullptr && transport_->IsConnected();
}

bool AgentConnection::SendAgentReady(const AgentReady& ready) {
    return SendFrame(MessageType::kAgentReady, EncodeAgentReady(ready));
}

bool AgentConnection::SendScriptMessage(const ScriptMessage& message) {
    return SendFrame(MessageType::kScriptMessage, EncodeScriptMessage(message));
}

bool AgentConnection::SendFrame(MessageType type, const std::vector<uint8_t>& payload) {
    if (transport_ == nullptr || !transport_->IsConnected()) {
        return false;
    }

    Frame frame(type, next_msg_id_++, payload);
    const std::vector<uint8_t> bytes = frame.Serialize();
    std::lock_guard<std::mutex> lock(send_mutex_);
    return transport_->SendAll(bytes.data(), bytes.size());
}

void AgentConnection::SetMessageCallback(std::function<void(const ScriptPost&)> callback) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        message_callback_ = std::move(callback);
    }
}

void AgentConnection::SetScriptCreateHandler(
        std::function<ScriptCreateResponse(const ScriptCreate&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        script_create_handler_ = std::move(handler);
    }
}

void AgentConnection::SetScriptLoadHandler(
        std::function<ScriptResponse(const ScriptLoad&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        script_load_handler_ = std::move(handler);
    }
}

void AgentConnection::SetScriptUnloadHandler(
        std::function<ScriptResponse(const ScriptUnload&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        script_unload_handler_ = std::move(handler);
    }
}

void AgentConnection::SetSpawnInstallHandler(
        std::function<SpawnInstallResponse(const SpawnInstallRequest&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        spawn_install_handler_ = std::move(handler);
    }
}

void AgentConnection::SetSpawnUninstallHandler(
        std::function<SpawnUninstallResponse(const SpawnUninstallRequest&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        spawn_uninstall_handler_ = std::move(handler);
    }
}

void AgentConnection::SetRpcHandler(
        std::function<RpcResponse(const RpcRequest&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        rpc_handler_ = std::move(handler);
    }
}

void AgentConnection::SetResumeHandler(
        std::function<ResumeResponse(const ResumeRequest&)> handler) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        resume_handler_ = std::move(handler);
    }
}

void AgentConnection::StartRecvLoop() {
    EnsureRecvLoopStarted();
}

void AgentConnection::RequestDisconnectAfterCurrentReply() {
    disconnect_after_current_reply_.store(true, std::memory_order_release);
}

void AgentConnection::EnsureRecvLoopStarted() {
    if (transport_ == nullptr || !transport_->IsConnected()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (!message_callback_ && !script_create_handler_ && !script_load_handler_ &&
            !script_unload_handler_ && !spawn_install_handler_ &&
            !spawn_uninstall_handler_ && !rpc_handler_ && !resume_handler_) {
            return;
        }
    }

    bool expected = false;
    if (!recv_running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    recv_thread_ = std::thread(&AgentConnection::RecvLoop, this);
}

void AgentConnection::StopRecvLoop() {
    const bool was_running = recv_running_.exchange(false, std::memory_order_acq_rel);
    if (was_running && transport_ != nullptr && transport_->IsConnected()) {
        transport_->Disconnect();
    }

    if (recv_thread_.joinable()) {
        if (recv_thread_.get_id() == std::this_thread::get_id()) {
            NOOK_AGENT_CONN_LOGE("StopRecvLoop called from recv thread; detaching to avoid terminate");
            recv_thread_.detach();
        } else {
            recv_thread_.join();
        }
    }
}

void AgentConnection::RecvLoop() {
    while (recv_running_.load(std::memory_order_acquire)) {
        uint8_t header[Frame::kHeaderSize] = {};
        size_t header_received = 0;
        while (header_received < sizeof(header) &&
               recv_running_.load(std::memory_order_acquire)) {
            const ssize_t n = transport_->Recv(header + header_received,
                                               sizeof(header) - header_received,
                                               100);
            if (n < 0) {
                if (ShouldRetryReceive(transport_.get())) {
                    continue;
                }
                goto recv_loop_exit;
            }
            if (n == 0) {
                if (ShouldRetryReceive(transport_.get())) {
                    continue;
                }
                goto recv_loop_exit;
            }
            header_received += static_cast<size_t>(n);
        }

        const uint32_t payload_len = ReadPayloadLength(header);
        if (payload_len > Frame::kMaxPayloadSize) {
            transport_->Disconnect();
            break;
        }

        std::vector<uint8_t> bytes(Frame::kHeaderSize + payload_len);
        std::copy(header, header + Frame::kHeaderSize, bytes.begin());
        size_t payload_received = 0;
        while (payload_received < payload_len &&
               recv_running_.load(std::memory_order_acquire)) {
            const ssize_t n = transport_->Recv(bytes.data() + Frame::kHeaderSize + payload_received,
                                               payload_len - payload_received,
                                               100);
            if (n < 0) {
                if (ShouldRetryReceive(transport_.get())) {
                    continue;
                }
                goto recv_loop_exit;
            }
            if (n == 0) {
                if (ShouldRetryReceive(transport_.get())) {
                    continue;
                }
                goto recv_loop_exit;
            }
            payload_received += static_cast<size_t>(n);
        }

        Frame frame;
        size_t consumed = 0;
        if (!Frame::Parse(bytes.data(), bytes.size(), &frame, &consumed)) {
            transport_->Disconnect();
            break;
        }

        if (frame.GetType() == MessageType::kScriptPost) {
            ScriptPost post;
            if (!DecodeScriptPost(frame.GetPayload().data(), frame.GetPayload().size(), &post)) {
                continue;
            }

            std::function<void(const ScriptPost&)> callback;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                callback = message_callback_;
            }
            if (callback) {
                callback(post);
            }
            continue;
        }

        if (frame.GetType() == MessageType::kScriptCreate) {
            ScriptCreate create;
            if (!DecodeScriptCreate(frame.GetPayload().data(), frame.GetPayload().size(), &create)) {
                continue;
            }

            std::function<ScriptCreateResponse(const ScriptCreate&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = script_create_handler_;
            }
            if (!handler) {
                NOOK_AGENT_CONN_LOGE("script create received before runtime ready");
                const ScriptCreateResponse response{
                    0u,
                    false,
                    ErrorInfo{-100, "agent runtime not ready for script create"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kScriptCreateResp,
                                     frame.GetMsgId(),
                                     EncodeScriptCreateResponse(response)));
                continue;
            }

            ScriptCreateResponse response = handler(create);
            const Frame reply(MessageType::kScriptCreateResp,
                              frame.GetMsgId(),
                              EncodeScriptCreateResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            continue;
        }

        if (frame.GetType() == MessageType::kScriptLoad) {
            ScriptLoad load;
            if (!DecodeScriptLoad(frame.GetPayload().data(), frame.GetPayload().size(), &load)) {
                continue;
            }

            std::function<ScriptResponse(const ScriptLoad&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = script_load_handler_;
            }
            if (!handler) {
                NOOK_AGENT_CONN_LOGE("script load received before runtime ready");
                const ScriptResponse response{
                    load.script_id,
                    false,
                    ErrorInfo{-100, "agent runtime not ready for script load"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kScriptLoadResp,
                                     frame.GetMsgId(),
                                     EncodeScriptResponse(response)));
                continue;
            }

            ScriptResponse response = handler(load);
            const Frame reply(MessageType::kScriptLoadResp,
                              frame.GetMsgId(),
                              EncodeScriptResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            continue;
        }

        if (frame.GetType() == MessageType::kScriptUnload) {
            ScriptUnload unload;
            if (!DecodeScriptUnload(frame.GetPayload().data(), frame.GetPayload().size(), &unload)) {
                continue;
            }

            std::function<ScriptResponse(const ScriptUnload&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = script_unload_handler_;
            }
            if (!handler) {
                NOOK_AGENT_CONN_LOGE("script unload received before runtime ready");
                const ScriptResponse response{
                    unload.script_id,
                    false,
                    ErrorInfo{-100, "agent runtime not ready for script unload"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kScriptUnloadResp,
                                     frame.GetMsgId(),
                                     EncodeScriptResponse(response)));
                continue;
            }

            ScriptResponse response = handler(unload);
            const Frame reply(MessageType::kScriptUnloadResp,
                              frame.GetMsgId(),
                              EncodeScriptResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            continue;
        }

        if (frame.GetType() == MessageType::kRpcRequest) {
            RpcRequest request;
            if (!DecodeRpcRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
                NOOK_AGENT_CONN_LOGE("rpc request decode failed");
                continue;
            }

            std::function<RpcResponse(const RpcRequest&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = rpc_handler_;
            }
            NOOK_AGENT_CONN_LOGI("rpc request received method=%s handler=%d",
                                 request.method.c_str(),
                                 handler ? 1 : 0);
            if (!handler) {
                NOOK_AGENT_CONN_LOGE("rpc request received before runtime ready method=%s",
                                     request.method.c_str());
                const RpcResponse response{
                    request.script_id,
                    false,
                    "",
                    ErrorInfo{-100, "agent runtime not ready for rpc request"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kRpcResponse,
                                     frame.GetMsgId(),
                                     EncodeRpcResponse(response)));
                continue;
            }

            RpcResponse response = handler(request);
            NOOK_AGENT_CONN_LOGI("rpc request handled method=%s success=%d error=%s",
                                 request.method.c_str(),
                                 response.success ? 1 : 0,
                                 response.error.message.c_str());
            const Frame reply(MessageType::kRpcResponse,
                              frame.GetMsgId(),
                              EncodeRpcResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            bool send_ok = false;
            {
                std::lock_guard<std::mutex> lock(send_mutex_);
                send_ok = transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            }
            if (send_ok &&
                disconnect_after_current_reply_.exchange(false, std::memory_order_acq_rel)) {
                NOOK_AGENT_CONN_LOGI("disconnect requested after rpc reply method=%s",
                                     request.method.c_str());
                transport_->Disconnect();
                break;
            }
            continue;
        }

        if (frame.GetType() == MessageType::kSpawnInstall) {
            SpawnInstallRequest request;
            if (!DecodeSpawnInstallRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
                NOOK_AGENT_CONN_LOGE("spawn install decode failed");
                continue;
            }

            std::function<SpawnInstallResponse(const SpawnInstallRequest&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = spawn_install_handler_;
            }
            if (!handler) {
                const SpawnInstallResponse response{
                    false,
                    ErrorInfo{-100, "agent runtime not ready for spawn install"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kSpawnInstallResp,
                                     frame.GetMsgId(),
                                     EncodeSpawnInstallResponse(response)));
                continue;
            }

            SpawnInstallResponse response = handler(request);
            const Frame reply(MessageType::kSpawnInstallResp,
                              frame.GetMsgId(),
                              EncodeSpawnInstallResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            continue;
        }

        if (frame.GetType() == MessageType::kSpawnUninstall) {
            SpawnUninstallRequest request;
            if (!DecodeSpawnUninstallRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
                NOOK_AGENT_CONN_LOGE("spawn uninstall decode failed");
                continue;
            }

            std::function<SpawnUninstallResponse(const SpawnUninstallRequest&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = spawn_uninstall_handler_;
            }
            if (!handler) {
                const SpawnUninstallResponse response{
                    false,
                    ErrorInfo{-100, "agent runtime not ready for spawn uninstall"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kSpawnUninstallResp,
                                     frame.GetMsgId(),
                                     EncodeSpawnUninstallResponse(response)));
                continue;
            }

            SpawnUninstallResponse response = handler(request);
            const Frame reply(MessageType::kSpawnUninstallResp,
                              frame.GetMsgId(),
                              EncodeSpawnUninstallResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
            continue;
        }

        if (frame.GetType() == MessageType::kResumeRequest) {
            ResumeRequest request;
            if (!DecodeResumeRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
                continue;
            }

            std::function<ResumeResponse(const ResumeRequest&)> handler;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                handler = resume_handler_;
            }
            if (!handler) {
                NOOK_AGENT_CONN_LOGE("resume request received before runtime ready pid=%u",
                                     request.pid);
                const ResumeResponse response{
                    request.pid,
                    ErrorInfo{-100, "agent runtime not ready for resume"},
                };
                SendReplyFrame(transport_.get(),
                               &send_mutex_,
                               Frame(MessageType::kResumeResponse,
                                     frame.GetMsgId(),
                                     EncodeResumeResponse(response)));
                continue;
            }

            ResumeResponse response = handler(request);
            const Frame reply(MessageType::kResumeResponse,
                              frame.GetMsgId(),
                              EncodeResumeResponse(response));
            const std::vector<uint8_t> reply_bytes = reply.Serialize();
            std::lock_guard<std::mutex> lock(send_mutex_);
            transport_->SendAll(reply_bytes.data(), reply_bytes.size());
        }
    }

recv_loop_exit:
    recv_running_.store(false, std::memory_order_release);
}

}  // namespace comm
}  // namespace nook
