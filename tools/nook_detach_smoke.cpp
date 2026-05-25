#include "communication/host/host_client.h"
#include "communication/transport/tcp_transport.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace {

void PrintUsage(const char* program) {
    std::cerr << "Usage: " << program << " <session_id> [timeout_ms] [host] [port]\n";
}

int ExitWithCleanup(int code) {
#ifdef _WIN32
    WSACleanup();
#endif
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        PrintUsage(argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 2;
    }
#endif

    const uint32_t session_id = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const int timeout_ms = argc >= 3 ? std::atoi(argv[2]) : 10000;
    const std::string host = argc >= 4 ? argv[3] : "127.0.0.1";
    const int port = argc >= 5 ? std::atoi(argv[4]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostClient client(std::move(transport));

    nook::comm::DetachRequest request;
    request.session_id = session_id;

    nook::comm::DetachResponse response;
    std::string error_message;
    if (!client.Detach(timeout_ms, request, &response, &error_message)) {
        std::cerr << "detach failed: " << error_message << "\n";
        return ExitWithCleanup(3);
    }

    std::cout << "detach ok: session_id=" << response.session_id << "\n";
    return ExitWithCleanup(0);
}
