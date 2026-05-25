#include "path_utils.h"

#include <cerrno>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace nook {
namespace comm {
namespace {

bool CreateSingleDirectory(const std::string& path) {
    if (path.empty()) {
        return true;
    }

#if defined(_WIN32)
    const int result = _mkdir(path.c_str());
#else
    const int result = mkdir(path.c_str(), 0777);
#endif
    return result == 0 || errno == EEXIST;
}

}  // namespace

std::string GetParentDirectory(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return {};
    }
    if (pos == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, pos);
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return true;
    }

    std::string current;
    const bool absolute_unix = !path.empty() && path[0] == '/';
    if (absolute_unix) {
        current = "/";
    }

    size_t start = absolute_unix ? 1 : 0;
    while (start <= path.size()) {
        const size_t pos = path.find_first_of("/\\", start);
        const std::string part = path.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/' && current.back() != '\\') {
                current.push_back('/');
            }
            current += part;
            if (!CreateSingleDirectory(current)) {
                return false;
            }
        }

        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }

    return true;
}

}  // namespace comm
}  // namespace nook
