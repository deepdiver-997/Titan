#include "gs/actor/actor_system.h"
#include "gs/actor/peer_manager.h"

#include <iostream>

namespace gs {

ActorSystem::GroupId ActorSystem::create_tick_group(const std::string& name,
                                                    int frequency_hz) {
    std::lock_guard<std::mutex> lk(_mutex);
    GroupId id = _next_group_id++;
    auto gi = std::make_unique<GroupInfo>();
    gi->id = id;
    gi->name = name;
    gi->frequency_hz = frequency_hz;
    _groups.push_back(std::move(gi));
    return id;
}

ActorId ActorSystem::spawn(std::unique_ptr<Actor> actor, GroupId group) {
    ActorId aid = _next_actor_id++;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _actors[aid] = std::move(actor);
        if (group != INVALID_GROUP) {
            for (auto& gp : _groups) {
                if (gp->id == group) {
                    gp->actor_ids.push_back(aid);
                    break;
                }
            }
        }
    }
    if (_peer_mgr) _peer_mgr->broadcast_register(aid);
    return aid;
}

void ActorSystem::set_peer_manager(PeerManager* pm) {
    _peer_mgr = pm;
    // When a new peer connects, push all local Actors to it.
    // ActorSystem's _mutex protects the iteration so spawn/sync don't race.
    pm->on_new_peer([this, pm](const std::string& addr) {
        std::lock_guard<std::mutex> lk(_mutex);
        for (const auto& [aid, actor] : _actors) {
            pm->send_register_to(addr, aid);
        }
    });
}

void ActorSystem::process_group(GroupId group) {
    GroupInfo* g = nullptr;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        for (auto& gp : _groups) {
            if (gp->id == group) { g = gp.get(); break; }
        }
    }
    if (!g || g->actor_ids.empty()) return;

    if (!_pool) {
        std::lock_guard<std::mutex> lk(_mutex);
        for (auto aid : g->actor_ids) {
            auto it = _actors.find(aid);
            if (it == _actors.end() || !it->second->is_active()) continue;
            it->second->swap_mailboxes();
            it->second->process_all();
            route_pending(aid, it->second->drain_outbox());
        }
        return;
    }

    int batch;
    {
        std::lock_guard<std::mutex> lk(g->sync.mutex);
        g->sync.version++;
        batch = g->sync.version;
        g->sync.pending = static_cast<int>(g->actor_ids.size());
    }

    for (auto aid : g->actor_ids) {
        boost::asio::post(*_pool, [this, aid, g, batch]() {
            {
                std::lock_guard<std::mutex> lk(_mutex);
                auto it = _actors.find(aid);
                if (it != _actors.end() && it->second->is_active()) {
                    it->second->swap_mailboxes();
                    it->second->process_all();
                    route_pending(aid, it->second->drain_outbox());
                }
            }
            {
                std::lock_guard<std::mutex> lk(g->sync.mutex);
                g->sync.pending--;
                if (g->sync.pending == 0) {
                    g->sync.version = batch + 1;
                    g->sync.cv.notify_all();
                }
            }
        });
    }

    {
        std::unique_lock<std::mutex> lk(g->sync.mutex);
        g->sync.cv.wait(lk, [g, batch] { return g->sync.version > batch; });
    }
}

void ActorSystem::route_pending(ActorId /*from*/,
                                std::vector<PendingMsg> pending) {
    for (auto& p : pending) {
        auto it = _actors.find(p.target_actor);
        if (it != _actors.end()) {
            it->second->send(p.target_actor, std::move(p.msg));
        }
    }
}

void ActorSystem::send(ActorId target, std::unique_ptr<Message> msg) {
    // Try local first.
    {
        std::lock_guard<std::mutex> lk(_mutex);
        auto it = _actors.find(target);
        if (it != _actors.end()) {
            it->second->send(target, std::move(msg));
            return;
        }
    }
    // Remote fallback — Actor is on another node.
    if (_peer_mgr) {
        _peer_mgr->send_to(target, std::move(msg));
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
            route_pending(id, actor->drain_outbox());
        }
    }
}

size_t ActorSystem::actor_count() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return _actors.size();
}

void ActorSystem::debug_list() const {
    std::lock_guard<std::mutex> lk(_mutex);
    std::cout << "\n=== Actors ===\n";
    for (const auto& [aid, actor] : _actors) {
        std::cout << "  [" << aid << "] " << actor->name()
                  << (actor->is_active() ? "" : " (paused)") << "\n";
    }
    std::cout << "\n=== Tick Groups ===\n";
    for (const auto& gp : _groups) {
        std::cout << "  group[" << gp->id << "] \"" << gp->name
                  << "\" @" << gp->frequency_hz << "Hz, "
                  << gp->actor_ids.size() << " actors\n";
    }
    std::cout << std::endl;
}

}  // namespace gs
