/**
 * @file transport.cpp
 * @brief Transport base class implementation
 */

#include "transport.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <unistd.h>
#endif

namespace nook {
namespace comm {

bool Transport::SendAll(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = Send(data + sent, len - sent);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool Transport::RecvAll(uint8_t* buf, size_t len, int timeout_ms) {
    size_t received = 0;

    // Use configured timeout if not specified
    if (timeout_ms < 0) {
        timeout_ms = config_.read_timeout_ms;
    }

    while (received < len) {
        ssize_t n = Recv(buf + received, len - received, timeout_ms);
        if (n <= 0) {
            return false;
        }
        received += static_cast<size_t>(n);
    }
    return true;
}

void Transport::SetState(TransportState new_state) {
    if (state_ != new_state) {
        TransportState old_state = state_;
        state_ = new_state;
        if (on_state_change_) {
            on_state_change_(old_state, new_state);
        }
    }
}

void Transport::SetError(TransportError error, const std::string& message) {
    last_error_ = error;
    last_error_msg_ = message;
    SetState(TransportState::kError);
    if (on_error_) {
        on_error_(error, message);
    }
}

TransportError Transport::ErrnoToError(int err) {
#ifdef _WIN32
    switch (err) {
        case WSAECONNREFUSED:
            return TransportError::kConnectionRefused;
        case WSAECONNRESET:
            return TransportError::kConnectionReset;
        case WSAETIMEDOUT:
            return TransportError::kTimeout;
        case WSAEHOSTUNREACH:
        case WSAENETUNREACH:
            return TransportError::kHostUnreachable;
        case WSAEADDRINUSE:
            return TransportError::kAddressInUse;
        case WSAEACCES:
            return TransportError::kPermissionDenied;
        case WSAENETDOWN:
            return TransportError::kNetworkDown;
        default:
            return TransportError::kUnknown;
    }
#else
    switch (err) {
        case ECONNREFUSED:
            return TransportError::kConnectionRefused;
        case ECONNRESET:
        case EPIPE:
            return TransportError::kConnectionReset;
        case ETIMEDOUT:
            return TransportError::kTimeout;
        case EHOSTUNREACH:
        case ENETUNREACH:
            return TransportError::kHostUnreachable;
        case EADDRINUSE:
            return TransportError::kAddressInUse;
        case EINVAL:
        case EADDRNOTAVAIL:
            return TransportError::kInvalidAddress;
        case EACCES:
        case EPERM:
            return TransportError::kPermissionDenied;
        case ENETDOWN:
            return TransportError::kNetworkDown;
        default:
            return TransportError::kUnknown;
    }
#endif
}

}  // namespace comm
}  // namespace nook
