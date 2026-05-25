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
    const std::string header = ReadFile("server/ninjector_spawn_injector.h",
                                        "../../server/ninjector_spawn_injector.h");
    Require(!source.empty(), "failed to read server/ninjector_spawn_injector.cpp");
    Require(!header.empty(), "failed to read server/ninjector_spawn_injector.h");

    Require(Contains(header, "enum class ZygoteControlFailureState"),
            "spawn injector must define explicit zygote-control lifecycle states");
    Require(Contains(header, "kInjectAgent"),
            "spawn injector lifecycle state model must include inject stage");
    Require(Contains(header, "kInstallHook"),
            "spawn injector lifecycle state model must include install stage");
    Require(Contains(header, "kLaunchApp"),
            "spawn injector lifecycle state model must include launch stage");
    Require(Contains(header, "kTargetsArmed"),
            "spawn injector lifecycle state model must include armed-targets stage");
    Require(Contains(header, "kFinalizeClear"),
            "spawn injector lifecycle state model must include finalize clear stage");
    Require(Contains(source, "InferZygoteControlLifecycleStateFromError("),
            "spawn injector must infer zygote-control lifecycle state from error details");
    Require(Contains(source, "ShouldAllowFallbackForZygoteControlState("),
            "spawn injector must provide explicit fallback policy by lifecycle state");
    Require(Contains(source, "ShouldProbeZygoteControlFinalizeFallback("),
            "spawn injector must provide explicit finalize ownership probe policy");
    Require(!Contains(source, "ShouldProbeZygoteControlFinalizeFallback(bool enable_zygote_control,\n                                              bool has_residual_zygote_control_targets)"),
            "finalize ownership probe policy must move beyond the old two-bool signature");
    Require(Contains(source, "FormatZygoteControlLifecycleStageLog("),
            "spawn injector must provide structured lifecycle stage log formatting");
    Require(Contains(header, "RecordZygoteControlLifecycleStage("),
            "spawn injector must expose a private current lifecycle stage recorder");
    Require(Contains(header, "ReadZygoteControlLifecycleStage() const"),
            "spawn injector must expose a private current lifecycle stage reader");
    Require(Contains(header, "ClearZygoteControlLifecycleStage()"),
            "spawn injector must expose a private current lifecycle stage clearer");
    Require(Contains(header, "current_zygote_control_lifecycle_stage_"),
            "spawn injector must store current zygote-control lifecycle stage");
    Require(Contains(source, "RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kTargetsArmed);"),
            "zygote-control success path must record the targets-armed lifecycle stage");
    Require(Contains(source, "NOOK_SPAWN_LOGI(\"%s\",\n                        FormatZygoteControlLifecycleStageLog("),
            "spawn injector must emit structured lifecycle stage logs");
    Require(Contains(source, "FormatZygoteControlLifecycleStageLog(\"spawn-lifecycle\"") &&
            Contains(source, "\"fail\""),
            "spawn injector must emit structured spawn lifecycle failure logs");
    Require(Contains(source, "FormatZygoteControlLifecycleStageLog(\"finalize-lifecycle\"") &&
            Contains(source, "\"fail\""),
            "spawn injector must emit structured finalize lifecycle failure logs");
    Require(Contains(source,
                     "ShouldAllowFallbackForZygoteControlState(\n        InferZygoteControlLifecycleStateFromError(error),"),
            "spawn injector fallback policy must still support error-to-state compatibility routing");
    Require(Contains(source, "ClassifyZygoteControlFailureClass("),
            "spawn injector must continue routing failure class through a single classification entry");

    return 0;
}
