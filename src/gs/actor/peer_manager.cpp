#include "gs/actor/peer_manager.h"
#include "gs/net/tcp/peer.h"

#include "gs/common/logger.h"
#include <iostream>

namespace gs {

// ---- PeerManager ----------------------------------------------------------
PeerManager::PeerManager(boost::asio::io_context& io,
                         const std::string& listen_addr, uint16_t listen_port)
    : _io(io)
    , _my_addr(listen_addr + ":" + std::to_string(listen_port))
    , _acceptor(io, boost::asio::ip::tcp::endpoint(
               boost::asio::ip::make_address(listen_addr), listen_port))
{
    _known_nodes.insert(_my_addr);
    LOG_PEER_INFO("listening on {}", _my_addr);
}

void PeerManager::start_accept() { do_accept(); }

void PeerManager::do_accept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec,
               boost::asio::ip::tcp::socket socket) {
            if (ec) return;
            auto peer = std::make_shared<TcpPeer>(std::move(socket));
            auto addr = peer->remote_addr();
            peer->set_recv_callback(
                [this, addr](const std::vector<uint8_t>& data) {
                    handle_peer_data(addr, data);
                });
            {
                std::lock_guard<std::mutex> lk(_peer_mutex);
                _peers[addr] = peer;
                _known_nodes.insert(addr);
            }
            peer->start();
            // Gossip the new node + sync local actors.
            gossip_new_node(addr);
            if (_new_peer_cb) _new_peer_cb(addr);
            LOG_PEER_INFO("accepted {}", addr);
            do_accept();
        });
}

void PeerManager::connect_to_peer(const std::string& ip, uint16_t port) {
    auto addr = ip + ":" + std::to_string(port);
    if (_known_nodes.count(addr)) return;

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_io);
    boost::asio::ip::tcp::resolver resolver(_io);
    boost::asio::connect(*socket, resolver.resolve(ip, std::to_string(port)));

    auto peer = std::make_shared<TcpPeer>(std::move(*socket));
    peer->set_recv_callback(
        [this, addr](const std::vector<uint8_t>& data) {
            handle_peer_data(addr, data);
        });
    {
        std::lock_guard<std::mutex> lk(_peer_mutex);
        _peers[addr] = peer;
        _known_nodes.insert(addr);
    }
    peer->start();
    // Gossip new node + push local actors to new peer.
    gossip_new_node(addr);
    if (_new_peer_cb) _new_peer_cb(addr);
    LOG_PEER_INFO("connected to {}", addr);
}

void PeerManager::broadcast_register(ActorId aid) {
    auto data = make_frame(0xE0, encode_register(aid));
    std::lock_guard<std::mutex> lk(_peer_mutex);
    for (auto& [addr, peer] : _peers) peer->send(data);
}

void PeerManager::broadcast_unregister(ActorId aid) {
    auto data = make_frame(0xE1, encode_unregister(aid));
    std::lock_guard<std::mutex> lk(_peer_mutex);
    for (auto& [addr, peer] : _peers) peer->send(data);
}

void PeerManager::send_register_to(const std::string& addr, ActorId aid) {
    auto data = make_frame(0xE0, encode_register(aid));
    send_to_peer(addr, data);
}

void PeerManager::send_to(ActorId target, std::unique_ptr<Message> msg) {
    auto it = _routes.find(target);
    if (it == _routes.end()) return;
    auto data = make_frame(0xE3, actor_msg_to_wire(target, *msg));
    send_to_peer(it->second, data);
}

void PeerManager::send_to_peer(const std::string& addr,
                                const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lk(_peer_mutex);
    auto it = _peers.find(addr);
    if (it != _peers.end()) it->second->send(data);
}

void PeerManager::gossip_new_node(const std::string& new_addr) {
    auto data = make_frame(0xE2, encode_new_node(new_addr));
    std::lock_guard<std::mutex> lk(_peer_mutex);
    for (auto& [addr, peer] : _peers) {
        if (addr != new_addr) peer->send(data);
    }
}

void PeerManager::handle_peer_data(const std::string& peer_addr,
                                    const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    uint8_t type = data[0];
    std::string payload(data.begin() + 1, data.end());

    switch (type) {
    case 0xE0: {  // REGISTER_ACTOR
        ActorId aid; std::memcpy(&aid, payload.data(), 8);
        _routes[aid] = peer_addr;
        LOG_PEER_INFO("remote actor {} @ {}", aid, peer_addr);
        break;
    }
    case 0xE1: {  // UNREGISTER_ACTOR
        ActorId aid; std::memcpy(&aid, payload.data(), 8);
        _routes.erase(aid);
        break;
    }
    case 0xE2: {  // NEW_NODE
        std::string new_ip = payload;
        if (_known_nodes.insert(new_ip).second && new_ip != _my_addr) {
            auto colon = new_ip.rfind(':');
            if (colon != std::string::npos) {
                std::string ip = new_ip.substr(0, colon);
                uint16_t port = static_cast<uint16_t>(std::stoi(new_ip.substr(colon + 1)));
                connect_to_peer(ip, port);
            }
        }
        break;
    }
    case 0xE3: {  // ACTOR_MSG — forward to local Actor
        if (_actor_cb) {
            ActorId aid; std::memcpy(&aid, payload.data(), 8);
            // Deserialize full Message from payload in a real impl.
            _actor_cb(aid, nullptr);
        }
        break;
    }
    }
}

// ---- Wire encoding (static helpers) ---------------------------------------
std::vector<uint8_t> PeerManager::make_frame(uint8_t type,
                                              const std::string& payload) {
    std::vector<uint8_t> result(5 + payload.size());
    uint32_t len = static_cast<uint32_t>(1 + payload.size());
    result[0] = static_cast<uint8_t>(len >> 24);
    result[1] = static_cast<uint8_t>(len >> 16);
    result[2] = static_cast<uint8_t>(len >> 8);
    result[3] = static_cast<uint8_t>(len & 0xFF);
    result[4] = type;
    std::memcpy(result.data() + 5, payload.data(), payload.size());
    return result;
}

std::string PeerManager::encode_register(ActorId aid) {
    std::string s(8, '\0'); std::memcpy(s.data(), &aid, 8); return s;
}
std::string PeerManager::encode_unregister(ActorId aid) {
    return encode_register(aid);
}
std::string PeerManager::encode_new_node(const std::string& addr) {
    return addr;
}
std::string PeerManager::actor_msg_to_wire(ActorId target, Message& /*msg*/) {
    std::string s(8, '\0'); std::memcpy(s.data(), &target, 8);
    return s;  // Full serialization would include msg type + body
}

}  // namespace gs
