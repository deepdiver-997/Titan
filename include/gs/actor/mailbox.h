#pragma once

#include "gs/common/types.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace gs {

struct Message;

// Thread-safe message queue for Actors.
// Multiple producers → single consumer.
class Mailbox {
public:
    void push(std::unique_ptr<Message> msg, uint32_t tick = 0);
    std::unique_ptr<Message> pop();
    bool try_pop(std::unique_ptr<Message>& out);
    bool empty() const;
    size_t size() const;

    // Atomically swap out all messages. Returns the deque contents,
    // leaving this mailbox empty.
    std::deque<std::unique_ptr<Message>> swap_all();

#ifdef TITAN_DEBUG
    void set_owner(ActorId id) { _owner_id = id; }
#endif

private:
    mutable std::mutex _mutex;
    std::deque<std::unique_ptr<Message>> _queue;
    std::condition_variable _cv;

#ifdef TITAN_DEBUG
    ActorId _owner_id = 0;
#endif
};

}  // namespace gs
