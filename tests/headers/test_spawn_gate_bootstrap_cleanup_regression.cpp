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
    const std::string source = ReadFile("src/framework/NookComm.cpp",
                                        "../../src/framework/NookComm.cpp");
    Require(!source.empty(), "failed to read src/framework/NookComm.cpp");

    Require(Contains(source, "void ClearSpawnGateBootstrapHooks("),
            "NookComm must expose a dedicated spawn-gate bootstrap hook cleanup helper");
    Require(Contains(source, "NookJavaHookUnhook(new_application);"),
            "spawn-gate bootstrap cleanup must unhook or remove the newApplication request/hook");
    Require(Contains(source, "NookJavaHookUnhook(call_application_on_create);"),
            "spawn-gate bootstrap cleanup must unhook or remove the callApplicationOnCreate request/hook");
    Require(Contains(source, "JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(false);"),
            "spawn-gate bootstrap cleanup must release the lifecycle-ready requirement once the gate is done");
    Require(Contains(source, "ClearSpawnGateBootstrapHooks(new_application_hook_id, call_application_on_create_hook_id);"),
            "spawn-gate resume path must clear deferred bootstrap hooks when the gate is released");
    Require(Contains(source,
                     "#if defined(NOOK_ZYGOTE_HELPER_ONLY)\n    NOOK_COMM_LOGI(\"spawn gate bootstrap cleanup skipped helper-only build newApplication=%d callApplicationOnCreate=%d\""),
            "helper-only spawn-gate cleanup must skip async JavaHook unhook to avoid tearing down bootstrap hooks from the helper DSO immediately after resume");

    return 0;
}
