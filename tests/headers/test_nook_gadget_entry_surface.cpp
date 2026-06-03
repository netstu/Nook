#include "gadget/nook_gadget_runtime.h"
#include "nook/NookGadget.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace nook {
namespace test_support {

extern int g_ensure_control_channel_call_count;
extern int g_bridge_initialize_call_count;
extern int g_notify_runtime_ready_call_count;
void ResetGadgetRuntimeTestStubs();

}  // namespace test_support
}  // namespace nook

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
    const std::string source = ReadFile("src/gadget/nook_gadget_entry.cpp");
    Require(!source.empty(), "failed to read src/gadget/nook_gadget_entry.cpp");
    Require(Contains(source, "NookGadgetInitialize"),
            "gadget entry source must define NookGadgetInitialize");
    Require(Contains(source, "constructor"),
            "gadget entry source must provide a constructor-backed auto-init path");
    Require(Contains(source, "EnsureDefaultInitializers"),
            "gadget entry source must install gadget-owned default initializers");
    Require(Contains(source, "EnsureControlChannelReadyForCurrentProcess"),
            "gadget entry source must route control initialization through the control channel helper");
    Require(Contains(source, "NookScriptRuntimeBridgeInitialize"),
            "gadget entry source must route bridge initialization through the script bridge helper");
    Require(Contains(source, "NotifyRuntimeReadyToServer"),
            "gadget entry source must route runtime-ready notification through the runtime-ready helper");

    nook::gadget::ResetRuntimeForTesting();
    nook::test_support::ResetGadgetRuntimeTestStubs();

    Require(NookGadgetInitialize() == NOOK_STATUS_OK,
            "NookGadgetInitialize must succeed through gadget runtime initialization");
    Require(nook::test_support::g_ensure_control_channel_call_count == 1,
            "NookGadgetInitialize must call the control channel helper once");
    Require(nook::test_support::g_bridge_initialize_call_count == 1,
            "NookGadgetInitialize must call the bridge helper once");
    Require(nook::test_support::g_notify_runtime_ready_call_count == 1,
            "NookGadgetInitialize must call the runtime-ready helper once");

    Require(NookGadgetInitialize() == NOOK_STATUS_OK,
            "repeated NookGadgetInitialize calls must remain idempotent");
    Require(nook::test_support::g_ensure_control_channel_call_count == 1,
            "repeated NookGadgetInitialize calls must not rerun control init");
    Require(nook::test_support::g_bridge_initialize_call_count == 1,
            "repeated NookGadgetInitialize calls must not rerun bridge init");
    Require(nook::test_support::g_notify_runtime_ready_call_count == 1,
            "repeated NookGadgetInitialize calls must not rerun runtime-ready notify");

    nook::gadget::ResetRuntimeForTesting();
    return 0;
}
