// Titan Gateway — registration center + health check + client redirect.
//
// Listens on a single TCP port. Message types:
//   FROM GAME NODE:  "PING actors=1,2,3"          → heartbeat + route update
//   FROM CLIENT:     "LOOKUP scene_1"             → return "OK ip:port"
//
// Nodes self-terminate on partition (see architecture.md §13).
#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using boost::asio::ip::tcp;

// ---- Route table entry ---------------------------------------------------
struct NodeInfo {
    std::string addr;                           // "ip:port"
    std::unordered_set<uint64_t> actor_ids;     // actors on this node
    std::chrono::steady_clock::time_point last_ping;
    bool alive = true;
};

// ---- Global state ---------------------------------------------------------
static std::mutex g_mutex;
static std::unordered_map<std::string, NodeInfo> g_nodes;  // addr → info
static std::unordered_map<uint64_t, std::string> g_routes; // actor_id → addr

// ---- Helper: send a length-prefixed string -------------------------------
static void send_str(tcp::socket& sock, const std::string& s) {
    std::vector<uint8_t> data(4 + s.size());
    uint32_t len = static_cast<uint32_t>(s.size());
    data[0] = static_cast<uint8_t>(len >> 24);
    data[1] = static_cast<uint8_t>(len >> 16);
    data[2] = static_cast<uint8_t>(len >> 8);
    data[3] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(data.data() + 4, s.data(), s.size());
    boost::asio::write(sock, boost::asio::buffer(data));
}

// ---- Handle one TCP connection (game node or client) ---------------------
static void handle_conn(tcp::socket sock) {
    auto addr = sock.remote_endpoint().address().to_string() + ":" +
                std::to_string(sock.remote_endpoint().port());
    std::cout << "[gateway] connection from " << addr << std::endl;

    try {
        while (true) {
            uint8_t header[4];
            boost::system::error_code ec;
            size_t n = boost::asio::read(sock, boost::asio::buffer(header), ec);
            if (ec || n < 4) break;
            uint32_t blen = (static_cast<uint32_t>(header[0]) << 24) |
                            (static_cast<uint32_t>(header[1]) << 16) |
                            (static_cast<uint32_t>(header[2]) << 8) |
                            static_cast<uint32_t>(header[3]);
            std::vector<uint8_t> body(blen);
            boost::asio::read(sock, boost::asio::buffer(body), ec);
            if (ec) break;
            std::string msg(body.begin(), body.end());

            // ---- PING from game node: "PING actors=1,2,3" ---------------
            if (msg.rfind("PING ", 0) == 0) {
                std::lock_guard<std::mutex> lk(g_mutex);
                auto& info = g_nodes[addr];
                info.addr = addr;
                info.last_ping = std::chrono::steady_clock::now();

                // Parse actor list.
                auto eq = msg.find('=');
                if (eq != std::string::npos) {
                    std::istringstream iss(msg.substr(eq + 1));
                    std::string token;
                    // Remove old routes for this node.
                    for (auto aid : info.actor_ids)
                        g_routes.erase(aid);
                    info.actor_ids.clear();
                    while (std::getline(iss, token, ',')) {
                        if (token.empty()) continue;
                        uint64_t aid = std::stoull(token);
                        info.actor_ids.insert(aid);
                        g_routes[aid] = addr;
                    }
                }
                if (!info.alive) {
                    info.alive = true;
                    std::cout << "[gateway] node " << addr << " recovered\n";
                }
                send_str(sock, "PONG");
            }
            // ---- LOOKUP from client: "LOOKUP scene_1" -------------------
            else if (msg.rfind("LOOKUP ", 0) == 0) {
                uint64_t actor_id = std::stoull(msg.substr(7));
                std::lock_guard<std::mutex> lk(g_mutex);
                auto it = g_routes.find(actor_id);
                if (it != g_routes.end()) {
                    send_str(sock, "OK " + it->second);
                } else {
                    send_str(sock, "NOT_FOUND");
                }
            }
        }
    } catch (...) {}
    std::cout << "[gateway] " << addr << " disconnected\n";
}

// ---- Health check loop ---------------------------------------------------
static void health_check(boost::asio::io_context& io, std::atomic<bool>& running) {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::lock_guard<std::mutex> lk(g_mutex);
        auto now = std::chrono::steady_clock::now();
        for (auto& [addr, info] : g_nodes) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - info.last_ping)
                               .count();
            if (elapsed > 9 && info.alive) {  // 3 missed pings
                info.alive = false;
                std::cerr << "[gateway] WARN: node " << addr
                          << " unresponsive (" << elapsed << "s)\n";
            }
        }
    }
}

// ---- Main ----------------------------------------------------------------
static std::atomic<bool> g_running{true};
static void sig_handler(int) { g_running.store(false); }

int main(int argc, char* argv[]) {
    uint16_t port = 9000;
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

    std::signal(SIGINT, sig_handler);
    std::cout << "=== Titan Gateway :" << port << " ===\n";
    std::cout << "[gateway] P2P data-plane bypass — gateway does routing only\n";

    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port));

    // Health check thread.
    std::thread health_thread(health_check, std::ref(io), std::ref(g_running));

    // Accept loop.
    while (g_running.load()) {
        boost::system::error_code ec;
        tcp::socket sock = acceptor.accept(ec);
        if (ec) break;
        std::thread(handle_conn, std::move(sock)).detach();
    }

    health_thread.join();
    std::cout << "[gateway] stopped.\n";
    return 0;
}
