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
    const std::string header = ReadFile("server/ninjector_spawn_injector.h",
                                        "../../server/ninjector_spawn_injector.h");
    const std::string source = ReadFile("server/ninjector_spawn_injector.cpp",
                                        "../../server/ninjector_spawn_injector.cpp");
    Require(!header.empty(), "failed to read server/ninjector_spawn_injector.h");
    Require(!source.empty(), "failed to read server/ninjector_spawn_injector.cpp");

    Require(Contains(header, "enum class SpawnTerminalBackend"),
            "spawn injector should model terminal backend tags as an explicit enum");
    Require(Contains(header, "enum class SpawnRouteAttempt"),
            "spawn injector should model route attempts as explicit enum state");
    Require(Contains(header, "enum class SpawnFallbackPolicy"),
            "spawn injector should model fallback policy as explicit enum state");
    Require(Contains(header, "enum class SpawnFinalStatus"),
            "spawn injector should model final spawn status as explicit enum state");
    Require(Contains(header, "SpawnTerminalBackend terminal_primary_backend"),
            "spawn outcome should store primary backend as enum state");
    Require(Contains(header, "SpawnTerminalBackend terminal_secondary_backend"),
            "spawn outcome should store secondary backend as enum state");
    Require(Contains(header, "SpawnRouteAttempt route_attempt"),
            "spawn outcome should record the chosen route as enum state");
    Require(Contains(header, "SpawnFallbackPolicy fallback_policy"),
            "spawn outcome should record fallback policy as enum state");
    Require(Contains(header, "SpawnFinalStatus final_status"),
            "spawn outcome should record final status as enum state");
    Require(Contains(source, "SpawnTerminalBackendToString("),
            "spawn injector should map terminal backend enum state to structured log strings");
    Require(Contains(source, "FinalizeSpawnOutcome("),
            "spawn injector should centralize terminal spawn outcome handling");
    Require(Contains(source, "ClassifyTerminalSpawnOutcome("),
            "spawn injector should centralize terminal outcome classification");
    Require(Contains(source, "ShouldAllowZygoteControlFallback("),
            "spawn injector should centralize zygote-control fallback classification");
    Require(Contains(source, "CommitSuccessfulSpawnOutcome("),
            "spawn injector should centralize successful spawn outcome commit");
    Require(Contains(source, "switch (outcome.final_status)"),
            "spawn injector terminal handling should pivot on explicit outcome final status");

    return 0;
}
