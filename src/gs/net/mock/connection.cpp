#include "gs/net/mock/connection.h"

namespace gs {

MockConnection::MockConnection(const std::string& remote_addr)
    : _remote_addr(remote_addr) {}

void MockConnection::feed_data(const uint8_t* data, size_t len) {
    _recv_buf.append(data, len);
}

void MockConnection::feed_data(const std::vector<uint8_t>& data) {
    _recv_buf.append(data.data(), data.size());
}

void MockConnection::send(const std::vector<uint8_t>& data) {
    _sent.push_back(data);
}

void MockConnection::close() {
    _closed = true;
    if (_close_cb) _close_cb();
}

}  // namespace gs
