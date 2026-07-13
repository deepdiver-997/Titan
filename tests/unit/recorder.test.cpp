#include <catch2/catch_test_macros.hpp>

#include "gs/debug/recorder.h"
#include "gs/debug/trace_event.h"

#include <cstring>
#include <fstream>

using namespace gs::debug;

TEST_CASE("Recorder: record and retrieve TCP packets", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    uint8_t data[] = {0x00, 0x00, 0x00, 0x05, 0x02, 0x10, 0x20, 0x30, 0x40};
    rec.record_tcp_packet(42, data, sizeof(data), 1);

    rec.stop();

    REQUIRE(rec.events().size() == 1);
    auto& ev = rec.events()[0];
    REQUIRE(ev.type == RecordedEvent::TcpPacketIn);
    REQUIRE(ev.tick_counter == 1);
    REQUIRE(ev.entity_id == 42);
    REQUIRE(ev.data.size() == sizeof(data));
    REQUIRE(ev.data[5] == 0x10);
}

TEST_CASE("Recorder: multiple events ordered by tick", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    rec.record_tcp_packet(1, nullptr, 0, 3);
    rec.record_tcp_packet(2, nullptr, 0, 1);
    rec.record_tcp_packet(3, nullptr, 0, 2);

    rec.stop();

    REQUIRE(rec.events().size() == 3);
}

TEST_CASE("Recorder: does not record when stopped", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    // Don't call start()

    rec.record_tcp_packet(1, nullptr, 0, 0);

    REQUIRE(rec.events().empty());
}

TEST_CASE("Recorder: clear after recording", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    rec.record_tcp_packet(1, nullptr, 0, 0);
    REQUIRE(rec.events().size() == 1);

    rec.clear();
    REQUIRE(rec.events().empty());
}

TEST_CASE("Recorder: peer messages recorded", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    rec.record_peer_message(100, nullptr, 0, 5);

    rec.stop();
    REQUIRE(rec.events().size() == 1);
    REQUIRE(rec.events()[0].type == RecordedEvent::PeerActorMsg);
    REQUIRE(rec.events()[0].tick_counter == 5);
    REQUIRE(rec.events()[0].entity_id == 100);
}

TEST_CASE("Recorder: record_actor_spawned", "[debug][recorder]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    rec.record_actor_spawned(42, "test_actor");

    rec.stop();
    auto& ev = rec.events()[0];
    REQUIRE(ev.type == RecordedEvent::ActorSpawned);
    REQUIRE(ev.entity_id == 42);
    REQUIRE(std::string(ev.data.begin(), ev.data.end()) == "test_actor");
}

// ---- Serialization round-trip tests ---------------------------------------

TEST_CASE("Recorder: save and load events", "[debug][recorder][io]") {
    auto& rec = Recorder::instance();
    rec.clear();
    rec.start();

    uint8_t payload[] = {0xAA, 0xBB};
    rec.record_tcp_packet(10, payload, 2, 7);
    rec.record_peer_message(20, payload, 2, 8);
    rec.stop();

    const auto path = "test_events.bin";
    rec.save(path);

    auto& rec2 = Recorder::instance();
    rec2.load(path);
    REQUIRE(rec2.events().size() == 2);
    REQUIRE(rec2.events()[0].entity_id == 10);
    REQUIRE(rec2.events()[1].entity_id == 20);

    std::remove(path);
}

TEST_CASE("Snapshot I/O: write and read", "[debug][snapshot][io]") {
    ServerSnapshot snap;
    snap.tick_counter = 42;

    ActorStateEntry entry;
    entry.actor_id = 1;
    entry.name = "test_actor";
    entry.active = true;
    entry.user_data = {0x01, 0x02, 0x03};
    snap.actors.push_back(entry);

    const auto path = "test_snapshot.bin";
    write_snapshot(snap, path);

    auto loaded = read_snapshot(path);
    REQUIRE(loaded.tick_counter == 42);
    REQUIRE(loaded.actors.size() == 1);
    REQUIRE(loaded.actors[0].actor_id == 1);
    REQUIRE(loaded.actors[0].name == "test_actor");
    REQUIRE(loaded.actors[0].active == true);
    REQUIRE(loaded.actors[0].user_data.size() == 3);

    std::remove(path);
}
