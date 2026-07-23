#pragma once

#include "gs/net/protocol/fwd.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace gs {

class Session;
struct DirtySink;

// Application-level output channel bound to a Session.
//
// Two write modes (set at construction, immutable):
//   Append   — RPC, inventory, chat: data accumulates, never lost
//   Overwrite — position, velocity, state sync: only latest matters
//
// Flush frequency is set at construction via DirtySink*. On first write
// after a flush, the channel CAS-sets _dirty and enqueues a weak_ptr into
// the sink. NetSubsystem periodically swaps the sink's dirty list and
// calls flush_and_reset() on each still-alive channel.
//
// Channel lifecycle is managed by the owning entity (shared_ptr). When
// the entity destroys its Channel, weak_ptrs in the dirty list silently
// expire — no unregistration needed.
//
// Typical usage:
//   auto sink = server.net().get_sink(33);  // ~30 Hz flush group
//   auto ch = std::make_shared<Channel>(session, 0, Append, sink);
//   ch->write(data);
class Channel : public std::enable_shared_from_this<Channel> {
public:
    enum class WriteMode { Append, Overwrite };

    Channel(std::shared_ptr<Session> session, int conn_slot,
            WriteMode mode, DirtySink* sink);

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // ---- Write (thread-safe) -------------------------------------------
    //
    // Append mode:  data appends to the internal buffer.
    // Overwrite mode: buffer is replaced (old data discarded).
    //
    // On first write since last flush, CAS-sets _dirty and enqueues a
    // weak_ptr into _sink.
    void write(const std::vector<uint8_t>& data);
    void write(std::vector<uint8_t>&& data);

    // ---- Flush (called by NetSubsystem timer task) ---------------------
    //
    // Resets _dirty, checks connection validity, swaps buffer, sends.
    // Returns false if the underlying connection is dead.
    bool flush_and_reset();

    SessionId session_id() const { return _sid; }
    int conn_slot() const { return _conn_slot; }
    WriteMode mode() const { return _mode; }

private:
    std::weak_ptr<Session> _session;
    SessionId _sid;
    int _conn_slot;
    WriteMode _mode;

    // Which flush group to enqueue into on first write.
    DirtySink* _sink = nullptr;

    std::atomic<bool> _dirty{false};
    std::mutex _buf_mutex;
    std::vector<uint8_t> _buffer;
};

}  // namespace gs
