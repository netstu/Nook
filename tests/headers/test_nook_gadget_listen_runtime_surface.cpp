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
    const std::string runtime_header = ReadFile("src/gadget/nook_gadget_runtime.h");
    Require(!runtime_header.empty(), "failed to read src/gadget/nook_gadget_runtime.h");
    Require(Contains(runtime_header, "using ListenInitializer = NookStatus (*)(const GadgetConfig&);"),
            "nook_gadget_runtime.h must declare the listen initializer hook");
    Require(Contains(runtime_header, "SetListenInitializerForTesting"),
            "nook_gadget_runtime.h must declare the listen initializer test hook");
    Require(Contains(runtime_header, "ResetListenInitializerForTesting"),
            "nook_gadget_runtime.h must declare the listen initializer reset hook");

    const std::string comm_internal = ReadFile("src/framework/NookCommInternal.h");
    Require(!comm_internal.empty(), "failed to read src/framework/NookCommInternal.h");
    Require(Contains(comm_internal, "AdoptInboundControlChannelTransportForCurrentProcess"),
            "NookCommInternal.h must declare the inbound control-channel adopt helper");

    const std::string runtime_source = ReadFile("src/gadget/nook_gadget_runtime.cpp");
    Require(!runtime_source.empty(), "failed to read src/gadget/nook_gadget_runtime.cpp");
    Require(Contains(runtime_source, "g_listen_initializer"),
            "nook_gadget_runtime.cpp must track a listen initializer");
    Require(Contains(runtime_source, "DefaultListenInitializer"),
            "nook_gadget_runtime.cpp must provide the default listen initializer");

    const std::string listener_source = ReadFile("src/gadget/nook_gadget_direct_listener.cpp");
    Require(!listener_source.empty(), "failed to read src/gadget/nook_gadget_direct_listener.cpp");
    Require(Contains(listener_source, "AdoptInboundControlChannelTransportForCurrentProcess"),
            "direct gadget listener must hand accepted transports into NookComm");
    Require(Contains(listener_source, "MessageType::kAttachRequest"),
            "direct gadget listener must handle host attach requests");
    Require(Contains(listener_source, "MessageType::kAttachResponse"),
            "direct gadget listener must send attach responses");
    Require(Contains(listener_source, "NotifyRuntimeReadyToServer"),
            "direct gadget listener must emit runtime-ready after attach handoff");
    Require(Contains(listener_source, "UnixListener"),
            "direct gadget listener must support localabstract unix listeners for gadget attach");
    Require(!Contains(listener_source, "return \"127.0.0.1\";"),
            "direct gadget listener must not force loopback as the default listen address");

    return 0;
}
