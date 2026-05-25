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
    Require(Contains(source, "SwapRuntimeLibDirectory"),
            "ninjector_compat must keep runtime lib/lib64 alias helper for remote module resolution");
    Require(Contains(source, "BuildModulePathCandidates"),
            "ninjector_compat must build module path candidates for remote address resolution");
    Require(Contains(source, "GetModuleBaseFromCandidates"),
            "ninjector_compat must resolve module bases from aliased path candidates");
    Require(Contains(source, "GetRemoteAddr: module alias resolved"),
            "ninjector_compat must log alias-based remote module resolution for zygote debugging");
    return 0;
}
