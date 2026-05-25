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
    const std::string build_mk = ReadFile("build/android/Android.mk",
                                          "../../build/android/Android.mk");
    Require(!build_mk.empty(), "failed to read build/android/Android.mk");
    Require(Contains(build_mk, "NOOK_ZYGOTE_HELPER_SRC := \\"),
            "Android.mk must define a dedicated zygote helper source set");
    Require(Contains(build_mk, "LOCAL_MODULE := nook_zygote_helper"),
            "Android.mk must expose a dedicated nook_zygote_helper module");
    Require(Contains(build_mk, "LOCAL_MODULE_FILENAME := libnook-zygote-helper"),
            "zygote helper module must install to a dedicated library filename");
    Require(Contains(build_mk, "LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS) -DNOOK_ZYGOTE_HELPER_ONLY=1"),
            "zygote helper module must build with NOOK_ZYGOTE_HELPER_ONLY");

    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(comm_source, "#if !defined(NOOK_ZYGOTE_HELPER_ONLY)\n#include \"../agent_runtime/nook_script_runtime_bridge.h\"\n#endif"),
            "NookComm must make the runtime bridge include optional for zygote-helper-only builds");
    Require(Contains(comm_source, "#if defined(NOOK_ZYGOTE_HELPER_ONLY)\n    NOOK_COMM_LOGI(\"runtime bridge unavailable in zygote-helper-only build process=%s\""),
            "NookComm must log a helper-only runtime bridge short-circuit");
    Require(Contains(comm_source, "return NOOK_STATUS_NOT_IMPLEMENTED;"),
            "helper-only runtime bridge path must explicitly report not implemented");
    Require(Contains(comm_source, "NookAgentInitialize helper child control-ready ok process=%s"),
            "helper-only child path must notify server with control-ready before full-agent promotion");
    Require(Contains(comm_source, "bool ShouldSkipBootstrapHooksForHelperOnlyChild() {"),
            "NookComm must expose an explicit helper-only bootstrap-hook skip policy");
    Require(Contains(comm_source, "const std::string process_name = ReadProcessName();"),
            "helper-only bootstrap-hook skip policy must inspect the current process identity");
    Require(Contains(comm_source,
                     "LooksLikeEarlySpawnProcessNameLocal(process_name) &&\n           !IsPromotedStrictZygoteControlSpawnChild(process_name);"),
            "helper-only bootstrap-hook skip policy must still allow promoted strict children to install spawn-gate bootstrap hooks");
    Require(Contains(comm_source, "SpawnGateNewApplicationHookCallback helper-only wait process=%s"),
            "helper-only child path must still hold the spawn gate at Instrumentation.newApplication");
    Require(Contains(comm_source, "SpawnGateCallApplicationOnCreateHookCallback helper-only wait process=%s"),
            "helper-only child path must still hold the spawn gate at callApplicationOnCreate");
    return 0;
}
