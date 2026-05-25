#pragma once

#include <string>

namespace nook {
namespace comm {

std::string GetParentDirectory(const std::string& path);
bool EnsureDirectoryRecursive(const std::string& path);

}  // namespace comm
}  // namespace nook
