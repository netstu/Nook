/**
 * @file tcp_transport.h
 * @brief TCP socket transport implementation
 *
 * Used for Host <-> Server communication over network (via adb forward).
 */

#pragma once

#include "transport.h"
#include <string>
#include <atomic>

namespace nook {
namespace comm {

/**
 * TCP endpoint configuration
 */
struct TcpEndpoint {
    std::string host = "127.0.0.1";
    int port = 27042;

    TcpEndpoint() = default;
    TcpEndpoint(const std::string& h, int p) : host(h), port(p) {}
    TcpEndpoint(int p) : port(p) {}  // localhost with port
};

/**
 * @class TcpTransport
 * @brief TCP socket transport for network communication
 *
 * Supports both client mode (connect to server) and
 * server mode (accepted connection).
 */
class TcpTransport : public Transport {
public:
    /**
     * Create client-mode transport
     * @param endpoint Server endpoint to connect to
     */
    explicit TcpTransport(const TcpEndpoint& endpoint);

    /**
     * Create server-mode transport from accepted socket
     * @param accepted_fd Already connected socket fd
     * @param peer_host Peer host address (for logging)
     * @param peer_port Peer port (for logging)
     */
    TcpTransport(int accepted_fd, const std::string& peer_host, int peer_port);

    ~TcpTransport() override;

    // Transport interface
    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    TransportState GetState() const override;
    ssize_t Send(const uint8_t* data, size_t len) override;
    ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) override;
    int GetFd() const override { return socket_fd_; }
    const char* GetTypeName() const override { return "TCP"; }

    /**
     * Get connected endpoint info
     */
    const TcpEndpoint& GetEndpoint() const { return endpoint_; }

    /**
     * Set socket options (called after connect/accept)
     */
    bool ApplySocketOptions();

private:
    TcpEndpoint endpoint_;
    int socket_fd_ = -1;
    std::atomic<TransportState> atomic_state_{TransportState::kDisconnected};
    bool owns_socket_ = true;  // Whether to close socket on destruct

    bool WaitForReadable(int timeout_ms);
    bool WaitForWritable(int timeout_ms);
    void CloseSocket();
};

/**
 * @class TcpListener
 * @brief TCP server listener for accepting connections
 */
class TcpListener : public TransportListener {
public:
    /**
     * Create TCP listener
     * @param port Port to listen on
     * @param bind_address Address to bind to (default: all interfaces)
     * @param backlog Listen backlog
     */
    explicit TcpListener(int port,
                         const std::string& bind_address = "0.0.0.0",
                         int backlog = 16);

    ~TcpListener() override;

    // TransportListener interface
    bool Listen() override;
    void Close() override;
    std::unique_ptr<Transport> Accept(int timeout_ms = -1) override;
    int GetFd() const override { return listen_fd_; }
    bool IsListening() const override { return listen_fd_ >= 0; }

    int GetPort() const { return port_; }

private:
    int port_;
    std::string bind_address_;
    int backlog_;
    int listen_fd_ = -1;
};

}  // namespace comm
}  // namespace nook
