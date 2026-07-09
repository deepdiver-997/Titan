#include "gs/net/tcp_server.h"

#include <iostream>

namespace gs {

TcpServer::TcpServer(boost::asio::io_context& io_context, const ServerConfig& config)
    : _io_context(io_context)
    , _config(config)
    , _acceptor(io_context, tcp::endpoint(
          boost::asio::ip::make_address(config.listen_address),
          config.listen_port))
{
    std::cout << "[tcp_server] listening on " << config.listen_address
              << ":" << config.listen_port << std::endl;
}

void TcpServer::start() {
    do_accept();
}

void TcpServer::do_accept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto conn = std::make_shared<TcpConnection>(std::move(socket));
                if (_connection_cb) _connection_cb(conn);
                conn->start();
            } else {
                std::cerr << "[tcp_server] accept error: " << ec.message()
                          << std::endl;
            }
            do_accept();  // continue accepting
        });
}

}  // namespace gs
