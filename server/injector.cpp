#include "injector.h"

#include "../src/communication/protocol/messages.h"

namespace nook {
namespace server {

bool StubInjector::Spawn(const comm::SpawnRequest& request,
                         const std::string& agent_path,
                         int* pid,
                         std::string* error_message) {
    (void)request;
    (void)agent_path;
    if (pid != nullptr) {
        *pid = 0;
    }
    if (error_message != nullptr) {
        *error_message = "StubInjector is not connected to Nook spawn yet";
    }
    return false;
}

bool StubInjector::FinalizeSpawn(const comm::SpawnRequest& request,
                                 std::string* error_message) {
    (void)request;
    if (error_message != nullptr) {
        *error_message = "StubInjector is not connected to Nook spawn yet";
    }
    return false;
}

bool StubInjector::InjectAgent(int pid,
                               const std::string& agent_path,
                               const std::string& ready_token,
                               std::string* error_message) {
    (void)pid;
    (void)agent_path;
    (void)ready_token;
    if (error_message != nullptr) {
        *error_message = "StubInjector is not connected to Nook injector yet";
    }
    return false;
}

bool StubInjector::InjectSpawnChildAgent(int pid,
                                         const std::string& agent_path,
                                         std::string* error_message) {
    (void)pid;
    (void)agent_path;
    if (error_message != nullptr) {
        *error_message = "StubInjector is not connected to Nook spawn child injector yet";
    }
    return false;
}

}  // namespace server
}  // namespace nook
