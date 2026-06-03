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

bool ContainsModuleBlockWith(const std::string& contents,
                             const char* module_name,
                             const char* required_text) {
    const std::string marker = std::string("LOCAL_MODULE := ") + module_name;
    const size_t block_start = contents.find(marker);
    if (block_start == std::string::npos) {
        return false;
    }

    size_t block_end = contents.find("include $(CLEAR_VARS)", block_start + marker.size());
    if (block_end == std::string::npos) {
        block_end = contents.size();
    }

    const size_t text_pos = contents.find(required_text, block_start);
    return text_pos != std::string::npos && text_pos < block_end;
}

bool ContainsModuleBlockWithout(const std::string& contents,
                                const char* module_name,
                                const char* forbidden_text) {
    const std::string marker = std::string("LOCAL_MODULE := ") + module_name;
    const size_t block_start = contents.find(marker);
    if (block_start == std::string::npos) {
        return false;
    }

    size_t block_end = contents.find("include $(CLEAR_VARS)", block_start + marker.size());
    if (block_end == std::string::npos) {
        block_end = contents.size();
    }

    const size_t text_pos = contents.find(forbidden_text, block_start);
    return text_pos == std::string::npos || text_pos >= block_end;
}

}  // namespace

int main() {
    const std::string android_mk = ReadFile("build/android/Android.mk",
                                            "../../build/android/Android.mk");
    Require(!android_mk.empty(), "failed to read build/android/Android.mk");
    Require(Contains(android_mk, "LOCAL_MODULE := nook_gadget"),
            "Android.mk must define a dedicated nook_gadget module");
    Require(ContainsModuleBlockWith(android_mk, "nook_gadget",
                                    "LOCAL_MODULE_FILENAME := libnook-gadget"),
            "nook_gadget module must emit libnook-gadget.so");
    Require(ContainsModuleBlockWithout(android_mk, "nook_gadget", "../../server/server_main.cpp"),
            "nook_gadget must not directly compile nook-server entrypoints");
    Require(ContainsModuleBlockWith(android_mk, "nook_gadget",
                                    "-DNOOK_DISABLE_AGENT_AUTO_INIT=1"),
            "nook_gadget must disable legacy agent auto-init in its build flags");

    const std::string header = ReadFile("include/nook/NookGadget.h",
                                        "../../include/nook/NookGadget.h");
    Require(!header.empty(), "failed to read include/nook/NookGadget.h");
    Require(Contains(header, "NookStatus NookGadgetInitialize(void);"),
            "NookGadget.h must expose NookGadgetInitialize");

    const std::string nook_comm = ReadFile("src/framework/NookComm.cpp",
                                           "../../src/framework/NookComm.cpp");
    Require(!nook_comm.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(nook_comm,
                     "#if defined(__ANDROID__) && !defined(_WIN32) && !defined(NOOK_DISABLE_AGENT_AUTO_INIT)"),
            "NookComm auto-init must be guardable so nook_gadget can disable legacy startup");
    Require(Contains(nook_comm, "NookStatus EnsureControlChannelReadyForCurrentProcess()"),
            "NookComm must expose a control-only startup path for nook_gadget");

    return 0;
}
