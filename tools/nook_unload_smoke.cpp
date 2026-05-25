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
              << " <package> <script_source> [script_name] [verify_reload=0|1] "
                 "[agent_ready_timeout_ms] [host] [port]\n";
}

int ExitWithCleanup(int code) {
#ifdef _WIN32
    WSACleanup();
#endif
    return code;
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
    const bool verify_reload = argc >= 5 ? (std::atoi(argv[4]) != 0) : false;
    const int agent_ready_timeout_ms = argc >= 6 ? std::atoi(argv[5]) : 10000;
    const std::string host = argc >= 7 ? argv[6] : "127.0.0.1";
    const int port = argc >= 8 ? std::atoi(argv[7]) : 27042;

    auto transport = std::make_unique<nook::comm::TcpTransport>(nook::comm::TcpEndpoint{host, port});
    nook::comm::HostSpawnClient client(std::move(transport));

    nook::comm::SpawnRequest request;
    request.identifier = package_name;

    nook::comm::HostSpawnResult spawn = client.SpawnAndWait(
        request,
        nook::comm::HostSpawnOptions{
            .response_timeout_ms = agent_ready_timeout_ms,
            .agent_ready_timeout_ms = agent_ready_timeout_ms,
        });

    if (!spawn.spawn_response_received) {
        std::cerr << "spawn request failed: " << spawn.error_message << "\n";
        return ExitWithCleanup(3);
    }

    if (spawn.spawn_response.error.code != 0) {
        std::cerr << "spawn response error: code=" << spawn.spawn_response.error.code
                  << " message=" << spawn.error_message << "\n";
        return ExitWithCleanup(4);
    }

    std::cout << "spawn response ok: pid=" << spawn.spawn_response.pid << "\n";

    if (!spawn.agent_ready_received) {
        std::cerr << "agent ready timeout: " << spawn.error_message << "\n";
        return ExitWithCleanup(5);
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
        return ExitWithCleanup(6);
    }

    std::cout << "script create ok: script_id=" << create_response.script_id << "\n";

    nook::comm::ScriptLoad load;
    load.script_id = create_response.script_id;

    nook::comm::ScriptResponse load_response;
    if (!client.LoadScript(load, 5000, &load_response, &error_message)) {
        std::cerr << "load script failed: " << error_message << "\n";
        return ExitWithCleanup(7);
    }

    std::cout << "script load ok: script_id=" << load_response.script_id << "\n";

    nook::comm::ScriptUnload unload;
    unload.script_id = create_response.script_id;

    nook::comm::ScriptResponse unload_response;
    if (!client.UnloadScript(unload, 5000, &unload_response, &error_message)) {
        std::cerr << "unload script failed: " << error_message << "\n";
        return ExitWithCleanup(8);
    }

    std::cout << "script unload ok: script_id=" << unload_response.script_id << "\n";

    if (verify_reload) {
        nook::comm::ScriptResponse reload_response;
        const bool reload_ok = client.LoadScript(load, 5000, &reload_response, &error_message);
        if (reload_ok) {
            std::cerr << "reload after unload unexpectedly succeeded: script_id="
                      << reload_response.script_id << "\n";
            return ExitWithCleanup(9);
        }

        std::cout << "reload after unload failed as expected: " << error_message << "\n";
    }

    return ExitWithCleanup(0);
}
