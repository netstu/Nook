#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
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
    const std::string source = ReadFile("tools/ensure_nook_debug_keystore.ps1");
    Require(!source.empty(), "failed to read tools/ensure_nook_debug_keystore.ps1");
    Require(Contains(source, "keytool"),
            "ensure_nook_debug_keystore.ps1 must use keytool");
    Require(Contains(source, "androiddebugkey"),
            "ensure_nook_debug_keystore.ps1 must default to androiddebugkey");
    Require(Contains(source, "build\\keystore\\nook-debug.keystore"),
            "ensure_nook_debug_keystore.ps1 must default to the repo debug keystore path");
    Require(Contains(source, "[ensure-nook-debug-keystore] created="),
            "ensure_nook_debug_keystore.ps1 must emit a stable creation marker");
    return 0;
}
