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
    const std::string ninjector_source = ReadFile("server/ninjector_compat.cpp",
                                                  "../../server/ninjector_compat.cpp");
    const std::string symbi_source = ReadFile("server/symbi_injector_local.cpp",
                                              "../../server/symbi_injector_local.cpp");
    Require(!ninjector_source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(!symbi_source.empty(), "failed to read server/symbi_injector_local.cpp");

    Require(Contains(ninjector_source, "am start -n "),
            "start target app helper must launch an explicit component with am start -n");
    Require(!Contains(ninjector_source, "tail -n 1"),
            "start target app helper must not depend on tail-based shell parsing");
    Require(!Contains(ninjector_source, "am start $(cmd package resolve-activity --brief '"),
            "start target app helper must not depend on shell command substitution for resolve-activity");

    Require(Contains(symbi_source, "am start -n "),
            "symbi start target app helper must launch an explicit component with am start -n");
    Require(!Contains(symbi_source, "tail -n 1"),
            "symbi start target app helper must not depend on tail-based shell parsing");
    Require(!Contains(symbi_source, "am start $(cmd package resolve-activity --brief '"),
            "symbi start target app helper must not depend on shell command substitution for resolve-activity");
    return 0;
}
