#include <catch2/catch_test_macros.hpp>

#include "gs/net/recv_buffer.h"

using namespace gs;

TEST_CASE("RecvBuffer: append and swap_out", "[net][recv_buffer]") {
    RecvBuffer buf;

    REQUIRE(buf.size() == 0);

    uint8_t data[] = {0x01, 0x02, 0x03};
    buf.append(data, 3);
    REQUIRE(buf.size() == 3);

    auto out = buf.swap_out();
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == 0x01);
    REQUIRE(out[1] == 0x02);
    REQUIRE(out[2] == 0x03);

    // Buffer is empty after swap_out.
    REQUIRE(buf.size() == 0);

    // swap_out on empty buffer returns empty vector.
    auto empty = buf.swap_out();
    REQUIRE(empty.empty());
}

TEST_CASE("RecvBuffer: multiple appends", "[net][recv_buffer]") {
    RecvBuffer buf;
    buf.append(reinterpret_cast<const uint8_t*>("hello"), 5);
    buf.append(reinterpret_cast<const uint8_t*>("world"), 5);
    REQUIRE(buf.size() == 10);

    auto out = buf.swap_out();
    REQUIRE(out.size() == 10);
    REQUIRE(std::string(out.begin(), out.end()) == "helloworld");
}

TEST_CASE("RecvBuffer: append zero bytes", "[net][recv_buffer]") {
    RecvBuffer buf;
    buf.append(nullptr, 0);
    REQUIRE(buf.size() == 0);
}
