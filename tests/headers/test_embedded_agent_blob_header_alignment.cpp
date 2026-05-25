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
    const std::string android_mk = ReadFile("build/android/Android.mk",
                                            "../../build/android/Android.mk");
    const std::string blob_defs = ReadFile("server/embedded_blob_defs.cpp",
                                           "../../server/embedded_blob_defs.cpp");
    const std::string server_main = ReadFile("server/server_main.cpp",
                                             "../../server/server_main.cpp");
    const std::string injector_compat = ReadFile("server/ninjector_compat.cpp",
                                                 "../../server/ninjector_compat.cpp");
    const std::string ncore_fallback = ReadFile("server/ncore_fallback.cpp",
                                                "../../server/ncore_fallback.cpp");
    const std::string spawn_header = ReadFile("server/ninjector_spawn_injector.h",
                                              "../../server/ninjector_spawn_injector.h");
    const std::string blob_script = ReadFile("tools/build_embedded_agent_blob.ps1",
                                             "../../tools/build_embedded_agent_blob.ps1");

    Require(!android_mk.empty(), "failed to read build/android/Android.mk");
    Require(!blob_defs.empty(), "failed to read server/embedded_blob_defs.cpp");
    Require(!server_main.empty(), "failed to read server/server_main.cpp");
    Require(!injector_compat.empty(), "failed to read server/ninjector_compat.cpp");
    Require(!ncore_fallback.empty(), "failed to read server/ncore_fallback.cpp");
    Require(!spawn_header.empty(), "failed to read server/ninjector_spawn_injector.h");
    Require(!blob_script.empty(), "failed to read tools/build_embedded_agent_blob.ps1");

    Require(Contains(blob_script, "server\\\\generated\\\\nook_embedded_agent_blob.h"),
            "embedded agent blob script must target nook_embedded_agent_blob.h");
    Require(Contains(blob_script, "#if defined(NOOK_DEFINE_EMBEDDED_AGENT_BLOB)"),
            "embedded agent blob script must support single-definition mode");
    Require(Contains(blob_script, "extern const unsigned char kNookEmbeddedAgentBlob[];"),
            "embedded agent blob script must emit extern declaration for non-defining TUs");
    Require(Contains(blob_defs, "#define NOOK_DEFINE_EMBEDDED_AGENT_BLOB 1"),
            "embedded blob defs must define the embedded agent blob exactly once per module");
    Require(Contains(blob_defs, "#define NOOK_DEFINE_EMBEDDED_NCORE_BLOB 1"),
            "embedded blob defs must define the embedded ncore blob exactly once per module");
    Require(Contains(blob_defs, "\"generated/nook_embedded_agent_blob.h\""),
            "embedded blob defs must include the embedded agent blob header");
    Require(Contains(blob_defs, "\"generated/nook_embedded_ncore_blob.h\""),
            "embedded blob defs must include the embedded ncore blob header");
    Require(Contains(android_mk, "../../server/embedded_blob_defs.cpp"),
            "Android.mk must compile the embedded blob definition translation unit");
    Require(Contains(server_main, "generated/nook_embedded_agent_blob.h"),
            "server_main must include the generated embedded agent blob header");
    Require(Contains(injector_compat, "generated/nook_embedded_agent_blob.h"),
            "ninjector_compat must include the generated embedded agent blob header");
    Require(Contains(ncore_fallback, "generated/nook_embedded_agent_blob.h"),
            "ncore_fallback must include the generated embedded agent blob header");
    Require(Contains(spawn_header, "generated/nook_embedded_agent_blob.h"),
            "ninjector_spawn_injector header must include the generated embedded agent blob header");

    Require(!Contains(server_main, "nook_embedded_agent_blob_current.h"),
            "server_main must not include stale embedded agent blob current header");
    Require(!Contains(injector_compat, "nook_embedded_agent_blob_current.h"),
            "ninjector_compat must not include stale embedded agent blob current header");
    Require(!Contains(ncore_fallback, "nook_embedded_agent_blob_current.h"),
            "ncore_fallback must not include stale embedded agent blob current header");
    Require(!Contains(spawn_header, "nook_embedded_agent_blob_current.h"),
            "ninjector_spawn_injector header must not include stale embedded agent blob current header");
    return 0;
}
