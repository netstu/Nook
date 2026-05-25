#pragma once

#include "../protocol/message_types.h"
#include "../protocol/messages.h"
#include "../transport/transport.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace nook {
namespace comm {

class AgentConnection {
public:
    explicit AgentConnection(std::unique_ptr<Transport> transport);
    ~AgentConnection();

    bool Connect();
    bool IsConnected() const;
    bool SendAgentReady(const AgentReady& ready);
    bool SendScriptMessage(const ScriptMessage& message);
    void StartRecvLoop();
    void RequestDisconnectAfterCurrentReply();
    void SetMessageCallback(std::function<void(const ScriptPost&)> callback);
    void SetScriptCreateHandler(std::function<ScriptCreateResponse(const ScriptCreate&)> handler);
    void SetScriptLoadHandler(std::function<ScriptResponse(const ScriptLoad&)> handler);
    void SetScriptUnloadHandler(std::function<ScriptResponse(const ScriptUnload&)> handler);
    void SetSpawnInstallHandler(std::function<SpawnInstallResponse(const SpawnInstallRequest&)> handler);
    void SetSpawnUninstallHandler(std::function<SpawnUninstallResponse(const SpawnUninstallRequest&)> handler);
    void SetRpcHandler(std::function<RpcResponse(const RpcRequest&)> handler);
    void SetResumeHandler(std::function<ResumeResponse(const ResumeRequest&)> handler);

private:
    bool SendFrame(MessageType type, const std::vector<uint8_t>& payload);
    void EnsureRecvLoopStarted();
    void StopRecvLoop();
    void RecvLoop();

    std::unique_ptr<Transport> transport_;
    uint32_t next_msg_id_ = 1;
    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    std::function<void(const ScriptPost&)> message_callback_;
    std::function<ScriptCreateResponse(const ScriptCreate&)> script_create_handler_;
    std::function<ScriptResponse(const ScriptLoad&)> script_load_handler_;
    std::function<ScriptResponse(const ScriptUnload&)> script_unload_handler_;
    std::function<SpawnInstallResponse(const SpawnInstallRequest&)> spawn_install_handler_;
    std::function<SpawnUninstallResponse(const SpawnUninstallRequest&)> spawn_uninstall_handler_;
    std::function<RpcResponse(const RpcRequest&)> rpc_handler_;
    std::function<ResumeResponse(const ResumeRequest&)> resume_handler_;
    std::atomic<bool> disconnect_after_current_reply_{false};
    std::atomic<bool> recv_running_{false};
    std::thread recv_thread_;
};

}  // namespace comm
}  // namespace nook
