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
    const std::string server_main = ReadFile("server/server_main.cpp",
                                             "../../server/server_main.cpp");
    Require(!server_main.empty(), "failed to read server/server_main.cpp");
    Require(Contains(server_main, "generated/nook_embedded_agent_blob.h"),
            "server_main must include embedded agent blob header");
    Require(Contains(server_main, "generated/nook_embedded_zygote_helper_blob.h"),
            "server_main must include embedded zygote helper blob header");
    Require(Contains(server_main, "generated/nook_embedded_ncore_blob.h"),
            "server_main must include embedded ncore blob header");
    Require(Contains(server_main,
                     "embedded agent blob size=%u source_size=%u sha256=%s source=%s built_utc=%s"),
            "server_main must log embedded agent blob metadata at startup");
    Require(Contains(server_main,
                     "embedded zygote helper blob size=%u source_size=%u sha256=%s source=%s built_utc=%s"),
            "server_main must log embedded zygote helper blob metadata at startup");
    Require(Contains(server_main,
                     "embedded ncore blob size=%u source_size=%u sha256=%s source=%s built_utc=%s"),
            "server_main must log embedded ncore blob metadata at startup");
    return 0;
}
