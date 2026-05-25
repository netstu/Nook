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
    std::cerr << "Usage: " << program << " <pid|identifier> [timeout_ms] [host] [port]\n";
}

bool IsNumeric(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
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

    const std::string target = argv[1];
    const int timeout_ms = argc >= 3 ? std::atoi(argv[2]) : 10000;
    const std::string host = argc >= 4 ? argv[3] : "127.0.0.1";
    const int port = argc >= 5 ? std::atoi(argv[4]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostClient client(std::move(transport));

    nook::comm::AttachRequest request;
    if (IsNumeric(target)) {
        request.pid = static_cast<uint32_t>(std::strtoul(target.c_str(), nullptr, 10));
    } else {
        request.identifier = target;
    }

    nook::comm::AttachResponse response;
    std::string error_message;
    if (!client.Attach(timeout_ms, request, &response, &error_message)) {
        std::cerr << "attach failed: " << error_message << "\n";
        return ExitWithCleanup(3);
    }

    std::cout << "attach ok: session_id=" << response.session_id
              << " pid=" << response.pid
              << " process_name=" << response.process_name << "\n";
    return ExitWithCleanup(0);
}
