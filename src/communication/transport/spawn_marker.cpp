#include "spawn_marker.h"

#include "path_utils.h"

#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace nook {
namespace comm {
namespace {

bool FileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

bool FindSinglePendingSpawnMarker(const std::string& base_dir,
                                  std::string* marker_path,
                                  std::string* process_name) {
    std::vector<std::string> candidates;

#if defined(_WIN32)
    const std::string pattern = base_dir + "\\*.marker";
    WIN32_FIND_DATAA find_data{};
    HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            candidates.push_back(find_data.cFileName);
        }
    } while (FindNextFileA(handle, &find_data) != 0);
    FindClose(handle);
#else
    DIR* dir = opendir(base_dir.c_str());
    if (dir == nullptr) {
        return false;
    }
    for (dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() >= 7 && name.substr(name.size() - 7) == ".marker") {
            candidates.push_back(name);
        }
    }
    closedir(dir);
#endif

    if (candidates.size() != 1u) {
        return false;
    }

    const std::string file_name = candidates.front();
    if (marker_path != nullptr) {
        *marker_path = base_dir + "/" + file_name;
    }
    if (process_name != nullptr) {
        *process_name = file_name.substr(0, file_name.size() - 7);
    }
    return true;
}

}  // namespace

std::string GetDefaultSpawnMarkerDirectory() {
#if defined(__ANDROID__)
    const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return std::string(runtime_dir) + "/spawn_markers";
    }
    return "/data/local/tmp/nook/spawn_markers";
#else
    return "build/tmp/spawn_markers";
#endif
}

std::string SanitizeSpawnMarkerName(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

bool LooksLikeEarlySpawnProcessName(const std::string& name) {
    return name == "zygote" || name == "zygote64" ||
           name == "usap32" || name == "usap64" ||
           name == "<pre-initialized>" || name == "pre-initialized";
}

std::string BuildSpawnMarkerPath(const std::string& process_name, const std::string& base_dir) {
    const std::string file_name = SanitizeSpawnMarkerName(process_name);
    if (file_name.empty()) {
        return {};
    }
    return base_dir + "/" + file_name + ".marker";
}

bool HasSpawnMarker(const std::string& process_name, const std::string& base_dir) {
    const std::string path = BuildSpawnMarkerPath(process_name, base_dir);
    return !path.empty() && FileExists(path);
}

bool CreateSpawnMarker(const std::string& process_name, const std::string& base_dir) {
    const std::string path = BuildSpawnMarkerPath(process_name, base_dir);
    if (path.empty()) {
        return false;
    }
    if (!EnsureDirectoryRecursive(GetParentDirectory(path))) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    file << "spawn";
    return file.good();
}

bool ConsumeSpawnMarker(const std::string& process_name, const std::string& base_dir) {
    const std::string path = BuildSpawnMarkerPath(process_name, base_dir);
    if (path.empty() || !FileExists(path)) {
        return false;
    }
    return std::remove(path.c_str()) == 0;
}

bool FindSinglePendingSpawnMarker(std::string* process_name, const std::string& base_dir) {
    return FindSinglePendingSpawnMarker(base_dir, nullptr, process_name);
}

bool ConsumeSinglePendingSpawnMarker(std::string* process_name, const std::string& base_dir) {
    std::string marker_path;
    std::string marker_name;
    if (!FindSinglePendingSpawnMarker(base_dir, &marker_path, &marker_name)) {
        return false;
    }
    if (std::remove(marker_path.c_str()) != 0) {
        return false;
    }
    if (process_name != nullptr) {
        *process_name = marker_name;
    }
    return true;
}

bool ConsumeAnySingleSpawnMarker(const std::string& base_dir) {
    return ConsumeSinglePendingSpawnMarker(nullptr, base_dir);
}

bool RemoveSpawnMarker(const std::string& process_name, const std::string& base_dir) {
    const std::string path = BuildSpawnMarkerPath(process_name, base_dir);
    if (path.empty() || !FileExists(path)) {
        return true;
    }
    return std::remove(path.c_str()) == 0;
}

}  // namespace comm
}  // namespace nook
