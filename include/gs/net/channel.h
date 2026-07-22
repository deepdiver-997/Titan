#pragma once

#include "gs/net/protocol/fwd.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace gs {

class Session;

// Application-level output channel bound to a Session.
//
// Each Channel is tied to one connection slot (0=reliable, 1=unreliable)
// of a Session. The Channel holds a weak_ptr<Session> — if the Session
// is destroyed (e.g. client disconnect), flush() silently drops buffered
// data. The Session has no knowledge of Channels.
//
// Writes are thread-safe (internal mutex). Flush is called by a timer
// task registered with TitanServer's IO frequency group.
//
// Typical usage:
//   auto ch = std::make_shared<Channel>(session_sp, 0);
//   server.add_to_io_group(io_grp, ch);
//
//   // From any thread / actor callback:
//   ch->write(payload);
//
//   // On shutdown:
//   server.remove_from_io_group(io_grp, ch);
class Channel {
public:
    // session must be a valid shared_ptr (usually from SessionManager).
    // Channel stores a weak_ptr — safe if Session outlives the shared_ptr.
    Channel(std::shared_ptr<Session> session, int channel_idx);

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Thread-safe: append data to the internal buffer.
    void write(const std::vector<uint8_t>& data);
    void write(std::vector<uint8_t>&& data);

    // Called by the IO timer task. Swaps the internal buffer under
    // lock, then attempts to lock the weak_ptr<Session> and send.
    // If the session has been destroyed, data is silently dropped.
    void flush();

    SessionId session_id() const { return _sid; }
    int channel_idx() const { return _channel_idx; }

private:
    std::weak_ptr<Session> _session;  // safe cross-thread access
    SessionId _sid;                   // cached for logging / identification
    int _channel_idx;                 // 0=reliable, 1=unreliable

    std::mutex _buf_mutex;
    std::vector<uint8_t> _buffer;
};

}  // namespace gs
