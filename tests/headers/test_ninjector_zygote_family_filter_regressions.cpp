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
    Require(Contains(source, "process.name == \"zygote\" || process.name == \"zygote64\" ||"),
            "spawn injector must enumerate the full recognized zygote family");
    Require(Contains(source, "process.name == \"usap32\" || process.name == \"usap64\""),
            "spawn injector must include usap processes in the recognized zygote family");
    Require(!Contains(source, "IsCompatibleZygoteFamily(config_.spawn_source_process, target.second)"),
            "spawn injector must not drop recognized zygote-family targets by source-family compatibility");
    Require(!Contains(source, "zygote monitor skip incompatible target"),
            "spawn injector must not skip recognized zygote-family targets as incompatible");
    Require(Contains(source, "ShouldSkipUnsupportedZygoteTargetArch("),
            "spawn injector must explicitly gate unsupported zygote target architectures");
    Require(Contains(source, "zygote monitor skip unsupported target pid=%d process=%s reason=%s"),
            "spawn injector must log when a recognized zygote target is skipped for unsupported architecture");
    Require(Contains(source, "32-bit zygote unsupported by current arm64-only build"),
            "spawn injector must explain why 32-bit zygote targets are skipped on arm64-only builds");
    return 0;
}
