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
    const std::string spawn_source = ReadFile("server/ninjector_spawn_injector.cpp",
                                              "../../server/ninjector_spawn_injector.cpp");
    Require(!spawn_source.empty(), "failed to read server/ninjector_spawn_injector.cpp");
    Require(Contains(spawn_source, "kEmbeddedZygoteHelperSentinel"),
            "strict zygote-control must define a dedicated embedded zygote helper sentinel");
    Require(Contains(spawn_source, "return agent_path == kEmbeddedAgentSentinel ||\n           agent_path == kEmbeddedZygoteHelperSentinel;"),
            "runtime dir resolution must treat the zygote helper sentinel as embedded");
    Require(Contains(spawn_source, "inject_arg = kEmbeddedZygoteHelperSentinel;"),
            "strict zygote-control route must switch zygote injection target to the helper sentinel");
    Require(Contains(spawn_source, "const bool strict_helper_local_control = use_embedded_zygote_helper;"),
            "embedded zygote helper availability must drive the stable helper-only zygote-control route");
    Require(Contains(spawn_source, "kEmbeddedZygoteHelperRuntimeDirPrefix"),
            "embedded helper route must support explicit runtime-dir forwarding instead of relying on process-global env only");

    const std::string compat_source = ReadFile("server/ninjector_compat.cpp",
                                               "../../server/ninjector_compat.cpp");
    Require(!compat_source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(Contains(compat_source, "kNookEmbeddedZygoteHelperBlob"),
            "zygote helper embedded blob must be referenced by the injector");
    Require(Contains(compat_source, "\"libnook-zygote-helper\""),
            "strict zygote-control helper injection must use a dedicated memfd name");
    Require(Contains(compat_source, "BuildVersionedEmbeddedName(\"libnook-zygote-helper\""),
            "strict zygote-control helper injection must version the embedded helper memfd name");
    Require(Contains(compat_source, "\"NookAgentInitializeForZygoteControl\""),
            "zygote helper injection must keep the existing zygote-control init symbol");
    Require(Contains(compat_source,
                     "InjectEmbeddedZygoteHelperByPid: existing current zygote helper base"),
            "strict zygote-control helper injection must detect already-loaded current helper images");
    Require(Contains(compat_source, "InjectEmbeddedZygoteHelperByPid: ignore stale zygote helper base"),
            "strict zygote-control helper injection must ignore stale helper mappings from older server builds");
    Require(Contains(compat_source, "\"NookAgentReinitializeForZygoteControl\""),
            "strict zygote-control helper injection must support helper reinitialization");

    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(comm_source, "forced reinit helper-only local monitor ok process=%s"),
            "helper-only forced reinit must keep zygote on local-monitor path");
    return 0;
}
