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
    const std::string source = ReadFile("src/framework/nook_zygote_control.cpp",
                                        "../../src/framework/nook_zygote_control.cpp");
    Require(!source.empty(), "failed to read src/framework/nook_zygote_control.cpp");

    Require(Contains(source,
                     "nativeForkAndSpecialize child forked current=%s; defer activation to safer hooks"),
            "nativeForkAndSpecialize callback must explicitly defer child activation away from raw jstring access");
    Require(Contains(source,
                     "nativeSpecializeAppProcess post-original current=%s; defer activation to safer hooks"),
            "nativeSpecializeAppProcess callback must explicitly defer child activation away from raw jstring access");
    Require(Contains(source,
                     "forkAndSpecialize child forked current=%s; defer activation to safer hooks"),
            "forkAndSpecialize wrapper callback must explicitly defer child activation away from raw jstring access");
    Require(Contains(source,
                     "specializeAppProcess post-original current=%s; defer activation to safer hooks"),
            "specializeAppProcess wrapper callback must explicitly defer child activation away from raw jstring access");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"nativeForkAndSpecialize\""),
            "nativeForkAndSpecialize callback must retain the guarded nice-name fallback activation path");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"forkAndSpecialize\""),
            "forkAndSpecialize callback must retain the guarded nice-name fallback activation path");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"nativeSpecializeAppProcess\""),
            "nativeSpecializeAppProcess callback must retain the guarded nice-name fallback activation path");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"specializeAppProcess\""),
            "specializeAppProcess callback must retain the guarded nice-name fallback activation path");
    return 0;
}
