#pragma once

#include <string>

namespace nook {
namespace comm {
class Frame;
class Session;
struct SpawnRequest;
}
namespace server {
class Injector;
class SessionRegistry;
struct ServerHandlerConfig;

void ExecuteSpawnRequest(SessionRegistry* registry,
                         Injector* injector,
                         const ServerHandlerConfig& config,
                         comm::Session& session,
                         const comm::Frame& frame);

}  // namespace server
}  // namespace nook
