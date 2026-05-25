#include "communication/host/host_spawn_client.h"
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
    std::cerr << "Usage: " << program
              << " <package> <post_json> [agent_ready_timeout_ms] [script_message_timeout_ms] [host] [port]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
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

    const std::string package_name = argv[1];
    const std::string post_json = argv[2];
    const int agent_ready_timeout_ms = argc >= 4 ? std::atoi(argv[3]) : 10000;
    const int script_message_timeout_ms = argc >= 5 ? std::atoi(argv[4]) : 5000;
    const std::string host = argc >= 6 ? argv[5] : "127.0.0.1";
    const int port = argc >= 7 ? std::atoi(argv[6]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostSpawnClient client(std::move(transport));

    nook::comm::SpawnRequest request;
    request.identifier = package_name;

    nook::comm::HostSpawnResult result = client.SpawnAndWait(
        request,
        nook::comm::HostSpawnOptions{
            .response_timeout_ms = 5000,
            .agent_ready_timeout_ms = agent_ready_timeout_ms,
        });

    if (!result.spawn_response_received) {
        std::cerr << "spawn request failed: " << result.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 3;
    }

    if (result.spawn_response.error.code != 0) {
        std::cerr << "spawn response error: code=" << result.spawn_response.error.code
                  << " message=" << result.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 4;
    }

    std::cout << "spawn response ok: pid=" << result.spawn_response.pid << "\n";

    if (!result.agent_ready_received) {
        std::cerr << "agent ready timeout: " << result.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 5;
    }

    std::cout << "agent ready: pid=" << result.agent_ready.pid
              << " name=" << result.agent_ready.process_name
              << " arch=" << result.agent_ready.arch
              << " version=" << result.agent_ready.version << "\n";

    nook::comm::ScriptPost post;
    post.script_id = 0;
    post.message = post_json;

    std::string error_message;
    if (!client.SendScriptPost(post, &error_message)) {
        std::cerr << "send script post failed: " << error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 6;
    }

    std::cout << "script post sent: " << post.message << "\n";

    nook::comm::ScriptMessage message;
    if (!client.WaitForScriptMessage(script_message_timeout_ms, &message)) {
        std::cerr << "script message timeout after " << script_message_timeout_ms << " ms\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 7;
    }

    std::cout << "script message: script_id=" << message.script_id
              << " json=" << message.message
              << " data_len=" << message.data.size() << "\n";

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
