#pragma once

#include "gs/common/types.h"
#include "gs/net/tcp_connection.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gs {

// Manages all active TCP connections (thread-safe).
//
// Used by the tick loop to swap out all recv buffers at the start of a tick,
// and to send responses back to specific players.
class ConnectionManager {
public:
    struct Entry {
        std::shared_ptr<TcpConnection> conn;
        EntityId player_id;
    };

    void add(EntityId player_id, std::shared_ptr<TcpConnection> conn);
    void remove(EntityId player_id);

    // Swap out all recv buffers and return them keyed by player_id.
    // Only entries with non-empty buffers are included.
    std::unordered_map<EntityId, std::vector<uint8_t>> swap_all_buffers();

    // Send data to a specific player.
    void send_to(EntityId player_id, const std::vector<uint8_t>& data);

    // Snapshot of all entries for iteration.
    std::vector<Entry> snapshot() const;

    size_t size() const;

private:
    mutable std::mutex _mutex;
    std::unordered_map<EntityId, Entry> _entries;
};

}  // namespace gs
