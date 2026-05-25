/**
 * @file test_transport_compile.cpp
 * @brief Compile-time verification for transport headers
 *
 * This file verifies that transport headers compile correctly on host.
 * Run: g++ -std=c++17 -I ./include -I ./src tests/communication/test_transport_compile.cpp -o test_transport.exe
 */

#include <iostream>
#include <cstdlib>

// Test transport base class
#include "communication/transport/transport.h"

// Test TCP transport (cross-platform)
#include "communication/transport/tcp_transport.h"

// Unix transport only on non-Windows
#ifndef _WIN32
#include "communication/transport/unix_transport.h"
#endif

using namespace nook::comm;

// Verify enum values
static_assert(static_cast<int>(TransportState::kDisconnected) == 0, "");
static_assert(static_cast<int>(TransportState::kConnected) == 2, "");

// Verify error enum
static_assert(static_cast<int>(TransportError::kNone) == 0, "");

int main() {
    std::cout << "Transport header compilation test passed!" << std::endl;

    // Test TcpEndpoint construction
    TcpEndpoint ep1;
    TcpEndpoint ep2("127.0.0.1", 8080);
    TcpEndpoint ep3(9000);

    std::cout << "TcpEndpoint: " << ep2.host << ":" << ep2.port << std::endl;

    // Test TransportConfig
    TransportConfig config;
    config.tcp_nodelay = true;
    config.connect_timeout_ms = 3000;

    std::cout << "TransportConfig: timeout=" << config.connect_timeout_ms << "ms" << std::endl;

#ifndef _WIN32
    // Test Unix socket path
    std::string sock_path = GetDefaultSocketPath();
    std::cout << "Default socket path: " << sock_path << std::endl;
    setenv("NOOK_RUNTIME_DIR", "/data/local/tmp/nook-test", 1);
    std::string overridden_sock_path = GetDefaultSocketPath();
    std::cout << "Overridden socket path: " << overridden_sock_path << std::endl;
#endif

    std::cout << "All compile tests passed!" << std::endl;
    return 0;
}
