#include "gs/net/tcp_connection.h"

#include <iostream>

namespace gs {

TcpConnection::TcpConnection(tcp::socket socket)
    : _socket(std::move(socket)) {
    _remote_addr = _socket.remote_endpoint().address().to_string() + ":" +
                   std::to_string(_socket.remote_endpoint().port());
}

void TcpConnection::start() {
    read_header();
}

void TcpConnection::send(const std::vector<uint8_t>& data) {
    if (_closed) return;
    do_write(data);
}

void TcpConnection::close() {
    if (_closed) return;
    _closed = true;
    boost::system::error_code ec;
    _socket.close(ec);
    if (_close_cb) _close_cb();
}

void TcpConnection::read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(_socket, boost::asio::buffer(_header_buf),
        [this, self](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                handle_error("read_header: " + ec.message());
                return;
            }
            uint32_t length =
                (static_cast<uint32_t>(_header_buf[0]) << 24) |
                (static_cast<uint32_t>(_header_buf[1]) << 16) |
                (static_cast<uint32_t>(_header_buf[2]) << 8) |
                static_cast<uint32_t>(_header_buf[3]);
            if (length > 1024 * 1024) {
                handle_error("message too large");
                return;
            }
            read_body(length);
        });
}

void TcpConnection::read_body(uint32_t length) {
    auto self = shared_from_this();
    _body_buf.resize(length);
    boost::asio::async_read(_socket, boost::asio::buffer(_body_buf),
        [this, self, length](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                handle_error("read_body: " + ec.message());
                return;
            }
            // Append full frame: [4B header][body] so parse_input can
            // decode length-prefixed messages from RecvBuffer.
            _recv_buf.append(_header_buf.data(), 4);
            _recv_buf.append(_body_buf.data(), length);
            read_header();
        });
}

void TcpConnection::do_write(std::vector<uint8_t> data) {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(_socket, boost::asio::buffer(*buf),
        [this, self, buf](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                handle_error("write: " + ec.message());
            }
        });
}

void TcpConnection::handle_error(const std::string& what) {
    if (!_closed) {
        std::cerr << "[connection " << _remote_addr << "] " << what << std::endl;
        close();
    }
}

}  // namespace gs
