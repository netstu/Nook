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
    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");

    Require(Contains(source, "bool InjectEmbeddedAgentByPidSuspendedWithSpawnContext("),
            "embedded symbi path should expose an atomic child-side spawn-context injector");
    Require(Contains(source, "SpawnViaSymbi: zygote gate installed without zygote env prewarm"),
            "symbi external-agent path should log zygote gate without zygote env prewarm");
    Require(Contains(source, "SpawnViaSymbi: child-owned host-side inject begin child_pid=%d so=%s runtime_dir=%s token_set=%d"),
            "symbi external-agent path should deliver spawn context during the same child-owned inject stage");
    Require(Contains(source, "SpawnViaSymbiEmbedded: zygote gate installed without zygote env prewarm"),
            "symbi embedded-agent path should log zygote gate without zygote env prewarm");
    Require(Contains(source, "SpawnViaSymbiEmbedded: child-owned memfd inject begin child_pid=%d runtime_dir=%s token_set=%d"),
            "embedded symbi path should pass spawn context during the atomic child inject phase");
    Require(Contains(source, "const bool spawn_child_init ="),
            "atomic embedded injector should identify spawn-child init separately from zygote-control init");
    Require(Contains(source, "if (spawn_token_env_set && !spawn_child_init)"),
            "spawn-child atomic inject must preserve NOOK_SPAWN_TOKEN for deferred runtime-ready handoff");
    Require(!Contains(source,
                      "bool env_ok = RemoteSetEnv(static_cast<pid_t>(zygote_pid), \"NOOK_RUNTIME_DIR\", runtime_dir);"),
            "symbi zygote stage must not prewarm NOOK_RUNTIME_DIR on zygote");
    Require(!Contains(source,
                      "env_ok = RemoteSetEnv(static_cast<pid_t>(zygote_pid), \"NOOK_SPAWN_TOKEN\", spawn_token);"),
            "symbi zygote stage must not prewarm NOOK_SPAWN_TOKEN on zygote");
    Require(!Contains(source,
                      "PrewarmSpawnChildContext(static_cast<pid_t>(spawn_result.child_pid),"),
            "symbi child-owned handoff must not use a standalone child env prewarm attach before inject");
    Require(!Contains(source, "bool PrewarmSpawnChildContext(pid_t pid,"),
            "obsolete standalone child context prewarm helper should be removed once symbi handoff is fully child-owned");
    Require(Contains(comm_source, "void ClearSpawnTokenAfterRuntimeReadyLocked("),
            "agent runtime path should expose an explicit post-runtime-ready spawn-token cleanup helper");
    Require(Contains(comm_source, "if (stage == nook::comm::AgentReadyStage::kRuntime) {"),
            "agent runtime path should only clear spawn token after runtime-stage AGENT_READY");
    Require(Contains(comm_source, "ClearSpawnTokenAfterRuntimeReadyLocked(ready.spawn_token);"),
            "runtime-stage AGENT_READY send should clear spawn token after the authoritative handoff is complete");
    Require(Contains(comm_source, "bool ShouldPrimeActivatedSpawnChildBootstrap(const std::string& process_name)"),
            "NookComm should expose an explicit policy helper for inherited child bootstrap priming");
    Require(Contains(comm_source, "skip synchronous child bootstrap prime for child-owned spawn process=%s"),
            "child-owned symbi spawn should skip inherited child bootstrap priming before the real embedded agent arrives");
    Require(Contains(comm_source, "NookAgentInitialize defer inherited child connect for early process=%s token=%s"),
            "child-owned symbi spawn should suppress early-process comm connect before the embedded runtime agent is injected");
    Require(Contains(comm_source,
                     "conservative spawn gate arming for spawned child without JNIEnv process=%s"),
            "child-owned spawn should conservatively arm the spawn gate when Java env is not ready yet");
    Require(Contains(comm_source,
                     "const char* spawn_token = std::getenv(\"NOOK_SPAWN_TOKEN\");"),
            "spawn gate probe should distinguish ordinary attach/runtime processes from spawned child-owned processes");

    return 0;
}
