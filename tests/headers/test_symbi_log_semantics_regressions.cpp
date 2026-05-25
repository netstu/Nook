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
    const std::string handlers = ReadFile("server/server_handlers.cpp",
                                          "../../server/server_handlers.cpp");
    Require(!handlers.empty(), "failed to read server/server_handlers.cpp");

    Require(Contains(handlers, "control-stage AGENT_READY"),
            "server_handlers must explicitly label control-stage AGENT_READY");
    Require(Contains(handlers, "runtime-stage AGENT_READY"),
            "server_handlers must explicitly label runtime-stage AGENT_READY");

    const std::string server_main = ReadFile("server/server_main.cpp",
                                             "../../server/server_main.cpp");
    Require(!server_main.empty(), "failed to read server/server_main.cpp");

    Require(Contains(server_main, "agent socket connected session="),
            "server_main must label raw agent socket connections distinctly from AGENT_READY");

    return 0;
}
