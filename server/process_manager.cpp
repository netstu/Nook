#include "process_manager.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <fstream>
#include <cstdio>
#endif

namespace nook {
namespace server {

std::vector<ProcessInfo> ProcessManager::EnumerateProcesses() const {
    std::vector<ProcessInfo> processes;

#if !defined(_WIN32)
    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return processes;
    }

    for (dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        int pid = 0;
        try {
            pid = std::stoi(entry->d_name);
        } catch (...) {
            continue;
        }

        std::ifstream cmdline("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
        std::string name;
        std::getline(cmdline, name, '\0');
        if (!name.empty()) {
            processes.push_back(ProcessInfo{pid, name});
        }
    }

    closedir(dir);
#endif

    return processes;
}

std::vector<AppInfo> ProcessManager::EnumerateApps() const {
    std::vector<AppInfo> apps;

#if !defined(_WIN32)
    FILE* pipe = popen("pm list packages 2>/dev/null", "r");
    if (pipe == nullptr) {
        return apps;
    }

    char line[512] = {0};
    while (fgets(line, sizeof(line), pipe) != nullptr) {
        std::string entry = line;
        while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
            entry.pop_back();
        }

        constexpr const char* prefix = "package:";
        if (entry.rfind(prefix, 0) == 0 && entry.size() > 8) {
            apps.push_back(AppInfo{entry.substr(8)});
        }
    }

    pclose(pipe);
#endif

    return apps;
}

}  // namespace server
}  // namespace nook
