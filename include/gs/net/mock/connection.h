#pragma once

#include "gs/net/i_connection.h"
#include "gs/net/recv_buffer.h"

#include <string>
#include <vector>

namespace gs {

// Mock IConnection for testing — simulate network data without a real
// TCP connection.  Use feed_data() to push inbound bytes and inspect
// sent_data() to verify what the server tried to send.
class MockConnection : public IConnection {
public:
    MockConnection() = default;
    explicit MockConnection(const std::string& remote_addr);

    // ---- Test control -----------------------------------------------------

    // Simulate receiving data from the network.
    void feed_data(const uint8_t* data, size_t len);
    void feed_data(const std::vector<uint8_t>& data);

    // All data that has been "sent" since last clear_sent().
    const std::vector<std::vector<uint8_t>>& sent_data() const { return _sent; }
    void clear_sent() { _sent.clear(); }

    void set_remote_addr(const std::string& addr) { _remote_addr = addr; }
    void set_closed(bool closed) { _closed = closed; }

    // ---- IConnection interface --------------------------------------------

    void send(const std::vector<uint8_t>& data) override;
    void close() override;
    const std::string& remote_addr() const override { return _remote_addr; }
    bool is_closed() const override { return _closed; }
    std::vector<uint8_t> swap_recv_buffer() override {
        return _recv_buf.swap_out();
    }
    void set_close_callback(CloseCallback cb) override { _close_cb = std::move(cb); }

private:
    RecvBuffer _recv_buf;
    std::vector<std::vector<uint8_t>> _sent;
    std::string _remote_addr = "mock:0";
    bool _closed = false;
    CloseCallback _close_cb;
};

}  // namespace gs
