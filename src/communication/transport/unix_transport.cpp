/**
 * @file unix_transport.cpp
 * @brief Unix Domain Socket transport implementation
 */

#include "unix_transport.h"
#include "path_utils.h"

#if !defined(_WIN32)

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#ifdef __ANDROID__
    #include <android/log.h>
    #define LOG_TAG "NookComm"
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
    #include <cstdio>
    #define LOGD(...) ((void)0)
    #define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

namespace nook {
namespace comm {

namespace {

bool IsAbstractSocketAddress(const std::string& socket_path) {
    return !socket_path.empty() && socket_path[0] == '@';
}

socklen_t BuildUnixSocketAddress(const std::string& socket_path, struct sockaddr_un* addr) {
    if (addr == nullptr || socket_path.empty()) {
        return 0;
    }

    std::memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;

    if (IsAbstractSocketAddress(socket_path)) {
        const std::string abstract_name = socket_path.substr(1);
        if (abstract_name.empty() || abstract_name.size() + 1 > sizeof(addr->sun_path)) {
            return 0;
        }
        addr->sun_path[0] = '\0';
        std::memcpy(addr->sun_path + 1, abstract_name.data(), abstract_name.size());
        return static_cast<socklen_t>(
            offsetof(struct sockaddr_un, sun_path) + 1 + abstract_name.size());
    }

    if (socket_path.size() >= sizeof(addr->sun_path)) {
        return 0;
    }
    std::strncpy(addr->sun_path, socket_path.c_str(), sizeof(addr->sun_path) - 1);
    return static_cast<socklen_t>(sizeof(struct sockaddr_un));
}

std::string BuildDefaultAbstractSocketName() {
#ifdef __ANDROID__
    const char* runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
    std::string seed =
        (runtime_dir != nullptr && runtime_dir[0] != '\0')
            ? std::string(runtime_dir)
            : std::string("/data/local/tmp/nook");
    uint32_t hash = 2166136261u;
    for (unsigned char ch : seed) {
        hash ^= ch;
        hash *= 16777619u;
    }
    char name[64] = {};
    std::snprintf(name, sizeof(name), "@nook-%08x.sock", hash);
    return std::string(name);
#else
    return "/tmp/nook.sock";
#endif
}

bool SetCloseOnExec(int fd) {
    if (fd < 0) {
        return false;
    }

    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return false;
    }

    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

int CreateUnixStreamSocket() {
#if defined(SOCK_CLOEXEC)
    int cloexec_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (cloexec_fd >= 0) {
        return cloexec_fd;
    }
#endif

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        (void)SetCloseOnExec(fd);
    }
    return fd;
}

int AcceptUnixSocket(int listen_fd) {
#if defined(__linux__) && defined(SOCK_CLOEXEC)
    int cloexec_accepted_fd = accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (cloexec_accepted_fd >= 0) {
        return cloexec_accepted_fd;
    }
    if (errno != ENOSYS && errno != EINVAL) {
        return cloexec_accepted_fd;
    }
#endif

    int accepted_fd = accept(listen_fd, nullptr, nullptr);
    if (accepted_fd >= 0) {
        (void)SetCloseOnExec(accepted_fd);
    }
    return accepted_fd;
}

}  // namespace

// ============================================================================
// UnixTransport Implementation
// ============================================================================

UnixTransport::UnixTransport(const std::string& socket_path)
    : socket_path_(socket_path), owns_socket_(true) {
    LOGD("UnixTransport: created client mode, path %s", socket_path_.c_str());
}

UnixTransport::UnixTransport(int accepted_fd)
    : socket_fd_(accepted_fd), owns_socket_(true) {
    atomic_state_.store(TransportState::kConnected);
    state_ = TransportState::kConnected;
    LOGD("UnixTransport: created from accepted fd %d", accepted_fd);
}

UnixTransport::~UnixTransport() {
    Disconnect();
}

bool UnixTransport::Connect() {
    if (IsConnected()) {
        return true;
    }

    if (socket_path_.empty()) {
        SetError(TransportError::kInvalidAddress, "Socket path is empty");
        return false;
    }

    SetState(TransportState::kConnecting);
    atomic_state_.store(TransportState::kConnecting);

    // Create socket
    socket_fd_ = CreateUnixStreamSocket();
    if (socket_fd_ < 0) {
        int err = errno;
        SetError(ErrnoToError(err), std::string("Failed to create socket: ") + strerror(err));
        atomic_state_.store(TransportState::kError);
        return false;
    }

    // Prepare address
    struct sockaddr_un addr{};
    const socklen_t addr_len = BuildUnixSocketAddress(socket_path_, &addr);
    if (addr_len == 0) {
        CloseSocket();
        SetError(TransportError::kInvalidAddress, "Socket path too long");
        atomic_state_.store(TransportState::kError);
        return false;
    }

    // Set non-blocking for connect with timeout
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    bool need_blocking_restore = false;
    if (config_.connect_timeout_ms > 0 && flags >= 0) {
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        need_blocking_restore = true;
    }

    // Connect
    int ret = connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), addr_len);
    if (ret < 0) {
        int err = errno;
        if (err != EINPROGRESS) {
            CloseSocket();
            SetError(ErrnoToError(err), std::string("Connect failed: ") + strerror(err));
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
        getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &sock_err, &len);
        if (sock_err != 0) {
            CloseSocket();
            SetError(ErrnoToError(sock_err), std::string("Connect failed: ") + strerror(sock_err));
            atomic_state_.store(TransportState::kError);
            return false;
        }
    }

    // Restore blocking mode
    if (need_blocking_restore) {
        fcntl(socket_fd_, F_SETFL, flags);
    }

    SetState(TransportState::kConnected);
    atomic_state_.store(TransportState::kConnected);
    LOGD("UnixTransport: connected to %s", socket_path_.c_str());
    return true;
}

void UnixTransport::Disconnect() {
    if (socket_fd_ >= 0) {
        CloseSocket();
    }
    SetState(TransportState::kDisconnected);
    atomic_state_.store(TransportState::kDisconnected);
}

bool UnixTransport::IsConnected() const {
    return atomic_state_.load() == TransportState::kConnected;
}

TransportState UnixTransport::GetState() const {
    return atomic_state_.load();
}

ssize_t UnixTransport::Send(const uint8_t* data, size_t len) {
    if (!IsConnected() || socket_fd_ < 0) {
        return -1;
    }

    ssize_t sent = send(socket_fd_, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0;  // Would block, try again
        }
        SetError(ErrnoToError(err), std::string("Send failed: ") + strerror(err));
        atomic_state_.store(TransportState::kError);
        return -1;
    }
    return sent;
}

ssize_t UnixTransport::Recv(uint8_t* buf, size_t len, int timeout_ms) {
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

    ssize_t received = recv(socket_fd_, buf, len, 0);
    if (received < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0;  // Would block
        }
        SetError(ErrnoToError(err), std::string("Recv failed: ") + strerror(err));
        atomic_state_.store(TransportState::kError);
        return -1;
    }

    if (received == 0) {
        // Connection closed by peer
        LOGD("UnixTransport: connection closed by peer");
        SetState(TransportState::kDisconnected);
        atomic_state_.store(TransportState::kDisconnected);
    }

    return received;
}

bool UnixTransport::GetPeerCredentials(pid_t* pid, uid_t* uid, gid_t* gid) const {
    if (socket_fd_ < 0) {
        return false;
    }

#ifdef __ANDROID__
    struct ucred cred{};
    socklen_t len = sizeof(cred);
    if (getsockopt(socket_fd_, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        LOGE("UnixTransport: failed to get peer credentials: %s", strerror(errno));
        return false;
    }
    if (pid) *pid = cred.pid;
    if (uid) *uid = cred.uid;
    if (gid) *gid = cred.gid;
    return true;
#elif defined(__linux__)
    struct ucred cred{};
    socklen_t len = sizeof(cred);
    if (getsockopt(socket_fd_, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        return false;
    }
    if (pid) *pid = cred.pid;
    if (uid) *uid = cred.uid;
    if (gid) *gid = cred.gid;
    return true;
#else
    // Not supported on other platforms
    (void)pid;
    (void)uid;
    (void)gid;
    return false;
#endif
}

bool UnixTransport::WaitForReadable(int timeout_ms) {
    if (socket_fd_ < 0) return false;

    struct pollfd pfd{};
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0 && (pfd.revents & POLLIN);
}

bool UnixTransport::WaitForWritable(int timeout_ms) {
    if (socket_fd_ < 0) return false;

    struct pollfd pfd{};
    pfd.fd = socket_fd_;
    pfd.events = POLLOUT;

    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0 && (pfd.revents & POLLOUT);
}

void UnixTransport::CloseSocket() {
    if (socket_fd_ >= 0 && owns_socket_) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

// ============================================================================
// UnixListener Implementation
// ============================================================================

UnixListener::UnixListener(const std::string& socket_path, int backlog)
    : socket_path_(socket_path), backlog_(backlog) {
    LOGD("UnixListener: created for %s", socket_path_.c_str());
}

UnixListener::~UnixListener() {
    Close();
}

bool UnixListener::Listen() {
    if (listen_fd_ >= 0) {
        return true;  // Already listening
    }

    if (socket_path_.empty()) {
        LOGE("UnixListener: socket path is empty");
        return false;
    }

    const bool is_abstract = IsAbstractSocketAddress(socket_path_);
    if (!is_abstract) {
        const std::string parent_dir = GetParentDirectory(socket_path_);
        if (!EnsureDirectoryRecursive(parent_dir)) {
            LOGE("UnixListener: failed to create parent dir: %s", parent_dir.c_str());
            return false;
        }

        unlink(socket_path_.c_str());
    }

    // Create socket
    listen_fd_ = CreateUnixStreamSocket();
    if (listen_fd_ < 0) {
        LOGE("UnixListener: failed to create socket: %s", strerror(errno));
        return false;
    }

    // Prepare address
    struct sockaddr_un addr{};
    const socklen_t addr_len = BuildUnixSocketAddress(socket_path_, &addr);
    if (addr_len == 0) {
        LOGE("UnixListener: socket path too long");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Bind
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), addr_len) < 0) {
        LOGE("UnixListener: failed to bind: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (!is_abstract) {
        chmod(socket_path_.c_str(), 0777);
    }

    // Listen
    if (listen(listen_fd_, backlog_) < 0) {
        LOGE("UnixListener: failed to listen: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        if (!is_abstract) {
            unlink(socket_path_.c_str());
        }
        return false;
    }

    LOGD("UnixListener: listening on %s", socket_path_.c_str());
    return true;
}

void UnixListener::Close() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        if (!IsAbstractSocketAddress(socket_path_)) {
            unlink(socket_path_.c_str());
        }
        LOGD("UnixListener: closed");
    }
}

std::unique_ptr<Transport> UnixListener::Accept(int timeout_ms) {
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

    int client_fd = AcceptUnixSocket(listen_fd_);
    if (client_fd < 0) {
        LOGE("UnixListener: accept failed: %s", strerror(errno));
        return nullptr;
    }

    LOGD("UnixListener: accepted connection, fd=%d", client_fd);

    return std::make_unique<UnixTransport>(client_fd);
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string GetDefaultSocketPath() {
    return BuildDefaultAbstractSocketName();
}

}  // namespace comm
}  // namespace nook

#endif  // !defined(_WIN32)
