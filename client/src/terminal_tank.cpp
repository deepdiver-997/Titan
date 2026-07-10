// Minimal tank client — stdin commands, raw TCP.
// Usage: terminal_tank <host> <port>
// Commands: w/a/s/d to move 5px, f to fire, q to quit.
#include <boost/asio.hpp>

#include <cstring>
#include <iostream>
#include <string>

using boost::asio::ip::tcp;

static std::vector<uint8_t> make_packet(uint8_t type,
                                        const void* data, size_t len) {
    std::vector<uint8_t> payload(1 + len);
    payload[0] = type;
    std::memcpy(payload.data() + 1, data, len);
    uint32_t plen = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> result(4 + plen);
    result[0] = static_cast<uint8_t>(plen >> 24);
    result[1] = static_cast<uint8_t>(plen >> 16);
    result[2] = static_cast<uint8_t>(plen >> 8);
    result[3] = static_cast<uint8_t>(plen & 0xFF);
    std::memcpy(result.data() + 4, payload.data(), plen);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: terminal_tank <host> <port>\n";
        std::cout << "  w/a/s/d  move 5px\n";
        std::cout << "  f        fire forward\n";
        std::cout << "  q        quit\n";
        return 1;
    }

    boost::asio::io_context io;
    tcp::socket socket(io);
    tcp::resolver resolver(io);
    boost::asio::connect(socket, resolver.resolve(argv[1], argv[2]));

    std::cout << "Connected. w/a/s/d move, f fire, q quit\n";

    float x = 100, y = 100;
    float facing_dx = 0, facing_dy = -1;

    // Non-blocking read thread for server messages.
    std::thread reader([&]() {
        uint8_t header[4];
        std::vector<uint8_t> body;
        while (true) {
            boost::system::error_code ec;
            size_t n = boost::asio::read(socket,
                                         boost::asio::buffer(header), ec);
            if (ec || n < 4) break;
            uint32_t blen = (static_cast<uint32_t>(header[0]) << 24) |
                            (static_cast<uint32_t>(header[1]) << 16) |
                            (static_cast<uint32_t>(header[2]) << 8) |
                            static_cast<uint32_t>(header[3]);
            body.resize(blen);
            boost::asio::read(socket, boost::asio::buffer(body), ec);
            if (ec) break;
            std::string text(body.begin(), body.end());
            std::cout << "<<< " << text << std::endl;
        }
    });

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "q") break;

        if (line == "w") {
            y -= 5; facing_dx = 0; facing_dy = -1;
            auto pkt = make_packet(0x02, &x, 8);
            boost::asio::write(socket, boost::asio::buffer(pkt));
            std::cout << ">>> MOVE (" << x << "," << y << ")\n";
        } else if (line == "s") {
            y += 5; facing_dx = 0; facing_dy = 1;
            auto pkt = make_packet(0x02, &x, 8);
            boost::asio::write(socket, boost::asio::buffer(pkt));
            std::cout << ">>> MOVE (" << x << "," << y << ")\n";
        } else if (line == "a") {
            x -= 5; facing_dx = -1; facing_dy = 0;
            auto pkt = make_packet(0x02, &x, 8);
            boost::asio::write(socket, boost::asio::buffer(pkt));
            std::cout << ">>> MOVE (" << x << "," << y << ")\n";
        } else if (line == "d") {
            x += 5; facing_dx = 1; facing_dy = 0;
            auto pkt = make_packet(0x02, &x, 8);
            boost::asio::write(socket, boost::asio::buffer(pkt));
            std::cout << ">>> MOVE (" << x << "," << y << ")\n";
        } else if (line == "f") {
            float dir[2] = {facing_dx, facing_dy};
            auto pkt = make_packet(0x06, dir, 8);
            boost::asio::write(socket, boost::asio::buffer(pkt));
            std::cout << ">>> FIRE dir=(" << facing_dx << "," << facing_dy << ")\n";
        }
    }

    socket.close();
    reader.detach();
    return 0;
}
