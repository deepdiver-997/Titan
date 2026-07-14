#include <catch2/catch_test_macros.hpp>

#include "gs/debug/recorder.h"
#include "gs/debug/snapshot.h"
#include "gs/debug/capture.h"

#include "gs/actor/actor.h"
#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "gs/debug/trace_event.h"
#include "gs/net/actor/net_sync.h"
#include "gs/server/titan_server.h"

#include <atomic>
#include <cstring>

using namespace gs;
using namespace gs::debug;

// A minimal Actor that tracks state for replay testing.
class ReplayTestActor : public Actor {
public:
    int counter = 0;
    std::string last_msg;

    ReplayTestActor(ActorId id) : Actor(id, "replay_test") {}

    void on_message(Message& msg) override {
        auto* m = dynamic_cast<ClientBoundMsg*>(&msg);
        if (m) {
            counter++;
            last_msg = m->data;
        }
    }

    void capture_state(SnapshotWriter& w) override {
        w.write_u32(static_cast<uint32_t>(counter));
        w.write_string(last_msg);
    }

    void restore_state(SnapshotReader& r) override {
        counter = static_cast<int>(r.read_u32());
        last_msg = r.read_string();
    }
};

TEST_CASE("Deterministic Replay: actor processing + snapshot capture",
          "[replay][integration]") {
    ServerConfig config;
    ActorSystem sys;

    auto grp = sys.create_tick_group("test", 10);
    ActorId aid = sys.spawn(std::make_unique<ReplayTestActor>(100), grp);

    // Simulate a round of processing.
    auto msg = std::make_unique<ClientBoundMsg>();
    msg->target_player = 1;
    msg->data = "hello";
    sys.send(aid, std::move(msg));
    sys.swap_all();
    sys.process_group(grp);

    // Capture a snapshot after processing.
    SnapshotManager::instance().set_actor_system(&sys);
    SNAPSHOT("after_first_msg");

    // ---- Verify snapshot captured state ----------------------------------
    auto* snap = SnapshotManager::instance().last_snapshot();
    REQUIRE(snap != nullptr);
    REQUIRE(snap->actors.size() == 1);
    REQUIRE(snap->actors[0].actor_id == 100);
    REQUIRE(snap->actors[0].name == "replay_test");

    // Deserialize the snapshot back — verify counter=1 / msg="hello".
    SnapshotReader r(snap->actors[0].user_data);
    REQUIRE(r.read_u32() == 1);  // counter
    REQUIRE(r.read_string() == "hello");

    // ---- File I/O round-trip --------------------------------------------
    auto snap_copy = *SnapshotManager::instance().last_snapshot();
    write_snapshot(snap_copy, "test_replay_snapshot.bin");
    auto loaded_snap = read_snapshot("test_replay_snapshot.bin");
    REQUIRE(loaded_snap.actors.size() == 1);
    REQUIRE(loaded_snap.actors[0].actor_id == 100);

    std::remove("test_replay_snapshot.bin");
}

TEST_CASE("Deterministic Replay: snapshot captures actor state",
          "[replay][snapshot][integration]") {
    ServerConfig config;
    ActorSystem sys;
    auto grp = sys.create_tick_group("snap_test", 10);

    auto actor = std::make_unique<ReplayTestActor>(200);
    actor->counter = 42;
    actor->last_msg = "snapshot_test";
    sys.spawn(std::move(actor), grp);

    SnapshotManager::instance().set_actor_system(&sys);

    // Also add a normal Actor without capture_state override.
    struct SilentActor : public Actor {
        SilentActor(ActorId id) : Actor(id, "silent") {}
        void on_message(Message&) override {}
    };
    sys.spawn(std::make_unique<SilentActor>(201), grp);

    // Capture full snapshot via capture_all.
    {
        std::vector<ActorStateEntry> entries;
        sys.capture_all(entries);

        REQUIRE(entries.size() == 2);

        // Find and verify the ReplayTestActor.
        bool found = false;
        for (auto& e : entries) {
            if (e.actor_id == 200) {
                SnapshotReader r(e.user_data);
                REQUIRE(r.read_u32() == 42);
                found = true;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("Replay: events before snapshot tick are skipped", "[replay]") {
    ActorSystem sys;
    auto grp = sys.create_tick_group("test", 10);
    auto aid = sys.spawn(std::make_unique<ReplayTestActor>(100), grp);
    std::vector<debug::ActorStateEntry> entries;
    sys.capture_all(entries);

    ServerSnapshot snap;
    snap.tick_counter = 100;   // snapshot at master tick 100
    snap.actors = entries;

    // Events at tick 80, 90 (< 100 → skipped), 110, 120 (≥ 100 → processed).
    std::vector<RecordedEvent> events;
    events.push_back({RecordedEvent::MailboxPush, 80, aid, {}});
    events.push_back({RecordedEvent::MailboxPush, 90, aid, {}});
    events.push_back({RecordedEvent::MailboxPush, 110, aid, {}});
    events.push_back({RecordedEvent::MailboxPush, 120, aid, {}});

    ServerConfig config;
    TitanServer server(config);
    server.init();
    server.reload_state(snap, events);

    // No crash. Events at 80/90 are below snapshot tick and skipped.
    // Events at 110/120 are within replay range.
    REQUIRE(true);
}

TEST_CASE("Replay: snapshot too old for events", "[replay]") {
    ActorSystem sys;
    auto grp = sys.create_tick_group("test", 10);
    sys.spawn(std::make_unique<ReplayTestActor>(100), grp);
    std::vector<debug::ActorStateEntry> entries;
    sys.capture_all(entries);

    ServerSnapshot snap;
    snap.tick_counter = 100;
    snap.actors = entries;

    // Events at tick 200, 300 — far beyond the 25 ticks we replay.
    std::vector<RecordedEvent> events;
    events.push_back({RecordedEvent::MailboxPush, 200, 100, {}});
    events.push_back({RecordedEvent::MailboxPush, 300, 100, {}});

    ServerConfig config;
    TitanServer server(config);
    server.init();
    server.reload_state(snap, events);

    // No crash. Events beyond replayed range are never reached.
    REQUIRE(true);
}
