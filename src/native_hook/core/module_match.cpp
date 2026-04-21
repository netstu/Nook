#include "module_match.h"

#include <cstring>

namespace ElfHooker {

bool module_path_matches(const char* mapped_path, const char* module_name) {
    if (mapped_path == nullptr || mapped_path[0] == '\0' ||
        module_name == nullptr || module_name[0] == '\0') {
        return false;
    }

    if (std::strcmp(mapped_path, module_name) == 0) {
        return true;
    }

    const char* basename = std::strrchr(mapped_path, '/');
    if (basename != nullptr && std::strcmp(basename + 1, module_name) == 0) {
        return true;
    }

    return std::strstr(mapped_path, module_name) != nullptr;
}

}  // namespace ElfHooker
