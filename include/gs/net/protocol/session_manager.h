#pragma once

#include "gs/net/protocol/session.h"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gs {

// Thread-safe Session lifecycle manager. Multiple actors on different
// threads can share one SessionManager — all internal state is mutex-
// protected.
//
// Typical usage:
//   1. Transport callback → session_mgr.add_connection(conn)
//   2. Per tick: bind_pending(callback) — binds new connections
//   3. Per tick: send/receive via Session acquired from find()
//   4. On close: remove(id)
class SessionManager {
public:
    // Callback invoked during bind_pending() when a new channel is
    // bound. The user should create their EntityId mapping here.
    // WARNING: called WITHOUT the internal mutex held — safe to call
    // add_connection() from within this callback.
    using SessionCallback = std::function<void(Session& session)>;
    void set_session_callback(SessionCallback cb) { _session_cb = std::move(cb); }

    // Callback invoked during remove() when a session is closed.
    using CloseCallback = std::function<void(SessionId id)>;
    void set_close_callback(CloseCallback cb) { _close_cb = std::move(cb); }

    // Register a raw IConnection (thread-safe, called from any thread).
    // The first packet must be a bind request [session_id=0][channel][len=0].
    void add_connection(std::shared_ptr<IConnection> conn);

    // Process pending connections. Calls _session_cb for each new/changed
    // session. The user should drain sessions in the callback.
    void bind_pending();

    // Find a session by ID (thread-safe). Returns nullptr if not found.
    Session* find(SessionId id);

    // Remove and close a session (thread-safe).
    void remove(SessionId id);

    // Generate a new unique SessionId (thread-safe).
    SessionId next_id() { return ++_next_id; }

    // Iterate all sessions with a callback (thread-safe, snapshot under lock).
    void for_each(std::function<void(Session&)> cb);

private:
    std::mutex _mgr_mutex;
    SessionId _next_id = 1;
    std::unordered_map<SessionId, Session> _sessions;
    std::vector<std::shared_ptr<IConnection>> _pending;
    SessionCallback _session_cb;
    CloseCallback _close_cb;
};

}  // namespace gs
