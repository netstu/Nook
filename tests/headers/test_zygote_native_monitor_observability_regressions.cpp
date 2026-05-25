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
                     "native zygote monitor installed fork=%d vfork=%d setArgV0=%d selinux=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d orig fork=%d vfork=%d setArgV0=%d selinux=%d"),
            "zygote native monitor install log must expose status, handle, and original-pointer observability");
    Require(Contains(source,
                     "zygote hooks install short-circuit handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d"),
            "zygote hook short-circuit path must report retained native and Java hook state");
    Require(Contains(source,
                     "zygote hooks installed javaHooks=%d nativeMonitor=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d"),
            "zygote hook install completion log must expose native and Java hook state");
    Require(Contains(source,
                     "zygote monitor init short-circuit process=%s nativeHooks=%d javaHooks=%d requested=%d envReady=%d instanceReady=%d handles fork=%d vfork=%d setArgV0=%d selinux=%d javaNativeFork=%d javaSpecialize=%d javaFork=%d javaWrapperSpecialize=%d"),
            "zygote monitor short-circuit log must expose current retained hook state");
    return 0;
}
