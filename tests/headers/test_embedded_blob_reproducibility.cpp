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

void RequireDeterministicBlobScript(const char* script_path,
                                    const char* fallback_path,
                                    const char* source_path_symbol,
                                    const char* source_time_symbol) {
    const std::string script = ReadFile(script_path, fallback_path);
    Require(!script.empty(), "failed to read embedded blob generator script");
    Require(Contains(script, "Split-Path -Leaf $sourcePath"),
            "embedded blob generator must sanitize source path to a stable leaf name");
    Require(Contains(script, source_path_symbol),
            "embedded blob generator must still emit source-name metadata");
    Require(Contains(script, "SourceSha256"),
            "embedded blob generator must keep stable content-hash metadata");
    Require(Contains(script, source_time_symbol),
            "embedded blob generator must keep the source timestamp field for compatibility");
    const std::string cleared_timestamp_line =
        std::string("static constexpr const char* ") + source_time_symbol + " = \"\";";
    Require(Contains(script, cleared_timestamp_line),
            "embedded blob generator must clear timestamp metadata for reproducible builds");
}

}  // namespace

int main() {
    RequireDeterministicBlobScript("tools/build_embedded_agent_blob.ps1",
                                   "../../tools/build_embedded_agent_blob.ps1",
                                   "kNookEmbeddedAgentSourcePath",
                                   "kNookEmbeddedAgentSourceLastWriteUtc");
    RequireDeterministicBlobScript("tools/build_embedded_ncore_blob.ps1",
                                   "../../tools/build_embedded_ncore_blob.ps1",
                                   "kNookEmbeddedNcoreSourcePath",
                                   "kNookEmbeddedNcoreSourceLastWriteUtc");
    RequireDeterministicBlobScript("tools/build_embedded_zygote_helper_blob.ps1",
                                   "../../tools/build_embedded_zygote_helper_blob.ps1",
                                   "kNookEmbeddedZygoteHelperSourcePath",
                                   "kNookEmbeddedZygoteHelperSourceLastWriteUtc");

    const std::string server_main = ReadFile("server/server_main.cpp",
                                             "../../server/server_main.cpp");
    Require(!server_main.empty(), "failed to read server/server_main.cpp");
    Require(Contains(server_main, "kNookEmbeddedAgentSourceSha256"),
            "server must keep logging content-hash metadata for embedded agent blobs");
    Require(Contains(server_main, "kNookEmbeddedNcoreSourceSha256"),
            "server must keep logging content-hash metadata for embedded ncore blobs");
    Require(Contains(server_main, "kNookEmbeddedZygoteHelperSourceSha256"),
            "server must keep logging content-hash metadata for embedded zygote helper blobs");

    return 0;
}
