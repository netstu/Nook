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
    const std::string injector_source = ReadFile("server/ninjector_compat.cpp",
                                                 "../../server/ninjector_compat.cpp");
    Require(!injector_source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(Contains(injector_source,
                     "RemoteSetEnv(pid, \"NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS\", \"0\")"),
            "zygote-control stable init must keep Java wrapper hooks disabled during zygote bootstrap");
    Require(Contains(injector_source,
                     "RemoteSetEnv(pid, \"NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS\", \"0\")"),
            "zygote-control stable init must keep Java native specialize hooks disabled during zygote bootstrap");
    Require(Contains(injector_source,
                     "const bool strict_helper_only ="),
            "zygote-control injector must distinguish strict helper-only init from the stable non-helper path");
    Require(Contains(injector_source,
                     "const char* desired_native_hooks = \"0\";"),
            "strict helper-only zygote-control injector must keep Java native specialize hooks disabled on zygisk devices");
    Require(Contains(injector_source,
                     "const char* desired_wrapper_hooks = \"0\";"),
            "strict helper-only zygote-control injector must keep Java wrapper hooks disabled");
    Require(Contains(injector_source,
                     "RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), \"NOOK_STRICT_ZYGOTE_REQUEST\")"),
            "legacy and embedded zygote spawn prep must proactively clear stale strict-request env before non-strict spawn handoff");
    Require(Contains(injector_source,
                     "RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), \"NOOK_STRICT_ZYGOTE_CONTROL\")"),
            "legacy and embedded zygote spawn prep must proactively clear stale strict-control env before non-strict spawn handoff");

    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(comm_source,
                     "zygote init env before normalize process=%s native=%s wrapper=%s"),
            "zygote-control init should log the zygote Java-hook env before normalization");
    Require(Contains(comm_source,
                     "desired_native_hooks_env"),
            "zygote-control init must derive the desired native Java-hook env from the injected configuration");
    Require(Contains(comm_source,
                     "if (helper_only_local_control) {\n        desired_native_hooks_env = \"0\";\n        desired_wrapper_hooks_env = \"0\";"),
            "strict helper-only zygote-control must force helper env to native=0 wrapper=0 and rely on parent native specialize hooks");
    Require(Contains(comm_source,
                     "desired_wrapper_hooks_env"),
            "zygote-control init must derive the desired wrapper Java-hook env from the injected configuration");
    Require(Contains(comm_source,
                     "setenv(\"NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS\", desired_native_hooks_env, 1)"),
            "zygote-control init must normalize the native Java-hook env to the injected value");
    Require(Contains(comm_source,
                     "setenv(\"NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS\", desired_wrapper_hooks_env, 1)"),
            "zygote-control init must normalize the wrapper Java-hook env to the injected value");
    Require(Contains(comm_source,
                     "zygote init env after normalize process=%s native=%s wrapper=%s"),
            "zygote-control init should log the zygote Java-hook env after normalization");
    return 0;
}
