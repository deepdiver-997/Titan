#include "gs/actor/actor.h"

namespace gs {

Actor::Actor(ActorId id, std::string name) : _id(id), _name(std::move(name)) {}

void Actor::send(ActorId target, std::unique_ptr<Message> msg) {
    _next_mailbox.push(std::move(msg));
}

void Actor::send_deferred(ActorId target, std::unique_ptr<Message> msg) {
    _outbox.push_back({target, std::move(msg)});
}

void Actor::swap_mailboxes() {
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

std::vector<PendingMsg> Actor::drain_outbox() {
    std::vector<PendingMsg> result;
    result.swap(_outbox);
    return result;
}

}  // namespace gs
