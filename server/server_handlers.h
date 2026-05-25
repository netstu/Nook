#pragma once

#include "process_manager.h"

#include <cstdint>
#include <functional>
#include <string>

namespace nook {
namespace comm {
class Frame;
class MessageDispatcher;
class Session;
}
namespace server {

class Injector;
class SessionRegistry;

struct ServerHandlerConfig {
    std::string agent_path;
    uint32_t spawn_ready_timeout_ms = 0;
    std::function<std::vector<ProcessInfo>()> enumerate_processes;
    std::function<std::vector<AppInfo>()> enumerate_apps;
    std::function<bool(int pid)> suspend_process;
    // Phase 1 keeps the legacy field name, but resume now means releasing the spawn gate.
    std::function<bool(int pid)> resume_process;
    // Test-only hook for pinning spawn-response race windows.
    std::function<void(int pid, const std::string& spawn_token)> on_spawn_context_bound;
};

void RegisterServerHandlers(comm::MessageDispatcher* dispatcher,
                            SessionRegistry* registry,
                            Injector* injector,
                            const ServerHandlerConfig& config);

bool ReplayCachedScriptMessages(SessionRegistry* registry,
                                int pid,
                                comm::Session& session,
                                const char* log_context,
                                uint32_t host_session_id);
bool ReplayCachedAgentReadyThenScriptMessages(SessionRegistry* registry,
                                              int pid,
                                              const std::string& process_name,
                                              comm::Session& session,
                                              uint32_t host_session_id,
                                              bool require_runtime_stage);

}  // namespace server
}  // namespace nook
