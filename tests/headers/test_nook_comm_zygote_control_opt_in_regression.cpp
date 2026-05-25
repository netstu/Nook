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
    const std::string source = ReadFile("src/framework/NookComm.cpp",
                                        "../../src/framework/NookComm.cpp");
    Require(!source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(source,
                     "return value != nullptr && std::strcmp(value, \"1\") == 0;"),
            "agent-side zygote-control opt-in must require explicit env value 1");
    Require(Contains(source,
                     "if (IsExperimentalZygoteControlEnabled()) {"),
            "zygote auto-initialize path must remain gated by the experimental switch");
    return 0;
}
