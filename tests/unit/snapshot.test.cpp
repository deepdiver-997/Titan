#include <catch2/catch_test_macros.hpp>

#include "gs/debug/snapshot.h"

#include <cstring>

using namespace gs::debug;

TEST_CASE("SnapshotWriter/Reader: round-trip scalars", "[debug][snapshot]") {
    SnapshotWriter w;
    w.write_u8(0xAB);
    w.write_u32(123456);
    w.write_u64(0xDEADBEEFCAFEBABEULL);
    w.write_string("hello");

    SnapshotReader r(w.data());
    REQUIRE(r.read_u8() == 0xAB);
    REQUIRE(r.read_u32() == 123456);
    REQUIRE(r.read_u64() == 0xDEADBEEFCAFEBABEULL);
    REQUIRE(r.read_string() == "hello");
}

TEST_CASE("SnapshotWriter/Reader: round-trip bytes", "[debug][snapshot]") {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04};

    SnapshotWriter w;
    w.write_bytes(buf, 4);

    SnapshotReader r(w.data());
    uint8_t out[4];
    r.read_bytes(out, 4);
    REQUIRE(memcmp(buf, out, 4) == 0);
}

TEST_CASE("SnapshotWriter/Reader: empty string", "[debug][snapshot]") {
    SnapshotWriter w;
    w.write_string("");

    SnapshotReader r(w.data());
    REQUIRE(r.read_string().empty());
}

TEST_CASE("SnapshotWriter/Reader: empty data", "[debug][snapshot]") {
    SnapshotWriter w;
    auto data = w.take_data();
    REQUIRE(data.empty());
}

TEST_CASE("SnapshotWriter/Reader: exhausted check", "[debug][snapshot]") {
    SnapshotWriter w;
    w.write_u8(42);

    SnapshotReader r(w.data());
    REQUIRE_FALSE(r.exhausted());
    REQUIRE(r.read_u8() == 42);
    REQUIRE(r.exhausted());
}
