#pragma once

#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/net/tcp_connection.h"

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gs {

using boost::asio::ip::tcp;

// TCP server. Accepts connections, holds weak_ptrs to them for batch I/O.
class TcpServer {
public:
    using ConnectionCallback =
        std::function<void(std::shared_ptr<TcpConnection>)>;

    TcpServer(boost::asio::io_context& io_context, const ServerConfig& config);

    void set_connection_callback(ConnectionCallback cb) {
        _connection_cb = std::move(cb);
    }

    void start();
    void close();
    // Stop accepting, send REDIRECT to all clients, then close acceptor.
    void drain(const std::string& new_ip, uint16_t new_port);

    uint16_t port() const { return _config.listen_port; }

    // Swap all live connections' recv buffers. Returns {player_id → bytes}.
    // TODO: when protocol includes entity_id in message body, return flat
    // vector<vector<uint8_t>> instead — messages become self-contained.
    std::unordered_map<EntityId, std::vector<uint8_t>> swap_all_buffers();

    // Send data to a specific connection by player_id.
    void send_to(EntityId player_id, const std::vector<uint8_t>& data);

    // Register a connection with a player_id.
    void register_conn(EntityId player_id, std::shared_ptr<TcpConnection> conn);
    void unregister_conn(EntityId player_id);

    size_t conn_count() const;

private:
    void do_accept();

    boost::asio::io_context& _io_context;
    const ServerConfig& _config;
    tcp::acceptor _acceptor;
    ConnectionCallback _connection_cb;

    mutable std::mutex _conn_mutex;
    std::unordered_map<EntityId, std::weak_ptr<TcpConnection>> _connections;
};

}  // namespace gs
