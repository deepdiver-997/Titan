#include "gs/net/channel.h"
#include "gs/net/protocol/session.h"

namespace gs {

Channel::Channel(std::shared_ptr<Session> session, int channel_idx)
    : _session(session)
    , _sid(session->id())
    , _channel_idx(channel_idx) {}

void Channel::write(const std::vector<uint8_t>& data) {
    std::lock_guard lk(_buf_mutex);
    _buffer.insert(_buffer.end(), data.begin(), data.end());
}

void Channel::write(std::vector<uint8_t>&& data) {
    std::lock_guard lk(_buf_mutex);
    _buffer.insert(_buffer.end(),
                   std::make_move_iterator(data.begin()),
                   std::make_move_iterator(data.end()));
}

void Channel::flush() {
    std::vector<uint8_t> pending;
    {
        std::lock_guard lk(_buf_mutex);
        if (_buffer.empty()) return;
        pending.swap(_buffer);
    }

    auto session = _session.lock();
    if (!session) return;  // Session destroyed, drop data

    session->send(_channel_idx, pending);
}

}  // namespace gs
