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
              << " <package> <script_source> [script_name] [agent_ready_timeout_ms] [script_message_timeout_ms] [host] [port]\n";
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
    const std::string script_source = argv[2];
    const std::string script_name = argc >= 4 ? argv[3] : "smoke.js";
    const int agent_ready_timeout_ms = argc >= 5 ? std::atoi(argv[4]) : 10000;
    const int script_message_timeout_ms = argc >= 6 ? std::atoi(argv[5]) : 5000;
    const std::string host = argc >= 7 ? argv[6] : "127.0.0.1";
    const int port = argc >= 8 ? std::atoi(argv[7]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostSpawnClient client(std::move(transport));

    nook::comm::SpawnRequest request;
    request.identifier = package_name;

    nook::comm::HostSpawnResult spawn = client.SpawnAndWait(
        request,
        nook::comm::HostSpawnOptions{
            .response_timeout_ms = 5000,
            .agent_ready_timeout_ms = agent_ready_timeout_ms,
        });

    if (!spawn.spawn_response_received) {
        std::cerr << "spawn request failed: " << spawn.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 3;
    }

    if (spawn.spawn_response.error.code != 0) {
        std::cerr << "spawn response error: code=" << spawn.spawn_response.error.code
                  << " message=" << spawn.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 4;
    }

    std::cout << "spawn response ok: pid=" << spawn.spawn_response.pid << "\n";

    if (!spawn.agent_ready_received) {
        std::cerr << "agent ready timeout: " << spawn.error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 5;
    }

    std::cout << "agent ready: pid=" << spawn.agent_ready.pid
              << " name=" << spawn.agent_ready.process_name
              << " arch=" << spawn.agent_ready.arch
              << " version=" << spawn.agent_ready.version << "\n";

    nook::comm::ScriptCreate create;
    create.name = script_name;
    create.source = script_source;

    nook::comm::ScriptCreateResponse create_response;
    std::string error_message;
    if (!client.CreateScript(create, 5000, &create_response, &error_message)) {
        std::cerr << "create script failed: " << error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 6;
    }

    std::cout << "script create ok: script_id=" << create_response.script_id << "\n";

    nook::comm::ScriptLoad load;
    load.script_id = create_response.script_id;

    nook::comm::ScriptResponse load_response;
    if (!client.LoadScript(load, 5000, &load_response, &error_message)) {
        std::cerr << "load script failed: " << error_message << "\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 7;
    }

    std::cout << "script load ok: script_id=" << load_response.script_id << "\n";

    nook::comm::ScriptMessage message;
    if (!client.WaitForScriptMessage(script_message_timeout_ms, &message)) {
        std::cerr << "script message timeout after " << script_message_timeout_ms << " ms\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 8;
    }

    std::cout << "script message: script_id=" << message.script_id
              << " json=" << message.message
              << " data_len=" << message.data.size() << "\n";

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
