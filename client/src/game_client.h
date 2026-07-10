#pragma once

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tank {

using boost::asio::ip::tcp;

// Entity state received from server.
struct EntityState {
    uint64_t id;
    float x, y;
    bool is_player = false;
};

// AOI event from server.
enum class EventType : uint8_t { Enter = 0, Leave = 1, Move = 2 };

struct AoiEvent {
    EventType type;
    uint64_t entity_id;
    float x, y;
};

// TCP client that connects to Titan server.
// Sends movement input, receives AOI events.
class GameClient {
public:
    using AoiCallback = std::function<void(const AoiEvent&)>;

    GameClient();
    ~GameClient();

    void connect(const std::string& host, uint16_t port);
    void disconnect();

    // Send a move command to the server.
    void send_move(float x, float y);

    // Send a fire command (normalized direction).
    void send_fire(float dx, float dy);

    // Poll for incoming data (non-blocking). Call each frame.
    void poll();

    // Current entities visible to us.
    const std::unordered_map<uint64_t, EntityState>& entities() const {
        return _entities;
    }

    // Our own player ID (set on first enter event).
    uint64_t player_id() const { return _player_id; }

    void set_aoi_callback(AoiCallback cb) { _aoi_cb = std::move(cb); }

private:
    void try_read();
    void parse_message(const std::vector<uint8_t>& data);

    boost::asio::io_context _io;
    tcp::socket _socket;
    bool _connected = false;
    uint64_t _player_id = 0;

    std::array<uint8_t, 4> _header_buf = {};
    std::vector<uint8_t> _body_buf;

    std::unordered_map<uint64_t, EntityState> _entities;
    AoiCallback _aoi_cb;
};

// ---- Wire protocol helpers (match server's message.h) -------------------
std::vector<uint8_t> make_move_packet(float x, float y);
std::vector<uint8_t> make_fire_packet(float dx, float dy);

}  // namespace tank
