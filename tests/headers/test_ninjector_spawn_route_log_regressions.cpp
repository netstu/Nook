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
    const std::string header = ReadFile("server/ninjector_spawn_injector.h",
                                        "../../server/ninjector_spawn_injector.h");
    Require(!header.empty(), "failed to read server/ninjector_spawn_injector.h");

    Require(Contains(source, "FormatZygoteControlSpawnDecisionLog("),
            "spawn injector must keep structured zygote-control route log helper");
    Require(!Contains(source, "explicit symbi spawn requested; skip zygote control pkg=%s"),
            "spawn injector must not keep legacy plain-text explicit symbi skip log");
    Require(!Contains(source, "default spawn keeps stable legacy path pkg=%s"),
            "spawn injector must not keep legacy plain-text stable-path skip log");
    Require(!Contains(source, "zygote control fallback to symbi disabled pkg=%s"),
            "spawn injector must not keep legacy plain-text symbi-disabled log");
    Require(Contains(source, "skip-zygote-control-explicit-symbi"),
            "spawn injector should label the explicit symbi route as an explicit zygote-control skip");
    Require(Contains(source, "symbi requested via --symbi"),
            "spawn injector should explain that the symbi route came from the public --symbi flag");
    Require(Contains(source, "skip-stable-default"),
            "spawn injector should label the default stable route when zygote-control is skipped");
    Require(Contains(source, "zygote-control disabled"),
            "spawn injector should explain when the default route stays on the stable backend");
    Require(Contains(header, "enum class SpawnPrimaryRoute"),
            "spawn injector should keep an explicit primary-route model instead of only implicit boolean routing");
    Require(Contains(source, "policy.primary_route = SpawnPrimaryRoute::kStrictZygoteControl;"),
            "strict zygote-control should map to an explicit primary-route mode");
    Require(Contains(source, "policy.primary_route == SpawnPrimaryRoute::kSymbiDefault"),
            "default symbi-preferred routing should branch on the explicit primary-route mode");
    Require(Contains(source, "symbi spawn failed (--symbi): "),
            "explicit symbi failures should reference the public --symbi flag");

    const std::string cli_source = ReadFile("host/nook-py/nook/cli.py",
                                            "../../host/nook-py/nook/cli.py");
    Require(!cli_source.empty(), "failed to read host/nook-py/nook/cli.py");
    Require(Contains(cli_source, "--symbi"),
            "cli must expose the public --symbi experimental spawn flag");
    Require(!Contains(cli_source, "--spawn-symbi"),
            "cli must not keep the older public --spawn-symbi flag");

    return 0;
}
