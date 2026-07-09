#pragma once

#include "gs/common/config.h"
#include "gs/net/tcp_connection.h"

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace gs {

using boost::asio::ip::tcp;

// TCP server that accepts connections and creates TcpConnection sessions.
class TcpServer {
public:
    using ConnectionCallback =
        std::function<void(std::shared_ptr<TcpConnection>)>;

    TcpServer(boost::asio::io_context& io_context, const ServerConfig& config);

    void set_connection_callback(ConnectionCallback cb) {
        _connection_cb = std::move(cb);
    }

    void start();

    uint16_t port() const { return _config.listen_port; }

private:
    void do_accept();

    boost::asio::io_context& _io_context;
    const ServerConfig& _config;
    tcp::acceptor _acceptor;
    ConnectionCallback _connection_cb;
};

}  // namespace gs
