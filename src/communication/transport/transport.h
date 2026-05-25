/**
 * @file transport.h
 * @brief Transport layer abstraction for Nook communication
 *
 * Provides an abstract interface for different transport implementations
 * (TCP, Unix Socket, etc.) used in Host-Server and Server-Agent communication.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <functional>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #ifndef __MINGW32__
        using ssize_t = int;
    #endif
#else
    #include <sys/types.h>
#endif

namespace nook {
namespace comm {

/**
 * Transport connection state
 */
enum class TransportState {
    kDisconnected,  // Not connected
    kConnecting,    // Connection in progress
    kConnected,     // Connected and ready
    kError          // Error state
};

/**
 * Transport error codes
 */
enum class TransportError {
    kNone = 0,
    kConnectionRefused,
    kConnectionReset,
    kTimeout,
    kHostUnreachable,
    kAddressInUse,
    kInvalidAddress,
    kPermissionDenied,
    kNetworkDown,
    kUnknown
};

/**
 * Transport configuration options
 */
struct TransportConfig {
    int connect_timeout_ms = 5000;      // Connection timeout
    int read_timeout_ms = -1;           // Read timeout (-1 = blocking)
    int write_timeout_ms = -1;          // Write timeout (-1 = blocking)
    size_t recv_buffer_size = 65536;    // Receive buffer size
    size_t send_buffer_size = 65536;    // Send buffer size
    bool tcp_nodelay = true;            // Disable Nagle's algorithm
    bool reuse_addr = true;             // Allow address reuse
};

/**
 * Callback types
 */
using StateChangeCallback = std::function<void(TransportState old_state,
                                                TransportState new_state)>;
using ErrorCallback = std::function<void(TransportError error,
                                          const std::string& message)>;

/**
 * @class Transport
 * @brief Abstract base class for transport implementations
 *
 * Provides a common interface for different transport mechanisms.
 * Implementations include TcpTransport and UnixTransport.
 */
class Transport {
public:
    virtual ~Transport() = default;

    // Prevent copy
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Allow move
    Transport(Transport&&) = default;
    Transport& operator=(Transport&&) = default;

    /**
     * Establish connection
     * @return true if connection successful
     */
    virtual bool Connect() = 0;

    /**
     * Close connection gracefully
     */
    virtual void Disconnect() = 0;

    /**
     * Check if connected
     * @return true if in connected state
     */
    virtual bool IsConnected() const = 0;

    /**
     * Get current state
     * @return Current transport state
     */
    virtual TransportState GetState() const = 0;

    /**
     * Send data
     * @param data Pointer to data buffer
     * @param len Number of bytes to send
     * @return Number of bytes sent, or -1 on error
     */
    virtual ssize_t Send(const uint8_t* data, size_t len) = 0;

    /**
     * Send all data (blocks until all sent or error)
     * @param data Pointer to data buffer
     * @param len Number of bytes to send
     * @return true if all bytes sent successfully
     */
    virtual bool SendAll(const uint8_t* data, size_t len);

    /**
     * Receive data
     * @param buf Buffer to receive into
     * @param len Maximum bytes to receive
     * @param timeout_ms Timeout in milliseconds (-1 = use config default)
     * @return Number of bytes received, 0 on disconnect, -1 on error
     */
    virtual ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) = 0;

    /**
     * Receive exact number of bytes (blocks until all received or error)
     * @param buf Buffer to receive into
     * @param len Exact number of bytes to receive
     * @param timeout_ms Total timeout in milliseconds
     * @return true if all bytes received successfully
     */
    virtual bool RecvAll(uint8_t* buf, size_t len, int timeout_ms = -1);

    /**
     * Get underlying file descriptor (for poll/select integration)
     * @return File descriptor, or -1 if not applicable
     */
    virtual int GetFd() const = 0;

    /**
     * Get last error code
     * @return Last error that occurred
     */
    virtual TransportError GetLastError() const { return last_error_; }

    /**
     * Get last error message
     * @return Human-readable error message
     */
    virtual std::string GetLastErrorMessage() const { return last_error_msg_; }

    /**
     * Configure transport options
     * @param config Configuration options
     */
    virtual void Configure(const TransportConfig& config) { config_ = config; }

    /**
     * Get current configuration
     * @return Current configuration
     */
    const TransportConfig& GetConfig() const { return config_; }

    /**
     * Set state change callback
     * @param callback Function to call on state changes
     */
    void SetStateChangeCallback(StateChangeCallback callback) {
        on_state_change_ = std::move(callback);
    }

    /**
     * Set error callback
     * @param callback Function to call on errors
     */
    void SetErrorCallback(ErrorCallback callback) {
        on_error_ = std::move(callback);
    }

    /**
     * Get transport type name (for logging)
     * @return Transport type name
     */
    virtual const char* GetTypeName() const = 0;

protected:
    Transport() = default;

    /**
     * Set state and notify callback
     */
    void SetState(TransportState new_state);

    /**
     * Set error and notify callback
     */
    void SetError(TransportError error, const std::string& message);

    /**
     * Convert errno to TransportError
     */
    static TransportError ErrnoToError(int err);

    TransportConfig config_;
    TransportState state_ = TransportState::kDisconnected;
    TransportError last_error_ = TransportError::kNone;
    std::string last_error_msg_;

    StateChangeCallback on_state_change_;
    ErrorCallback on_error_;
};

/**
 * Utility: Create a listening socket (TCP or Unix)
 * Used by server-side code to accept connections.
 */
class TransportListener {
public:
    virtual ~TransportListener() = default;

    /**
     * Start listening
     * @return true if successful
     */
    virtual bool Listen() = 0;

    /**
     * Stop listening and close socket
     */
    virtual void Close() = 0;

    /**
     * Accept incoming connection
     * @param timeout_ms Timeout in milliseconds (-1 = blocking)
     * @return New transport for accepted connection, or nullptr
     */
    virtual std::unique_ptr<Transport> Accept(int timeout_ms = -1) = 0;

    /**
     * Get listening socket fd
     */
    virtual int GetFd() const = 0;

    /**
     * Check if listening
     */
    virtual bool IsListening() const = 0;
};

}  // namespace comm
}  // namespace nook
