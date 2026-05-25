#pragma once

#include <string>

namespace nook {
namespace framework {

bool ShouldAutoInitializeNookAgent(const std::string& process_name);
bool ShouldActivateInheritedNookAgent(const std::string& process_name, bool has_spawn_marker);

}  // namespace framework
}  // namespace nook
