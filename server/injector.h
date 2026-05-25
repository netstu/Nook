#pragma once

#include <string>

namespace nook {
namespace comm {
struct SpawnRequest;
}
namespace server {

class Injector {
public:
    virtual ~Injector() = default;
    // Begins a spawn transaction. The authoritative child pid is resolved later
    // from AGENT_READY and must not be inferred from this call's output.
    virtual bool Spawn(const comm::SpawnRequest& request,
                       const std::string& agent_path,
                       int* pid,
                       std::string* error_message) = 0;
    virtual bool FinalizeSpawn(const comm::SpawnRequest& request,
                               std::string* error_message) = 0;
    virtual bool InjectAgent(int pid,
                             const std::string& agent_path,
                             const std::string& ready_token,
                             std::string* error_message) = 0;
    virtual bool InjectSpawnChildAgent(int pid,
                                       const std::string& agent_path,
                                       std::string* error_message) = 0;
};

class StubInjector final : public Injector {
public:
    bool Spawn(const comm::SpawnRequest& request,
               const std::string& agent_path,
               int* pid,
               std::string* error_message) override;
    bool FinalizeSpawn(const comm::SpawnRequest& request,
                       std::string* error_message) override;
    bool InjectAgent(int pid,
                     const std::string& agent_path,
                     const std::string& ready_token,
                     std::string* error_message) override;
    bool InjectSpawnChildAgent(int pid,
                               const std::string& agent_path,
                               std::string* error_message) override;
};

}  // namespace server
}  // namespace nook
