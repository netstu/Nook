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

    Require(Contains(source, "FormatZygoteControlFinalizeDecisionLog("),
            "spawn injector must keep structured zygote-control finalize route log helper");
    Require(Contains(source, "stage=finalize-route"),
            "spawn injector must emit finalize-route structured logs");

    return 0;
}
