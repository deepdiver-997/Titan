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
        std::shared_ptr<Session> sp;

        if (hdr->session_id != 0) {
            // Re-bind to existing session.
            std::lock_guard lk(_mgr_mutex);
            auto it = _sessions.find(hdr->session_id);
            if (it == _sessions.end()) {
                LOG_NET_WARN("bind: session {} not found", hdr->session_id);
                conn->close();
                continue;
            }
            it->second->attach(hdr->channel, conn);
            sid = hdr->session_id;
            sp = it->second;
        } else {
            // New session.
            std::lock_guard lk(_mgr_mutex);
            sid = next_id();
            sp = std::make_shared<Session>(sid);
            sp->attach(hdr->channel, conn);
            _sessions[sid] = sp;
            LOG_NET_INFO("new session {} channel {}", sid, (int)hdr->channel);
        }

        // Callback outside lock, passing shared_ptr.
        if (_session_cb && sp) {
            _session_cb(sp);
        }
    }
}

void SessionManager::bind_pending_framed() {
    decltype(_pending) local;
    {
        std::lock_guard lk(_mgr_mutex);
        local.swap(_pending);
    }
    if (local.empty()) return;

    for (auto& conn : local) {
        auto framed = conn->swap_recv_buffer();

        // Strip TcpConnection's [4B len][body] framing.
        std::vector<uint8_t> raw;
        size_t off = 0;
        while (off + 4 <= framed.size()) {
            uint32_t flen =
                (static_cast<uint32_t>(framed[off]) << 24) |
                (static_cast<uint32_t>(framed[off+1]) << 16) |
                (static_cast<uint32_t>(framed[off+2]) << 8) |
                static_cast<uint32_t>(framed[off+3]);
            off += 4;
            if (off + flen > framed.size()) break;
            raw.insert(raw.end(), framed.begin() + off,
                       framed.begin() + off + flen);
            off += flen;
        }

        if (raw.size() < sizeof(SessionHeader)) {
            std::lock_guard lk(_mgr_mutex);
            _pending.push_back(conn);
            continue;
        }

        auto* hdr = reinterpret_cast<SessionHeader*>(raw.data());
        SessionId sid = INVALID_SESSION_ID;
        std::shared_ptr<Session> sp;

        if (hdr->session_id != 0) {
            std::lock_guard lk(_mgr_mutex);
            auto it = _sessions.find(hdr->session_id);
            if (it == _sessions.end()) {
                LOG_NET_WARN("bind: session {} not found", hdr->session_id);
                conn->close();
                continue;
            }
            it->second->attach(hdr->channel, conn);
            sid = hdr->session_id;
            sp = it->second;
        } else {
            std::lock_guard lk(_mgr_mutex);
            sid = next_id();
            sp = std::make_shared<Session>(sid);
            sp->attach(hdr->channel, conn);
            _sessions[sid] = sp;
            LOG_NET_INFO("new session {} channel {}", sid, (int)hdr->channel);
        }

        if (_session_cb && sp) {
            _session_cb(sp);
        }
    }
}

std::shared_ptr<Session> SessionManager::find(SessionId id) {
    std::lock_guard lk(_mgr_mutex);
    auto it = _sessions.find(id);
    return it != _sessions.end() ? it->second : nullptr;
}

void SessionManager::remove(SessionId id) {
    std::shared_ptr<Session> sp;
    {
        std::lock_guard lk(_mgr_mutex);
        auto it = _sessions.find(id);
        if (it == _sessions.end()) return;
        sp = it->second;
        _sessions.erase(it);
    }
    // Close connections outside lock; shared_ptr keeps Session alive.
    sp->close();
    if (_close_cb) _close_cb(id);
}

void SessionManager::for_each(std::function<void(Session&)> cb) {
    std::vector<std::shared_ptr<Session>> snapshot;
    {
        std::lock_guard lk(_mgr_mutex);
        snapshot.reserve(_sessions.size());
        for (auto& [sid, sp] : _sessions) snapshot.push_back(sp);
    }
    for (auto& sp : snapshot) {
        cb(*sp);
    }
}

}  // namespace gs
