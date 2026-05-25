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
    const std::string source = ReadFile("server/ninjector_spawn_injector.cpp",
                                        "../../server/ninjector_spawn_injector.cpp");
    Require(!source.empty(), "failed to read server/ninjector_spawn_injector.cpp");

    Require(Contains(source, "FormatZygoteControlTerminalOutcomeLog("),
            "spawn injector must keep structured terminal outcome log helper");
    Require(Contains(source, "FormatZygoteControlTerminalOutcomeLog(\"spawn-result\""),
            "spawn injector must emit structured spawn-result logs");
    Require(Contains(source, "FormatZygoteControlTerminalOutcomeLog(\"finalize-result\""),
            "spawn injector must emit structured finalize-result logs");

    return 0;
}
