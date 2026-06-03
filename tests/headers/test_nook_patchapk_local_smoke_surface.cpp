#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
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
    const std::string source = ReadFile("tools/nook_patchapk_local_smoke.py");
    Require(!source.empty(), "failed to read tools/nook_patchapk_local_smoke.py");
    Require(Contains(source, "tools/nook_patchapk.py"),
            "local patchapk smoke runner must invoke tools/nook_patchapk.py");
    Require(Contains(source, "libs/arm64-v8a/libnook-gadget.so"),
            "local patchapk smoke runner must validate the gadget library artifact");
    Require(Contains(source, "--bootstrap-mode"),
            "local patchapk smoke runner must surface bootstrap-mode selection");
    Require(Contains(source, "proxy-loader"),
            "local patchapk smoke runner must validate the proxy-loader bootstrap path");
    Require(Contains(source, "assets/nook-gadget/config.json"),
            "local patchapk smoke runner must validate config asset emission");
    Require(Contains(source, "assets/nook-gadget/startup.js"),
            "local patchapk smoke runner must validate startup asset emission");
    Require(Contains(source, "lib/arm64-v8a/libnook-gadget.so"),
            "local patchapk smoke runner must validate synthetic native-lib placement");
    Require(Contains(source, "System;->loadLibrary"),
            "local patchapk smoke runner must validate loadLibrary injection");
    Require(Contains(source, "NookProxyApplication"),
            "local patchapk smoke runner must validate proxy application generation");
    Require(Contains(source, "nook-patchapk-local-smoke"),
            "local patchapk smoke runner must emit a stable success/failure prefix");
    return 0;
}
