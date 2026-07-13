#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"

#include <boost/asio.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>


namespace gs {

class PeerManager;

namespace debug {
struct ActorStateEntry;
}  // namespace debug

// Manages Actor lifecycle with tick frequency groups.
//
// Multi-threaded execution: call set_thread_pool() to enable parallel actor
// processing within a tick group. Each actor runs on the thread pool; the
// group waits for all actors to finish before the next tick fires.
//
// Without a thread pool, process_group() runs actors serially on the caller.
//
// Thread safety: _routing_mutex uses shared_mutex.
//   send()           — shared_lock (concurrent reads).
//   spawn/swap_all/process_all — unique_lock (exclusive writes).
//
// Debug snapshot isolation uses a separate _debug_mutex:
//   process_group()  — shared_lock (concurrent with other process_group).
//   capture_all()    — unique_lock (waits for all process_group).
class ActorSystem {
public:
    using GroupId = int;
    static constexpr GroupId INVALID_GROUP = -1;

    ActorSystem() = default;
    ~ActorSystem() = default;

    // ---- Distributed routing (optional) -----------------------------------

    // Call after creating PeerManager. Wires up sync-on-connect so new peers
    // automatically receive this node's full Actor list (under _mutex).
    void set_peer_manager(PeerManager* pm);

    // ---- Thread pool (optional) -------------------------------------------

    void set_thread_pool(boost::asio::thread_pool* pool) { _pool = pool; }

    // ---- Tick groups ------------------------------------------------------

    GroupId create_tick_group(const std::string& name, int frequency_hz);
    ActorId spawn(std::unique_ptr<Actor> actor, GroupId group);

    // Process one tick group. If a thread pool is set, actors run in parallel
    // and the call blocks until all complete.
    void process_group(GroupId group);

    // ---- Individual actor operations --------------------------------------

    void send(ActorId target, std::unique_ptr<Message> msg);
    void swap_all();
    void process_all();

    // ---- Debug / Snapshot -------------------------------------------------

    size_t actor_count() const;

    // Capture all Actor states into a vector of ActorStateEntry.
    // Acquires exclusive lock — waits for all process_group() to finish.
    void capture_all(std::vector<debug::ActorStateEntry>& out) const;

    struct GroupSync {
        int version = 0;
        int pending = 0;
        std::mutex mutex;
        std::condition_variable cv;
    };

    struct GroupInfo {
        GroupId id;
        std::string name;
        int frequency_hz;
        std::vector<ActorId> actor_ids;
        GroupSync sync;  // field, not pointer — constructed in-place
    };
    const std::vector<std::unique_ptr<GroupInfo>>& groups() const { return _groups; }

private:
    void route_pending(ActorId from, std::vector<PendingMsg> pending);

    mutable std::shared_mutex _routing_mutex;
    std::unordered_map<ActorId, std::unique_ptr<Actor>> _actors;
    std::vector<std::unique_ptr<GroupInfo>> _groups;
    GroupId _next_group_id = 0;
    ActorId _next_actor_id = 1;
    boost::asio::thread_pool* _pool = nullptr;
    PeerManager* _peer_mgr = nullptr;

    // Debug snapshot lock.
    // process_group() takes shared_lock (read-concurrent).
    // capture_all()    takes unique_lock  (write-exclusive).
    mutable std::shared_mutex _debug_mutex;
};

}  // namespace gs
