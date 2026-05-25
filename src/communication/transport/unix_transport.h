/**
 * @file unix_transport.h
 * @brief Unix Domain Socket transport implementation
 *
 * Used for Server <-> Agent communication within the same device.
 * Provides lower overhead than TCP for local IPC.
 */

#pragma once

#include "transport.h"
#include <string>
#include <atomic>

// Unix socket is only available on Unix-like systems
#if !defined(_WIN32)

#include <sys/un.h>

namespace nook {
namespace comm {

/**
 * @class UnixTransport
 * @brief Unix Domain Socket transport for local IPC
 *
 * Supports both client mode (connect to socket path) and
 * server mode (accepted connection).
 */
class UnixTransport : public Transport {
public:
    /**
     * Create client-mode transport
     * @param socket_path Path to Unix socket file
     */
    explicit UnixTransport(const std::string& socket_path);

    /**
     * Create server-mode transport from accepted socket
     * @param accepted_fd Already connected socket fd
     */
    explicit UnixTransport(int accepted_fd);

    ~UnixTransport() override;

    // Transport interface
    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    TransportState GetState() const override;
    ssize_t Send(const uint8_t* data, size_t len) override;
    ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) override;
    int GetFd() const override { return socket_fd_; }
    const char* GetTypeName() const override { return "Unix"; }

    /**
     * Get socket path
     */
    const std::string& GetSocketPath() const { return socket_path_; }

    /**
     * Get peer credentials (uid, gid, pid)
     * Only available after connection is established.
     * Useful for authentication.
     *
     * @param pid Output: peer process ID
     * @param uid Output: peer user ID
     * @param gid Output: peer group ID
     * @return true if credentials retrieved successfully
     */
    bool GetPeerCredentials(pid_t* pid, uid_t* uid, gid_t* gid) const;

private:
    std::string socket_path_;
    int socket_fd_ = -1;
    std::atomic<TransportState> atomic_state_{TransportState::kDisconnected};
    bool owns_socket_ = true;

    bool WaitForReadable(int timeout_ms);
    bool WaitForWritable(int timeout_ms);
    void CloseSocket();
};

/**
 * @class UnixListener
 * @brief Unix socket server listener for accepting connections
 */
class UnixListener : public TransportListener {
public:
    /**
     * Create Unix socket listener
     * @param socket_path Path to create socket at
     * @param backlog Listen backlog
     */
    explicit UnixListener(const std::string& socket_path, int backlog = 16);

    ~UnixListener() override;

    // TransportListener interface
    bool Listen() override;
    void Close() override;
    std::unique_ptr<Transport> Accept(int timeout_ms = -1) override;
    int GetFd() const override { return listen_fd_; }
    bool IsListening() const override { return listen_fd_ >= 0; }

    const std::string& GetSocketPath() const { return socket_path_; }

private:
    std::string socket_path_;
    int backlog_;
    int listen_fd_ = -1;
};

/**
 * Get default Nook socket path
 * @return Address like "@nook-<hash>.sock" on Android, file path elsewhere
 */
std::string GetDefaultSocketPath();

}  // namespace comm
}  // namespace nook

#endif  // !defined(_WIN32)
