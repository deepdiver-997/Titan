#pragma once

#include "gs/actor/mailbox.h"
#include "gs/common/types.h"

#include <deque>
#include <memory>

namespace gs {

// Base class for all Actors.
//
// Double-buffered mailbox design:
//   _cur_msgs    — messages being processed in the current tick
//   _next_mailbox — thread-safe inbox for messages arriving from other
//                   Actors mid-tick; will be processed next tick
//
// Tick flow:
//   tick start:
//     1. swap_mailboxes()  → _next_mailbox contents move to _cur_msgs
//     2. push_now()        → TCP-parsed events added to _cur_msgs
//     3. process_all()     → iterate _cur_msgs, on_message() for each
//        (during on_message, send() pushes to _next_mailbox)
//
// This ensures: no actor sees mid-tick messages from other actors until
// the next tick — the "world is frozen" semantics described in the
// game-server architecture discussion.
class Actor {
public:
    explicit Actor(ActorId id);
    virtual ~Actor() = default;

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    ActorId id() const { return _id; }
    bool is_active() const { return _active; }
    void set_active(bool a) { _active = a; }

    // ---- Message passing -------------------------------------------------

    // Send a message to be processed NEXT tick (mid-tick inter-actor comm).
    void send(std::unique_ptr<Message> msg);

    // Push a message to be processed THIS tick (used during TCP parsing).
    void push_now(std::unique_ptr<Message> msg);

    // Swap next → cur. Called at tick start.
    void swap_mailboxes();

    // Process all messages in cur_msgs.
    void process_all();

    // Process one message from cur_msgs.
    bool process_one();

protected:
    virtual void on_message(Message& msg) = 0;

private:
    ActorId _id;
    Mailbox _next_mailbox;                          // thread-safe, for next tick
    std::deque<std::unique_ptr<Message>> _cur_msgs; // tick-local, consumed now
    bool _active = true;
};

}  // namespace gs
