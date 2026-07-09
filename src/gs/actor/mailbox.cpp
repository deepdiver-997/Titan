#include "gs/actor/mailbox.h"
#include "gs/common/types.h"

namespace gs {

void Mailbox::push(std::unique_ptr<Message> msg) {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _queue.push_back(std::move(msg));
    }
    _cv.notify_one();
}

std::unique_ptr<Message> Mailbox::pop() {
    std::unique_lock<std::mutex> lk(_mutex);
    _cv.wait(lk, [this] { return !_queue.empty(); });
    auto msg = std::move(_queue.front());
    _queue.pop_front();
    return msg;
}

bool Mailbox::try_pop(std::unique_ptr<Message>& out) {
    std::lock_guard<std::mutex> lk(_mutex);
    if (_queue.empty()) return false;
    out = std::move(_queue.front());
    _queue.pop_front();
    return true;
}

bool Mailbox::empty() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return _queue.empty();
}

size_t Mailbox::size() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return _queue.size();
}

std::deque<std::unique_ptr<Message>> Mailbox::swap_all() {
    std::deque<std::unique_ptr<Message>> result;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        result.swap(_queue);
    }
    return result;
}

}  // namespace gs
