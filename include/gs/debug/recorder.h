#pragma once

#include "gs/debug/trace_event.h"

#include <atomic>
#include <string>
#include <vector>

namespace gs {

class ActorSystem;
class TitanServer;

namespace debug {

// ---- Recorder — records external inputs for deterministic replay ----------
//
// Records everything a deterministic replay needs:
//   - TCP packets received (with entity_id = player_id)
//   - PeerManager Actor messages from other nodes
//   - Actor spawn events (for replay setup)
//
// Not recorded (deterministically derivable):
//   - Internal Actor→Actor messages
//   - TimingWheel tick events
//   - Actor processing order
class Recorder {
public:
    static Recorder& instance();

    void start();
    void stop();
    void clear();
    bool is_recording() const { return _recording.load(); }

    // Record entry points.
    void record_tcp_packet(uint64_t player_id, const uint8_t* data,
                           size_t len, uint64_t tick_counter);
    void record_peer_message(uint64_t node_id, const uint8_t* data,
                             size_t len, uint64_t tick_counter);
    void record_actor_spawned(uint64_t actor_id, const std::string& name);

    // Record a message push to an Actor's mailbox.
    void record_mailbox_push(uint64_t actor_id, uint32_t tick);

    // Persist to file.
    void save(const std::string& path) const;
    void load(const std::string& path);

    const std::vector<RecordedEvent>& events() const { return _events; }

private:
    Recorder() = default;

    std::atomic<bool> _recording{false};
    std::vector<RecordedEvent> _events;
};

// ---- Replay helpers -------------------------------------------------------
//
// tick_run(): execute a deterministic replay from a known snapshot.
//
// Usage:
//   auto snap = read_snapshot("init.snap");
//   auto events = read_events("session.trace");
//   server.reload_state(snap, events);  // disaster recovery
//
}  // namespace debug
}  // namespace gs
