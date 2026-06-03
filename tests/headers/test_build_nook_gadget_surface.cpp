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
    const std::string source = ReadFile("tools/build_nook_gadget.ps1");
    Require(!source.empty(), "failed to read tools/build_nook_gadget.ps1");
    Require(Contains(source, "\"nook_gadget\""),
            "build_nook_gadget.ps1 must build the nook_gadget module");
    Require(Contains(source, "nook_gadget_smoke"),
            "build_nook_gadget.ps1 must optionally support building the smoke module");
    Require(Contains(source, "build/android/Android.mk"),
            "build_nook_gadget.ps1 must use the Android.mk build entrypoint");
    Require(Contains(source, "build/android/Application_static.mk"),
            "build_nook_gadget.ps1 must use the static application mk");
    Require(Contains(source, "libnook-gadget.so"),
            "build_nook_gadget.ps1 must validate the primary gadget artifact");
    Require(Contains(source, "[build-nook-gadget] ok"),
            "build_nook_gadget.ps1 must emit a stable success marker");

    const std::string patch_smoke = ReadFile("tools/nook_gadget_patch_smoke.ps1");
    Require(Contains(patch_smoke, "build_nook_gadget.ps1"),
            "nook_gadget_patch_smoke.ps1 must point missing-artifact users at build_nook_gadget.ps1");
    return 0;
}
