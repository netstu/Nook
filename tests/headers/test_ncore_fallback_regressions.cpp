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
    const std::string source = ReadFile("server/ncore_fallback.cpp",
                                        "../../server/ncore_fallback.cpp");
    Require(!source.empty(), "failed to read server/ncore_fallback.cpp");
    Require(Contains(source, "extern \"C\" void ainject("),
            "ncore fallback must export ainject");
    Require(Contains(source, "extern \"C\" void aclear("),
            "ncore fallback must export aclear");
    Require(Contains(source, "ResolveSymbolAddress("),
            "ncore fallback must resolve symbols through Nook");
    Require(Contains(source, "InstallInlineHook("),
            "ncore fallback must install hooks through Nook");
    Require(!Contains(source, "Dobby"),
            "ncore fallback must not depend on Dobby");
    Require(Contains(source, "dlsym(handle, \"NookAgentInitializeForSpawnChild\")"),
            "ncore fallback must resolve the spawn-child entrypoint instead of the generic init");
    Require(Contains(source, "calling NookAgentInitializeForSpawnChild symbol=%p so=%s"),
            "ncore fallback logs must reflect the spawn-child entrypoint");
    Require(Contains(source, "NookAgentInitializeForSpawnChild returned status=%d so=%s"),
            "ncore fallback must report the spawn-child init result");

    const std::string android_mk = ReadFile("build/android/Android.mk",
                                            "../../build/android/Android.mk");
    Require(!android_mk.empty(), "failed to read build/android/Android.mk");
    Require(Contains(android_mk, "LOCAL_MODULE := nook_ncore"),
            "Android.mk must define nook_ncore module");
    Require(Contains(android_mk, "LOCAL_MODULE_FILENAME := libncore"),
            "Android.mk must emit libncore.so");
    Require(Contains(android_mk, "../../server/embedded_agent_blob_defs.cpp"),
            "Android.mk must give nook_ncore an agent-only embedded blob definition translation unit");
    Require(!Contains(android_mk, "NOOK_NCORE_FALLBACK_SRC := \\\n    ../../server/embedded_blob_defs.cpp"),
            "nook_ncore must not compile the full embedded blob definition unit or libncore will recursively embed libncore itself");

    const std::string server_main = ReadFile("server/server_main.cpp",
                                             "../../server/server_main.cpp");
    Require(!server_main.empty(), "failed to read server/server_main.cpp");
    Require(Contains(server_main, "generated/nook_embedded_ncore_blob.h"),
            "server_main must include embedded ncore blob header");
    Require(Contains(server_main, "NOOK_NCORE_PATH"),
            "server_main must manage NOOK_NCORE_PATH");
    Require(Contains(server_main, "embedded ncore blob size=%u source_size=%u sha256=%s source=%s built_utc=%s"),
            "server_main must log embedded ncore blob metadata");

    const std::string ncore_blob_script = ReadFile("tools/build_embedded_ncore_blob.ps1",
                                                   "../../tools/build_embedded_ncore_blob.ps1");
    Require(!ncore_blob_script.empty(), "failed to read tools/build_embedded_ncore_blob.ps1");
    Require(Contains(ncore_blob_script, "NOOK_EMBEDDED_NCORE_SOURCE"),
            "ncore blob script must support NOOK_EMBEDDED_NCORE_SOURCE override");
    Require(Contains(ncore_blob_script, "server\\\\generated\\\\nook_embedded_ncore_blob.h"),
            "ncore blob script must target generated ncore header");
    Require(Contains(ncore_blob_script, "build\\\\single-server-staging\\\\arm64-v8a\\\\libncore.so"),
            "ncore blob script must consider staged deployable single-server ncore artifacts");
    Require(Contains(ncore_blob_script, "build\\\\single-server-package\\\\arm64-v8a\\\\libncore.so"),
            "ncore blob script must consider packaged single-server ncore artifacts");
    Require(Contains(ncore_blob_script, "Split-Path -Leaf $sourcePath"),
            "ncore blob script must sanitize source metadata to the stable leaf name");
    Require(Contains(ncore_blob_script, "kNookEmbeddedNcoreSourcePath"),
            "ncore blob script must record the embedded ncore source path metadata");
    Require(Contains(ncore_blob_script, "kNookEmbeddedNcoreSourceSha256"),
            "ncore blob script must record the embedded ncore source hash metadata");
    Require(Contains(ncore_blob_script, "kNookEmbeddedNcoreSourceLastWriteUtc = \"\";"),
            "ncore blob script must clear timestamp metadata for reproducible builds");
    Require(Contains(ncore_blob_script, "priorityPrefixes"),
            "ncore blob script must prefer deployable/staged artifacts over raw obj/local outputs");

    const std::string agent_blob_defs = ReadFile("server/embedded_agent_blob_defs.cpp",
                                                 "../../server/embedded_agent_blob_defs.cpp");
    Require(!agent_blob_defs.empty(), "failed to read server/embedded_agent_blob_defs.cpp");
    Require(Contains(agent_blob_defs, "#define NOOK_DEFINE_EMBEDDED_AGENT_BLOB 1"),
            "agent-only blob defs must define the embedded agent blob exactly once for libncore");
    Require(!Contains(agent_blob_defs, "NOOK_DEFINE_EMBEDDED_NCORE_BLOB"),
            "agent-only blob defs must not define the embedded ncore blob");

    return 0;
}
