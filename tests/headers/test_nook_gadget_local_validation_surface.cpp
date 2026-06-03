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
    const std::string source = ReadFile("tools/nook_gadget_local_validation.ps1");
    Require(!source.empty(), "failed to read tools/nook_gadget_local_validation.ps1");
    Require(Contains(source, "nook_patchapk.py"),
            "local validation wrapper must include patch tool preflight");
    Require(Contains(source, "nook_patchapk_local_smoke.py --bootstrap-mode minimal"),
            "local validation wrapper must run the minimal patch smoke");
    Require(Contains(source, "nook_patchapk_local_smoke.py --bootstrap-mode proxy-loader"),
            "local validation wrapper must run the proxy-loader patch smoke");
    Require(Contains(source, "build_nook_gadget.ps1"),
            "local validation wrapper must know how to build the gadget artifact when missing");
    Require(Contains(source, "test_nook_gadget_runtime_init"),
            "local validation wrapper must include gadget runtime init regression coverage");
    Require(Contains(source, "test_nook_gadget_startup_rpc"),
            "local validation wrapper must include startup rpc regression coverage");
    Require(Contains(source, "[nook-gadget-local-validation] ok"),
            "local validation wrapper must emit a stable success marker");
    return 0;
}
