#include "gs/net/tcp/peer.h"

#include <iostream>

namespace gs {

TcpPeer::TcpPeer(boost::asio::ip::tcp::socket socket)
    : _socket(std::move(socket)) {
    _addr = _socket.remote_endpoint().address().to_string() + ":" +
            std::to_string(_socket.remote_endpoint().port());
}

TcpPeer::~TcpPeer() { close(); }

void TcpPeer::start() { read_header(); }

void TcpPeer::send(const std::vector<uint8_t>& data) {
    if (_closed) return;
    do_write(data);
}

void TcpPeer::close() {
    if (_closed) return;
    _closed = true;
    boost::system::error_code ec;
    _socket.close(ec);
}

void TcpPeer::read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(_socket, boost::asio::buffer(_header_buf),
        [this, self](boost::system::error_code ec, size_t) {
            if (ec) { close(); return; }
            uint32_t len = (static_cast<uint32_t>(_header_buf[0]) << 24) |
                           (static_cast<uint32_t>(_header_buf[1]) << 16) |
                           (static_cast<uint32_t>(_header_buf[2]) << 8) |
                           static_cast<uint32_t>(_header_buf[3]);
            if (len > 65536) { close(); return; }
            read_body(len);
        });
}

void TcpPeer::read_body(uint32_t length) {
    auto self = shared_from_this();
    _body_buf.resize(length);
    boost::asio::async_read(_socket, boost::asio::buffer(_body_buf),
        [this, self](boost::system::error_code ec, size_t) {
            if (ec) { close(); return; }
            if (_recv_cb) _recv_cb(_body_buf);
            read_header();
        });
}

void TcpPeer::do_write(std::vector<uint8_t> data) {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(_socket, boost::asio::buffer(*buf),
        [this, self, buf](boost::system::error_code ec, size_t) {
            if (ec) close();
        });
}

}  // namespace gs
