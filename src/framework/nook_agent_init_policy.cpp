#include "framework/nook_agent_init_policy.h"

namespace nook {
namespace framework {
namespace {

bool LooksLikeEarlySpawnProcessNameLocal(const std::string& process_name) {
    return process_name == "zygote" || process_name == "zygote64" ||
           process_name == "usap32" || process_name == "usap64" ||
           process_name == "<pre-initialized>" || process_name == "pre-initialized";
}

}  // namespace

bool ShouldAutoInitializeNookAgent(const std::string& process_name) {
    return !process_name.empty() &&
           !LooksLikeEarlySpawnProcessNameLocal(process_name);
}

bool ShouldActivateInheritedNookAgent(const std::string& process_name, bool has_spawn_marker) {
    (void)has_spawn_marker;
    return ShouldAutoInitializeNookAgent(process_name);
}

}  // namespace framework
}  // namespace nook
