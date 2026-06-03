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
    const std::string comm_internal = ReadFile("src/framework/NookCommInternal.h");
    Require(!comm_internal.empty(), "failed to read src/framework/NookCommInternal.h");
    Require(Contains(comm_internal, "EnsureOutboundControlChannelReadyForCurrentProcess"),
            "NookCommInternal.h must declare the outbound gadget control-channel helper");
    Require(Contains(comm_internal, "NotifyRuntimeReadyToServer"),
            "NookCommInternal.h must declare the runtime-ready helper for gadget connect mode");

    const std::string comm_source = ReadFile("src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(Contains(comm_source, "tcp_transport.h"),
            "NookComm.cpp must include tcp_transport.h for gadget connect mode");
    Require(Contains(comm_source, "EnsureOutboundControlChannelReadyForCurrentProcess"),
            "NookComm.cpp must implement the outbound gadget control-channel helper");
    Require(Contains(comm_source, "TcpTransport"),
            "NookComm.cpp must build outbound gadget connections on TcpTransport");

    const std::string runtime_source = ReadFile("src/gadget/nook_gadget_runtime.cpp");
    Require(!runtime_source.empty(), "failed to read src/gadget/nook_gadget_runtime.cpp");
    Require(Contains(runtime_source, "EnsureOutboundControlChannelReadyForCurrentProcess"),
            "nook_gadget_runtime.cpp must route connect mode through the outbound control helper");
    Require(!Contains(runtime_source, "DefaultConnectInitializer(const GadgetConfig& config) {\n    (void)config;\n    return NOOK_STATUS_NOT_IMPLEMENTED;\n}"),
            "nook_gadget_runtime.cpp must not leave gadget connect mode unimplemented");

    return 0;
}
