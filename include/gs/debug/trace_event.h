#pragma once

#include <cstdint>
#include <vector>

namespace gs::debug {

// ---- RecordedEvent — a single external input event -----------------------
//
// Only external (non-deterministic) inputs are recorded:
//   - TCP packets received from clients
//   - PeerManager messages from other server nodes
//
// Internal Actor→Actor messages are NOT recorded — they are deterministically
// derivable from the initial state + external inputs.
struct RecordedEvent {
    enum Type : uint8_t {
        TcpPacketIn = 0,       // TCP input from a client connection
        PeerActorMsg = 1,      // Inter-node Actor message
        ActorSpawned = 2,      // New Actor created (for replay setup)
    };

    Type type;
    uint64_t tick_counter;     // monotonic tick counter at capture time
    uint64_t entity_id;        // player / node identifier
    std::vector<uint8_t> data; // raw payload
};

// ---- Snapshot — point-in-time copy of all Actor states -------------------
//
// A snapshot captures:
//   - Every Actor's state (via virtual capture_state())
//   - The configuration
//   - The tick counter
//
// It does NOT capture external network state (TCP connections, peer links)
// because those are re-established during replay from recorded events.
struct ActorStateEntry {
    uint64_t actor_id;
    std::string name;
    bool active;
    std::vector<uint8_t> user_data; // serialized by Actor::capture_state()
};

struct ServerSnapshot {
    uint64_t tick_counter;
    std::vector<ActorStateEntry> actors;
    // Config is embedded in the replay file once — not per snapshot.
};

// ---- Serialization helpers (simple binary, no external dependency) -------
//
// Custom binary format (big-endian where applicable):
//   [snapshot header][actor count][actor entries...]
//
// Actor entry:
//   [aid 8B][name_len 2B][name...][active 1B][user_data_len 4B][user_data...]
//
void write_snapshot(ServerSnapshot& snap, const std::string& path);
ServerSnapshot read_snapshot(const std::string& path);

void write_events(const std::vector<RecordedEvent>& events,
                  const std::string& path);
std::vector<RecordedEvent> read_events(const std::string& path);

}  // namespace gs::debug
