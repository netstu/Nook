#include "gadget/nook_gadget_direct_listener.h"

#include "communication/protocol/frame.h"
#include "communication/protocol/messages.h"
#include "communication/transport/tcp_transport.h"
#include "communication/transport/unix_transport.h"
#include "framework/NookCommInternal.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <cstdio>
#include <unistd.h>
#define NOOK_GADGET_LISTEN_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookCommApi", __VA_ARGS__))
#define NOOK_GADGET_LISTEN_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookCommApi", __VA_ARGS__))
#else
#define NOOK_GADGET_LISTEN_LOGI(...) ((void)0)
#define NOOK_GADGET_LISTEN_LOGE(...) ((void)0)
#endif

namespace nook {
namespace gadget {
namespace {

std::mutex& ListenerMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<nook::comm::TransportListener>& DirectListener() {
    static std::unique_ptr<nook::comm::TransportListener> listener;
    return listener;
}

std::thread& ListenerThread() {
    static std::thread thread;
    return thread;
}

std::atomic<bool>& ListenerRunning() {
    static std::atomic<bool> running{false};
    return running;
}

std::string& ListenerAddress() {
    static std::string address;
    return address;
}

int& ListenerPort() {
    static int port = 0;
    return port;
}

std::string ReadProcessNameLocal() {
#if defined(__ANDROID__)
    FILE* fp = fopen("/proc/self/cmdline", "r");
    if (fp == nullptr) {
        return {};
    }

    char cmdline[256] = {0};
    fgets(cmdline, sizeof(cmdline), fp);
    fclose(fp);
    return cmdline;
#else
    return {};
#endif
}

bool SendFrameDirect(nook::comm::Transport* transport,
                     nook::comm::MessageType type,
                     uint32_t message_id,
                     std::vector<uint8_t> payload) {
    if (transport == nullptr || !transport->IsConnected()) {
        return false;
    }

    const nook::comm::Frame frame(type, message_id, std::move(payload));
    const std::vector<uint8_t> bytes = frame.Serialize();
    return transport->SendAll(bytes.data(), bytes.size());
}

bool ReceiveFrameDirect(nook::comm::Transport* transport,
                        nook::comm::Frame* frame) {
    if (transport == nullptr || frame == nullptr) {
        return false;
    }

    uint8_t header[nook::comm::Frame::kHeaderSize] = {};
    if (!transport->RecvAll(header, sizeof(header), 5000)) {
        return false;
    }

    const uint32_t payload_len =
        (static_cast<uint32_t>(header[0]) << 24) |
        (static_cast<uint32_t>(header[1]) << 16) |
        (static_cast<uint32_t>(header[2]) << 8) |
        static_cast<uint32_t>(header[3]);
    if (payload_len > nook::comm::Frame::kMaxPayloadSize) {
        return false;
    }

    std::vector<uint8_t> bytes(nook::comm::Frame::kHeaderSize + payload_len);
    for (size_t i = 0; i < nook::comm::Frame::kHeaderSize; ++i) {
        bytes[i] = header[i];
    }
    if (payload_len > 0 &&
        !transport->RecvAll(bytes.data() + nook::comm::Frame::kHeaderSize, payload_len, 5000)) {
        return false;
    }

    size_t consumed = 0;
    return nook::comm::Frame::Parse(bytes.data(), bytes.size(), frame, &consumed);
}

std::string ResolveListenAddress(const GadgetConfig& config) {
    if (!config.interaction.address.empty()) {
        return config.interaction.address;
    }
    return {};
}

bool IsAbstractListenAddress(const std::string& address) {
    return !address.empty() && address[0] == '@';
}

bool MatchesAttachTarget(const nook::comm::AttachRequest& request,
                         uint32_t pid,
                         const std::string& process_name) {
    const bool has_pid = request.pid != 0;
    const bool has_identifier = !request.identifier.empty();
    if (!has_pid && !has_identifier) {
        return false;
    }
    if (has_pid && request.pid != pid) {
        return false;
    }
    if (has_identifier && request.identifier != process_name) {
        return false;
    }
    return true;
}

void HandleAttachRequest(std::unique_ptr<nook::comm::Transport> transport,
                         const nook::comm::Frame& request_frame) {
    nook::comm::AttachRequest request;
    if (!nook::comm::DecodeAttachRequest(request_frame.GetPayload().data(),
                                         request_frame.GetPayload().size(),
                                         &request)) {
        nook::comm::AttachResponse response;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "invalid attach request";
        (void)SendFrameDirect(transport.get(),
                              nook::comm::MessageType::kAttachResponse,
                              request_frame.GetMsgId(),
                              nook::comm::EncodeAttachResponse(response));
        return;
    }

    const uint32_t pid = static_cast<uint32_t>(getpid());
    const std::string process_name = ReadProcessNameLocal();
    if (!MatchesAttachTarget(request, pid, process_name)) {
        nook::comm::AttachResponse response;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INVALID_ARGUMENT);
        response.error.message = "attach target mismatch";
        (void)SendFrameDirect(transport.get(),
                              nook::comm::MessageType::kAttachResponse,
                              request_frame.GetMsgId(),
                              nook::comm::EncodeAttachResponse(response));
        return;
    }

    if (nook::framework::HasActiveControlChannelConnection()) {
        nook::comm::AttachResponse response;
        response.error.code = static_cast<int32_t>(NOOK_STATUS_INTERNAL_ERROR);
        response.error.message = "gadget direct attach already active";
        (void)SendFrameDirect(transport.get(),
                              nook::comm::MessageType::kAttachResponse,
                              request_frame.GetMsgId(),
                              nook::comm::EncodeAttachResponse(response));
        return;
    }

    nook::comm::AttachResponse response;
    response.session_id = 1u;
    response.pid = pid;
    response.process_name = process_name;
    if (!SendFrameDirect(transport.get(),
                         nook::comm::MessageType::kAttachResponse,
                         request_frame.GetMsgId(),
                         nook::comm::EncodeAttachResponse(response))) {
        NOOK_GADGET_LISTEN_LOGE("direct listener failed to send ATTACH_RESPONSE process=%s",
                                process_name.c_str());
        return;
    }

    const NookStatus adopt_status =
        nook::framework::AdoptInboundControlChannelTransportForCurrentProcess(std::move(transport));
    if (adopt_status != NOOK_STATUS_OK) {
        NOOK_GADGET_LISTEN_LOGE("direct listener failed to adopt inbound transport status=%d process=%s",
                                adopt_status,
                                process_name.c_str());
        return;
    }

    const NookStatus ready_status = nook::framework::NotifyRuntimeReadyToServer();
    if (ready_status != NOOK_STATUS_OK) {
        NOOK_GADGET_LISTEN_LOGE("direct listener failed to notify runtime-ready status=%d process=%s",
                                ready_status,
                                process_name.c_str());
    }
}

void HandleProcessListRequest(std::unique_ptr<nook::comm::Transport> transport,
                              const nook::comm::Frame& request_frame) {
    const uint32_t pid = static_cast<uint32_t>(getpid());
    nook::comm::ProcessListResponse response;
    response.processes.push_back(nook::comm::ProcessEntry{pid, ReadProcessNameLocal()});
    (void)SendFrameDirect(transport.get(),
                          nook::comm::MessageType::kProcessListResp,
                          request_frame.GetMsgId(),
                          nook::comm::EncodeProcessListResponse(response));
}

void HandleAppListRequest(std::unique_ptr<nook::comm::Transport> transport,
                          const nook::comm::Frame& request_frame) {
    nook::comm::AppListResponse response;
    const std::string process_name = ReadProcessNameLocal();
    if (!process_name.empty()) {
        response.apps.push_back(nook::comm::AppEntry{process_name});
    }
    (void)SendFrameDirect(transport.get(),
                          nook::comm::MessageType::kAppListResp,
                          request_frame.GetMsgId(),
                          nook::comm::EncodeAppListResponse(response));
}

void HandleAcceptedConnection(std::unique_ptr<nook::comm::Transport> transport) {
    nook::comm::Frame frame;
    if (!ReceiveFrameDirect(transport.get(), &frame)) {
        return;
    }

    switch (frame.GetType()) {
        case nook::comm::MessageType::kAttachRequest:
            HandleAttachRequest(std::move(transport), frame);
            return;
        case nook::comm::MessageType::kProcessListReq:
            HandleProcessListRequest(std::move(transport), frame);
            return;
        case nook::comm::MessageType::kAppListReq:
            HandleAppListRequest(std::move(transport), frame);
            return;
        default:
            NOOK_GADGET_LISTEN_LOGE("direct listener unsupported first frame type=%u",
                                    static_cast<unsigned>(frame.GetType()));
            return;
    }
}

void DirectListenerThreadMain() {
    while (ListenerRunning().load(std::memory_order_acquire)) {
        std::unique_ptr<nook::comm::Transport> transport;
        {
            std::lock_guard<std::mutex> lock(ListenerMutex());
            if (DirectListener() == nullptr) {
                ListenerRunning().store(false, std::memory_order_release);
                return;
            }
            transport = DirectListener()->Accept(200);
        }
        if (!transport) {
            continue;
        }

        std::thread(&HandleAcceptedConnection, std::move(transport)).detach();
    }
}

}  // namespace

NookStatus EnsureDirectAttachListenerForCurrentProcess(const GadgetConfig& config) {
#if defined(__ANDROID__)
    if (config.interaction.type != "listen" || config.interaction.port <= 0) {
        return NOOK_STATUS_OK;
    }

    const std::string address = ResolveListenAddress(config);
    const int port = config.interaction.port;

    std::lock_guard<std::mutex> lock(ListenerMutex());
    if (ListenerRunning().load(std::memory_order_acquire)) {
        if (ListenerPort() == port && ListenerAddress() == address) {
            return NOOK_STATUS_OK;
        }
        NOOK_GADGET_LISTEN_LOGE("direct listener already active on %s:%d requested=%s:%d",
                                ListenerAddress().c_str(),
                                ListenerPort(),
                                address.c_str(),
                                port);
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    std::unique_ptr<nook::comm::TransportListener> listener;
    if (IsAbstractListenAddress(address)) {
        listener = std::make_unique<nook::comm::UnixListener>(address);
    } else {
        listener = std::make_unique<nook::comm::TcpListener>(port, address);
    }
    if (!listener->Listen()) {
        NOOK_GADGET_LISTEN_LOGE("direct listener listen failed address=%s port=%d",
                                address.c_str(),
                                port);
        return NOOK_STATUS_INTERNAL_ERROR;
    }

    ListenerAddress() = address;
    ListenerPort() = port;
    DirectListener() = std::move(listener);
    ListenerRunning().store(true, std::memory_order_release);
    ListenerThread() = std::thread(&DirectListenerThreadMain);
    ListenerThread().detach();
    NOOK_GADGET_LISTEN_LOGI("direct listener ready address=%s port=%d",
                            address.c_str(),
                            port);
    return NOOK_STATUS_OK;
#else
    (void)config;
    return NOOK_STATUS_OK;
#endif
}

}  // namespace gadget
}  // namespace nook
