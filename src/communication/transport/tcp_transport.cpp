/**
 * @file tcp_transport.cpp
 * @brief TCP socket transport implementation
 */

#include "tcp_transport.h"

#include <cstring>
#include <cerrno>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define SHUT_RDWR SD_BOTH
    #define close closesocket
    #define poll WSAPoll
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
#endif

#ifdef __ANDROID__
    #include <android/log.h>
    #define LOG_TAG "NookComm"
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
    #define LOGD(...) ((void)0)
    #define LOGE(...) ((void)0)
#endif

namespace nook {
namespace comm {

// ============================================================================
// TcpTransport Implementation
// ============================================================================

TcpTransport::TcpTransport(const TcpEndpoint& endpoint)
    : endpoint_(endpoint), owns_socket_(true) {
    LOGD("TcpTransport: created client mode, target %s:%d",
         endpoint_.host.c_str(), endpoint_.port);
}

TcpTransport::TcpTransport(int accepted_fd, const std::string& peer_host, int peer_port)
    : endpoint_(peer_host, peer_port),
      socket_fd_(accepted_fd),
      owns_socket_(true) {
    atomic_state_.store(TransportState::kConnected);
    state_ = TransportState::kConnected;
    LOGD("TcpTransport: created from accepted fd %d, peer %s:%d",
         accepted_fd, peer_host.c_str(), peer_port);
}

TcpTransport::~TcpTransport() {
    Disconnect();
}

bool TcpTransport::Connect() {
    if (IsConnected()) {
        return true;
    }

    SetState(TransportState::kConnecting);
    atomic_state_.store(TransportState::kConnecting);

    // Resolve address
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    std::string port_str = std::to_string(endpoint_.port);

    int ret = getaddrinfo(endpoint_.host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0 || result == nullptr) {
        SetError(TransportError::kInvalidAddress,
                 std::string("Failed to resolve address: ") + endpoint_.host);
        atomic_state_.store(TransportState::kError);
        return false;
    }

    // Create socket
    socket_fd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_fd_ < 0) {
        int err = errno;
        freeaddrinfo(result);
        SetError(ErrnoToError(err), "Failed to create socket");
        atomic_state_.store(TransportState::kError);
        return false;
    }

    // Set non-blocking for connect with timeout
    bool need_blocking_restore = false;
#ifndef _WIN32
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (config_.connect_timeout_ms > 0 && flags >= 0) {
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        need_blocking_restore = true;
    }
#endif

    // Connect
    ret = connect(socket_fd_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (ret < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
#else
        int err = errno;
        if (err != EINPROGRESS) {
#endif
            CloseSocket();
            SetError(ErrnoToError(err), "Connect failed");
            atomic_state_.store(TransportState::kError);
            return false;
        }

        // Wait for connection with timeout
        if (!WaitForWritable(config_.connect_timeout_ms)) {
            CloseSocket();
            SetError(TransportError::kTimeout, "Connect timed out");
            atomic_state_.store(TransportState::kError);
            return false;
        }

        // Check connection result
        int sock_err = 0;
        socklen_t len = sizeof(sock_err);
        getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sock_err), &len);
        if (sock_err != 0) {
            CloseSocket();
            SetError(ErrnoToError(sock_err), "Connect failed");
            atomic_state_.store(TransportState::kError);
            return false;
        }
    }

    // Restore blocking mode
#ifndef _WIN32
    if (need_blocking_restore) {
        fcntl(socket_fd_, F_SETFL, flags);
    }
#endif

    // Apply socket options
    ApplySocketOptions();

    SetState(TransportState::kConnected);
    atomic_state_.store(TransportState::kConnected);
    LOGD("TcpTransport: connected to %s:%d", endpoint_.host.c_str(), endpoint_.port);
    return true;
}

void TcpTransport::Disconnect() {
    if (socket_fd_ >= 0) {
        CloseSocket();
    }
    SetState(TransportState::kDisconnected);
    atomic_state_.store(TransportState::kDisconnected);
}

bool TcpTransport::IsConnected() const {
    return atomic_state_.load() == TransportState::kConnected;
}

TransportState TcpTransport::GetState() const {
    return atomic_state_.load();
}

ssize_t TcpTransport::Send(const uint8_t* data, size_t len) {
    if (!IsConnected() || socket_fd_ < 0) {
        return -1;
    }

#ifdef _WIN32
    int flags = 0;
#else
    int flags = MSG_NOSIGNAL;  // Prevent SIGPIPE
#endif

    ssize_t sent = send(socket_fd_, reinterpret_cast<const char*>(data), len, flags);
    if (sent < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0;  // Would block, try again
        }
        SetError(ErrnoToError(err), "Send failed");
        atomic_state_.store(TransportState::kError);
        return -1;
    }
    return sent;
}

ssize_t TcpTransport::Recv(uint8_t* buf, size_t len, int timeout_ms) {
    if (!IsConnected() || socket_fd_ < 0) {
        return -1;
    }

    // Use config timeout if not specified
    if (timeout_ms < 0) {
        timeout_ms = config_.read_timeout_ms;
    }

    // Wait for data if timeout specified
    if (timeout_ms > 0) {
        if (!WaitForReadable(timeout_ms)) {
            // Timeout - not an error, just no data
            return 0;
        }
    }

    ssize_t received = recv(socket_fd_, reinterpret_cast<char*>(buf), len, 0);
    if (received < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0;  // Would block
        }
        SetError(ErrnoToError(err), "Recv failed");
        atomic_state_.store(TransportState::kError);
        return -1;
    }

    if (received == 0) {
        // Connection closed by peer
        LOGD("TcpTransport: connection closed by peer");
        SetState(TransportState::kDisconnected);
        atomic_state_.store(TransportState::kDisconnected);
    }

    return received;
}

bool TcpTransport::ApplySocketOptions() {
    if (socket_fd_ < 0) {
        return false;
    }

    int optval = 1;

    // TCP_NODELAY - disable Nagle's algorithm for lower latency
    if (config_.tcp_nodelay) {
        setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&optval), sizeof(optval));
    }

    // SO_REUSEADDR
    if (config_.reuse_addr) {
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&optval), sizeof(optval));
    }

    // Set buffer sizes if specified
    if (config_.recv_buffer_size > 0) {
        int size = static_cast<int>(config_.recv_buffer_size);
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    if (config_.send_buffer_size > 0) {
        int size = static_cast<int>(config_.send_buffer_size);
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    return true;
}

bool TcpTransport::WaitForReadable(int timeout_ms) {
    if (socket_fd_ < 0) return false;

    struct pollfd pfd{};
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0 && (pfd.revents & POLLIN);
}

bool TcpTransport::WaitForWritable(int timeout_ms) {
    if (socket_fd_ < 0) return false;

    struct pollfd pfd{};
    pfd.fd = socket_fd_;
    pfd.events = POLLOUT;

    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0 && (pfd.revents & POLLOUT);
}

void TcpTransport::CloseSocket() {
    if (socket_fd_ >= 0 && owns_socket_) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

// ============================================================================
// TcpListener Implementation
// ============================================================================

TcpListener::TcpListener(int port, const std::string& bind_address, int backlog)
    : port_(port), bind_address_(bind_address), backlog_(backlog) {
    LOGD("TcpListener: created for %s:%d", bind_address_.c_str(), port_);
}

TcpListener::~TcpListener() {
    Close();
}

bool TcpListener::Listen() {
    if (listen_fd_ >= 0) {
        return true;  // Already listening
    }

    // Create socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd_ < 0) {
        LOGE("TcpListener: failed to create socket: %s", strerror(errno));
        return false;
    }

    // Set SO_REUSEADDR
    int optval = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    // Bind
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind_address_.empty() || bind_address_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr);
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("TcpListener: failed to bind: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(listen_fd_, backlog_) < 0) {
        LOGE("TcpListener: failed to listen: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    LOGD("TcpListener: listening on %s:%d", bind_address_.c_str(), port_);
    return true;
}

void TcpListener::Close() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        LOGD("TcpListener: closed");
    }
}

std::unique_ptr<Transport> TcpListener::Accept(int timeout_ms) {
    if (listen_fd_ < 0) {
        return nullptr;
    }

    // Wait for connection if timeout specified
    if (timeout_ms >= 0) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0) {
            return nullptr;  // Timeout or error
        }
    }

    struct sockaddr_in peer_addr{};
    socklen_t addr_len = sizeof(peer_addr);

    int client_fd = accept(listen_fd_,
                           reinterpret_cast<struct sockaddr*>(&peer_addr),
                           &addr_len);
    if (client_fd < 0) {
        LOGE("TcpListener: accept failed: %s", strerror(errno));
        return nullptr;
    }

    // Get peer info
    char peer_host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, peer_host, sizeof(peer_host));
    int peer_port = ntohs(peer_addr.sin_port);

    LOGD("TcpListener: accepted connection from %s:%d", peer_host, peer_port);

    auto transport = std::make_unique<TcpTransport>(client_fd, peer_host, peer_port);
    transport->ApplySocketOptions();
    return transport;
}

}  // namespace comm
}  // namespace nook
