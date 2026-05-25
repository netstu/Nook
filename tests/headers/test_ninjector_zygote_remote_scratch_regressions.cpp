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

    Require(Contains(source, "bool UseRemoteMmapScratch(pid_t pid)"),
            "ninjector_compat must provide a dedicated remote scratch policy helper");
    Require(Contains(source, "RemoteAllocScratch("),
            "ninjector_compat must centralize remote scratch allocation");
    Require(Contains(source, "RemoteFreeScratch("),
            "ninjector_compat must centralize remote scratch cleanup");
    Require(Contains(source, "UseRemoteMmapScratch(pid) ? \"mmap\" : \"malloc\""),
            "ninjector_compat should log whether zygote-family remote scratch uses mmap or malloc");
    Require(Contains(source, "reinterpret_cast<void*>(mmap)"),
            "ninjector_compat remote scratch must support mmap-backed allocation");
    Require(Contains(source, "reinterpret_cast<void*>(munmap)"),
            "ninjector_compat remote scratch must support mmap-backed cleanup");
    Require(Contains(source, "UseRemoteMmapScratch(pid)"),
            "ninjector_compat must gate zygote-family scratch allocation through the dedicated policy");
    Require(Contains(source, "bool HostWriteFullyToRemoteProcFd(pid_t pid, int remote_fd, const uint8_t* data, size_t size)"),
            "ninjector_compat should expose a host-side /proc/<pid>/fd memfd write helper");
    Require(Contains(source, "HostWriteFullyToRemoteProcFd(pid, fd, data, size)"),
            "RemoteWriteFullyToFd should prefer host-side /proc/<pid>/fd writes before ptrace fallback");
    Require(Contains(source, "host /proc write fallback to ptrace"),
            "ninjector_compat should keep an explicit log when host-side memfd write falls back to ptrace");
    Require(Contains(source, "BuildVersionedEmbeddedName(\"libnook-zygote-helper\""),
            "embedded zygote-helper injection must use a versioned memfd name so zygote does not keep reusing a stale helper image across server rebuilds");
    Require(Contains(source, "InjectEmbeddedZygoteHelperByPid: ignore stale zygote helper base"),
            "embedded zygote-helper injection must log when it detects and bypasses an older helper already loaded in zygote");

    return 0;
}
