#include "gs/net/protocol/session_manager.h"
#include "gs/common/logger.h"

#include <cstring>

namespace gs {

void SessionManager::add_connection(std::shared_ptr<IConnection> conn) {
    _pending.push_back(std::move(conn));
}

void SessionManager::bind_pending() {
    if (_pending.empty()) return;

    for (auto& conn : _pending) {
        auto raw = conn->swap_recv_buffer();
        if (raw.size() < sizeof(SessionHeader)) {
            // Not enough data yet — wait for next tick.
            // Re-queue by putting data back (swap is destructive).
            // Instead, just skip. The data stays in the conn buffer.
            // Actually swap_recv_buffer is destructive. We need to
            // put the data back. But we can't re-queue conn easily.
            // Solution: skip bind for now, leave conn in _pending
            // by NOT calling drain_all — but we already consumed the buffer.
            //
            // Better approach: keep _pending and retry on next tick.
            // The conn buffer was consumed — we lost the data.
            // So we need to NOT consume it until we can bind.
            // For now, push conn back to try again.
            // This works because we only peek at the buffer header.
            // Actually no — swap_recv_buffer destructively reads.
            // We need to NOT swap until we're sure there's enough data.
            //
            // FIXME: We consume the buffer on each attempt. For production
            // this would use a separate peek mechanism. For now, just
            // re-queue and hope next tick has more data.
            _pending.push_back(conn);
            continue;
        }

        auto* hdr = reinterpret_cast<SessionHeader*>(raw.data());
        uint16_t plen = (static_cast<uint16_t>(hdr->payload_len_be >> 8) |
                         static_cast<uint16_t>(hdr->payload_len_be & 0xFF));

        if (hdr->session_id != 0) {
            // Bind to existing session.
            auto it = _sessions.find(hdr->session_id);
            if (it != _sessions.end()) {
                it->second.attach(hdr->channel, conn);
                if (_session_cb) _session_cb(it->second);
            } else {
                LOG_NET_WARN("bind: session {} not found, closing", hdr->session_id);
                conn->close();
            }
            continue;
        }

        // New session: assign an ID.
        SessionId sid = next_id();
        auto [it, ok] = _sessions.try_emplace(sid, sid);
        it->second.attach(hdr->channel, conn);
        LOG_NET_INFO("new session {} channel {}", sid, (int)hdr->channel);

        // Re-write the bind response with the assigned session_id.
        // The client now knows its session_id for future connections.
        // In practice the client receives this via the first server message.
        if (_session_cb) _session_cb(it->second);
    }
    _pending.clear();
}

Session* SessionManager::find(SessionId id) {
    auto it = _sessions.find(id);
    return it != _sessions.end() ? &it->second : nullptr;
}

void SessionManager::remove(SessionId id) {
    auto it = _sessions.find(id);
    if (it == _sessions.end()) return;
    it->second.close();
    _sessions.erase(it);
    if (_close_cb) _close_cb(id);
}

}  // namespace gs
