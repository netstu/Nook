#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFile(const char* primary, const char* fallback = nullptr) {
    std::ifstream input(primary, std::ios::binary);
    if (!input && fallback != nullptr) {
        input.open(fallback, std::ios::binary);
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::string source = ReadFile("server/ninjector_compat.cpp",
                                        "../../server/ninjector_compat.cpp");
    Require(!source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(Contains(source, "bool IsActualZygoteFamilyProcess(pid_t pid) {"),
            "ninjector compat must expose a dedicated actual-zygote filter helper");
    Require(Contains(source, "bool ReadProcessRealUid(pid_t pid, uid_t* uid) {"),
            "actual zygote filtering must inspect the tracee real uid");
    Require(Contains(source, "bool ReadProcessParentPid(pid_t pid, pid_t* parent_pid) {"),
            "actual zygote filtering must inspect the tracee parent pid");
    Require(Contains(source, "parent_pid == static_cast<pid_t>(1)"),
            "zygote64/zygote must only use legacy attach when they are direct init children");
    Require(Contains(source, "const std::string parent_name = ReadProcessCmdlineBasename(parent_pid);"),
            "usap processes must validate their authoritative zygote parent before forcing legacy attach");
    Require(Contains(source, "AttachProcess: forcing legacy attach for actual zygote-family pid=%d"),
            "attach logging must distinguish true zygote/usap targets from promoted spawn children");
    Require(!Contains(source, "AttachProcess: forcing legacy attach for zygote-family pid=%d"),
            "attach logging must not imply that every zygote-named process is a real zygote target");
    return 0;
}
