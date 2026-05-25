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

    Require(Contains(server_main, "embedded agent selected path="),
            "server_main must keep embedded agent selection in the default path");
    Require(!Contains(server_main, "sidecar ncore selected path="),
            "server_main must not auto-prefer sidecar ncore when embedded ncore is available");
    Require(!Contains(server_main, "sidecar agent selected path="),
            "server_main must not auto-prefer sidecar agent when embedded agent is available");
    Require(Contains(server_main, "explicit ncore path selected path="),
            "server_main must still support explicit ncore override");
    Require(Contains(server_main, "explicit agent path selected path="),
            "server_main must still support explicit agent override");

    return 0;
}
