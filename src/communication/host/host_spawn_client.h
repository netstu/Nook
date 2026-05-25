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

struct HostSpawnOptions {
    int response_timeout_ms = 20000;
    int agent_ready_timeout_ms = 5000;
};

struct HostSpawnResult {
    bool spawn_response_received = false;
    bool agent_ready_received = false;
    // In phase 1 this pid refers to a gate-held child returned by spawn.
    SpawnResponse spawn_response;
    AgentReady agent_ready;
    std::string error_message;
};

class HostSpawnClient {
public:
    explicit HostSpawnClient(std::unique_ptr<Transport> transport);
    ~HostSpawnClient();

    HostSpawnResult SpawnAndWait(const SpawnRequest& request,
                                 const HostSpawnOptions& options = HostSpawnOptions{});
    // Phase 1 resume preserves the old request shape but now releases the spawn gate.
    bool Resume(uint32_t pid,
                int timeout_ms,
                ResumeResponse* response,
                std::string* error_message = nullptr);
    bool CreateScript(const ScriptCreate& create,
                      int timeout_ms,
                      ScriptCreateResponse* response,
                      std::string* error_message = nullptr);
    bool LoadScript(const ScriptLoad& load,
                    int timeout_ms,
                    ScriptResponse* response,
                    std::string* error_message = nullptr);
    bool UnloadScript(const ScriptUnload& unload,
                      int timeout_ms,
                      ScriptResponse* response,
                      std::string* error_message = nullptr);
    bool CallRpc(const RpcRequest& request,
                 int timeout_ms,
                 RpcResponse* response,
                 std::string* error_message = nullptr);
    bool SendScriptPost(const ScriptPost& post, std::string* error_message = nullptr);
    bool WaitForScriptMessage(int timeout_ms, ScriptMessage* message);

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
                                         AgentReady* ready) const;
    bool TryTakeScriptMessageLocked(ScriptMessage* message) const;

    Transport* transport_ = nullptr;
    Session session_;
    bool session_started_ = false;

    mutable std::mutex agent_ready_mutex_;
    std::condition_variable agent_ready_cv_;
    std::vector<SequencedAgentReady> agent_ready_events_;

    mutable std::mutex script_message_mutex_;
    std::condition_variable script_message_cv_;
    std::vector<ScriptMessage> script_messages_;
};

}  // namespace comm
}  // namespace nook
