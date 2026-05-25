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
    const std::string source = ReadFile("host/nook-py/nook/cli.py",
                                        "../../host/nook-py/nook/cli.py");
    Require(!source.empty(), "failed to read host/nook-py/nook/cli.py");

    Require(Contains(source, "nook-cli -U -f com.demo.target -l hook.js --symbi"),
            "cli help must document the explicit --symbi experimental route");
    Require(Contains(source, "\"--symbi\""),
            "cli parser must expose a --symbi flag");
    Require(Contains(source, "dest=\"spawn_symbi\""),
            "cli parser should keep routing the explicit symbi flag into spawn_symbi");
    Require(!Contains(source, "\"--spawn-symbi\""),
            "cli should not keep the older --spawn-symbi public flag");

    return 0;
}
