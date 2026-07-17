#pragma once

#include "gs/net/protocol/session.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace gs {

// Manages all Sessions and binds new IConnections to the correct
// Session based on the protocol header.
//
// Usage in tick loop:
//   server.schedule_tick(16, [&]() {
//       session_mgr.bind_pending();            // bind new connections
//       for (auto& [id, session] : sessions()) {
//           auto packets = session.drain_all();
//           for (auto& pkt : packets) { ... }  // game logic
//       }
//       sys.swap_all();
//       ...
//   });
class SessionManager {
public:
    // Callback invoked when a Session is fully alive (at least one
    // channel bound) or when a new channel is attached to an existing
    // session. The user should set up their EntityId mapping here.
    using SessionCallback = std::function<void(Session& session)>;
    void set_session_callback(SessionCallback cb) { _session_cb = std::move(cb); }

    // Callback when a session is lost (all channels closed).
    using CloseCallback = std::function<void(SessionId id)>;
    void set_close_callback(CloseCallback cb) { _close_cb = std::move(cb); }

    // Register a raw IConnection. The first packet on this connection
    // must be a bind request [session_id=0][channel][len=0].
    // This method stores the connection; bind_pending() processes it
    // on the next tick (thread-safe).
    void add_connection(std::shared_ptr<IConnection> conn);

    // Process pending connections: read bind request, attach to session.
    // Call this once per tick, before draining sessions.
    void bind_pending();

    // Access sessions.
    Session* find(SessionId id);
    const std::unordered_map<SessionId, Session>& sessions() const { return _sessions; }

    // Remove a session and close all its channels.
    void remove(SessionId id);

    // Generate a new unique SessionId.
    SessionId next_id() { return ++_next_id; }

private:
    SessionId _next_id = 1;
    std::unordered_map<SessionId, Session> _sessions;
    std::vector<std::shared_ptr<IConnection>> _pending;
    SessionCallback _session_cb;
    CloseCallback _close_cb;
};

}  // namespace gs
