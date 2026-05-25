#include "framework/nook_agent_runtime.h"

#include <cstdlib>

namespace nook {
namespace framework {
namespace {

std::string DirnameOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return std::string();
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

}  // namespace

std::string ResolveRuntimeDirectoryFromAgentPath(const std::string& agent_path) {
    const char* existing_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    if (existing_runtime_dir != nullptr && existing_runtime_dir[0] != '\0') {
        return std::string(existing_runtime_dir);
    }
    if (agent_path.rfind("/proc/", 0) == 0 && agent_path.find("/fd/") != std::string::npos) {
        return std::string();
    }
    if (agent_path.rfind("/memfd:", 0) == 0 || agent_path.rfind("memfd:", 0) == 0) {
        return std::string();
    }
    return DirnameOf(agent_path);
}

}  // namespace framework
}  // namespace nook
