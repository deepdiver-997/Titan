#pragma once

#include "gs/actor/actor.h"
#include "gs/common/types.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gs {

// Manages Actor lifecycle and provides message-passing.
//
// Tick flow (orchestrated by main loop / thread pool):
//   1. swap_all()   — swap every Actor's next_mailbox → cur_msgs
//   2. Parse TCP data → push_now() to target Actor's cur_msgs
//   3. process_all() — each Actor serial-processes its cur_msgs
//      (mid-tick send() goes to next_mailbox, visible next tick)
class ActorSystem {
public:
    ActorSystem() = default;
    ~ActorSystem() = default;

    ActorId spawn(std::unique_ptr<Actor> actor);

    // Send to Actor's next_mailbox (mid-tick inter-actor communication).
    void send(ActorId id, std::unique_ptr<Message> msg);

    // Push to Actor's cur_msgs (TCP-parsed, same-tick consumption).
    void push_now(ActorId id, std::unique_ptr<Message> msg);

    // Swap all Actors' mailboxes (next → cur).
    void swap_all();

    // Process all cur_msgs for all Actors.
    void process_all();

    // Get snapshot of all Actor IDs (for iteration).
    std::vector<ActorId> actor_ids() const;

    size_t actor_count() const;

private:
    mutable std::mutex _mutex;
    std::unordered_map<ActorId, std::unique_ptr<Actor>> _actors;
    ActorId _next_id = 1;
};

}  // namespace gs
