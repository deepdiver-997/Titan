#include "gs/net/protocol/session_manager.h"
#include "gs/common/logger.h"

#include <cstring>

namespace gs {

void SessionManager::add_connection(std::shared_ptr<IConnection> conn) {
    std::lock_guard lk(_mgr_mutex);
    _pending.push_back(std::move(conn));
}

void SessionManager::bind_pending() {
    // Swap under lock, process outside so callbacks can safely
    // call add_connection() without deadlock.
    decltype(_pending) local;
    {
        std::lock_guard lk(_mgr_mutex);
        local.swap(_pending);
    }
    if (local.empty()) return;

    for (auto& conn : local) {
        auto raw = conn->swap_recv_buffer();
        if (raw.size() < sizeof(SessionHeader)) {
            // Not enough data yet — re-queue for next tick.
            std::lock_guard lk(_mgr_mutex);
            _pending.push_back(conn);
            continue;
        }

        auto* hdr = reinterpret_cast<SessionHeader*>(raw.data());
        SessionId sid = INVALID_SESSION_ID;

        if (hdr->session_id != 0) {
            // Re-bind to existing session.
            std::lock_guard lk(_mgr_mutex);
            auto it = _sessions.find(hdr->session_id);
            if (it == _sessions.end()) {
                LOG_NET_WARN("bind: session {} not found", hdr->session_id);
                conn->close();
                continue;
            }
            it->second.attach(hdr->channel, conn);
            sid = hdr->session_id;
        } else {
            // New session.
            std::lock_guard lk(_mgr_mutex);
            sid = next_id();
            auto [it, ok] = _sessions.try_emplace(sid, sid);
            it->second.attach(hdr->channel, conn);
            LOG_NET_INFO("new session {} channel {}", sid, (int)hdr->channel);
        }

        // Callback outside lock.
        if (_session_cb && sid != INVALID_SESSION_ID) {
            auto* s = find(sid);
            if (s) _session_cb(*s);
        }
    }
}

Session* SessionManager::find(SessionId id) {
    std::lock_guard lk(_mgr_mutex);
    auto it = _sessions.find(id);
    return it != _sessions.end() ? &it->second : nullptr;
}

void SessionManager::remove(SessionId id) {
    std::lock_guard lk(_mgr_mutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end()) return;
    it->second.close();
    _sessions.erase(it);
    if (_close_cb) _close_cb(id);
}

void SessionManager::for_each(std::function<void(Session&)> cb) {
    std::vector<SessionId> ids;
    {
        std::lock_guard lk(_mgr_mutex);
        ids.reserve(_sessions.size());
        for (auto& [sid, _] : _sessions) ids.push_back(sid);
    }
    for (auto sid : ids) {
        auto* s = find(sid);
        if (s) cb(*s);
    }
}

}  // namespace gs
