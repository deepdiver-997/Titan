#include <catch2/catch_test_macros.hpp>

#include "gs/net/mock/connection.h"

using namespace gs;

TEST_CASE("MockConnection: feed_data and swap_recv_buffer", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();

    uint8_t data[] = {0x00, 0x00, 0x00, 0x05, 0x02, 0x01, 0x02, 0x03, 0x04};
    conn->feed_data(data, sizeof(data));

    auto buf = conn->swap_recv_buffer();
    REQUIRE(buf.size() == sizeof(data));
    REQUIRE(buf[4] == 0x02);  // message type byte
}

TEST_CASE("MockConnection: feed_data overload with vector", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    conn->feed_data(data);

    auto buf = conn->swap_recv_buffer();
    REQUIRE(buf.size() == 3);
}

TEST_CASE("MockConnection: send captures output", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();

    conn->send({0x01, 0x02, 0x03});
    conn->send({0x04, 0x05});

    REQUIRE(conn->sent_data().size() == 2);
    REQUIRE(conn->sent_data()[0].size() == 3);
    REQUIRE(conn->sent_data()[1].size() == 2);
}

TEST_CASE("MockConnection: clear_sent", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();
    conn->send({0x01});
    REQUIRE(conn->sent_data().size() == 1);
    conn->clear_sent();
    REQUIRE(conn->sent_data().empty());
}

TEST_CASE("MockConnection: close triggers callback", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();
    bool closed = false;
    conn->set_close_callback([&]() { closed = true; });

    conn->close();
    REQUIRE(closed);
    REQUIRE(conn->is_closed());
}

TEST_CASE("MockConnection: remote_addr control", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();
    REQUIRE(conn->remote_addr() == "mock:0");

    conn->set_remote_addr("192.168.1.1:54321");
    REQUIRE(conn->remote_addr() == "192.168.1.1:54321");
}

TEST_CASE("MockConnection: send after close still captures", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();
    conn->close();
    REQUIRE(conn->is_closed());

    // send() still records data even when closed (for test verification)
    conn->send({0xFF});
    REQUIRE(conn->sent_data().size() == 1);
}

TEST_CASE("MockConnection: multiple feed and swap cycles", "[net][mock]") {
    auto conn = std::make_shared<MockConnection>();

    conn->feed_data({0x01});
    REQUIRE(conn->swap_recv_buffer().size() == 1);

    conn->feed_data({0x02, 0x03});
    REQUIRE(conn->swap_recv_buffer().size() == 2);

    // Empty after second swap
    REQUIRE(conn->swap_recv_buffer().empty());
}
