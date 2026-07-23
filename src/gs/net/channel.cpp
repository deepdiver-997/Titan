#include "gs/net/channel.h"
#include "gs/net/net_subsystem.h"
#include "gs/net/protocol/session.h"

namespace gs {

Channel::Channel(std::shared_ptr<Session> session, int conn_slot,
                 WriteMode mode, DirtySink* sink)
    : _session(session)
    , _sid(session->id())
    , _conn_slot(conn_slot)
    , _mode(mode)
    , _sink(sink) {}

void Channel::write(const std::vector<uint8_t>& data) {
    bool was_dirty = _dirty.exchange(true, std::memory_order_acq_rel);
    {
        std::lock_guard lk(_buf_mutex);
        if (_mode == WriteMode::Overwrite) {
            _buffer = data;
        } else {
            _buffer.insert(_buffer.end(), data.begin(), data.end());
        }
    }
    if (!was_dirty && _sink) {
        std::lock_guard lk(_sink->mtx);
        _sink->dirty.push_back(weak_from_this());
    }
}

void Channel::write(std::vector<uint8_t>&& data) {
    bool was_dirty = _dirty.exchange(true, std::memory_order_acq_rel);
    {
        std::lock_guard lk(_buf_mutex);
        if (_mode == WriteMode::Overwrite) {
            _buffer = std::move(data);
        } else {
            _buffer.insert(_buffer.end(),
                           std::make_move_iterator(data.begin()),
                           std::make_move_iterator(data.end()));
        }
    }
    if (!was_dirty && _sink) {
        std::lock_guard lk(_sink->mtx);
        _sink->dirty.push_back(weak_from_this());
    }
}

bool Channel::flush_and_reset() {
    _dirty.store(false, std::memory_order_release);

    auto session = _session.lock();
    if (!session) return false;

    std::vector<uint8_t> pending;
    {
        std::lock_guard lk(_buf_mutex);
        if (_buffer.empty()) return true;
        pending.swap(_buffer);
    }

    return session->send(_conn_slot, pending);
}

}  // namespace gs
