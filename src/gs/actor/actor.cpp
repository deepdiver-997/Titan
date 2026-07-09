#include "gs/actor/actor.h"

namespace gs {

Actor::Actor(ActorId id) : _id(id) {}

void Actor::send(std::unique_ptr<Message> msg) {
    _next_mailbox.push(std::move(msg));
}

void Actor::push_now(std::unique_ptr<Message> msg) {
    _cur_msgs.push_back(std::move(msg));
}

void Actor::swap_mailboxes() {
    // Move next_mailbox contents into cur_msgs.
    auto pending = _next_mailbox.swap_all();
    for (auto& msg : pending) {
        _cur_msgs.push_back(std::move(msg));
    }
}

void Actor::process_all() {
    while (!_cur_msgs.empty()) {
        auto msg = std::move(_cur_msgs.front());
        _cur_msgs.pop_front();
        if (msg) on_message(*msg);
    }
}

bool Actor::process_one() {
    if (_cur_msgs.empty()) return false;
    auto msg = std::move(_cur_msgs.front());
    _cur_msgs.pop_front();
    if (msg) on_message(*msg);
    return true;
}

}  // namespace gs
