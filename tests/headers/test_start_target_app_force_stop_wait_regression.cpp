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

    Require(Contains(source, "bool WaitForProcessExitByName(const char* process_name, uint32_t timeout_ms)"),
            "StartTargetApp should provide a dedicated process-exit wait helper after force-stop");
    Require(Contains(source, "bool WaitForProcessStartByName(const char* process_name, uint32_t timeout_ms)"),
            "StartTargetApp should provide a dedicated process-start wait helper after am start");
    Require(Contains(source, "const bool exited = WaitForProcessExitByName(package_name, 2000);"),
            "StartTargetApp should wait for the old target process to exit before am start");
    Require(Contains(source, "am start -S -n "),
            "StartTargetApp should force a fresh start without blocking on ActivityManager completion");
    Require(!Contains(source, "am start -S -W -n "),
            "StartTargetApp must not block on am start -W while spawn gate still holds the child");
    Require(Contains(source, "const bool started = WaitForProcessStartByName(package_name, 10000);"),
            "StartTargetApp should verify that the target process really started after am start");
    Require(Contains(source, "force-stop ret=%d exited=%d start ret=%d started=%d"),
            "StartTargetApp logs should expose whether force-stop drained the old target and whether a fresh process appeared");

    return 0;
}
