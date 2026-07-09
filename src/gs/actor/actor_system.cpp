#include "gs/actor/actor_system.h"

namespace gs {

ActorId ActorSystem::spawn(std::unique_ptr<Actor> actor) {
    ActorId id = _next_id++;
    std::lock_guard<std::mutex> lk(_mutex);
    _actors[id] = std::move(actor);
    return id;
}

void ActorSystem::send(ActorId id, std::unique_ptr<Message> msg) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = _actors.find(id);
    if (it != _actors.end()) {
        it->second->send(std::move(msg));
    }
}

void ActorSystem::push_now(ActorId id, std::unique_ptr<Message> msg) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = _actors.find(id);
    if (it != _actors.end()) {
        it->second->push_now(std::move(msg));
    }
}

void ActorSystem::swap_all() {
    std::lock_guard<std::mutex> lk(_mutex);
    for (auto& [id, actor] : _actors) {
        actor->swap_mailboxes();
    }
}

void ActorSystem::process_all() {
    std::lock_guard<std::mutex> lk(_mutex);
    for (auto& [id, actor] : _actors) {
        if (actor->is_active()) {
            actor->process_all();
        }
    }
}

std::vector<ActorId> ActorSystem::actor_ids() const {
    std::lock_guard<std::mutex> lk(_mutex);
    std::vector<ActorId> ids;
    ids.reserve(_actors.size());
    for (const auto& [id, _] : _actors) {
        ids.push_back(id);
    }
    return ids;
}

size_t ActorSystem::actor_count() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return _actors.size();
}

}  // namespace gs
