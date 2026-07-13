#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gs {

// Abstract client connection — swap TCP/QUIC/WebSocket without touching
// game logic.  Mirrors IPeer but for client-facing connections.
//
// Each connection owns a recv buffer written by the transport layer and
// consumed by the tick thread via swap_recv_buffer().
class IConnection : public std::enable_shared_from_this<IConnection> {
public:
    using CloseCallback = std::function<void()>;

    virtual ~IConnection() = default;

    // Send raw bytes to the client.
    virtual void send(const std::vector<uint8_t>& data) = 0;

    // Close the connection.
    virtual void close() = 0;

    virtual std::string remote_addr() const = 0;
    virtual bool is_closed() const = 0;

    // Atomically consume all received data since the last call.
    // Called by the tick thread — the transport layer fills the buffer
    // on its own thread.
    virtual std::vector<uint8_t> swap_recv_buffer() = 0;

    // Register a callback invoked when the connection is closed.
    virtual void set_close_callback(CloseCallback cb) = 0;
};

}  // namespace gs
