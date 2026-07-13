#pragma once

#include "gs/net/i_connection.h"
#include "gs/net/recv_buffer.h"

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gs {

using boost::asio::ip::tcp;

// A single TCP connection (client session).
// Continuously does async reads, appending raw bytes to a RecvBuffer.
// Message parsing happens later in the tick loop, not in the I/O callback.
class TcpConnection : public IConnection,
                       public std::enable_shared_from_this<TcpConnection> {
public:
    using CloseCallback = std::function<void()>;

    explicit TcpConnection(tcp::socket socket);

    void start();
    void send(const std::vector<uint8_t>& data) override;
    void close() override;

    void set_close_callback(CloseCallback cb) override { _close_cb = std::move(cb); }

    const std::string& remote_addr() const override { return _remote_addr; }
    bool is_closed() const override { return _closed; }

    // Atomically consume all received data (IConnection interface).
    std::vector<uint8_t> swap_recv_buffer() override {
        return _recv_buf.swap_out();
    }

private:
    void read_header();
    void read_body(uint32_t length);
    void do_write(std::vector<uint8_t> data);
    void handle_error(const std::string& what);

    tcp::socket _socket;
    std::string _remote_addr;

    std::array<uint8_t, 4> _header_buf = {};
    std::vector<uint8_t> _body_buf;

    RecvBuffer _recv_buf;
    CloseCallback _close_cb;

    bool _closed = false;
};

}  // namespace gs
