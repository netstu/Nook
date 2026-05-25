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
    std::cerr << "Usage: " << program << " [timeout_ms] [host] [port]\n";
}

int ExitWithCleanup(int code) {
#ifdef _WIN32
    WSACleanup();
#endif
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 4) {
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

    const int timeout_ms = argc >= 2 ? std::atoi(argv[1]) : 5000;
    const std::string host = argc >= 3 ? argv[2] : "127.0.0.1";
    const int port = argc >= 4 ? std::atoi(argv[3]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostClient client(std::move(transport));

    nook::comm::AppListResponse response;
    std::string error_message;
    if (!client.EnumerateApps(timeout_ms, &response, &error_message)) {
        std::cerr << "app list failed: " << error_message << "\n";
        return ExitWithCleanup(3);
    }

    std::cout << "app count: " << response.apps.size() << "\n";
    for (const auto& app : response.apps) {
        std::cout << app.package_name << "\n";
    }

    return ExitWithCleanup(0);
}
