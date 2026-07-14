// === Manual Integration Test Client =======================================
//
// Connects to a Titan tank_battle server, sends hardcoded move/fire commands,
// and prints all server responses. Useful for catching runtime bugs in the
// framework (actor scheduling, AOI, network I/O, etc.).
//
// Build:  cmake --build build && ./build/tests/manual/tank_test_client
// Server: ./build/examples/tank_battle/titan_tank_server
//
// Protocol (length-prefixed binary):
//   [4B big-endian length][payload]
//
// Client → Server:
//   0x02 (Move):  [type 1B][x 4B][y 4B]  — total 9 bytes
//   0x06 (Fire):  [type 1B][dx 4B][dy 4B] — total 9 bytes
//
// Server → Client (text-based AOI):
//   ENTER <id> <x> <y>
//   MOVE  <id> <x> <y>
//   LEAVE <id>
//
// ============================================================================

#include <boost/asio.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

// ---- Helpers ---------------------------------------------------------------

// Encode a length-prefixed frame: [4B big-endian len][payload]
static std::vector<uint8_t> make_frame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(4 + payload.size());
    uint32_t len = static_cast<uint32_t>(payload.size());
    frame[0] = static_cast<uint8_t>(len >> 24);
    frame[1] = static_cast<uint8_t>(len >> 16);
    frame[2] = static_cast<uint8_t>(len >> 8);
    frame[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(frame.data() + 4, payload.data(), payload.size());
    return frame;
}

// Build a Move command: type=0x02, pos=(x, y)
static std::vector<uint8_t> make_move(float x, float y) {
    std::vector<uint8_t> p(9);
    p[0] = 0x02;  // MsgType::Move
    std::memcpy(&p[1], &x, 4);
    std::memcpy(&p[5], &y, 4);
    return p;
}

// Build a Fire command: type=0x06, direction=(dx, dy), normalized
static std::vector<uint8_t> make_fire(float dx, float dy) {
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0) { dx /= len; dy /= len; }
    std::vector<uint8_t> p(9);
    p[0] = 0x06;  // MsgType::Fire
    std::memcpy(&p[1], &dx, 4);
    std::memcpy(&p[5], &dy, 4);
    return p;
}

// ---- Main ------------------------------------------------------------------

int main() {
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(
            boost::asio::ip::make_address("127.0.0.1"), 8888));

        std::cout << "[client] connected to 127.0.0.1:8888\n";

        // Step 1: Send a MOVE to establish identity.
        // The server assigns a player_id on connection and sends back
        // an "ENTER <id> <x> <y>" welcome message.
        std::cout << "[client] sending MOVE (100, 100)...\n";
        auto move1 = make_frame(make_move(100.f, 100.f));
        boost::asio::write(socket, boost::asio::buffer(move1));

        // Read the server's response (ENTER + possible AOI events).
        // We do a short sleep to let the server process the tick.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Read available data.
        std::array<uint8_t, 4096> buf;
        boost::system::error_code ec;
        size_t n = socket.read_some(boost::asio::buffer(buf), ec);
        if (n > 0) {
            std::cout << "[client] received " << n << " bytes:\n";
            // Try to parse as text (the server sends text-based AOI).
            std::string text(buf.data(), buf.data() + n);
            std::cout << text << "\n";
        }

        // Step 2: Move to a new position.
        std::cout << "[client] sending MOVE (200, 150)...\n";
        auto move2 = make_frame(make_move(200.f, 150.f));
        boost::asio::write(socket, boost::asio::buffer(move2));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        n = socket.read_some(boost::asio::buffer(buf), ec);
        if (n > 0) {
            std::cout << "[client] received " << n << " bytes:\n";
            std::string text(buf.data(), buf.data() + n);
            std::cout << text << "\n";
        }

        // Step 3: Fire a bullet.
        std::cout << "[client] sending FIRE (dir=1,0)...\n";
        auto fire = make_frame(make_fire(1.f, 0.f));
        boost::asio::write(socket, boost::asio::buffer(fire));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        n = socket.read_some(boost::asio::buffer(buf), ec);
        if (n > 0) {
            std::cout << "[client] received " << n << " bytes:\n";
            std::string text(buf.data(), buf.data() + n);
            std::cout << text << "\n";
        }

        // Step 4: Another move.
        std::cout << "[client] sending MOVE (300, 200)...\n";
        auto move3 = make_frame(make_move(300.f, 200.f));
        boost::asio::write(socket, boost::asio::buffer(move3));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        n = socket.read_some(boost::asio::buffer(buf), ec);
        if (n > 0) {
            std::cout << "[client] received " << n << " bytes:\n";
            std::string text(buf.data(), buf.data() + n);
            std::cout << text << "\n";
        }

        std::cout << "[client] test sequence complete. closing.\n";
        socket.close();

    } catch (std::exception& e) {
        std::cerr << "[client] error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
