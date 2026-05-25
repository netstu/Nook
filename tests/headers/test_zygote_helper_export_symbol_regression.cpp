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
    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(comm_source, "NOOK_AGENT_EXPORT NookStatus NookAgentInitializeForZygoteControl(void) {"),
            "zygote helper build must export NookAgentInitializeForZygoteControl");
    Require(Contains(comm_source, "NOOK_AGENT_EXPORT NookStatus NookAgentReinitializeForZygoteControl(void) {"),
            "zygote helper build must export NookAgentReinitializeForZygoteControl");
    Require(Contains(comm_source, "#define NOOK_AGENT_EXPORT __attribute__((visibility(\"default\"), used))"),
            "zygote helper exports must retain default visibility");

    const std::string android_mk = ReadFile("build/android/Android.mk",
                                            "../../build/android/Android.mk");
    Require(!android_mk.empty(), "failed to read build/android/Android.mk");
    Require(Contains(android_mk, "LOCAL_MODULE := nook_zygote_helper"),
            "Android build must define nook_zygote_helper");
    Require(Contains(android_mk, "LOCAL_MODULE_FILENAME := libnook-zygote-helper"),
            "zygote helper output name must remain libnook-zygote-helper");
    Require(Contains(android_mk, "LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS) -DNOOK_ZYGOTE_HELPER_ONLY=1"),
            "zygote helper build must stay on the dedicated helper-only compile path");
    return 0;
}
