#pragma once

#include "gs/net/i_connection.h"

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gs {

using boost::asio::ip::tcp;

// Thread-safe receive buffer — network thread writes, tick thread swaps.
class RecvBuffer {
public:
    void append(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lk(_mutex);
        _buf.insert(_buf.end(), data, data + len);
    }

    // Atomically take all buffered data, leaving the buffer empty.
    std::vector<uint8_t> swap_out() {
        std::vector<uint8_t> out;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            out.swap(_buf);
        }
        return out;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(_mutex);
        return _buf.size();
    }

private:
    mutable std::mutex _mutex;
    std::vector<uint8_t> _buf;
};

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
