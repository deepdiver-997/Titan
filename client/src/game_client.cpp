#include "game_client.h"

#include <cstring>
#include <iostream>
#include <sstream>

namespace tank {

GameClient::GameClient() : _socket(_io) {}

GameClient::~GameClient() { disconnect(); }

void GameClient::connect(const std::string& host, uint16_t port) {
    tcp::resolver resolver(_io);
    boost::asio::connect(_socket, resolver.resolve(host, std::to_string(port)));
    _connected = true;
    std::cout << "[tank_client] connected to " << host << ":" << port << std::endl;
    try_read();
}

void GameClient::disconnect() {
    if (!_connected) return;
    _connected = false;
    boost::system::error_code ec;
    _socket.close(ec);
}

void GameClient::send_move(float x, float y) {
    if (!_connected) return;
    auto data = make_move_packet(x, y);
    boost::system::error_code ec;
    boost::asio::write(_socket, boost::asio::buffer(data), ec);
}

void GameClient::send_fire(float dx, float dy) {
    if (!_connected) return;
    auto data = make_fire_packet(dx, dy);
    boost::system::error_code ec;
    boost::asio::write(_socket, boost::asio::buffer(data), ec);
}

void GameClient::poll() {
    if (!_connected) return;
    _io.poll();
}

void GameClient::try_read() {
    // Read exactly 4-byte length prefix.
    boost::asio::async_read(_socket, boost::asio::buffer(_header_buf),
        [this](boost::system::error_code ec, size_t) {
            if (ec || !_connected) return;
            uint32_t body_len =
                (static_cast<uint32_t>(_header_buf[0]) << 24) |
                (static_cast<uint32_t>(_header_buf[1]) << 16) |
                (static_cast<uint32_t>(_header_buf[2]) << 8) |
                static_cast<uint32_t>(_header_buf[3]);
            if (body_len == 0 || body_len > 65536) {
                try_read();
                return;
            }
            _body_buf.resize(body_len);
            boost::asio::async_read(_socket, boost::asio::buffer(_body_buf),
                [this](boost::system::error_code ec2, size_t) {
                    if (!ec2) parse_message(_body_buf);
                    try_read();
                });
        });
}

void GameClient::parse_message(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    // The server sends plain-text AOI events wrapped in length-prefixed
    // packets by encode_message(). Parse the text format.
    std::string text(data.begin(), data.end());

    // Format: "ENTER <id> <x> <y>" or "LEAVE <id>"
    if (text.rfind("ENTER ", 0) == 0) {
        std::istringstream iss(text.substr(6));
        uint64_t id; int x = 0, y = 0;
        iss >> id >> x >> y;
        _entities[id] = {id, (float)x, (float)y};
        if (_player_id == 0) _player_id = id;
        if (_aoi_cb) _aoi_cb({EventType::Enter, id, (float)x, (float)y});
    } else if (text.rfind("MOVE ", 0) == 0) {
        std::istringstream iss(text.substr(5));
        uint64_t id; int x = 0, y = 0;
        iss >> id >> x >> y;
        auto it = _entities.find(id);
        if (it != _entities.end()) {
            it->second.x = (float)x;
            it->second.y = (float)y;
        }
        if (_aoi_cb) _aoi_cb({EventType::Move, id, (float)x, (float)y});
    } else if (text.rfind("LEAVE ", 0) == 0) {
        uint64_t id = std::stoull(text.substr(6));
        _entities.erase(id);
        if (_aoi_cb) _aoi_cb({EventType::Leave, id, 0, 0});
    }
    // Binary AOI event (0x04) — for future use
    else if (!data.empty() && data[0] == 0x04 && data.size() >= 11) {
        AoiEvent ev;
        ev.type = static_cast<EventType>(data[1]);
        std::memcpy(&ev.entity_id, data.data() + 2, 8);
        std::memcpy(&ev.x, data.data() + 10, 4);
        std::memcpy(&ev.y, data.data() + 14, 4);

        if (ev.type == EventType::Enter) {
            _entities[ev.entity_id] = {ev.entity_id, ev.x, ev.y};
        } else if (ev.type == EventType::Leave) {
            _entities.erase(ev.entity_id);
        } else if (ev.type == EventType::Move) {
            auto it = _entities.find(ev.entity_id);
            if (it != _entities.end()) {
                it->second.x = ev.x;
                it->second.y = ev.y;
            }
        }
        if (_aoi_cb) _aoi_cb(ev);
    }
}

std::vector<uint8_t> make_move_packet(float x, float y) {
    std::vector<uint8_t> payload(9);
    payload[0] = 0x02;
    std::memcpy(payload.data() + 1, &x, 4);
    std::memcpy(payload.data() + 5, &y, 4);

    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> result(4 + len);
    result[0] = static_cast<uint8_t>(len >> 24);
    result[1] = static_cast<uint8_t>(len >> 16);
    result[2] = static_cast<uint8_t>(len >> 8);
    result[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(result.data() + 4, payload.data(), len);
    return result;
}

std::vector<uint8_t> make_fire_packet(float dx, float dy) {
    std::vector<uint8_t> payload(9);
    payload[0] = 0x06;  // MsgType::Fire
    std::memcpy(payload.data() + 1, &dx, 4);
    std::memcpy(payload.data() + 5, &dy, 4);

    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> result(4 + len);
    result[0] = static_cast<uint8_t>(len >> 24);
    result[1] = static_cast<uint8_t>(len >> 16);
    result[2] = static_cast<uint8_t>(len >> 8);
    result[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(result.data() + 4, payload.data(), len);
    return result;
}

}  // namespace tank
