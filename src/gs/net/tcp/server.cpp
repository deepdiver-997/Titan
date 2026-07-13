#include "gs/net/tcp/server.h"
#include "gs/net/message.h"

#include <iostream>

namespace gs {

TcpServer::TcpServer(boost::asio::io_context& io_context,
                     const ServerConfig& config)
    : _io_context(io_context)
    , _config(config)
    , _acceptor(io_context, tcp::endpoint(
          boost::asio::ip::make_address(config.listen_address),
          config.listen_port))
{
    std::cout << "[tcp_server] listening on " << config.listen_address
              << ":" << config.listen_port << std::endl;
}

void TcpServer::start() { do_accept(); }

void TcpServer::close() {
    boost::system::error_code ec;
    _acceptor.close(ec);
}

void TcpServer::register_conn(EntityId player_id,
                               std::shared_ptr<IConnection> conn) {
    std::lock_guard<std::mutex> lk(_conn_mutex);
    _connections[player_id] = conn;
}

void TcpServer::unregister_conn(EntityId player_id) {
    std::lock_guard<std::mutex> lk(_conn_mutex);
    _connections.erase(player_id);
}

std::unordered_map<EntityId, std::vector<uint8_t>>
TcpServer::swap_all_buffers() {
    std::unordered_map<EntityId, std::vector<uint8_t>> result;
    std::lock_guard<std::mutex> lk(_conn_mutex);
    for (auto it = _connections.begin(); it != _connections.end();) {
        auto conn = it->second.lock();
        if (!conn || conn->is_closed()) {
            it = _connections.erase(it);
            continue;
        }
        auto data = conn->swap_recv_buffer();
        if (!data.empty()) result[it->first] = std::move(data);
        ++it;
    }
    return result;
}

void TcpServer::send_to(EntityId player_id,
                         const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lk(_conn_mutex);
    auto it = _connections.find(player_id);
    if (it != _connections.end()) {
        auto conn = it->second.lock();
        if (conn) conn->send(data);
    }
}

size_t TcpServer::conn_count() const {
    std::lock_guard<std::mutex> lk(_conn_mutex);
    return _connections.size();
}

void TcpServer::drain(const std::string& new_ip, uint16_t new_port) {
    std::cout << "[tcp_server] draining → redirecting clients to "
              << new_ip << ":" << new_port << std::endl;

    // Build REDIRECT packet: [type 0xF0][ip_len 1B][ip str][port 2B]
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(MsgType::Redirect));
    payload.push_back(static_cast<uint8_t>(new_ip.size()));
    payload.insert(payload.end(), new_ip.begin(), new_ip.end());
    payload.push_back(static_cast<uint8_t>(new_port >> 8));
    payload.push_back(static_cast<uint8_t>(new_port & 0xFF));
    auto redirect_pkt = encode_message(payload);

    // Send REDIRECT to all connected clients.
    {
        std::lock_guard<std::mutex> lk(_conn_mutex);
        for (auto& [pid, weak] : _connections) {
            auto conn = weak.lock();
            if (conn) conn->send(redirect_pkt);
        }
    }

    // Stop accepting new connections.
    close();
}

void TcpServer::do_accept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted)
                    std::cerr << "[tcp_server] accept error: "
                              << ec.message() << std::endl;
                return;
            }
            auto conn = std::make_shared<TcpConnection>(std::move(socket));
            if (_connection_cb) _connection_cb(conn);
            conn->start();
            do_accept();
        });
}

}  // namespace gs
