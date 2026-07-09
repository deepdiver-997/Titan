#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

// Simple length-prefixed protocol (matches server).
static std::vector<uint8_t> make_message(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> result;
    uint32_t len = static_cast<uint32_t>(payload.size());
    result.resize(4 + len);
    result[0] = static_cast<uint8_t>((len >> 24) & 0xFF);
    result[1] = static_cast<uint8_t>((len >> 16) & 0xFF);
    result[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    result[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(result.data() + 4, payload.data(), len);
    return result;
}

static std::vector<uint8_t> make_move_msg(float x, float y) {
    std::vector<uint8_t> payload(9);
    payload[0] = 0x02;  // MsgType::Move
    std::memcpy(payload.data() + 1, &x, 4);
    std::memcpy(payload.data() + 5, &y, 4);
    return make_message(payload);
}

// Simulates a single player.
void simulate_player(int player_id, const std::string& host,
                     const std::string& port,
                     std::atomic<bool>& running) {
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);
        tcp::resolver resolver(io);
        boost::asio::connect(socket, resolver.resolve(host, port));

        std::cout << "[player " << player_id << "] connected" << std::endl;

        // Random movement generator.
        std::mt19937 rng(player_id);
        std::uniform_real_distribution<float> dir(-2.0f, 2.0f);
        float x = 100.0f + player_id * 50.0f;
        float y = 100.0f + player_id * 30.0f;

        while (running.load()) {
            // Move randomly.
            x += dir(rng);
            y += dir(rng);
            if (x < 0) x = 0;
            if (y < 0) y = 0;

            auto msg = make_move_msg(x, y);
            boost::asio::write(socket, boost::asio::buffer(msg));

            // Try to read any server messages (AOI events, etc.).
            uint8_t header[4];
            boost::system::error_code ec;
            size_t n = socket.read_some(boost::asio::buffer(header), ec);
            if (!ec && n == 4) {
                uint32_t body_len =
                    (static_cast<uint32_t>(header[0]) << 24) |
                    (static_cast<uint32_t>(header[1]) << 16) |
                    (static_cast<uint32_t>(header[2]) << 8) |
                    static_cast<uint32_t>(header[3]);
                std::vector<uint8_t> body(body_len);
                boost::asio::read(socket, boost::asio::buffer(body));
                // Print AOI events.
                if (!body.empty() && body[0] == 0x04) {
                    auto event_type = body[1];
                    uint64_t entity_id;
                    std::memcpy(&entity_id, body.data() + 2, 8);
                    const char* type_str = event_type == 0   ? "ENTER"
                                           : event_type == 1 ? "LEAVE"
                                                             : "MOVE";
                    std::cout << "[player " << player_id << "] " << type_str
                              << " entity=" << entity_id << std::endl;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30Hz
        }

        socket.close();
    } catch (const std::exception& e) {
        std::cerr << "[player " << player_id << "] error: " << e.what()
                  << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: titan_client <host> <port> <num_players>"
                  << std::endl;
        std::cout << "Example: titan_client 127.0.0.1 8888 5" << std::endl;
        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];
    int num_players = std::stoi(argv[3]);

    std::cout << "=== Titan Client Simulator ===" << std::endl;
    std::cout << "Connecting " << num_players << " players to " << host << ":"
              << port << std::endl;

    std::atomic<bool> running{true};
    std::vector<std::thread> players;

    for (int i = 0; i < num_players; ++i) {
        players.emplace_back(simulate_player, i + 1, host, port,
                             std::ref(running));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // stagger
    }

    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    running.store(false);

    for (auto& t : players) {
        t.join();
    }

    std::cout << "All players disconnected." << std::endl;
    return 0;
}
