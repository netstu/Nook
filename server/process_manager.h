#pragma once

#include <string>
#include <vector>

namespace nook {
namespace server {

struct ProcessInfo {
    int pid = 0;
    std::string name;
};

struct AppInfo {
    std::string package_name;
};

class ProcessManager {
public:
    std::vector<ProcessInfo> EnumerateProcesses() const;
    std::vector<AppInfo> EnumerateApps() const;
};

}  // namespace server
}  // namespace nook
